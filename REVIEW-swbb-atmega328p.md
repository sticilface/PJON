## SoftwareBitBang on the ATmega328P (pin 2 / PD2): analysis

Scope: the `SoftwareBitBang` strategy as used on ATmega328P nodes at 16MHz on
pin 2, together with the PJON SoftwareBitBang bootloader of this branch.
Questions: code size and memory, significant bugs, speed / efficiency (direct
port access as in the bootloader), and compatibility with the bootloader.

Everything below was measured on two simulated ATmega328P (simavr, cycle
accurate, both cores in lock step on one wire). The test tools are in
[`bootloader/SoftwareBitBang/ATmega328P/test/swbb`](bootloader/SoftwareBitBang/ATmega328P/test/swbb/README.md).
Naming used in the tables:

- `O`: original library (`master`)
- `P`: this branch, portable implementation (`digitalRead`/`digitalWrite`), bug fixes only
- `F`: this branch with `#define SWBB_PIN 2` (direct port access)

Unless stated otherwise a test sends 200 packets of 20 bytes with **one
attempt each** (no retries), so the result is the raw link reliability.

> **"Router" in this document.** Two different things are called router:
> the *Melvan I2C router* (ESP32 → I2C → ATmega328P, PJON id 254, SWBB mode 3
> on pin 2, see `REVIEW-melvan-router.md`), and PJON's *router mode*
> (`set_router`, `PJONRouter`, `PJONSimpleSwitch`: a device that acknowledges
> packets addressed to other ids). Each mention below says which one.

### Summary

| | Original | This branch, `SWBB_PIN 2` |
|---|---|---|
| Flash, sketch using PJON only | 6010 / 4474 bytes (tx / rx test sketch) | 5360 / 3842 bytes (−650 / −632) |
| `bus.receive()` on an idle bus | ~40us | ~14us |
| Mode 3, single attempt success | 99.5% with 325 bus contentions | 100%, no contention |
| Mode 4, single attempt success | 82% with 205 bus contentions | 100%, no contention |
| Mode 3 with interrupt load (15us every 256us) on the device | 0% | 100% |
| `can_start()` reports a busy bus as free (mode 3) | 16% | 0% |
| Bootloader simulation tests | 14/14 | 14/14 |

Use on every node (bootloader, Melvan I2C router and plain nodes stay compatible with
each other in any combination):
```cpp
#define SWBB_MODE 3
#define SWBB_PIN  2
#include <PJONSoftwareBitBang.h>
```

### 1. Bugs

**1.1 Bus contention during the acknowledgement (all AVR, worst in modes 3/4).**
The transmitter keeps the bus busy with a short pulse every ~52us while
it waits for the ACK; the receiver must answer right after one of those
pulses. In `send_response()`:
- it waits for the pulse for only `SWBB_BIT_WIDTH` (24us in mode 3), shorter
  than the pulse period, so about half of the time it answers at a random
  moment instead of after a pulse;
- it calls `pinMode(OUTPUT)` before `digitalWrite(HIGH)`: the pin drives the bus
  **low** for a few us, against the transmitter's high pulse;

and in `receive_response()` the transmitter ends each pulse with
`digitalWrite(LOW)` then `pinMode(INPUT)`, driving the bus low for ~4us while
the receiver may already be answering. Result: two outputs shorted against
each other (only the series resistors limit the current) and corrupted ACKs:
325 contention events per 200 packets in mode 3, 205 in mode 4 (0 in mode 1
because of the pulse period / bit width ratio).
*Fixed in both implementations*: the receiver waits up to
`SWBB_RESPONSE_PULSE_WAIT` (100us) for the pulse, the bus is set high before the
pin becomes an output and released (input) before the output register is
cleared. Contention is now 0 in all modes, also against original devices.

**1.2 `can_start()` misses another device's acknowledgement phase.** It
samples the bus ~12 times over ~330us, but the keep-busy pulses of a device
waiting for an ACK are only ~9.5us long: with the original code
`can_start()` reports the bus free in **16%** of the calls in mode 3 (10%
with 7us pulses in mode 1), so a third device can start transmitting on
top of an acknowledgement. *Fixed*: the bus is polled continuously (both
implementations), 0% in all cases (`bench.sh`).

**1.3 The receiver's bit timing is not centred on the PJDL bit duration.**
The `Timing.h` values compensate the execution time of `digitalRead`, but
the effective sampling period of the original receiver is longer than the
specified bit. Measured acceptance window of the data bit duration
(transmitter with exact timing, 50 packets each):

| Mode | PJDL bit | Original receiver accepts | Exact receiver (bootloader, `F`) accepts |
|---|---|---|---|
| 1 | 44us | 44-46.5+us | 42.5-46us |
| 2 | 40us | 40.5-42.5+us | 38.5-42us |
| 3 | 28us | ~28.2-29.8us | 27-29.5us |
| 4 | 26us | ~26.4-27.8us | 25-27.5us |

