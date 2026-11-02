## SoftwareBitBang on hardware: first bench results (intSWBB6, 2026-11-01/02)

Everything in the other `REVIEW-swbb-*` documents was measured in simulation. This is the first run on real
hardware: ATmega328P boards at 16MHz (crystal) on a short bench bus, shared with an ATmega2560 node and the
Melvan I2C router (an ATmega328P that bridges the bus to an ESP32 server). The bus has a pull-down and the
boards use pin 2. No long cable: the 20m star of the target installation was not available.

Library versions tested: fcf6f9e0 (modes 5/6, `SWBB_INTERRUPT_WAKE`), then the fixes below, then 8ba4996a (mode
7, table CRC32, bus discharge on release). Test firmware and raw results are in the Melvan repository
(`firmware/swbb6-bench`).

### 1. Bug: no acknowledgement ever arrived (fixed)

As first pushed (fcf6f9e0), **every acknowledged send failed** on hardware, in every mode (3 and 6) and with every
combination of `SWBB_PIN` and `SWBB_INTERRUPT_WAKE`. The receivers got the frames and answered; the original
library on the same boards and wires got 100/100.

Cause: `SWBB_PIN` released the bus (input, no pull-up) without driving it low first. Measured on the bench, a
released bus took **5.9-6.7us** to read low (0.7us when driven low). `receive_response()` released its keep busy bit
and immediately took its own decaying pulse for the start of the response; `sync()` failed and it returned
`SWBB_FAIL`. The ideal-wire simulation can't show it.

Fixed by 7866a831 (wait for the bus to fall after the release, `SWBB_RELEASE_FALL`), then by the analog bus model
and the discharge in `bus_release()` (23e880f0, see [REVIEW-swbb-cable.md](REVIEW-swbb-cable.md)). Everything below
was measured with the fix.

### 2. Two boards, mode 6, `SWBB_PIN` (`F`) and `SWBB_INTERRUPT_WAKE` (`W`)

One attempt per packet, ACK requested unless stated; 40 bytes payload unless stated.

| | `W` | `F` |
|---|---|---|
| Packets/s, 3 / 10 / 20 / 30 / 40 B | 490 / 288 / 204 / 157 / 128 | 493 / 282 / 198 / 153 / 125 |
| Payload with ACK, 40 B | 5.11 kB/s | 4.99 kB/s |
| Payload without ACK, 40 B | 5.56 kB/s, 0 lost | 5.56 kB/s, 0 lost |
| Raw link, 500 per length per direction | 5000/5000 | 5000/5000 |
| Soak, 4 x 5000 x 40 B, both directions, 1 attempt / retries | 20000/20000, 0 lost / dup / bad | 20000/20000 |

**Receiver busy between `bus.receive()` calls**, 20 B, 200 packets, received:

| Busy | 0 | 200us | 500us | 1ms | 3ms | 10ms |
|---|---|---|---|---|---|---|
| `W`, back to back | 200 | 192 | 168 | 155 | 116 | 58 |
| `F`, back to back | 200 | 11 | 7 | 0 | 0 | 0 |
| `W`, a packet every 10ms | | | | 200 | 200 | 191 |
| `F`, a packet every 10ms | | | | 3 | 6 | 0 |

**Timer2 interrupt every 256us** on the receiver, received out of 200:

| Handler | 5us | 10us | 15us | 25us | 40us |
|---|---|---|---|---|---|
| `W` | 200 | 200 | 200 | 199 | 0 |
| `F` | 200 | 200 | 200 | 139 | 0 |

(The simulation gave `W` 83/100 at 40us in mode 6; on hardware it is 0.)

**Other traffic.**
- A frame to an absent id before each packet: 300/300, every foreign frame dropped after its first byte.
- Real mode 3 frames from another device (the router) during 1000-packet transfers with retries: 1000/1000, no
  corruption, ~7% slower. `W` woke on the mode 3 frames and rejected them at the frame initializer.

**`millis()`** on a `W` receiver over 23.6s of back to back reception: within 68ppm of the host clock, `micros()`
and `millis()` agree within 1ms: no lost timer ticks.

**Two devices sending to each other** (both call `bus.receive()` between their sends): `W` 300/300 both ways.
`F` with default settings deadlocks: neither listens inside `send_packet_blocking`, every packet waits for the
3.5s timeouts. With `PJON_RECEIVE_WHILE_SENDING_BLOCKING` it works (300/300 and 283/300).

Flash / RAM of the test sketch: `W` 11.4 kB / 922 B, `F` 9.8 kB / 795 B.

### 3. Found with `SWBB_INTERRUPT_WAKE` on hardware

- **Header-only interrupt and state.** In a project with several source files that include PJON, the link failed
  (duplicate `__vector_1`) and every file had its own frame queue. Fixed by `SWBB_WAKE_SHARED` /
  `SWBB_WAKE_IMPLEMENTATION` (47cfe91c, extended to mode 7 and `SWBB_INTERRUPT` by 24099a2d).
