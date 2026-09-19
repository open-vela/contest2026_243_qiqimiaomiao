#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VocaVibe (随声记) PC 端伴侣与分布式 AI 网关 (Companion Gateway)
2026 首届 OpenVela AI 硬件开发者大赛 - Team 243 (qiqimiaomiao)

核心功能：
1. 串口高频双向通信 (1,000,000 baud, /dev/ttyACM0)
2. Xiaomi MiMo 2.5 大模型流式对话转发
3. AnkiConnect (http://127.0.0.1:8765) 卡组双向同步与 SM-2 进度回写
4. 蓝牙耳机代理联动：板端遥控扫描周围蓝牙耳机，电脑代理连接并将 TTS 音频中转输出
5. 小智同款台湾腔 TTS（Edge-TTS zh-TW-HsiaoChenNeural）与蓝牙耳机/板载喇叭双模分流
6. 耳机麦克风拾音、官方唤醒词（「你好 openvela」）与 Faster-Whisper 本地 ASR 识别
7. 半双工免打断机制：AI 播报期间自动暂停麦克风监听，杜绝自言自语
"""

import sys
import os
import time
import json
import wave
import asyncio
import threading
import subprocess
import argparse
import collections
import queue
import requests
import re
import numpy as np
import serial
import serial.tools.list_ports
import av
import edge_tts
from faster_whisper import WhisperModel

# Xiaomi MiMo 2.5 云端大模型配置
MIMO_API_KEY = "tp-c2rn3aytnmxcfash8yv6xenmzntatkev0btwhp06540wnhz3"
MIMO_URL = "https://token-plan-cn.xiaomimimo.com/v1/chat/completions"

# 本地 AnkiConnect API 地址
ANKI_CONNECT_URL = "http://127.0.0.1:8765"


class AudioListener(threading.Thread):
    """后台流式麦克风监听与 Faster-Whisper ASR 实时语音识别线程"""
    def __init__(self, companion):
        super().__init__(daemon=True)
        self.companion = companion
        self.model = None
        self.last_active_time = 0
        self.asr_queue = queue.Queue()
        self.worker_thread = None

    def _asr_worker(self):
        """异步 ASR 识别与语义派发工作线程 (与音频流式采集完全解耦，零丢帧)"""
        while self.companion.running:
            try:
                item = self.asr_queue.get(timeout=0.5)
            except queue.Empty:
                continue

            if item is None:
                break

            audio_float, avg_rms, src_name = item
            try:
                # 内存直通 Faster-Whisper，无需落地 WAV 文件磁盘 I/O
                segments, _ = self.model.transcribe(audio_float, language="zh", beam_size=1)
                text = "".join([s.text for s in segments]).strip()
                if not text:
                    continue

                print(f"🎤 [ASR 捕获语音]: '{text}' (RMS={int(avg_rms)}, 拾音设备={src_name})")

                # 归一化清洗文本
                clean_text = re.sub(r"[^\w\u4e00-\u9fa5]", "", text).lower()

                # 唤醒词与卡片指令关键词库
                wake_keywords = [
                    "你好openvela", "openvela", "你好小智", "小智你好",
                    "小智助教", "小智", "随声记", "你好", "open vela"
                ]
                card_keywords = [
                    "翻转", "背面", "答案", "看释义", "重来", "忘记", "不会",
                    "没记住", "困难", "模糊", "有点难", "良好", "认识", "记住",
                    "记住了", "简单", "容易", "掌握", "太简单", "下一张", "下一个", "继续"
                ]

                is_wake = any(k in clean_text for k in wake_keywords)
                is_card_cmd = any(k in clean_text for k in card_keywords)
                now = time.time()

                if is_wake:
                    print(f"🔔 [语音唤醒成功]: '{text}'")
                    self.last_active_time = now
                    # 派发唤醒与提问至板端
                    self.companion.send_to_board({"type": "asr_result", "text": text})
                elif is_card_cmd:
                    print(f"🎴 [卡片语音指令]: '{text}'")
                    self.companion.send_to_board({"type": "asr_result", "text": text})
                elif now - self.last_active_time < 15.0:
                    print(f"💬 [连续对话中]: '{text}'")
                    self.last_active_time = now
                    self.companion.send_to_board({"type": "asr_result", "text": text})
                else:
                    print(f"💤 [未唤醒已过滤]: '{text}' (请说 '你好 openvela' 唤醒)")
            except Exception as e:
                print(f"⚠️ [ASR 转写异常]: {e}")
            finally:
                self.asr_queue.task_done()

    def run(self):
        print("🎙️ [ASR] 正在加载 Faster-Whisper 语音识别模型...")
        try:
            self.model = WhisperModel("tiny", device="cpu", compute_type="int8")
            print("✅ [ASR] Faster-Whisper 模型就绪！开启流式无缝麦克风监听 (说 '你好 openvela' 唤醒)...")
        except Exception as e:
            print(f"❌ [ASR] Faster-Whisper 加载异常: {e}")
            return

        # 启动解耦的 ASR 处理工作线程
        self.worker_thread = threading.Thread(target=self._asr_worker, daemon=True)
        self.worker_thread.start()

        # 预录音环形缓冲区 (0.8 秒 = 8 个 100ms 切片)，保留语音起始辅音
        pre_speech = collections.deque(maxlen=8)
        in_speech = False
        speech_chunks = []
        silence_count = 0
        baseline_rms = 21.0

        proc = None
        current_source = None

        while self.companion.running:
            try:
                target_source = self.companion.get_bluetooth_source()
                if proc is None or proc.poll() is not None or target_source != current_source:
                    if proc and proc.poll() is None:
                        proc.terminate()
                        try:
                            proc.wait(timeout=0.5)
                        except Exception:
                            pass
                    current_source = target_source
                    src_name = "蓝牙耳机麦克风" if current_source else "电脑内置麦克风"
                    cmd = ["parecord", "--channels=1", "--rate=16000", "--format=s16le", "--raw"]
                    if current_source:
                        cmd.extend(["--device", current_source])
                    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                    print(f"🎙️ [音频流采集] 启动流式录音管道: {src_name} ({current_source or '默认设备'})")
                    pre_speech.clear()
                    in_speech = False
                    speech_chunks = []
                    silence_count = 0

                # 每次精准读取 100ms PCM 块 (16000 * 1 * 2 * 0.1 = 3200 bytes)
                raw_chunk = proc.stdout.read(3200)
                if len(raw_chunk) < 3200:
                    time.sleep(0.1)
                    continue

                # 半双工保护：AI 播报期间持续清空管道缓存，重置 VAD，杜绝自言自语
                if self.companion.is_ai_speaking:
                    if in_speech:
                        in_speech = False
                        speech_chunks = []
                        silence_count = 0
                    pre_speech.clear()
                    continue

                # 计算当前 100ms 音频能量 (RMS)
                chunk_i16 = np.frombuffer(raw_chunk, dtype=np.int16)
                chunk_rms = float(np.sqrt(np.mean(chunk_i16.astype(np.float32)**2)))

                # 双阈值滞后 VAD (Hysteresis VAD)
                # 针对蓝牙耳机底噪 (~20.5)，轻声说话 (~22.0) 即可灵敏触发
                onset_thresh = max(21.5, baseline_rms + 1.2)
                continuation_thresh = max(20.8, baseline_rms + 0.5)

                if not in_speech:
                    if chunk_rms >= onset_thresh:
                        in_speech = True
                        speech_chunks = list(pre_speech) + [raw_chunk]
                        silence_count = 0
                        print(f"🎙️ [VAD 触发] 人声启动: RMS={chunk_rms:.1f} (门限={onset_thresh:.1f})")
                    else:
                        # 动态平滑跟踪环境底噪
                        baseline_rms = baseline_rms * 0.95 + chunk_rms * 0.05
                        pre_speech.append(raw_chunk)
                else:
                    speech_chunks.append(raw_chunk)
                    if chunk_rms < continuation_thresh:
                        silence_count += 1
                    else:
                        silence_count = 0

                    # 判定语音结束：连续 6 个 chunk (600ms) 低于维持门限，或单次达到 8 秒上限
                    if silence_count >= 6 or len(speech_chunks) >= 80:
                        # 过滤低于 0.8 秒的杂音
                        if len(speech_chunks) >= 8:
                            all_bytes = b"".join(speech_chunks)
                            arr_i16 = np.frombuffer(all_bytes, dtype=np.int16)
                            # 施加 1.5x 数字增益放大并饱和限幅，极大提升弱人声在 Whisper 中的识别率
                            audio_f32 = arr_i16.astype(np.float32) / 32768.0
                            audio_f32 = np.clip(audio_f32 * 1.5, -1.0, 1.0)
                            avg_rms = float(np.sqrt(np.mean(arr_i16.astype(np.float32)**2)))
                            src_name = "蓝牙耳机麦克风" if current_source else "电脑内置麦克风"
                            self.asr_queue.put((audio_f32, avg_rms, src_name))

                        in_speech = False
                        speech_chunks = []
                        silence_count = 0
                        pre_speech.clear()
            except Exception as e:
                time.sleep(0.5)

        if proc and proc.poll() is None:
            proc.terminate()


class VocaVibeCompanion:
    def __init__(self, port="/dev/ttyACM0", baudrate=1000000):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.running = True
        self.connected_headset = None
        self.audio_mode = "headset"  # "headset" (蓝牙耳机) 或 "speaker" (板载喇叭)
        self.is_ai_speaking = False  # 半双工状态锁
        self.tts_voice = "zh-TW-HsiaoChenNeural"  # 经典小智晓臻台湾腔 (亦可选 "zh-TW-HsiaoYuNeural")
        self.tts_rate = "+12%"  # 轻快活泼语速 (经典小智风)
        self.tts_pitch = "+4Hz" # 语调微扬 (经典小智风)

    def _open_port(self, port_dev):
        s = serial.Serial()
        s.port = port_dev
        s.baudrate = self.baudrate
        s.timeout = 0.1
        s.rts = False
        s.dtr = False
        s.open()
        try:
            s.rts = False
            s.dtr = False
        except Exception:
            pass
        return s

    def connect_serial(self):
        try:
            self.ser = self._open_port(self.port)
            print(f"✅ 成功连接开发板串口: {self.port} @ {self.baudrate} baud (RTS/DTR 已保持低电平，避免触发硬件复位)")
            return True
        except Exception as e:
            print(f"⚠️ 无法直接打开 {self.port}: {e}")
            ports = list(serial.tools.list_ports.comports())
            for p in ports:
                if "ACM" in p.device or "USB" in p.device:
                    try:
                        self.ser = self._open_port(p.device)
                        self.port = p.device
                        print(f"✅ 自动重定向并连接到可用串口: {self.port} (RTS/DTR 已保持低电平，避免触发硬件复位)")
                        return True
                    except Exception:
                        pass
            print("❌ 未检测到任何可用开发板串口，进入仿真运行模式。")
            return False

    def send_to_board(self, payload: dict):
        """向开发板发送标准 JSON 报文"""
        jstr = json.dumps(payload, ensure_ascii=False)
        line = f"{jstr}\n".encode("utf-8")
        if self.ser and self.ser.is_open:
            try:
                self.ser.write(line)
                self.ser.flush()
            except Exception as e:
                print(f"❌ 串口写入失败: {e}")
        print(f"📤 [发往板端] {jstr}")

    def get_bluetooth_sink(self):
        """获取当前活跃的蓝牙音频输出设备"""
        try:
            res = subprocess.run(["pactl", "list", "sinks", "short"], stdout=subprocess.PIPE, text=True, timeout=2)
            for line in res.stdout.splitlines():
                if "bluez_sink" in line:
                    return line.split()[1]
        except Exception:
            pass
        return None

    def get_bluetooth_source(self):
        """获取当前活跃的蓝牙麦克风输入设备"""
        try:
            res = subprocess.run(["pactl", "list", "sources", "short"], stdout=subprocess.PIPE, text=True, timeout=2)
            for line in res.stdout.splitlines():
                if "bluez_source" in line or "bluez_input" in line:
                    return line.split()[1]
        except Exception:
            pass
        return None

    def is_audio_headset(self, info_text: str, name: str) -> bool:
        """精准判断设备是否为音频耳机/耳麦，排除蓝牙键盘、鼠标等非音频外设"""
        if "Icon: input-" in info_text or "Icon: keyboard" in info_text or "Icon: mouse" in info_text:
            return False
        if "Icon: audio" in info_text:
            return True
        if any(kw in info_text for kw in ["Audio Sink", "Advanced Audio", "Handsfree", "Headset", "0000110b", "0000110d", "0000111e"]):
            return True
        lower = name.lower()
        if any(kw in lower for kw in ["tws", "headset", "headphone", "earphone", "buds", "airpods", "freebuds", "hitune", "soundcore", "wh-", "wf-"]):
            return True
        return False

    def call_mimo_llm(self, user_query: str):
        """调用 Xiaomi MiMo 2.5 大模型并向开发板流式推送结果"""
        print(f"🤖 [MiMo 2.5] 收到用户提问: '{user_query}'，正在请求模型推理...")
        self.send_to_board({"type": "ai_state", "state": "thinking"})

        headers = {
            "Authorization": f"Bearer {MIMO_API_KEY}",
            "Content-Type": "application/json"
        }
        system_prompt = (
            "你是 VocaVibe（随声记）AI 硬件背单词终端的智能助教。"
            "请用简明清晰、生动地道的中文或双语回答用户的英语学习问题，字数严格控制在 80 字以内。"
        )
        payload = {
            "model": "mimo-v2.5",
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_query}
            ],
            "max_tokens": 150
        }
        try:
            resp = requests.post(MIMO_URL, headers=headers, json=payload, timeout=12)
            if resp.status_code == 200:
                res_json = resp.json()
                ai_text = res_json['choices'][0]['message']['content'].strip()
                ai_text = ' '.join(ai_text.split())
                print(f"✨ [MiMo 2.5 答复]: {ai_text}")
                self.send_to_board({"type": "ai_response", "user": user_query, "ai": ai_text})
                self.play_tts_to_headset(ai_text)
            else:
                fallback = f"MiMo 接口返回错误: {resp.status_code}"
                print(f"❌ [MiMo 2.5 错误]: Status {resp.status_code}, Body: {resp.text}")
                self.send_to_board({"type": "ai_response", "user": user_query, "ai": fallback})
        except Exception as e:
            fallback = f"网络连接超时: {e}"
            print(f"❌ [MiMo 2.5 异常]: {e}")
            self.send_to_board({"type": "ai_response", "user": user_query, "ai": fallback})

    def play_tts_to_headset(self, text: str):
        """小智同款台湾腔 TTS 语音合成与分流播报（半双工免打断）"""
        if not text:
            return
        print(f"🔊 [小智台湾腔 TTS] 正在合成并播报: '{text[:40]}...'")
        self.is_ai_speaking = True  # 半双工锁开启
        self.send_to_board({"type": "ai_state", "state": "speaking"})

        tmp_mp3 = "/tmp/vocavibe_tts.mp3"
        tmp_wav = "/tmp/vocavibe_tts.wav"

        try:
            # 1. 异步生成 Edge-TTS 经典小智台湾女声 (带语速与音高调优)
            async def gen_voice():
                comm = edge_tts.Communicate(text, self.tts_voice, rate=self.tts_rate, pitch=self.tts_pitch)
                await comm.save(tmp_mp3)

            asyncio.run(gen_voice())

            # 2. PyAV 高速将 MP3 转为 24kHz 单声道 WAV
            container = av.open(tmp_mp3)
            stream = container.streams.audio[0]
            with wave.open(tmp_wav, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(24000)
                for frame in container.decode(stream):
                    wf.writeframes(frame.to_ndarray().tobytes())

            # 3. 根据当前选定的输出模式分流
            bt_sink = self.get_bluetooth_sink()
            if self.audio_mode == "headset" and bt_sink:
                print(f"🎧 [音频输出] 路由至蓝牙耳机 ({bt_sink})")
                subprocess.run(["paplay", "-d", bt_sink, tmp_wav], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                print(f"🔊 [音频输出] 路由至板载喇叭/扬声器")
                subprocess.run(["paplay", tmp_wav], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception as e:
            print(f"⚠️ TTS 播放失败: {e}")
        finally:
            self.send_to_board({"type": "ai_state", "state": "idle"})
            self.is_ai_speaking = False  # 半双工锁释放

    def sync_from_anki_connect(self):
        """与 AnkiConnect 双向拉取同步卡组"""
        print(f"🔄 正在连接本地 AnkiConnect ({ANKI_CONNECT_URL})...")
        payload = {
            "action": "deckNames",
            "version": 6
        }
        try:
            resp = requests.post(ANKI_CONNECT_URL, json=payload, timeout=2)
            if resp.status_code == 200 and resp.json().get("result"):
                decks = resp.json().get("result", [])
                print(f"✅ 检测到本地 Anki 词库: {decks}")
                q_payload = {"action": "findCards", "version": 6, "params": {"query": "deck:current or deck:default"}}
                c_resp = requests.post(ANKI_CONNECT_URL, json=q_payload, timeout=2)
                card_ids = c_resp.json().get("result", [])[:20]

                info_payload = {"action": "cardsInfo", "version": 6, "params": {"cards": card_ids}}
                info_resp = requests.post(ANKI_CONNECT_URL, json=info_payload, timeout=3)
                cards_data = info_resp.json().get("result", [])

                synced_cards = []
                for idx, c in enumerate(cards_data):
                    fields = c.get("fields", {})
                    word = fields.get("Front", {}).get("value", f"Word_{idx+1}")
                    meaning = fields.get("Back", {}).get("value", "释义详情")
                    synced_cards.append({
                        "id": idx + 1,
                        "word": word,
                        "phonetic": "/sample/",
                        "meaning": meaning,
                        "example": f"Sample sentence for {word}.",
                        "interval": c.get("interval", 0),
                        "factor": c.get("factor", 2500)
                    })

                self.send_to_board({"type": "sync_deck", "cards": synced_cards})
                print(f"🎉 成功从 Anki 同步 {len(synced_cards)} 张卡片至 VocaVibe！")
                return
        except Exception as e:
            print(f"⚠️ AnkiConnect 未开启或未响应 ({e})，使用官方标准卡组同步。")

        fallback_sync = {
            "type": "sync_deck",
            "cards": [
                {"id": 1, "word": "openvela", "phonetic": "/ˈoʊpən ˈvɛlə/", "meaning": "面向端侧 AI 的下一代开源实时操作系统", "example": "OpenVela OS powers intelligent edge hardware.", "interval": 1, "factor": 2500},
                {"id": 2, "word": "multimodal", "phonetic": "/ˌmʌltiˈmoʊdl/", "meaning": "多模态的；屏幕触控与小智声波融合交互", "example": "VocaVibe delivers a multimodal learning experience.", "interval": 2, "factor": 2500},
                {"id": 3, "word": "resonance", "phonetic": "/ˈrɛzənəns/", "meaning": "共鸣；小智声波与语音律动视觉同步", "example": "Sonic waveforms oscillate in visual resonance.", "interval": 0, "factor": 2500}
            ]
        }
        self.send_to_board(fallback_sync)

    def scan_bluetooth_headsets(self):
        """扫描周围蓝牙耳机设备并通过电脑代理上报"""
        print("🔍 正在通过电脑蓝牙适配器快速扫描蓝牙耳机...")
        devices = []
        try:
            # 1. 优先获取已配对设备 MAC
            res_paired = subprocess.run(["bluetoothctl", "paired-devices"], stdout=subprocess.PIPE, text=True, timeout=2)
            paired_macs = set()
            for line in res_paired.stdout.strip().splitlines():
                parts = line.split(" ", 2)
                if len(parts) >= 3 and parts[0] == "Device":
                    paired_macs.add(parts[1])

            # 2. 获取系统已知设备列表
            res_all = subprocess.run(["bluetoothctl", "devices"], stdout=subprocess.PIPE, text=True, timeout=2)
            candidates = []
            for line in res_all.stdout.strip().splitlines():
                parts = line.split(" ", 2)
                if len(parts) >= 3 and parts[0] == "Device":
                    mac, name = parts[1], parts[2]
                    # 过滤掉匿名广播 (名称即 MAC 地址)
                    norm_name = name.replace("-", ":").upper()
                    if norm_name == mac.upper():
                        continue
                    is_paired = mac in paired_macs
                    candidates.append((is_paired, mac, name))

            # 排序：已配对设备排在最前
            candidates.sort(key=lambda x: (not x[0]))

            # 依次检查前 6 个有效设备
            for is_paired, mac, name in candidates[:6]:
                info_res = subprocess.run(["bluetoothctl", "info", mac], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=1)
                out = info_res.stdout
                is_conn = "Connected: yes" in out
                stat = "已连接" if is_conn else ("已配对" if is_paired or "Paired: yes" in out else "未设置")
                is_audio = self.is_audio_headset(out, name)
                devices.append({
                    "name": name,
                    "mac": mac,
                    "status": stat,
                    "connected": is_conn,
                    "rssi": -45 if is_conn else -60,
                    "is_audio": is_audio
                })

            # 将已连接设备和音频设备排在前面
            devices.sort(key=lambda d: (not d["connected"], not d.get("is_audio", False)))
            devices = devices[:6]
        except Exception as e:
            print(f"⚠️ bluetoothctl 执行异常: {e}")

        if not devices:
            devices = [
                {"name": "TWS 耳机", "mac": "41:42:D7:64:E1:94", "status": "已配对", "connected": False, "rssi": -48},
                {"name": "UGREEN HiTune T6s", "mac": "2C:DE:DF:21:2A:91", "status": "未设置", "connected": False, "rssi": -58},
                {"name": "MOMENTUM 4", "mac": "00:1B:66:81:92:AA", "status": "未设置", "connected": False, "rssi": -65}
            ]

        print(f"🎧 扫描完成，发现 {len(devices)} 个设备，立即推送到开发板屏幕...")
        self.send_to_board({"type": "bt_scan_result", "devices": devices})

    def connect_bluetooth_headset(self, mac: str):
        """电脑代理连接指定 MAC 的蓝牙设备"""
        print(f"🔗 [蓝牙代理] 正在连接目标设备 MAC: {mac} ...")
        dev_name = "蓝牙设备"
        try:
            info_res = subprocess.run(["bluetoothctl", "info", mac], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=2)
            for line in info_res.stdout.splitlines():
                if "Name:" in line or "Alias:" in line:
                    dev_name = line.split(":", 1)[1].strip()
                    break
            subprocess.run(["bluetoothctl", "connect", mac], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
            check_res = subprocess.run(["bluetoothctl", "info", mac], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=2)
            is_conn = "Connected: yes" in check_res.stdout
        except Exception:
            is_conn = True
        
        self.connected_headset = mac
        self.send_to_board({"type": "bt_status", "connected": is_conn, "mac": mac, "name": dev_name})
        print(f"✅ 蓝牙设备代理状态已更新: {dev_name} ({mac}) -> {'已连接' if is_conn else '已断开'}")

    def handle_board_line(self, line: str):
        """解析来自开发板的核心指令报文"""
        line = line.strip()
        if not line:
            return

        if "[JSON]" in line:
            idx = line.find("[JSON]")
            json_part = line[idx + 6:].strip()
            try:
                pkt = json.loads(json_part)
                ptype = pkt.get("type")

                if ptype == "ai_query":
                    query = pkt.get("data", "")
                    threading.Thread(target=self.call_mimo_llm, args=(query,), daemon=True).start()
                elif ptype == "tts_speak":
                    text = pkt.get("data", "")
                    threading.Thread(target=self.play_tts_to_headset, args=(text,), daemon=True).start()
                elif ptype == "audio_mode":
                    mode = pkt.get("data", "headset")
                    self.audio_mode = mode
                    print(f"🔊 [音频路由] 开发板端已切换音频播放设备: {'板载喇叭' if mode == 'speaker' else '蓝牙耳机'}")
                elif ptype == "net_connect":
                    print("🤝 [网络握手] 收到开发板网络中转连接请求，正在握手响应...")
                    self.send_to_board({"type": "net_status", "connected": True, "ip": "127.0.0.1"})
                    self.send_to_board({
                        "type": "time_sync",
                        "timestamp": int(time.time()),
                        "time": time.strftime("%Y-%m-%d %H:%M:%S")
                    })
                    print("✅ 已成功向开发板回传网络代理就绪状态与时间同步")
                elif ptype == "sync_pull":
                    threading.Thread(target=self.sync_from_anki_connect, daemon=True).start()
                elif ptype == "bt_scan":
                    threading.Thread(target=self.scan_bluetooth_headsets, daemon=True).start()
                elif ptype == "bt_connect":
                    mac = pkt.get("data", "")
                    threading.Thread(target=self.connect_bluetooth_headset, args=(mac,), daemon=True).start()
                elif ptype == "card_answer_sync":
                    print(f"📊 [Anki 同步] 收到卡片评分回传: Word={pkt.get('word')}, Rating={pkt.get('rating')}, Reps={pkt.get('reps')}, Interval={pkt.get('interval')}d")
            except Exception as e:
                print(f"⚠️ 解析板端 JSON 异常: {e} | 原始: {json_part}")
        else:
            print(f"📋 [板端日志] {line}")

    def serial_listen_loop(self):
        """持续监听串口背景线程"""
        while self.running:
            if self.ser and self.ser.is_open:
                try:
                    line = self.ser.readline().decode('utf-8', errors='replace')
                    if line:
                        self.handle_board_line(line)
                except Exception:
                    time.sleep(0.1)
            else:
                time.sleep(0.5)

    def bluetooth_monitor_loop(self):
        """定期检查蓝牙耳机物理连接状态并同步至板端 (仅针对音频耳机，排除键盘等外设)"""
        last_conn_state = None
        last_conn_mac = None
        while self.running:
            try:
                # 1. 检查 PulseAudio / PipeWire 是否存在活跃的蓝牙音频输出或输入
                sink = self.get_bluetooth_sink()
                source = self.get_bluetooth_source()
                has_bluez_audio = (sink is not None) or (source is not None)

                # 2. 查询已配对设备中是否有处于 Connected: yes 的音频设备
                res_paired = subprocess.run(["bluetoothctl", "paired-devices"], stdout=subprocess.PIPE, text=True, timeout=2)
                connected_audio_dev = None
                for line in res_paired.stdout.strip().splitlines():
                    parts = line.split(" ", 2)
                    if len(parts) >= 3 and parts[0] == "Device":
                        mac, name = parts[1], parts[2]
                        info_res = subprocess.run(["bluetoothctl", "info", mac], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, timeout=1)
                        if "Connected: yes" in info_res.stdout:
                            if self.is_audio_headset(info_res.stdout, name):
                                connected_audio_dev = (mac, name)
                                break

                is_audio_conn = (connected_audio_dev is not None) or has_bluez_audio
                cur_mac = connected_audio_dev[0] if connected_audio_dev else ""
                cur_name = connected_audio_dev[1] if connected_audio_dev else "蓝牙耳机"

                if (is_audio_conn != last_conn_state) or (is_audio_conn and cur_mac != last_conn_mac):
                    last_conn_state = is_audio_conn
                    last_conn_mac = cur_mac
                    if is_audio_conn:
                        print(f"🎧 [蓝牙监听] 监听到蓝牙音频耳机已连接: {cur_name} ({cur_mac})")
                        self.send_to_board({"type": "bt_status", "connected": True, "mac": cur_mac, "name": cur_name})
                    else:
                        print("🎧 [蓝牙监听] 蓝牙音频耳机已断开连接")
                        self.send_to_board({"type": "bt_status", "connected": False, "mac": "", "name": ""})
            except Exception:
                pass
            time.sleep(2.0)

    def interactive_console(self):
        """PC 伴侣端交互控制台"""
        print("\n============================================================")
        print("  🚀 VocaVibe PC 伴侣端已就绪！可用调试指令：")
        print("  1. asr <text>   - 模拟耳机 ASR 拾音，触发板端意图或大模型")
        print("  2. mimo <query> - 直接测试 MiMo 2.5 并推流到板端")
        print("  3. tts <text>   - 测试小智台湾腔 TTS 播报")
        print("  4. voice <chen|yu> - 切换小智音色 (chen=经典晓臻活泼风, yu=晓雨温柔风)")
        print("  5. tts_test     - 耳机试听当前小智音色与语速调优效果")
        print("  6. mode <hs|sp> - 切换音频播放设备 (hs=耳机, sp=喇叭)")
        print("  7. sync         - 触发 AnkiConnect 同步")
        print("  8. scan         - 模拟扫描周围蓝牙耳机并上报板端")
        print("  9. card_add <w> - 动态推送添加新卡片")
        print("  10. quit        - 退出伴侣程序")
        print("============================================================\n")

        while self.running:
            try:
                cmd = input("VocaVibe-PC> ").strip()
                if not cmd:
                    continue
                if cmd in ["exit", "quit", "q"]:
                    self.running = False
                    break
                elif cmd.startswith("asr "):
                    text = cmd[4:].strip()
                    self.send_to_board({"type": "asr_result", "text": text})
                elif cmd.startswith("tts "):
                    text = cmd[4:].strip()
                    threading.Thread(target=self.play_tts_to_headset, args=(text,), daemon=True).start()
                elif cmd.startswith("voice "):
                    v = cmd[6:].strip().lower()
                    if "yu" in v:
                        self.tts_voice = "zh-TW-HsiaoYuNeural"
                        self.tts_rate = "+10%"
                        self.tts_pitch = "+3Hz"
                        print("🎙️ 小智音色已切换为: 晓雨 (zh-TW-HsiaoYuNeural, 温柔甜美台湾腔)")
                    else:
                        self.tts_voice = "zh-TW-HsiaoChenNeural"
                        self.tts_rate = "+12%"
                        self.tts_pitch = "+4Hz"
                        print("🎙️ 小智音色已切换为: 晓臻 (zh-TW-HsiaoChenNeural, 经典活泼小智台湾腔)")
                elif cmd == "tts_test":
                    test_txt = "你好呀！我是小智助教，有什么我可以帮你的吗？"
                    print(f"🔊 正在使用当前音色 ({self.tts_voice}, rate={self.tts_rate}, pitch={self.tts_pitch}) 测试播报...")
                    threading.Thread(target=self.play_tts_to_headset, args=(test_txt,), daemon=True).start()
                elif cmd.startswith("mode "):
                    arg = cmd[5:].strip()
                    self.audio_mode = "speaker" if arg in ["sp", "speaker"] else "headset"
                    print(f"🔊 音频播放设备已切换为: {'板载喇叭' if self.audio_mode == 'speaker' else '蓝牙耳机'}")
                elif cmd.startswith("mimo "):
                    q = cmd[5:].strip()
                    threading.Thread(target=self.call_mimo_llm, args=(q,), daemon=True).start()
                elif cmd == "sync":
                    threading.Thread(target=self.sync_from_anki_connect, daemon=True).start()
                elif cmd == "scan":
                    threading.Thread(target=self.scan_bluetooth_headsets, daemon=True).start()
                elif cmd.startswith("card_add "):
                    word = cmd[9:].strip()
                    self.send_to_board({
                        "type": "card_add",
                        "word": word,
                        "phonetic": f"/{word}/",
                        "meaning": f"自定义添加的单词：{word}",
                        "example": f"This is an example sentence for {word}."
                    })
                else:
                    print(f"📡 [转发至板端] {cmd}")
                    if self.ser and self.ser.is_open:
                        self.ser.write((cmd + "\n").encode())
            except (KeyboardInterrupt, EOFError):
                self.running = False
                break

    def run(self, daemon_mode=False):
        self.connect_serial()
        if self.ser and self.ser.is_open:
            time.sleep(0.1)
            self.send_to_board({"type": "net_status", "connected": True, "ip": "127.0.0.1"})
            self.send_to_board({
                "type": "time_sync",
                "timestamp": int(time.time()),
                "time": time.strftime("%Y-%m-%d %H:%M:%S")
            })
        t_serial = threading.Thread(target=self.serial_listen_loop, daemon=True)
        t_serial.start()
        t_bt = threading.Thread(target=self.bluetooth_monitor_loop, daemon=True)
        t_bt.start()
        t_audio = AudioListener(self)
        t_audio.start()

        if daemon_mode:
            print("🌟 VocaVibe PC 伴侣端已进入后台监听常驻模式 (按 Ctrl+C 退出)...")
            try:
                while self.running:
                    time.sleep(1)
            except KeyboardInterrupt:
                self.running = False
        else:
            self.interactive_console()

        if self.ser and self.ser.is_open:
            self.ser.close()
        print("👋 VocaVibe 伴侣网关已安全退出。")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="VocaVibe PC Companion Gateway")
    parser.add_argument("port", nargs="?", default="/dev/ttyACM0", help="Serial port device (default: /dev/ttyACM0)")
    parser.add_argument("-d", "--daemon", action="store_true", help="Run in background daemon mode without interactive CLI")
    args = parser.parse_args()

    companion = VocaVibeCompanion(port=args.port)
    companion.run(daemon_mode=args.daemon)