The original receiver rejects a transmitter with the exact specified timing
in modes 3 and 4 and works with its own transmitter (~26.3us per bit in mode
4) only at the edge of its window: this is why mode 4 delivers only 82% of the
packets at the first attempt. It also means that spec-exact transmitters
(the bootloader's ACK is a single byte with leading 1 bits, so it is not
affected) can not reach original receivers reliably. Not changed in the
portable implementation (the `Timing.h` values are shared with other MCUs and
pins). The direct port access implementation transmits `PJDL bit + 1us` (the
middle of the overlap of both windows, within the PJDL −3/+11us tolerance) and
expects `PJDL bit + 0.5us`.

**1.4 Interrupts corrupt bytes.** Interrupts served while a byte is
transmitted stretch one of its bits, interrupts served while a byte is
received shift the sampling points. With the Timer0 interrupt alone the
effect is small, with a heavier one (a 15us handler every 256us, like a busy
I2C slave on the Melvan I2C router) the original library fails all the packets.
*Fixed with `SWBB_PIN`*: see 3.2.

**1.5 The receiver is late after each byte.** `sync()` measures the padding bit
from the moment it is called, not from its rising edge. Everything done
between two bytes (PJON's processing of the received byte, the call chain,
pending interrupts) is subtracted from the measured padding bit; if more than
40us (mode 3) are lost the byte is rejected. *Fixed with `SWBB_PIN`*: the time
elapsed since the end of the previous byte (Timer0) is added. In the
simulation, under interrupt load, the receiver started measuring 15-45us after
the rising edge.

**1.6 `sync()` fails if the padding bit is late.** If the receiver finishes a
byte slightly before the transmitter (bus still low) `sync()` fails at once,
although PJDL allows the padding bit to be preceded by a slightly longer 0.
*Fixed with `SWBB_PIN`*: the rising edge is awaited for up to one bit (as the
bootloader does).

**1.7 Minor.**
- `set_pins()` called with `SWBB_NOT_ASSIGNED` (255, the default arguments)
  called `digitalWrite(255)`/`pinMode(255)`, reading the Arduino pin tables out
  of bounds. Fixed.
- `_timeout` is 16 bit: `length * SWBB_RESPONSE_OFFSET` overflows for frames
  longer than 3276 bytes (not reachable on the ATmega328P). Not changed.
- `begin()` waits a random time up to `SWBB_INITIAL_DELAY` (1 second by
  default) at every boot. Lower it if the start up time matters.

### 2. Size and memory

Flash / RAM of the test sketches (mode 3, avr-gcc 7.3 -Os, same flags as the
Arduino IDE):

| Sketch | Original | `P` | `F` (`SWBB_PIN 2`) |
|---|---|---|---|
| Transmitter (send + receive) | 6010 / 544 | 5964 / 544 | 5360 / 543 |
| Receiver | 4474 / 511 | 4502 / 511 | 3842 / 510 |
| `AppWithBootloaderHook` (also uses `digitalRead` for the LED) | 4150 / 489 | 4178 / 489 | 3862 / 488 |

`SWBB_PIN` removes `digitalRead`, `digitalWrite`, `pinMode` and their lookup
tables (if the sketch does not use them elsewhere) and the per call overhead.
The strategy itself uses 3 bytes of RAM (`_timeout`, `_end`).

RAM is dominated by PJON itself, not by the strategy: in the receiver sketch
the `bus` object takes 401 bytes, 315 of them are the packet buffer used by
`bus.send()` / `bus.update()` (`PJON_MAX_PACKETS` 5 × 63 bytes):

| Configuration | Flash | RAM |
|---|---|---|
| default (`PJON_MAX_PACKETS` 5, `PJON_PACKET_MAX_LENGTH` 50) | 3834 | 510 |
| `PJON_MAX_PACKETS 1` | 3806 | 258 |
| `PJON_MAX_PACKETS 0`, `PJON_PACKET_MAX_LENGTH 40` | 3732 | 185 |

Nodes that only answer, or send with `send_packet` / `send_packet_blocking`,
do not need the packet buffer: `#define PJON_MAX_PACKETS 0` saves 325 bytes.

### 3. Speed and efficiency

**3.1 Wire speed.** The bit durations of each mode are fixed by PJDL and must
stay as they are to remain compatible with the bootloader and the other
devices, so the throughput of a correct exchange does not change (packets per
second are within 2% in all combinations). What changes:
- reliability: no retries in modes 3 and 4 (each retry costs the back-off
  delay, `attempts^5` microseconds and a new `can_start()`): modes 3 and 4
  become usable, mode 4 is 8% faster than mode 3 on the wire.
- CPU time: `bus.receive()` on an idle bus takes ~14us instead of ~40us
  (70389 instead of 24895 calls per second), the rest of the loop gets the
  time back and the receiver polls more often, so fewer frames are missed.
- a failed or garbled response ends `receive_response()` at once instead of
  waiting for the response timeout.

