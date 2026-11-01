"""Interrupt time of node B from a swbb_sim trace (B built with
SWBB_IRQ_PIN_DEBUG: PD3 high while the SoftwareBitBang interrupts run).
Prints the duration distribution and the share of CPU time during frames."""
import sys, collections

start, total, hist, durations = None, 0.0, collections.Counter(), []
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) == 3 and p[1] == 'dbg':
        t = float(p[0])
        if p[2] == '1':
            start = t
        elif start is not None:
            d = t - start
            durations.append((start, d))
            hist[round(d)] += 1
            start = None
print('interrupts:', len(durations), 'duration us (count):',
      sorted(hist.items()))
# CPU share in 1ms windows with traffic
windows = collections.Counter()
for s, d in durations:
    windows[int(s // 1000)] += d
busy = [v / 1000 for v in windows.values()]
if busy:
    print('CPU share in 1ms windows with edges: max %.0f%%, mean %.0f%%'
          % (100 * max(busy), 100 * sum(busy) / len(busy)))