- **The interrupt acknowledges before PJON has parsed the frame.** A receiver built without
  `PJON_INCLUDE_PACKET_ID` dropped frames carrying a packet id in `receive()` after the interrupt had already
  ACKed them: the sender saw every frame delivered while the application got none. The interrupt should apply
  the same header checks as `receive()` (packet id, port, mode/bus id bits) before it ACKs.
- **An interrupt ACK says nothing about the application.** Twice a node's main loop was hung (stack overflow,
  next item) while its interrupt went on acknowledging every frame, including a bootloader entry request and a
  whole "upload". Senders that need to know the application works must check an application level answer.
- **RAM.** The queue (`PJON_PACKET_MAX_LENGTH` + 2 bytes per frame) plus a whole frame reception running inside
  the interrupt, on top of whatever the application's stack holds, can overflow a 328P running a large
  application. Stack high-water mark after long multi-packet transfers, the same application in each build:
  mode 3 polling 49 bytes left, mode 6 `F` 53, mode 6 `W` with 2 frames 0 (heap pointer overwritten), with 1
  frame 0. Check the stack headroom of a `W` application.

### 4. PJON core: one packet id across retries (fixed)

`send_packet_blocking` assigned a new packet id on every attempt, so a retry after a lost ACK looked like a new
packet and `known_packet_id()` could not drop it. Through a router in mode 6: 26 duplicates in 407 messages. Fixed
by 60b83895 (the id is fixed once before the retry loop): 0.

### 5. Full system in mode 6: server, I2C, router, bus, two nodes

The Melvan router (polling, router mode) and two nodes in mode 6, driven by the server over I2C, through the
Melvan test suite (42 tests, 10s per measurement), against the same suite in mode 3:

| | mode 3 | mode 6, `F` nodes | mode 6, `W` nodes |
|---|---|---|---|
| Tests passed | 40/42 | **41/42** | 37/42 (RAM, §3) |
| Server → node, 8 / 32 B (messages/s) | 80 / 45 | 147 / 90 | 145 / 91 |
| Server → node p95 latency, 32 B | 37ms | 11ms | 11ms |
| Node → server, 32 B (messages/s) | 38 | 73 | 73 |
| Both directions at once, server → node | 17.9 | 15.0 | **25.9** |
| 256 B multi-packet message to a node | 236ms | 117ms | 102ms |

About **2x the message rate and 1/3-1/2 of the latency** of mode 3 on every path. That is less than the 2.4x of the
bare link, because the router's I2C turnaround is the same in both modes. No corruption, no reordering, no
duplicates (with §4), no silent loss with one sender.

Updating nodes on a mode 6 bus with mode 3 PJON bootloaders works without ISP:
1. The router holds the Melvan WAKE line high and sends the bootloader entry request in mode 6.
2. The node restarts into its mode 3 bootloader and, because of the WAKE level at reset, stays there with no
   timeout.
3. The server rewrites the router to a mode 3 build over I2C (twiboot, 2s) and updates the node.
4. The server puts the mode 6 router back.

Done for one and two nodes at a time.

### 6. Mode 7 (8ba4996a): the hardware test plan of [REVIEW-swbb-mode7.md](REVIEW-swbb-mode7.md)

`Mode7LinkTest` loop unchanged, 20 B payload. During the runs the server was held in reset, so the router (mode 3)
stayed silent.

| Plan step | Result |
|---|---|
| 1. Two boards, 10 min | **Mode 7: 221,619 / 221,621 acknowledged, 0 bad, 7.32 kB/s** (7.31-7.33 in every 5s window; the 2 failures are at the start, while the receiver was starting). Mode 6: 126,897 / 126,897, 4.19 kB/s. Mode 3: 53,020 / 53,020, 1.75 kB/s. Simulation: 7.22 / 4.17 / 1.75. Received = acknowledged. |
| 2. All bus devices connected and powered, 30 min | **661,095 / 661,095, 0 failed, 0 bad, 7.32 kB/s.** Short bench bus (no long star available). |
| 3. Timing (bus sniffer on a third 328P, 0.5us timer, ~1us polling; no scope) | Preamble 93-126us (median 110), data runs n x 4us (median deviation 0.0us, 5-95% -1.5..+1.0us), interrupt windows 41-82us including the adjacent low bits (48us nominal), keep busy 3.5us high / 22us low, ACK 6.5us low + 16us high. Within the sniffer's resolution of the design values. Edge times and idle line noise still need a scope. |
| 4. Two transmitters into one receiver, 10 min | Polling receiver: 3.56 + 3.63 = 7.19 kB/s together (bus full), fair 49/51, received = sum of acknowledged, 0 bad. **Wake receiver: 1.85 + 3.35 = 5.2 kB/s, unfair 36/64, 76 more acknowledged than received** (not yet explained: window alignment or acknowledged frames lost). Wake receiver with one transmitter: 7.54 kB/s, 116,950 / 116,950 (simulation 7.50). |
| 5. Mode 7 bootloader | Not done (needs ISP). |

