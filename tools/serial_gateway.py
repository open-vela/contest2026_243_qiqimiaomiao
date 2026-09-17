#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VocaVibe Unified AI Gateway & Serial Console
- Bridges board requests to MiMo 2.5 AI Cloud & Local Anki
- Handles interactive terminal IO at 1,000,000 baud
"""

import sys
import os
import time
import json
import threading
import requests
import serial

MIMO_API_KEY = "tp-c2rn3aytnmxcfash8yv6xenmzntatkev0btwhp06540wnhz3"
MIMO_URL = "https://token-plan-cn.xiaomimimo.com/anthropic/v1/messages"
MIMO_MODEL = "mimo-v2.5"

PORT = "/dev/ttyACM1"
BAUD = 1000000

def call_mimo_ai(prompt):
    headers = {
        "x-api-key": MIMO_API_KEY,
        "anthropic-version": "2023-06-01",
        "content-type": "application/json"
    }
    payload = {
        "model": MIMO_MODEL,
        "max_tokens": 300,
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
            return f"[MiMo Error {r.status_code}]: {r.text[:100]}"
    except Exception as e:
        return f"[Gateway Exception]: {e}"

def main():
    print("=" * 60)
    print("  🚀 VocaVibe 智能硬件终端 & MiMo 2.5 AI 网关")
    print(f"  串口: {PORT} @ {BAUD} bps | 大模型: 小米 {MIMO_MODEL}")
    print("=" * 60)

    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
    except Exception as e:
        print(f"❌ 无法打开串口 {PORT}: {e}")
        return

    running = True

    def serial_read_loop():
        while running:
            try:
                if ser.in_waiting:
                    line = ser.readline().decode('utf-8', errors='replace')
                    if not line:
                        continue
                    
                    # Check if board sent a query frame: [VV_TX]{"type":"query","data":"..."}
                    if "[VV_TX]" in line:
                        payload_str = line.split("[VV_TX]")[1].strip()
                        try:
                            req = json.loads(payload_str)
                            if req.get("type") == "query":
                                query_text = req.get("data", "")
                                print(f"\n🤖 [板端发起 AI 提问]: {query_text}")
                                print("⏳ [MiMo 2.5 云端推理中...]")
                                ai_reply = call_mimo_ai(query_text)
                                print(f"✨ [MiMo 2.5 回复]:\n{ai_reply}\n")
                                
                                # Send response back to board
                                frame = json.dumps({"type": "ai_response", "data": ai_reply}, ensure_ascii=False)
                                ser.write(f"{frame}\r\n".encode('utf-8'))
                                print("vocavibe> ", end='', flush=True)
                        except Exception as e:
                            pass
                    else:
                        print(line, end='', flush=True)
                else:
                    time.sleep(0.01)
            except Exception:
                break

    t = threading.Thread(target=serial_read_loop, daemon=True)
    t.start()

    try:
        while True:
            cmd = input()
            if cmd.lower() in ['exit', 'quit']:
                break
            ser.write((cmd + '\r\n').encode('utf-8'))
    except KeyboardInterrupt:
        print("\n退出网关交互...")
    finally:
        running = False
        ser.close()

if __name__ == '__main__':
    main()
