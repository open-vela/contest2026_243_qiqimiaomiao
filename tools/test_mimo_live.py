#!/usr/bin/env python3
import requests
import json
import serial
import time

MIMO_API_KEY = "tp-c2rn3aytnmxcfash8yv6xenmzntatkev0btwhp06540wnhz3"
MIMO_URL = "https://token-plan-cn.xiaomimimo.com/anthropic/v1/messages"

def test_mimo_direct():
    print("[1/2] Testing MiMo Cloud API connectivity from PC gateway...")
    headers = {
        "x-api-key": MIMO_API_KEY,
        "anthropic-version": "2023-06-01",
        "content-type": "application/json"
    }
    payload = {
        "model": "claude-3-5-sonnet-20241022",
        "max_tokens": 150,
        "messages": [
            {"role": "user", "content": "你好，请用一句话介绍你作为 VocaVibe 英语学习助手的自我介绍。"}
        ]
    }
    try:
        resp = requests.post(MIMO_URL, headers=headers, json=payload, timeout=10)
        print(f"  [MiMo Cloud Response Status]: {resp.status_code}")
        if resp.status_code == 200:
            res_json = resp.json()
            reply = res_json['content'][0]['text']
            print(f"  [MiMo AI Reply]: {reply.strip()}\n")
            return reply.strip()
        else:
            print(f"  [Error]: {resp.text}")
            return None
    except Exception as e:
        print(f"  [Exception]: {e}")
        return None

def test_push_to_board(ai_reply):
    if not ai_reply:
        return
    print("[2/2] Pushing AI response into SF32LB52 Board Console...")
    ser = serial.Serial('/dev/ttyACM1', 1000000, timeout=1.0)
    ser.write(b'\r\nstatus\r\n')
    time.sleep(0.3)
    out = ser.read(ser.in_waiting or 500).decode('utf-8', errors='replace')
    print("  [Board Current State]:\n", out.strip())
    
    # Send simulated query from board and push AI response
    frame = json.dumps({"type": "ai_response", "data": ai_reply}, ensure_ascii=False)
    ser.write(f"{frame}\r\n".encode('utf-8'))
    time.sleep(0.3)
    resp = ser.read(ser.in_waiting or 500).decode('utf-8', errors='replace')
    print("  [Board Received & Processed Frame]:\n", resp.strip())
    ser.close()

if __name__ == '__main__':
    reply = test_mimo_direct()
    test_push_to_board(reply)
