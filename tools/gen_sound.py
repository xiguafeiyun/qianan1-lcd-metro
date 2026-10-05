#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成地铁提示音 WAV（纯 Python 标准库，不需要装任何东西）

用法：
  python gen_sound.py door_open  -o door_open.wav    # 开门：两个音符（一高一低）
  python gen_sound.py door_close -o door_close.wav   # 关门：同高音连响 7 声
  python gen_sound.py beep       -o beep.wav         # 单声提示

自定义示例：
  python gen_sound.py door_close --count 7 --freq 880 --on 90 --off 60 -o b.wav
  python gen_sound.py door_open  --freq 1046 --freq2 784 -o o.wav

参数：
  --rate   采样率（默认 44100）
  --amp    音量 0~1（默认 0.38，留余量不削波）
  --freq   主音高 Hz（0 = 按类型自动）
  --freq2  第二音高 Hz（开门用，0 = 自动）
  --count  响几声（关门用，默认 7）
  --on     每声持续毫秒（默认 90）
  --off    每声之间静音毫秒（默认 60）
"""

import argparse
import math
import struct
import wave


def envelope(i, n, attack_ms, release_ms, rate):
    """淡入淡出，避免爆音（'咔'的一声）"""
    a = max(1, int(rate * attack_ms / 1000.0))
    r = max(1, int(rate * release_ms / 1000.0))
    if i < a:
        return i / a
    if i > n - r:
        return max(0.0, (n - i) / r)
    return 1.0


def tone(freq, dur_ms, rate, amp, attack_ms=8.0, release_ms=40.0, harmonic=0.28):
    """一节音：正弦 + 一点二次谐波（听着更饱满，更像电子蜂鸣器）"""
    n = int(rate * dur_ms / 1000.0)
    out = []
    for i in range(n):
        t = i / rate
        env = envelope(i, n, attack_ms, release_ms, rate)
        s = math.sin(2 * math.pi * freq * t) + harmonic * math.sin(4 * math.pi * freq * t)
        v = max(-1.0, min(1.0, s * amp * env))
        out.append(int(v * 32767))
    return out


def silence(dur_ms, rate):
    return [0] * int(rate * dur_ms / 1000.0)


def build(kind, args):
    rate, amp = args.rate, args.amp
    buf = []

    if kind in ("door_close", "bell"):
        # 关门铃：一个"高+低" = 一段旋律（两音挨得近），旋律之间空一段【明显】的
        hi = args.freq or 1046.5          # C6
        lo = args.freq2 or 784.0          # G5
        for k in range(args.pairs):
            buf += tone(hi, args.on, rate, amp, attack_ms=6.0, release_ms=90.0, harmonic=0.18)
            buf += silence(args.off, rate)          # 旋律内部：高 → 低，挨得近
            buf += tone(lo, args.on, rate, amp, attack_ms=6.0, release_ms=90.0, harmonic=0.18)
            if k != args.pairs - 1:
                buf += silence(args.gap, rate)      # 旋律之间：空明显的一段

    elif kind in ("door_open", "chime"):
        # 单组两音（备用）
        hi = args.freq or 1046.5
        lo = args.freq2 or 784.0
        for f in (hi, lo):
            buf += tone(f, args.on, rate, amp, attack_ms=6.0, release_ms=180.0, harmonic=0.18)
            buf += silence(args.off, rate)

    elif kind == "beep":
        buf += tone(args.freq or 880.0, 180, rate, amp)

    else:
        raise SystemExit("未知类型: " + kind)

    buf += silence(80, rate)          # 尾巴留一点静音，免得起止被切
    return buf, rate


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("kind", choices=["door_close", "door_open", "bell", "chime", "beep"])
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--rate", type=int, default=44100)
    ap.add_argument("--amp", type=float, default=0.38)
    ap.add_argument("--freq", type=float, default=0.0)
    ap.add_argument("--freq2", type=float, default=0.0)
    ap.add_argument("--pairs", type=int, default=5, help="重复几段旋律（高+低）")
    ap.add_argument("--count", type=int, default=7, help="连响几声（备用）")
    ap.add_argument("--on", type=int, default=220, help="每个音多长 ms")
    ap.add_argument("--off", type=int, default=90, help="旋律内部 高→低 的间隔 ms（小）")
    ap.add_argument("--gap", type=int, default=700, help="旋律与旋律之间的间隔 ms（大）")
    args = ap.parse_args()

    buf, rate = build(args.kind, args)
    with wave.open(args.out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack("<%dh" % len(buf), *buf))

    print("已生成 %s  (%.2f 秒, %d Hz, 单声道 16bit)" % (args.out, len(buf) / rate, rate))


if __name__ == "__main__":
    main()
