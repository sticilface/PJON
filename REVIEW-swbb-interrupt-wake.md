## SoftwareBitBang: interrupt triggered reception (`SWBB_INTERRUPT_WAKE`)

A hybrid of the polling receiver (`SWBB_PIN`) and the interrupt driven one
(`SWBB_INTERRUPT`, see [REVIEW-swbb-interrupt.md](REVIEW-swbb-interrupt.md)):
the rising edge of a frame triggers INT0 (pin 2) or INT1 (pin 3), and the
interrupt receives the whole frame with the cycle exact polling code. All
results come from the two device simavr simulation (ATmega328P, 16MHz), see
[`test/swbb`](bootloader/SoftwareBitBang/ATmega328P/test/swbb/README.md)
(`wake_eval.sh`, `size_wake.sh`).

Names: `F` = `SWBB_PIN` (polling), `IA` = `SWBB_INTERRUPT` +
`SWBB_INTERRUPT_ACK` (edge time stamps, Timer1), `W` = `SWBB_INTERRUPT_WAKE`,
`O` / `P` = original / portable implementation.

### Summary

| | `F` polling | `IA` time stamps | `W` interrupt triggered |
|---|---|---|---|
| Receives from O/P/F/IA/W, modes 1-4 | 200/200 | 200/200 | 200/200 |
| Modes 5 and 6 | yes | no (#error) | yes, 200/200 |
| Frames caught, receiver busy 1ms between `bus.receive()` (packets back to back / every 10ms) | 1% / 1% | 100% / 100% | 87% / 100% |
| Receiver busy 10ms (back to back / every 10ms) | 0% / 0% | 67% / 100% | 53% / 100% |
| Other interrupt handler every 256us, longest handled, mode 3 | 25us | 5us | 25us |
| Same, mode 1 / mode 5 / mode 6 | 25us / 15us / 15us | 10us / - / - | 40us / 25us / 25us |
| Timer used | Timer0 (read) | Timer1 (taken) | Timer0 (read) |
| Pins | any | 2, 3 | 2, 3 |
| Flash / RAM, receiver test sketch | 3870 / 542 | 6154 / 665 | 4994 / 701 |
| Acknowledgement | after the CRC in `bus.receive()` | from the interrupt | from the interrupt |
| 200 packets of 20 bytes, mode 3 (F sender) | 2.39s | 2.35s | 2.35s |

Conclusion: `W` is better than `IA` in every measurement except one (a
receiver busy for 1-3ms between `bus.receive()` calls *and* a sender
transmitting to it back to back), it uses no Timer1, supports modes 5 and 6
and tolerates 5 times longer interrupt handlers. **It should replace
`SWBB_INTERRUPT`.** Compared to polling it removes the need to call
`bus.receive()` often, at the cost of ~1.1kB of flash and ~110 bytes of RAM
(2 frames queue), and of the acknowledgement limits below.

### How it works

1. INT0 is set to trigger on rising edges. A rising edge less than
   `SWBB_WAKE_QUIET` (a padding bit + 9.5 data bits, 336us in mode 3) after
   the previous one is inside a frame or an exchange: the interrupt only
   records its time and returns (4.9us). Any transmitter keeps the bus low
   longer than that before a frame (`can_start`), so the first rising edge of
   a frame always passes.
2. Otherwise the interrupt masks INT0 and runs the `SWBB_PIN` receiver: the
   first padding bit is accepted whatever its measured duration (the
   interrupt entry may have been delayed), the other two pads of the frame
   initializer and the pad of every byte are fully checked, measured from the
   end of the previous byte as in `SWBB_PIN`.
3. Interrupts are disabled while each byte is received and enabled (nested
   in this interrupt, INT0 masked) during the following padding bit: other
   interrupts are served and `millis()` is kept.
4. Between bytes only the byte is stored and the id, header and length are
   checked; a frame addressed to another device is abandoned after its first
   byte. After the last byte the CRC8/CRC32 are checked (table CRC32, ~6us
   per byte, ~175us for a 29 bytes frame, the transmitter waits 588us) and the
   acknowledgement is transmitted (same code as `SWBB_PIN`).
5. The frame is stored in a queue of `SWBB_WAKE_FRAMES` (2) frames.
   `bus.receive()` hands the oldest one to PJON in one call
   (`receive_frame` returns the whole frame); `send_response` does nothing
   (already sent, or too late).
6. Transmission and `receive_response` are the `SWBB_PIN` code with INT0
   masked; `can_start` keeps INT0 enabled (a frame arriving meanwhile is
   received).

First version: the CRC was updated after each byte in the padding bit
(~15us). With a 15-25us interrupt handler every 256us and ~322us bytes, two
handler runs can fall in the same padding bit and with the CRC work they
used the whole 70us pad: 25us handlers in mode 3 dropped to 56%. Moving the
checks to the end of the frame fixed it (100%).

### Measurements (single attempt, no retries)

**Reception and transmission, 200 packets of 20 bytes** (`wake_eval.sh
matrix`): every sender (`O`, `P`, `F`, `IA`, `W`) to `W` 200/200 in modes 1-4,
`F` and `W` to `W` 200/200 in modes 5 and 6, no bus contention. `W` to
`F` 200/200 in all modes; `W` to `O`/`P` 200/200 in modes 1-3, 196-198/200
in mode 4 (the original receiver's own limit, as for `F`). `W` as uploader to
the bootloader (`run_boot_link.sh`): 200/200 chunks in modes 1, 3, 4, 5, 6
and in mode 3 with the bootloader at 8MHz.

The acknowledgement comes earlier than with polling, the exchange is ~1.5%
faster (mode 3, 200 packets: F→F 2.39s, F→W 2.35s; mode 6: 1.05s, 1.02s).

**Receiver busy between `bus.receive()` calls** (`wake_eval.sh busy` and
`gap`, mode 3, 100 packets):

| Busy time | `F` | `IA` | `W`, packets back to back | `W`, packets every 10ms |
|---|---|---|---|---|
| 0 | 100 | 100 | 100 | - |
| 200us | 50 | 100 | 100 | - |
| 500us | 3 | 100 | 100 | - |
| 1ms | 1 | 100 | 87 | 100 |
| 3ms | 1 | 100 | 75 | 100 |
| 10ms | 0 | 67 | 53 | 100 |

With packets back to back (each packet ~11.9ms, its frame ~9.6ms) the
interrupt receiving the frames takes ~90% of the CPU: an application that
needs 1ms or more between two `bus.receive()` calls can not keep up and the
queue fills (dropped frames are not acknowledged, the transmitter retries).
A larger queue barely helps (busy 3ms: 75, 81, 82 with 2, 3, 4 frames): it
is CPU time, not buffering. With 10ms between packets nothing is lost even
with a 10ms busy loop. `IA` uses less CPU per frame (~17%) because it only
decodes edges, but see the interrupt handler limits.

Mode 5, back to back: busy 1ms 83, 10ms 36 (`F`: 1, 0).

**Other interrupt handlers on the receiver** (`wake_eval.sh load`, a Timer2
handler every 256us lasting 5-40us, 100 packets):

| Handler | Mode 1 F / IA / W | Mode 3 F / IA / W | Mode 5 F / W | Mode 6 F / W |
|---|---|---|---|---|
| 5us | 100 / 100 / 100 | 100 / 100 / 100 | 100 / 100 | 100 / 100 |
| 10us | 100 / 100 / 100 | 100 / 80 / 100 | 100 / 100 | 100 / 100 |
| 15us | 100 / 88 / 100 | 100 / 5 / 100 | 100 / 100 | 100 / 100 |
| 25us | 100 / 1 / 100 | 100 / 0 / 100 | 97 / 100 | 97 / 100 |
| 40us | 93 / 0 / 100 | 0 / 0 / 0 | 0 / 82 | 0 / 83 |

**Traffic for other devices** (`wake_eval.sh foreign` and `cpu`): each
packet to `W` preceded by one to device 77, 100 + 100 packets: `W` 100/100
in modes 3, 5, 6 (one wake per frame, a few spurious wakes that fail at once).
Interrupt time measured with the debug pin:

| | Mode 3 | Mode 6 |
|---|---|---|
| Frame for this device (28 bytes, received and acknowledged) | 10.7ms (≈ its duration) | 4.2ms |
| Frame for another device (abandoned after the first byte) | 0.60ms | 0.30ms |
| Each other rising edge (rest of that frame, keep busy bits) | 4.9us | 4.9us |
| Total per frame for another device (~90 rising edges) | ~1.05ms (~11% of its duration) | ~0.75ms |
| Idle bus | 0 | 0 |

**millis()** (`wake_eval.sh millis`): `millis()` printed by the receiver
against the simulated time after 0.5-1.8s of traffic: offset 6.6-7.8ms with
`W`, 1.9ms with `F`; the offset does not grow with the duration (the
difference is the time taken to print `W`'s longer statistics line at
115200 baud). No timer ticks are lost.

**Flash / RAM** (`size_wake.sh`, receiver test sketch, mode 3): `F` 3870 /
542, `IA` 6154 / 665, `W` 4994 / 701 (1 frame 4954 / 649, 3 frames 5026 /
753; each frame of the queue is `PJON_PACKET_MAX_LENGTH` + 2 bytes).

**Bootloader suite** (`run_tests.sh`): 17/17 with the portable
implementation, 17/17 with `DIRECT_IO=1`. With the application and the
uploader built with `W` (`DIRECT_IO=1 SKETCH_FLAGS=-DSWBB_INTERRUPT_WAKE
./run_tests.sh router fast_modes`, the tests on pin 2): 4/4, so the
bootloader hook works in an application using `W`. This needed a fix in the
test simulator `test/sim.c`: it set the bus level writing the PIN register,
which does not trigger external interrupts; it now raises the port pin IRQ
(as `swbb_sim.c` already did).

### Limits and open risks

- **Acknowledgement:** sent by the interrupt only for frames with this
  device's id (passed to `begin`, a later `set_id` is not seen), local mode,
  no extended length, correct CRCs and a free place in the queue. Shared mode
  frames, router mode (acknowledging other device ids) and frames not
  acknowledged by the interrupt are never acknowledged: use the polling
  `SWBB_PIN` there (e.g. on a router).
- **CPU:** while a frame for this device arrives the application does not run
  for the duration of the frame (as when polling, but at any time, also in
  the middle of other work). Interrupts are served between bytes, the
  longest interrupt-free period is one byte (~0.3ms in mode 3), as with
  `SWBB_PIN`.
- **Saturated link:** an application that is busy for milliseconds between
  `bus.receive()` calls loses frames if a device sends to it back to back
  (above). Not seen with 10ms between packets.
- **Quiet time heuristic:** a frame is recognised only if the bus was low for
  `SWBB_WAKE_QUIET` before it. All implementations wait longer than that
  before transmitting (`can_start`: padding bit + 9.5 bits + latency +
  random); a device transmitting without `can_start` (`PJON_SIMPLEX` right
  after another frame) could be missed.
- **Interrupt handlers longer than ~25us** (modes 3-6) break reception, as
  with polling (40us fails in mode 3 for both).
- Pins 2 and 3 only (INT0/INT1). Simulation only, no hardware test.
