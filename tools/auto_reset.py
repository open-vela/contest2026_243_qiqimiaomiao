#!/usr/bin/env python3
import serial
import time
import sys

def hard_reset(port='/dev/ttyACM1', baud=1000000):
    print(f"[Auto Reset] Opening {port}...")
    ser = serial.Serial(port, baud, timeout=0.5)
    
    print("[Auto Reset] Pulling RTS LOW for 300ms (draining VCC load switch)...")
    ser.rts = True
    time.sleep(0.3)
    ser.rts = False
    print("[Auto Reset] Released RTS. Waiting for NSH boot banner...")
    
    start = time.time()
    booted = False
    while time.time() - start < 3.0:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='replace').strip()
            if line:
                print(f"  [Boot Log] {line}")
                if "NuttShell" in line or "nsh>" in line:
                    booted = True
                    break
        time.sleep(0.05)
        
    ser.close()
    if booted:
        print("\n[Auto Reset] >>> SUCCESS! Board rebooted cleanly via software RTS signal! <<<")
    else:
        print("\n[Auto Reset] Note: Chip did not reboot via RTS.")

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM1'
    hard_reset(port)
