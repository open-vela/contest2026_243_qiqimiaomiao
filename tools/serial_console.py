#!/usr/bin/env python3
"""
SF32LB52 串口交互控制台

背景：picocom 的回车（换行符）问题一直没解决（板子只认 \\r\\n，
picocom 的 --imap lfcrlf 救不回来），所以用这个 Python 脚本代替。

用法：
    python3 serial_console.py [串口号]      # 默认 /dev/ttyACM0

退出：
    输入 exit 或 quit，或按 Ctrl+C

说明（2026-08-26 实测）：
    - RTS 复位并不可靠，不一定让板子重启。
    - 板子烧录后本来就是运行着的（在 nsh> 等命令），所以脚本不依赖复位，
      打开串口直接进交互循环即可。
    - 如果确实想重启板子：按板上的 Reset 键或拔插 USB 线，启动日志会流式打印出来。
    - 原理：用 select 同时监听"键盘输入"和"串口数据"，两边都不阻塞。
"""

import serial
import select
import sys
import time

import glob
import os

def auto_find_port():
    ch343 = glob.glob('/dev/serial/by-id/*1a86*')
    if ch343 and os.path.exists(ch343[0]):
        return os.path.realpath(ch343[0])
    for p in ['/dev/ttyACM1', '/dev/ttyACM0', '/dev/ttyUSB0']:
        if os.path.exists(p):
            return p
    return '/dev/ttyACM0'

PORT = sys.argv[1] if len(sys.argv) > 1 else auto_find_port()
BAUD = 1000000


def main():
    ser = serial.Serial(PORT, BAUD, timeout=0)
    try:
        # 尽力复位（失败了也没关系，板子可能本来就在运行）
        try:
            ser.rts = True
            time.sleep(0.05)
            ser.rts = False
        except Exception:
            pass

        print("=== 串口交互控制台（输入 exit 或 Ctrl+C 退出）===", flush=True)
        print("    板子在运行的话直接敲命令即可；想重启按板上 Reset 键。", flush=True)

        while True:
            # 1) 把串口收到的数据全部打印出来（非阻塞）
            while True:
                chunk = ser.read(4096)
                if not chunk:
                    break
                sys.stdout.write(chunk.decode(errors="replace"))
                sys.stdout.flush()

            # 2) 看键盘有没有输入（不阻塞，0.05s 一查）
            rlist, _, _ = select.select([sys.stdin], [], [], 0.05)
            if rlist:
                line = sys.stdin.readline()
                if not line:  # 输入流结束
                    break
                cmd = line.rstrip("\n")
                if cmd in ("exit", "quit"):
                    break
                if cmd.strip():
                    ser.write((cmd + "\r\n").encode())
                    time.sleep(0.2)  # 给板子一点响应时间
    except KeyboardInterrupt:
        print("\n[已退出]")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