**Mode 3 traffic on a mode 7 bus.** With the server running, it polled two nodes through the router once a minute
in mode 3. Mode 7 then lost 666 of 222,165 packets in 10 minutes, all in bursts at the polls. Every device must
switch to mode 7, as the plan says.

### Open points (status after the fixes of track E, simulation only: re-test on hardware)

- **Header checks before the interrupt ACK (§3): fixed.** Both wake receivers (modes 1-6 and 7) now do the checks
  `PJON::receive()` does before it acknowledges (`SoftwareBitBangAccept.h`); frames PJON would drop are neither
  acknowledged nor stored. Reproduced in simulation (`accept_eval.sh`, 50 packets, modes 3 and 7): sender with packet
  id / port / MAC, wake receiver without the matching `PJON_INCLUDE_*`: before 50 acknowledged, 0 delivered; after 0
  acknowledged, 0 delivered. With the matching option and plain packets: 50 / 50 before and after.
- **Interrupt ACK of a stuck application (§3): `SWBB_WAKE_ALIVE_MS`.** The interrupt acknowledges only if
  `bus.receive()` ran in the last 100ms (default). A cleanly stuck application already stops acknowledging when its
  queue is full (2 frames, unchanged). On hardware the node kept acknowledging, which means its queue state was
  corrupted (stack overflow). Simulated by making the queue look empty after the application stops (`alive_eval.sh`):
  before 150/150 acknowledged with nothing delivered after the stop; after acknowledgements stop within ~100ms (27 of
  150 in mode 3, 46 in mode 7). Busy loops of 20 / 50 / 150ms between `bus.receive()` calls give the same results as
  before (the queue limits them first).
- **Stack headroom (§3): measured, partly reduced, check tool added.** Simulation (`stack_eval.sh`, receiver with a
  15us interrupt every 256us): the INT0 interrupt uses 42 (1 frame) / 47 (2 frames) bytes of stack in modes 3 and 6,
  40 / 42 in mode 7, nested interrupts included, on top of the application's stack at that moment. RAM of the wake
  receiver: `SWBB_WAKE_FRAMES` x (`PJON_PACKET_MAX_LENGTH` + 2) + ~14 bytes; the 18 bytes of statistics are now only
  compiled with `SWBB_WAKE_STATS` (mode 7: `SWBB_M7_STATS`, already optional). Splitting the interrupt in smaller
  functions made it use more stack (+7 bytes), not less. Use `src/utils/PJON_StackCheck.h` on the real application
  (`pjon_stack_paint()` first in `setup()`, `pjon_stack_free()` after heavy traffic; validated in simulation:
  RAM + deepest stack + free = 2048). The largest saving is usually `PJON_MAX_PACKETS` (5 x (`PJON_PACKET_MAX_LENGTH`
  + 13) bytes by default).
- **Mode 7, two transmitters into one wake receiver (§6 step 4): acknowledged-but-not-received explained.**
  `Mode7LinkTest` defined `MY_ID 1` for every transmitter. Two devices with the same id and the same payload counter
  send identical frames; when they start together the frames do not collide electrically, the receiver stores one and
  both transmitters see its acknowledgement. Reproduced with a 3 node simulation (`three.sh`, both transmitters id 1:
  acknowledged 664, received 660 with a wake receiver, 650 / 649 with a polling one); with id 1 for both but different
  payloads, or different ids: acknowledged = received in every run (modes 6 and 7, wake and polling receivers, with
  +500ppm and +0.5% clock difference between the transmitters, 1000 packets each). `Mode7LinkTest` now takes `-DMY_ID`
  and says each transmitter needs its own id. Re-test step 4 with different ids.
- **Mode 7 unfairness between two saturating transmitters: explained, not changed.** Reproduced in simulation (e.g.
  1:155 / 3:444 with a wake receiver). After each exchange both transmitters wait for the same idle bus; the one that
  releases its preamble later wins the contention slot, and the code path after an acknowledgement and after a busy
  bus differ by tens of microseconds, more than the random spread (0-16us delay + 0-28us slot), so the same
  transmitter keeps winning. Larger random spreads (32 slots, 96us delay) changed the split but did not make it fair
  and cost throughput. It only matters when two devices transmit back to back without retries; with
  `send_packet_blocking` (back-off) and real traffic it averages out. A fair medium access would need a different
  design (e.g. a random hold-off after every successful transmission).
- Long cable: fall time, noise on the idle line with the pull-down, range.
- Mode 7 bootloader on hardware.
