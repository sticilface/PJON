## SoftwareBitBang on the ATmega2560 (Arduino Mega)

The Melvan bus has an ATmega2560 node on Arduino Mega pin 3 (PE5, INT5; pin 2 =
PE4 is the project's WAKE line) next to ATmega328P nodes on pin 2 (PD2). Getting
mode 3 to work on the Mega took a lot of tuning on hardware. This document shows
why, and what the direct port access implementation (`SWBB_PIN`) changes. All
results come from the simulator (simavr, both MCUs at 16MHz, cycle accurate,
ideal wire and the analog cable model `-C 1500 -R 100 -P 100000` of
[REVIEW-swbb-cable.md](REVIEW-swbb-cable.md)), not from hardware.

Simulator changes: each node has its own MCU and bus pin (`swbb_sim -M
atmega2560,atmega328p -B 3,2`, `MCU_A`/`MCU_B`/`PIN_A`/`PIN_B` in
`run_swbb_tests.sh`); the Arduino shim has the Mega pin tables of the Arduino AVR
core (variants/mega) and calls `turnOffPWM` like the core for pins connected to a
timer: Mega pin 3 is TIMER3C, so every `digitalRead`/`digitalWrite` on it runs
that switch. The 328P shim is unchanged (pins 2, 12, 17 are not on a timer).
Scripts: `test/swbb/mega_eval.sh`, `mega_load.sh`, `mega_size.sh`, `mega_compile.sh`.

`M` = Mega on pin 3, `U` = 328P on pin 2, `M->U` = the Mega transmits. 100 packets
of 20 bytes, one attempt each (no retries).

### 1. Why mode 3 was hard on the Mega

With the original library (and the portable implementation of this branch, which
has the same timing):

| Mode | M->U | U->M | M->M | U->U |
|---|---|---|---|---|
| 1 | 100 | **0** | 98 | 100 |
| 2 | 99 | **0** | 100 | 100 |
| 3 | **0** | **0** | 36 (288 contention events) | 99 |
| 4 | **0** | **0** | 27 (340 contention events) | 83 |

Same on the cable model. Portable implementation: mode 3 M->U 0, U->M 0, M->M 93,
U->U 99; mode 4 0 / 0 / 82 / 90.

Cause: the timing of the original library is made of `delayMicroseconds` values
plus the execution time of `digitalRead`/`digitalWrite`, tuned on a 328P
(`Timing.h`). On the Mega, pin 3 these calls are slower: the 3 byte return
address of the 2560 (every call and return one cycle longer) and, mostly,
`turnOffPWM` because pin 3 is a PWM pin. `Timing.h` has Mega values for modes 1
and 2 only (tuned for other pins); modes 3 and 4 fall back to the 328P values.

Measured on the wire, mode 3 (spec: padding bit 70us, data bit 28us):

| Transmitter | Padding bit | Data bit | Keep busy bit |
|---|---|---|---|
| 328P, original | 69.5us | 28.0us | 9.5us |
| Mega pin 3, original | 73.5us | **31.5us** | 13.0us |

Receive window of the Mega's original receiver (an exact `SWBB_PIN` 328P
transmitter with the data bit swept, 40 packets each):

| Mode | Data bit accepted by the Mega | Spec | 328P transmits |
|---|---|---|---|
| 1 | 46-48us (0/40 at 44 and 50) | 44us | ~44us |
| 3 | ~32.5-35us (0/40 at 31 and 36) | 28us | ~28us |

So in mode 3 the Mega transmits bits 12% too long (a 328P receiver accepts
~28.2-29.8us) and samples as if bits were ~20% too long (it accepts only
~32.5-35us). The two windows do not overlap at all: no amount of retries helps,
only re-tuning `SWBB_BIT_WIDTH`/`SWBB_READ_DELAY` for that MCU, that pin and that
core version, which is what had to be done by hand. In mode 1 the Mega receiver
(window 46-48us) also misses 328P frames (44us).

### 2. Direct port access on the Mega

`SWBB_PIN` now supports the ATmega1280/2560 (`SoftwareBitBangAVRPins.h`, shared by
`SWBB_PIN`, `SWBB_INTERRUPT_WAKE`, `SWBB_INTERRUPT` and mode 7):

- Pins of ports A-G (in the I/O space reached by `sbi`/`cbi`/`sbis`): 0-5, 10-13,
  18-41, 50-61 (A0-A7). Ports H, J, K, L (pins 6-9, 14-17, 42-49, 62-69) are
  memory mapped: compile error.
- External interrupts for the wake receivers: 2 (INT4), 3 (INT5), 18 (INT3), 19
  (INT2), 20 (INT1), 21 (INT0), configured through `EICRA`/`EICRB`. On the 328P: 2
  (INT0), 3 (INT1) as before.
- The cycle counted code (bit loops, edge loops, delays) contains no calls, so the
  longer call/return of the 2560 does not change the timing; only the interrupt
  latency is one cycle longer. The bus timing is the same as on the 328P, to the
  cycle.

