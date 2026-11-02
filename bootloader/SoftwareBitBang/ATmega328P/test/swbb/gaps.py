#!/usr/bin/env python3
"""Idle gaps before each frame start in a swbb_sim -e trace: the time the
bus stayed low before a transmitter (A or C) drove it high after a long
idle (> min_us). Usage: gaps.py <trace> [min_us]"""
import sys, collections
min_us = float(sys.argv[2]) if len(sys.argv) > 2 else 150
ev = []
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) == 3 and p[1] in '01' and set(p[2]) <= set('ABC-'):
        ev.append((float(p[0]), p[1], p[2]))
last_fall = None
gaps = []
busy = 0.0
last_t = None
level = '0'
for t, l, w in ev:
    if last_t is not None and level == '1':
        busy += t - last_t
    last_t, level = t, l
    if l == '0':
        last_fall = t
    elif last_fall is not None and t - last_fall > min_us:
        gaps.append(t - last_fall)
if gaps:
    gaps.sort()
    n = len(gaps)
    print('gaps %d: min %.0f median %.0f p90 %.0f max %.0f mean %.0f us' % (
        n, gaps[0], gaps[n // 2], gaps[int(n * 0.9)], gaps[-1], sum(gaps) / n))
    h = collections.Counter(int(g // 100) * 100 for g in gaps)
    print('histogram (100us bins):', sorted(h.items()))
