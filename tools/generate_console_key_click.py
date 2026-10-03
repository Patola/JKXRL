#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the original, non-tonal spatial-keyboard click (no external samples)."""
import argparse
import io
import math
from pathlib import Path
import random
import struct
import wave

DEST = Path(__file__).resolve().parents[1] / 'z_vr_assets_base/sound/interface/console_key.wav'


def make_click():
    rate = 44100
    count = int(rate * 0.024)
    rng = random.Random(314159)
    low = slow = 0.0
    samples = []
    for i in range(count):
        t = i / rate
        noise = rng.uniform(-1, 1)
        low += 0.32 * (noise - low)
        slow += 0.035 * (low - slow)
        envelope = min(1.0, t / 0.0005) * math.exp(-t / 0.0038)
        envelope *= (1 - i / (count - 1)) ** 2
        samples.append((low - slow) * envelope)
    peak = max(abs(s) for s in samples)
    pcm = [round(s / peak * 0.20 * 32767) for s in samples]
    stream = io.BytesIO()
    with wave.open(stream, 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(struct.pack('<' + 'h' * count, *pcm))
    return stream.getvalue()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    content = make_click()
    if args.check:
        if not DEST.is_file() or DEST.read_bytes() != content:
            raise SystemExit('Console click missing/stale; run tools/generate_console_key_click.py')
    else:
        DEST.parent.mkdir(parents=True, exist_ok=True)
        DEST.write_bytes(content)
    print(f'Console click: 24 ms, mono PCM16, 44100 Hz, peak -14 dBFS: {DEST}')