Results, Mega pin 3 with 328P pin 2, ideal wire and cable model (identical):

| Implementation (both) | Mode | M->U | U->M | M->M | U->U | 100 packets (s) |
|---|---|---|---|---|---|---|
| `SWBB_PIN` | 3 | 100 | 100 | 100 | 100 | 1.17 |
| `SWBB_PIN` | 6 | 100 | 100 | 100 | 100 | 0.50 |
| `SWBB_PIN` | 7 | 100 | 100 | 100 | 100 | 0.30 |
| wake receiver | 3 | 100 | 100 | 100 | 100 | 1.16 |
| wake receiver | 6 | 100 | 100 | 100 | 100 | 0.50 |
| wake receiver | 7 | 100 | 100 | 100 | 100 | 0.29 |

No contention in any of them. Payload with ACK over these 100 packets (including
the start): mode 3 1.7 kB/s, mode 6 4.0 kB/s, mode 7 6.7 (polling) - 6.9 (wake)
kB/s, the same as between two 328P.

Mixed: Mega with the wake receiver, 328P with `SWBB_PIN`, modes 3, 6, 7: 100/100
both ways.

**Migration**: a Mega with `SWBB_PIN` and 328P nodes still on the original
library: 100/100 both ways in modes 1, 2, 3 and 4 (contention events in M->U
come from the original responder, see [REVIEW-swbb-atmega328p.md](REVIEW-swbb-atmega328p.md));
with 328P nodes on the portable implementation: 100/100 in modes 3 and 4. Only
the Mega needs `SWBB_PIN` for mode 3 to work.

**Interrupt load on the Mega** (Timer2 interrupt every 256us busy for 5-25us,
cable model): 100/100 both ways up to 20us in modes 3, 6 and 7 with both
implementations; at 25us `SWBB_PIN` receives 96 / 97 / 93 (modes 3 / 6 / 7), the
wake receiver 100 / 100 / 93.

**Mega busy between `bus.receive()` calls** (received out of 100, sender back to
back / a packet every 10ms):

| Busy | `SWBB_PIN` mode 3 | wake mode 3 | wake mode 6 | wake mode 7 |
|---|---|---|---|---|
| 1ms | 2 / 2 | 85 / 100 | 81 / 100 | 79 / 100 |
| 3ms | 0 / 1 | 75 / 100 | 60 / 100 | 44 / 100 |
| 10ms | 0 / 0 | 53 / 100 | 30 / 100 | 18 / 100 |

**Flash / RAM** of the receiver test sketch:

| | original, mode 3 | portable, mode 3 | `SWBB_PIN` 3 / 6 / 7 | wake 3 / 6 / 7 |
|---|---|---|---|---|
| Mega | 5172 / 543 | 5304 / 543 | 4204 / 4210 / 4434 flash, 542-567 RAM | 5558 / 5556 / 5290 flash, 717-722 RAM |
| 328P | 4494 / 543 | 4626 / 543 | 4036 / 4042 / 4270 flash, 542-567 RAM | 5382 / 5380 / 5114 flash, 717-722 RAM |

### 3. Frame hooks

`SWBB_FRAME_BEGIN()` / `SWBB_FRAME_END()` (empty by default, defined before
including PJON) are called around the frame reception in the interrupt of the
wake receivers (PJDL and mode 7: from the first rising edge until the frame is
stored and acknowledged, or discarded) and around `send_frame`,
`receive_response` and `send_response` of `SWBB_PIN`. They are always paired and
run inside the interrupt or with interrupts disabled, so they must be short. A
project can drive its own busy line with them, for example the Melvan gateway
telling the ESP32 not to address it over I2C while a frame is being received:

```cpp
#define SWBB_PIN 2
#define SWBB_INTERRUPT_WAKE
#define SWBB_FRAME_BEGIN() (PORTB |= _BV(PB0))  // project busy line high
#define SWBB_FRAME_END()   (PORTB &= ~_BV(PB0))
#include <PJONSoftwareBitBang.h>
```

Polling reception (frame by frame through `bus.receive()`) is not covered: the
strategy does not know where a frame ends. Mode 7's own polling send/receive
code does not call the hooks yet (only its wake receiver does). Tested in the
simulator: one begin and one end per received frame on the wake receiver.

### Risks

- Simulation only. The Mega's wire timing with `SWBB_PIN` is computed from
  cycle counts, the same as on the 328P (measured on hardware for the 328P), but
  the Mega itself has not been run on the bus with this code.
- The shim's `turnOffPWM` follows the Arduino AVR core; core versions that inline
  it differently change the original library's timing on the Mega by a few
  cycles per call (the conclusion, no overlap in mode 3, needs a change of ~4us
  per bit to flip).
- Do not put the bus on Mega pins 6-9, 14-17, 42-49 or 62-69 with `SWBB_PIN`.
- The wake receiver on Mega pin 3 uses INT5; pin 2 (INT4) stays free for the
  WAKE line (the receiver never touches it).
