#!/usr/bin/env python3
import serial
import time
import glob
import os
import sys

def get_port():
    ch343 = glob.glob('/dev/serial/by-id/*1a86*')
    if ch343 and os.path.exists(ch343[0]):
        return os.path.realpath(ch343[0])
    for p in ['/dev/ttyACM1', '/dev/ttyACM0', '/dev/ttyUSB0']:
        if os.path.exists(p):
            return p
    return '/dev/ttyACM1'

def run_cmd(cmd="help"):
    port = get_port()
    ser = serial.Serial(port, 1000000, timeout=0.3)
    
    # Flush incoming buffer
    ser.read(ser.in_waiting or 1)
    
    # Send newline to sync prompt
    ser.write(b"\r\n")
    time.sleep(0.1)
    
    # Send actual command
    ser.write((cmd + "\r\n").encode('utf-8'))
    time.sleep(0.5)
    
    # Read output
    output = b""
    start = time.time()
    while time.time() - start < 1.5:
        if ser.in_waiting:
            output += ser.read(ser.in_waiting)
        time.sleep(0.05)
        
    ser.close()
    print(output.decode('utf-8', errors='replace'))

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else "help"
    run_cmd(cmd)
