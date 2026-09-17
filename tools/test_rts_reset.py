#!/usr/bin/env python3
import serial
import time
import glob
import os
import sys

def get_uart_port():
    # Prefer CH343 USB-to-UART device
    ch343 = glob.glob('/dev/serial/by-id/*1a86*')
    if ch343 and os.path.exists(ch343[0]):
        return os.path.realpath(ch343[0])
    for p in ['/dev/ttyACM1', '/dev/ttyACM0', '/dev/ttyUSB0']:
        if os.path.exists(p):
            return p
    return '/dev/ttyACM1'

def test_reset():
    port = get_uart_port()
    baud = 1000000
    print(f"[RTS Reset] Connecting to UART bridge: {port} at {baud}...")
    try:
        ser = serial.Serial(port, baud, timeout=0.5)
    except Exception as e:
        print(f"[RTS Reset] Failed to open serial port: {e}")
        return False

    print("[RTS Reset] Pulsing RTS to hard-reset SoC...")
    ser.rts = True
    time.sleep(0.08)
    ser.rts = False
    print("[RTS Reset] Released RTS. Reading boot logs for 3 seconds...")

    start_time = time.time()
    boot_logs = []
    while time.time() - start_time < 3.0:
        line = ser.readline()
        if line:
            decoded = line.decode('utf-8', errors='replace').strip()
            if decoded:
                print(f"  [Board Boot] {decoded}")
                boot_logs.append(decoded)

    ser.close()
    if boot_logs:
        print(f"\n[RTS Reset] >>> SUCCESS! Received {len(boot_logs)} boot lines from board. <<<")
        return True
    else:
        print("\n[RTS Reset] WARNING: No output received after RTS pulse.")
        return False

if __name__ == "__main__":
    test_reset()