**3.2 Direct port access (`SWBB_PIN`).** The approach of the bootloader,
adapted to a library that shares the MCU with an application:
- The pin is read with `sbis`/`sbic` in loops of exactly 6 cycles (0.375us),
  written with `sbi`/`cbi`. The data bit loops are written in assembly, their
  duration does not depend on the compiler or on the data (measured on the
  wire: 28.0/70.0us in mode 3 at 16MHz for the specified 28/70us).
- Timer1 is not used (the application may need it): durations are counted with
  the edge waiting loops, interrupts are disabled while a byte is transmitted
  or received.
- Interrupts are served during the padding bit only, and the time they take,
  read from Timer0 (4us resolution, already running for `millis`, never
  modified), is subtracted from it: padding bits stay within ±4us even with
  long interrupt handlers, `millis()` / `micros()` do not lose time.
- The receiver measures the padding bit from its rising edge (see 1.5) and
  samples the data bits at a fixed number of cycles after its falling edge.
- Single attempt success with an interrupt load (a 15us handler every 256us,
  like a busy serial or I2C handler) on one of the two devices, 100 packets
  (`run_swbb_tests.sh` with `-DLOAD_ISR=15`):

| Transmitter → receiver | Mode 1, load on tx / on rx | Mode 3, load on tx / on rx |
|---|---|---|
| original → original | 0% / 0% | 0% / 0% |
| `F` → `F` | 100% / 100% | 100% / 100% |
| original → `F` | 0% / 100% | 0% / 100% |
| `F` → original | 100% / 0% | 100% / 0% |

  The device with the load works if it uses `SWBB_PIN`, whatever the other
  device uses. With `SWBB_ATOMIC 0` the direct port access transmitter fails
  like the original one (0%).

**Trade-off of `SWBB_ATOMIC` (default 1):** interrupts are disabled for up to
one byte (~0.3ms in mode 3, ~0.5ms in mode 1). Interrupts are delayed, not lost (except if the same interrupt
fires twice in that time). The hardware serial receiver holds 2 characters
plus the one being received, so at 115200 baud characters can be lost if they
arrive while the bus is busy: use a lower baud rate or `SWBB_ATOMIC 0` in
sketches that receive serial data during bus traffic. I2C slaves are not
affected (the hardware stretches the clock).

### 4. Compatibility

Single attempt success, 200 packets of 20 bytes, no interrupt load
(`matrix.sh`; contentions: times both devices drove the bus at the same time
with opposite levels):

| Transmitter → receiver | Mode 1 | Mode 2 | Mode 3 | Mode 4 |
|---|---|---|---|---|
| `O` → `O` | 200/200 (1 contention events) | 200/200 | 199/200 (325 contention events) | 164/200 (205 contention events) |
| `P` → `P` | 200/200 | 200/200 | 195/200 | 181/200 |
| `F` → `F` | 200/200 | 200/200 | 200/200 | 200/200 |
| `O` → `P` | 200/200 | 200/200 | 196/200 | 166/200 |
| `P` → `O` | 200/200 | 200/200 | 197/200 (6 contention events) | 189/200 (91 contention events) |
| `O` → `F` | 200/200 | 200/200 | 200/200 | 200/200 |
| `F` → `O` | 200/200 | 200/200 | 200/200 | 194/200 |
| `P` → `F` | 200/200 | 200/200 | 200/200 | 200/200 |
| `F` → `P` | 200/200 | 200/200 | 200/200 | 192/200 |

Over 1000 packets in mode 3 the original and the portable receivers lose
0.4-1.7% of the frames (which one loses more changes with the packet length:
it depends on when the Timer0 interrupt falls), the direct port access
receiver loses none.

Bootloader simulation tests (`run_tests.sh`: update through the application
hook, timeout, fresh chip, interrupted update, pins 2/12/17, modes 1-4, 8MHz
bootloader, 30kB image, protection, Melvan I2C router sender settings: mode 3 pin 2 with packet id
and port, missing / out of order chunks): **14/14 with the portable
implementation and 14/14 with `SWBB_PIN`** (`DIRECT_IO=1 ./run_tests.sh`) for
the uploader and the application. The bootloader itself is not changed.

The original receiver still loses some packets in mode 4 whoever transmits
(see 1.3): mixed networks in mode 4 work but retry more, update every device
to use `SWBB_PIN` to get 100%.

### 5. Not done / limits

- Results come from the simulator: it is cycle accurate for the CPU but the
  bus is ideal (no capacitance, no noise, no clock tolerance between devices).
  A hardware test with the real cable and the Melvan I2C router is still needed. The
  clock tolerance of ceramic resonators (±0.5%, Nano and Pro Mini clones) moves
  the last data bit by ~1us in mode 3, within the margins measured above.
- Only 16MHz was tested in the link tests (8MHz compiles, and the bootloader at
  8MHz receives from a 16MHz `SWBB_PIN` uploader).
- `SWBB_PIN` supports the ATmega88/168/328/328P/328PB with one pin (no separate
  input and output pins). Other AVRs could be added with their port tables.
- No mode faster than mode 4: the bootloader supports modes 1-4, a faster mode
  would need a new bootloader on every node.
