"""Interrupt time of a SWBB_INTERRUPT_WAKE receiver (node B built with
SWBB_IRQ_PIN_DEBUG: PD3 high while its interrupt runs) from a swbb_sim -e
trace: rising edges ignored, frames abandoned (other device or noise) and
frames received."""
import sys

durations, start = [], None
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) == 3 and p[1] == 'dbg':
        t = float(p[0])
        if p[2] == '1':
            start = t
        elif start is not None:
            durations.append(t - start)
            start = None
short = [d for d in durations if d < 50]
mid = [d for d in durations if 50 <= d < 3000]
full = [d for d in durations if d >= 3000]


def mean(v):
    return sum(v) / len(v) if v else 0


print('ignored rising edges: %d, %.1fus each' % (len(short), mean(short)))
print('abandoned frames: %d, %.0fus each' % (len(mid), mean(mid)))
print('received frames: %d, %.0fus each' % (len(full), mean(full)))
print('total interrupt time: %.1fms' % (sum(durations) / 1000))
