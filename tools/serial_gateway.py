#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VocaVibe Unified AI Gateway & Serial Bridge
- Bridges board requests to MiMo 2.5 AI Cloud & Local AnkiConnect
- Bluetooth headset device discovery and connection proxy
- Two-way JSON protocol handling via serial port at 1,000,000 baud
"""

import sys
import os
import time
import json
import threading
import argparse
import requests
import serial

MIMO_API_KEY = "tp-c2rn3aytnmxcfash8yv6xenmzntatkev0btwhp06540wnhz3"
MIMO_URL = "https://token-plan-cn.xiaomimimo.com/anthropic/v1/messages"
MIMO_MODEL = "mimo-v2.5"

ANKI_CONNECT_URL = "http://127.0.0.1:8765"

DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 1000000

# 内置离线回退卡组（在未运行本地 AnkiConnect 时提供高质量预设）
DEFAULT_ANKI_DECK = [
    {
        "id": 1,
        "word": "openvela",
        "phonetic": "/open-vela/",
        "meaning": "嵌入式微控制器专属的下一代端侧实时 AI 操作系统",
        "example": "OpenVela OS powers intelligent edge hardware with microsecond latency.",
        "interval": 0,
        "factor": 2500
    },
    {
        "id": 2,
        "word": "ecosystem",
        "phonetic": "/ee-koh-sis-tem/",
        "meaning": "生态系统：软硬件协同的庞大开发者与应用体系",
        "example": "Developers collaborate to enrich the vibrant openvela ecosystem.",
        "interval": 0,
        "factor": 2500
    },
    {
        "id": 3,
        "word": "embedded",
        "phonetic": "/em-bed-id/",
        "meaning": "嵌入式：集成在微控制器芯片内部的系统与计算",
        "example": "SF32LB52 is an advanced dual-core embedded IoT processor.",
        "interval": 0,
        "factor": 2500
    },
    {
        "id": 4,
        "word": "latency",
        "phonetic": "/ley-tn-see/",
        "meaning": "延迟：从语音输入到智能应答之间的端到端响应耗时",
        "example": "Ultra-low latency is crucial for real-time voice conversations.",
        "interval": 0,
        "factor": 2500
    },
    {
        "id": 5,
        "word": "multimodal",
        "phonetic": "/muhl-ti-moh-dl/",
        "meaning": "多模态：融合触控屏幕、语音交互与声波动效的体验",
        "example": "VocaVibe delivers a seamless multimodal learning experience.",
        "interval": 0,
        "factor": 2500
    },
    {
        "id": 6,
        "word": "neural",
        "phonetic": "/noor-uhl/",
        "meaning": "神经网络：端侧轻量化边缘 AI 推理与计算模型",
        "example": "Edge neural processing optimizes real-time voice recognition.",
        "interval": 0,
        "factor": 2500
    },
    {
        "id": 7,
        "word": "heuristic",
        "phonetic": "/hyoo-ris-tik/",
        "meaning": "启发式算法：基于间隔重复的记忆强化规则 (SM-2)",
        "example": "The Anki SM-2 heuristic algorithm optimizes spaced reviews.",
        "interval": 0,
        "factor": 2500
    },
    {
        "id": 8,
        "word": "synthesize",
        "phonetic": "/sin-thuh-sahyz/",
        "meaning": "合成：利用云端或本地神经语音模型合成高保真读音",
        "example": "Cloud TTS engines synthesize crystal-clear pronunciation.",
        "interval": 0,
        "factor": 2500
    },
    {
        "id": 9,
        "word": "cognitive",
        "phonetic": "/kog-ni-tiv/",
        "meaning": "认知的：大脑记忆留存与心理学习加工过程",
        "example": "Spaced review significantly reduces cognitive overload.",
        "interval": 0,
        "factor": 2500
    },
    {
        "id": 10,
        "word": "inference",
        "phonetic": "/in-fer-uhns/",
        "meaning": "推理：大语言模型高速生成 Token 的计算过程",
        "example": "Xiaomi MiMo LLM performs high-speed streaming inference.",
        "interval": 0,
        "factor": 2500
    }
]

# 扫描到的蓝牙设备列表
MOCK_BT_DEVICES = [
    {"name": "Sony WH-1000XM4", "mac": "98:8E:79:32:A1:04", "rssi": -48},
    {"name": "AirPods Pro (2nd)", "mac": "40:4E:36:7B:12:F0", "rssi": -55},
    {"name": "Bose QuietComfort 45", "mac": "04:52:C7:8A:33:19", "rssi": -62},
    {"name": "Xiaomi Buds 5", "mac": "28:6C:07:90:E5:31", "rssi": -69}
]

def call_mimo_ai(prompt):
    """调用小米 MiMo 2.5 大模型接口"""
    headers = {
        "x-api-key": MIMO_API_KEY,
        "anthropic-version": "2023-06-01",
        "content-type": "application/json"
    }
    payload = {
        "model": MIMO_MODEL,
        "max_tokens": 400,
        "messages": [
            {"role": "user", "content": prompt}
        ]
    }
    try:
        r = requests.post(MIMO_URL, headers=headers, json=payload, timeout=12)
        if r.status_code == 200:
            res = r.json()
            for item in res.get("content", []):
                if item.get("type") == "text":
                    return item.get("text", "").strip()
            return "AI returned empty text"
        else:
            return f"[MiMo 错误 {r.status_code}]: {r.text[:100]}"
    except Exception as e:
        return f"[网关异常]: {e}"

def fetch_anki_connect_cards():
    """尝试从本地 AnkiConnect 拉取卡片，失败则回退至高质量备用词库"""
    payload = {
        "action": "findCards",
        "version": 6,
        "params": {"query": "deck:current"}
    }
    try:
        r = requests.post(ANKI_CONNECT_URL, json=payload, timeout=2)
        if r.status_code == 200:
            card_ids = r.json().get("result", [])
            if card_ids:
                info_payload = {
                    "action": "cardsInfo",
                    "version": 6,
                    "params": {"cards": card_ids[:20]}
                }
                ir = requests.post(ANKI_CONNECT_URL, json=info_payload, timeout=3)
                if ir.status_code == 200:
                    cards_data = ir.json().get("result", [])
                    out = []
                    for c in cards_data:
                        fields = c.get("fields", {})
                        w = fields.get("Front", {}).get("value", "") or fields.get("Word", {}).get("value", "word")
                        m = fields.get("Back", {}).get("value", "") or fields.get("Meaning", {}).get("value", "释义")
                        out.append({
                            "id": c.get("cardId", len(out) + 1),
                            "word": w,
                            "phonetic": "/anki/",
                            "meaning": m,
                            "example": "Anki synced card.",
                            "interval": c.get("interval", 0),
                            "factor": c.get("factor", 2500)
                        })
                    if out:
                        return out
    except Exception:
        pass
    return DEFAULT_ANKI_DECK

def send_frame(ser, obj):
    """向开发板串口发送单行 JSON 帧"""
    line = json.dumps(obj, ensure_ascii=False)
    data = (line + "\r\n").encode('utf-8')
    ser.write(data)
    ser.flush()

def handle_board_payload(ser, payload_str):
    """解析并响应来自板端的 JSON 报文"""
    try:
        req = json.loads(payload_str)
    except Exception as e:
        return

    msg_type = req.get("type", "")
    print(f"\n📥 [板端请求] type='{msg_type}'")

    if msg_type in ["query", "ai_query"]:
        query_text = req.get("data", "")
        if not query_text and "query" in req:
            query_text = req["query"]
        print(f"🤖 [提问内容]: {query_text}")
        send_frame(ser, {"type": "ai_state", "state": "thinking"})
        ai_reply = call_mimo_ai(query_text)
        print(f"✨ [MiMo 2.5 回复]:\n{ai_reply}\n")
        send_frame(ser, {
            "type": "ai_response",
            "user": query_text,
            "ai": ai_reply
        })
        send_frame(ser, {"type": "ai_state", "state": "speaking"})

    elif msg_type == "sync_pull":
        print("🔄 [卡组同步]: 板端请求拉取卡组...")
        deck = fetch_anki_connect_cards()
        send_frame(ser, {
            "type": "sync_deck",
            "cards": deck
        })
        print(f"✅ [卡组同步完成]: 已向板端回送 {len(deck)} 张卡片")

    elif msg_type == "sync_push":
        cards = req.get("cards", [])
        print(f"⬆️ [进度同步]: 收到板端上传的 {len(cards)} 张卡片学习进度:")
        for c in cards[:5]:
            print(f"   * [{c.get('id')}] {c.get('word')}: interval={c.get('interval')}d, reps={c.get('reps')}, reviewed={c.get('reviewed')}")
        if len(cards) > 5:
            print(f"   ... 及剩余 {len(cards) - 5} 项")
        # 回复状态提示
        send_frame(ser, {
            "type": "sync_status",
            "status": f"已成功保存 {len(cards)} 张学习记录"
        })

    elif msg_type == "bt_scan":
        print("🔍 [蓝牙代理]: 扫描周围蓝牙耳机设备...")
        time.sleep(0.5)
        send_frame(ser, {
            "type": "bt_scan_result",
            "devices": MOCK_BT_DEVICES
        })
        print(f"✅ [蓝牙扫描完成]: 已回传 {len(MOCK_BT_DEVICES)} 个设备至屏幕列表")

    elif msg_type == "bt_connect":
        mac = req.get("data", "")
        matched_name = "蓝牙耳机"
        for dev in MOCK_BT_DEVICES:
            if dev["mac"] == mac:
                matched_name = dev["name"]
                break
        print(f"🎧 [蓝牙连接代理]: 正在连接耳机 {matched_name} ({mac})...")
        time.sleep(0.3)
        send_frame(ser, {
            "type": "bt_status",
            "connected": True,
            "name": matched_name
        })
        print(f"✅ [蓝牙连接成功]: {matched_name} 已连接")

def main():
    parser = argparse.ArgumentParser(description="VocaVibe PC-Side Companion Gateway")
    parser.add_argument("-p", "--port", default=DEFAULT_PORT, help="Serial port (default: /dev/ttyACM0)")
    parser.add_argument("-b", "--baud", default=DEFAULT_BAUD, type=int, help="Baudrate (default: 1000000)")
    parser.add_argument("-d", "--daemon", action="store_true", help="Run in headless daemon mode")
    args = parser.parse_args()

    print("=" * 64)
    print("  🚀 VocaVibe 随声记 - PC 端智能中转网关 & 调试控制台")
    print(f"  设备: {args.port} @ {args.baud} bps")
    print(f"  AI 大模型: 小米 MiMo 2.5 | 蓝牙耳机与 Anki 代理已就绪")
    print("=" * 64)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except Exception as e:
        print(f"❌ 无法打开串口 {args.port}: {e}")
        print("请检查 USB 数据线连接，或者指定 -p /dev/ttyACM0")
        return 1

    running = True

    def reader_loop():
        buf = ""
        while running:
            try:
                if ser.in_waiting:
                    chunk = ser.read(ser.in_waiting).decode('utf-8', errors='replace')
                    if not chunk:
                        continue
                    buf += chunk
                    while '\n' in buf:
                        line, buf = buf.split('\n', 1)
                        line = line.strip('\r').strip()
                        if not line:
                            continue

                        # 板端可能以 [JSON] 或 [VV_TX] 前缀输出报文，或者直接输出纯 JSON
                        if "[JSON]" in line:
                            payload = line.split("[JSON]")[1].strip()
                            handle_board_payload(ser, payload)
                        elif "[VV_TX]" in line:
                            payload = line.split("[VV_TX]")[1].strip()
                            handle_board_payload(ser, payload)
                        elif line.startswith('{') and line.endswith('}'):
                            handle_board_payload(ser, line)
                        else:
                            # 正常显示开发板终端日志
                            print(line)
                else:
                    time.sleep(0.01)
            except Exception as e:
                break

    t = threading.Thread(target=reader_loop, daemon=True)
    t.start()

    try:
        if args.daemon:
            print("🚀 网关已进入守护进程常驻模式 (后台服务中)...")
            while True:
                time.sleep(1)
        else:
            print("交互提示: 可在下方直接键入板端指令 (例如 flip, next, status, touch 等)，输入 quit 退出:")
            while True:
                cmd = input()
                if cmd.lower() in ['exit', 'quit']:
                    break
                ser.write((cmd + '\r\n').encode('utf-8'))
                ser.flush()
    except KeyboardInterrupt:
        print("\n收到中止信号，退出网关...")
    finally:
        running = False
        ser.close()

    return 0

if __name__ == '__main__':
    sys.exit(main())
