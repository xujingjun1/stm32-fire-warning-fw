#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
fake_vision_sender.py —— PC 端"假装自己是 RK3568"

作用：在你还没有板子、模型还没训练完、RKNN 还没量化通过的时候，
      用这个脚本按协议定时往 STM32 发视觉结果帧，把主控侧的
      解析 / 融合 / 联动 / 上云 / 显示全部先调通。
      这是把最难的视觉部署从关键路径上摘下来的关键一步。

依赖：pip install pyserial

用法：
  python fake_vision_sender.py --dry-run --scenario fire      # 只打印十六进制，不开串口
  python fake_vision_sender.py --port COM3 --scenario fire
  python fake_vision_sender.py --port COM3 --scenario smoke --period 0.5
  python fake_vision_sender.py --port COM3 --scenario garbage # 注入坏帧，测解析器健壮性
  python fake_vision_sender.py --list                          # 列出可用串口

scenario:
  none        发空背景帧（N=1, cls=0, conf=0）
  fire        火焰，置信度 --conf
  smoke       烟雾，置信度 --conf
  both        火焰 + 烟雾
  garbage     交替发正常帧和被篡改的坏帧，用于验证解析器不会误触发
"""

import argparse
import sys
import time

HEAD1, HEAD2 = 0xAA, 0x55
TAIL1, TAIL2 = 0x55, 0xAA

CLS_FIRE = 1
CLS_SMOKE = 2


def build_frame(objects):
    """objects: [(cls, conf), ...]，返回 bytes。校验 = (N + sum(cls) + sum(conf)) & 0xFF"""
    n = len(objects)
    if n == 0 or n > 8:
        raise ValueError("目标个数必须在 1..8 之间，当前 %d" % n)

    body = bytearray()
    body.append(n)
    chk = n
    for cls, conf in objects:
        body.append(cls & 0xFF)
        chk += cls & 0xFF
    for cls, conf in objects:
        body.append(conf & 0xFF)
        chk += conf & 0xFF
    body.append(chk & 0xFF)

    return bytes([HEAD1, HEAD2]) + bytes(body) + bytes([TAIL1, TAIL2])


def make_frame(scenario, conf):
    if scenario == "none":
        return build_frame([(0, 0)])
    if scenario == "fire":
        return build_frame([(CLS_FIRE, conf)])
    if scenario == "smoke":
        return build_frame([(CLS_SMOKE, conf)])
    if scenario == "both":
        return build_frame([(CLS_FIRE, conf), (CLS_SMOKE, max(0, conf - 20))])
    raise ValueError("未知场景: %s" % scenario)


def corrupt(frame):
    """篡改校验字节，制造一个坏帧"""
    b = bytearray(frame)
    b[-3] = (b[-3] + 1) & 0xFF
    return bytes(b)


def to_hex(frame):
    return " ".join("%02X" % x for x in frame)


def main():
    ap = argparse.ArgumentParser(description="模拟 RK3568 视觉模块向 STM32 发帧")
    ap.add_argument("--port", default="COM3", help="串口号，如 COM3 或 /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--period", type=float, default=1.0, help="发送周期（秒）")
    ap.add_argument("--scenario", default="fire",
                    choices=["none", "fire", "smoke", "both", "garbage"])
    ap.add_argument("--conf", type=int, default=85, help="置信度 0..100")
    ap.add_argument("--count", type=int, default=0, help="发送次数，0 表示一直发")
    ap.add_argument("--dry-run", action="store_true", help="只打印帧内容，不打开串口")
    ap.add_argument("--list", action="store_true", help="列出可用串口后退出")
    args = ap.parse_args()

    if args.list:
        try:
            from serial.tools import list_ports
            ports = list_ports.comports()
            if not ports:
                print("未发现可用串口")
            for p in ports:
                print("%s  %s" % (p.device, p.description))
        except ImportError:
            print("需要先安装 pyserial：pip install pyserial")
        return 0

    if args.dry_run:
        f = make_frame(args.scenario if args.scenario != "garbage" else "fire", args.conf)
        print("场景 %-8s -> %s  (%d 字节)" % (args.scenario, to_hex(f), len(f)))
        print("坏帧样例     -> %s" % to_hex(corrupt(f)))
        return 0

    try:
        import serial
    except ImportError:
        print("缺少 pyserial，请先执行：pip install pyserial")
        return 1

    print("打开 %s @ %d，场景=%s，周期=%.2fs，Ctrl+C 退出"
          % (args.port, args.baud, args.scenario, args.period))
    ser = serial.Serial(args.port, args.baud, timeout=1)

    sent = 0
    try:
        while True:
            if args.scenario == "garbage":
                f = make_frame("fire", args.conf)
                f = corrupt(f) if (sent % 2 == 1) else f
            else:
                f = make_frame(args.scenario, args.conf)

            ser.write(f)
            print("[%04d] TX %s" % (sent, to_hex(f)))
            sent += 1

            if args.count and sent >= args.count:
                break
            time.sleep(args.period)
    except KeyboardInterrupt:
        print("\n已停止")
    finally:
        ser.close()

    return 0


if __name__ == "__main__":
    sys.exit(main())
