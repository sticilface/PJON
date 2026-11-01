"""Acknowledgement latency from a swbb_sim edge trace (SIM_FLAGS=-e, A
transmits, B receives): for each frame prints when the transmitter starts its
keep busy bits, when the response starts and when the last keep busy bit was
transmitted, in microseconds after the end of the frame."""
import sys

ev = []
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) == 3 and p[1] in ('0', '1'):
        ev.append((float(p[0]), p[1], p[2]))

i = 1
while i < len(ev) - 1:
    t, level, who = ev[i]
    if level == '1' and who == 'A-' and ev[i + 1][0] - t < 12:
        end = ev[i - 1][0]  # last edge of the frame
        k, last = i, None
        while (k + 1 < len(ev) and ev[k][1] == '1' and ev[k][2] == 'A-'
               and ev[k + 1][0] - ev[k][0] < 12):
            last = ev[k][0]
            k += 2
        response = None
        if k < len(ev) and ev[k][2] == '-B' and ev[k][0] - end < 5000:
            response = round(ev[k][0] - end)
        print('keep busy from %dus, response at %s, last keep busy at %dus'
              % (round(t - end), response, round(last - end)))
        i = k
    else:
        i += 1
