#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure the MIDI clock an FM-1 running AMENITY gets, and how well it follows it, over USB-MIDI.

  python tools/jungle/fm1_clocklog.py [--seconds 60] [--csv build/clocklog]

Start this, then play the sender (a DAW, a drum machine) with its clock going to the FM-1. It reads AMENITY's logs
(app/user_breaks.c CMD_CLOCK): the time each clock pulse was read (24 MHz), the tempo AMENITY's loop has found, and,
per audio block while it follows, how far the break is from the loop's time line. Then it says:
  - the sender's tempo (a straight line through the pulses) and how far each pulse is from that line (jitter);
  - whether pulses come in bunches (a DAW that sends its clock once per audio buffer);
  - the sender's tempo beat by beat, and AMENITY's, and how much each moves;
  - how far the break strays from the clock.
With --csv it also writes PREFIX-pulses.csv and PREFIX-behind.csv. Needs mido with python-rtmidi; close the breaks
page and other MIDI apps that use the FM-1 first.
"""
import argparse
import statistics
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fm1_breaks import Link  # noqa: E402  (the port and the framing)

CMD_CLOCK = 0x64
HZ = 24_000_000


def fetch(link, which, start):
    """-> (first index, entries, period or None)"""
    b = link.command(CMD_CLOCK, bytes([which]) + struct.pack('<I', start), timeout=2.0)
    first, n = struct.unpack_from('<IB', b, 1)
    at, per = 6, None
    if which == 0:
        per = struct.unpack_from('<f', b, at)[0]
        at += 4
        return first, list(struct.unpack_from(f'<{n}I', b, at)), per
    return first, list(struct.unpack_from(f'<{n}h', b, at)), None


def fit(xs, ys):
    """least squares: -> slope, intercept"""
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    a = sxy / sxx
    return a, my - a * mx


def bpm_of(per_ticks):
    return 60.0 * HZ / (24.0 * per_ticks)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--seconds', type=float, default=60.0)
    ap.add_argument('--csv', help='write PREFIX-pulses.csv and PREFIX-behind.csv')
    a = ap.parse_args()
    link = Link()
    pulses, behind, periods, lost = [], [], [], 0
    p_next = b_next = 0                              # (what the logs hold already comes too: the last run is used)
    end = time.monotonic() + a.seconds
    print(f'listening for {a.seconds:.0f} s: play the sender now (Ctrl+C to stop sooner)')
    try:
        while time.monotonic() < end:
            for which in (0, 1):
                while True:
                    start = p_next if which == 0 else b_next
                    first, items, per = fetch(link, which, start)
                    if first > start and start:
                        lost += first - start
                    if which == 0:
                        pulses += [(first + i, t) for i, t in enumerate(items)] if items else []
                        if per:
                            periods.append((time.monotonic(), per))
                        p_next = first + len(items) if items else start
                    else:
                        behind += items
                        b_next = first + len(items) if items else start
                    if len(items) < (22 if which == 0 else 44):
                        break
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    print(f'{len(pulses)} pulses, {len(behind)} audio blocks while following' +
          (f', {lost} entries lost (the log ran over: read more often)' if lost else ''))
    if len(pulses) < 48:
        sys.exit('too few pulses: is the sender sending clock to the FM-1?')

    # the pulses: one run of the sender (from the last gap of half a second)
    ts = [t for _, t in pulses]
    begin = 0
    for i in range(1, len(ts)):
        if (ts[i] - ts[i - 1]) & 0xFFFFFFFF > HZ // 2:
            begin = i
    run = [(ts[i] - ts[begin]) & 0xFFFFFFFF for i in range(begin, len(ts))]
    xs = list(range(len(run)))
    slope, icpt = fit(xs, run)
    res = [(y - (slope * x + icpt)) * 1000.0 / HZ for x, y in zip(xs, run)]
    print(f'\nsender: {bpm_of(slope):.3f} BPM over {len(run)} pulses ({run[-1] / HZ:.1f} s)')
    print(f'  each pulse against that line: sd {statistics.pstdev(res):.2f} ms, '
          f'{min(res):+.2f} .. {max(res):+.2f} ms')
    gaps = [(run[i] - run[i - 1]) * 1000.0 / HZ for i in range(1, len(run))]
    together = sum(1 for g in gaps if g < 0.5)
    print(f'  from one pulse to the next: median {statistics.median(gaps):.2f} ms, '
          f'{min(gaps):.2f} .. {max(gaps):.2f} ms; read together with the one before: {together} of {len(gaps)}')
    beats = [bpm_of((run[i + 24] - run[i]) / 24.0) for i in range(0, len(run) - 24, 24)]
    if len(beats) > 1:
        print(f'  beat by beat: {min(beats):.2f} .. {max(beats):.2f} BPM (sd {statistics.pstdev(beats):.3f})')
    if periods:
        tempos = [bpm_of(p) for _, p in periods[len(periods) // 5:]]   # (after the first fifth: settled)
        print(f'AMENITY\'s tempo (its loop, read {len(periods)} times): {min(tempos):.3f} .. {max(tempos):.3f} BPM '
              f'(sd {statistics.pstdev(tempos):.4f})')
    if behind:
        tick_ms = 60000.0 / (bpm_of(slope) * 192.0)                     # an engine tick at that tempo
        ms = [v / 16.0 * tick_ms for v in behind[len(behind) // 5:]]
        print(f'the break against the clock (positive: behind), per audio block: mean {statistics.mean(ms):+.2f} ms, '
              f'sd {statistics.pstdev(ms):.2f} ms, {min(ms):+.2f} .. {max(ms):+.2f} ms')
    if a.csv:
        Path(a.csv).parent.mkdir(parents=True, exist_ok=True)
        with open(f'{a.csv}-pulses.csv', 'w') as f:
            f.write('index,t_ms,off_line_ms\n')
            for x, y, r in zip(xs, run, res):
                f.write(f'{x},{y * 1000.0 / HZ:.4f},{r:.4f}\n')
        with open(f'{a.csv}-behind.csv', 'w') as f:
            f.write('block,behind_ticks\n')
            for i, v in enumerate(behind):
                f.write(f'{i},{v / 16.0:.4f}\n')
        print(f'wrote {a.csv}-pulses.csv and {a.csv}-behind.csv')


if __name__ == '__main__':
    main()
