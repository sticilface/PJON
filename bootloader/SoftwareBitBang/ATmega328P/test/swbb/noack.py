#!/usr/bin/env python3
"""Frames not acknowledged in a 3 node swbb_sim -e trace (B receives): for
each frame started by A or C after a long idle, prints its first edges if B
did not answer before the next long idle. Usage: noack.py <trace> [idle_us]"""
import sys
idle = float(sys.argv[2]) if len(sys.argv) > 2 else 300
ev = []
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) == 3 and p[1] in '01' and set(p[2]) <= set('ABC-'):
        ev.append((float(p[0]), p[1], p[2]))
frames = []
last_fall = None
for i, (t, l, w) in enumerate(ev):
    if l == '0':
        last_fall = t
    elif last_fall is None or t - last_fall > idle:
        frames.append(i)
for k, i in enumerate(frames):
    j = frames[k + 1] if k + 1 < len(frames) else len(ev)
    if not any('B' in e[2] for e in ev[i:j]):
        seg = ev[max(i - 2, 0):min(i + 16, j)]
        print('frame at %.1f by %s, preceded by %.0fus idle:' % (
            ev[i][0], ev[i][2], ev[i][0] - (ev[i - 1][0] if i else 0)))
        print('  ' + ' '.join('%s%.1f%s' % ('H' if b[1] == '0' else 'L',
              b[0] - a[0], b[2].strip('-')) for a, b in zip(seg, seg[1:])))
