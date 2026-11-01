## SoftwareBitBang: interrupt driven receiver (prototype) on the ATmega328P

Question: can SoftwareBitBang receive with interrupts instead of polling, and
does that make it faster? Prototype:
[`SoftwareBitBangIRQ.h`](src/strategies/SoftwareBitBang/SoftwareBitBangIRQ.h),
evaluated against the polling direct port implementation (`SWBB_PIN`) on two
simulated ATmega328P at 16MHz (simavr, the test tools of
[`test/swbb`](bootloader/SoftwareBitBang/ATmega328P/test/swbb/README.md),
`irq_eval.sh`, `size_irq.sh`, `ack_latency.py`, `isr_time.py`).

```cpp
#define SWBB_MODE 3
#define SWBB_PIN  2              // INT0 (3 = INT1 also works)
#define SWBB_INTERRUPT           // interrupt driven reception
#define SWBB_INTERRUPT_ACK       // acknowledge from the interrupt (recommended)
#include <PJONSoftwareBitBang.h>
```

Names used below: `O` original library (master), `P` portable implementation,
`F` polling with `SWBB_PIN`, `I` interrupt reception with the acknowledgement
sent by PJON from `bus.receive()`, `IA` interrupt reception with the
acknowledgement sent from the interrupt. Results are single attempts (no
retries), 200 packets of 20 bytes (29 bytes on the wire) unless stated.

> **"Router" in this document.** Two different things are called router:
> the *Melvan I2C router* (ESP32 → I2C → ATmega328P, PJON id 254, SWBB mode 3
> on pin 2, see `REVIEW-melvan-router.md`), and PJON's *router mode*
> (`set_router`, `PJONRouter`, `PJONSimpleSwitch`: a device that acknowledges
> packets addressed to other ids). Each mention below says which one.

### Summary

- **Wire speed: no.** The bit durations are fixed by PJDL and every device
  (and the bootloader) must agree on them. Interrupt reception does not make a
  faster mode possible: the time stamps of the edges are delayed by any other
  interrupt that is running, so it tolerates *less* timing error than the
  polling receiver, which disables interrupts during each byte.
- **Effective speed: slightly, with `SWBB_INTERRUPT_ACK`.** The ACK starts
  ~88us after the end of the frame instead of ~451us (polling, CRC computed by
  PJON after the frame): 200 packets in 2.31s instead of 2.38s (3%).
- **What it really gives: frames are received while the application is busy.**
  With 0.2-3ms of work between two `bus.receive()` calls the polling receiver
  gets 2-34% of the frames at the first attempt, the interrupt receiver with
  `SWBB_INTERRUPT_ACK` 100%. No preamble or tight loop needed.
- **Costs and risks:** Timer1 (no `analogWrite` on 9/10, no Servo), INT0,
  +1.5-2.3kB flash, +95 bytes RAM, 10-18% CPU during traffic (0% idle), ~0.5ms
  in an interrupt for each acknowledgement, and **reception fails if other
  interrupts are long** (a 10-15us handler, like a busy I2C slave, drops mode 3
  reception to 80% / 5%). The ACK from `bus.receive()` (without
  `SWBB_INTERRUPT_ACK`) does not work with the default `SWBB_RESPONSE_OFFSET`.

Recommendation: worth it for **plain nodes** whose application is busy (it
replaces a long preamble and frequent polling), with `SWBB_INTERRUPT_ACK`, and
only if Timer1 is free and no other interrupt handler takes more than ~5us.
**Not for the Melvan I2C router**: its I2C interrupt delays the time stamps.
**Not for devices in PJON router mode** either: the acknowledgement in the
interrupt can not decide for other ids. A router also does not do long work
between `bus.receive()` calls, so it gains nothing. Keep the polling `SWBB_PIN` implementation as
default. It stays fully compatible: the interrupt node transmits with the
polling code and receives from every other implementation.

### 1. Design

- **Edge time stamps:** INT0 on any change, time read from Timer1 running
  free at F_CPU/8 (0.5us, wraps every 32.8ms). Timer0 (4us) is too coarse and
  shared with `millis`; Timer2 is 8 bit and would need an overflow interrupt.
  Timer1 is the cost: the Arduino core uses it for `analogWrite` on pins 9 and
  10 and the Servo library needs it. Hardware input capture (ICP1, time stamps
  free of interrupt latency) exists only on pin 8 (PB0), not on pin 2.
- **Decoding:** a state machine in the interrupt recognises the frame
  initializer (3 padding bits each followed by one low bit), then each byte:
  its padding bit is measured edge to edge, the data bits are found by rounding
  the time of each edge since the falling edge of the padding bit to whole bits
  (expected bit `SWBB_WIRE_BIT + 0.5`, so 28.0-29.0us transmitters all fit).
  A Timer1 compare interrupt ends each byte 9.5 bits after its
  synchronization, because trailing bits with the same level produce no edge.
  After a decoding error or a frame that can not be stored, everything is
  ignored until the bus is quiet for 12 bits, so data bits are not taken for a
  frame initializer.
- **Buffer:** one frame (`PJON_PACKET_MAX_LENGTH` bytes). `receive_frame()`
  serves its bytes to PJON while they arrive; the buffer is freed when PJON has
  read the last byte. While it is occupied new frames are ignored (and not
  acknowledged, so the transmitter retries).
- **Transmission, `can_start`, response:** the polling `SWBB_PIN` code with
  INT0 disabled.
- **Acknowledgement:**
  - `I` (default): PJON sends it from `bus.receive()` after its own CRC check.
    If the transmitter's response timeout already expired the ACK is not sent
    (it would disturb other traffic: without this check late ACKs caused bus
    contention in the tests).
  - `IA` (`SWBB_INTERRUPT_ACK`): the interrupt checks the recipient id (the one
    passed to `begin`), the header, the length, the header CRC8 and the CRC32 /
    CRC8 incrementally (CRC32 with the bootloader's 16 entries table, ~6us per
    byte) and acknowledges as soon as the last byte is received, then PJON's
    `send_response` is skipped for that frame. Local mode only (no bus id
    check), not for PJON router mode, no extended length.

### 2. Results

**2.1 Reception, all modes (single attempt, 200 packets, `irq_eval.sh matrix`)**

| Transmitter | Receiver `I` (delivered / acknowledged) | Receiver `IA` (delivered / acknowledged) |
|---|---|---|
| `O` original, modes 1-4 | 200 / 0 | 200 / 200 |
| `P` portable, modes 1-4 | 200 / 0-1 | 200 / 200 |
| `F` `SWBB_PIN`, modes 1-4 | 200 / 0-16 | 200 / 200 |
| interrupt node, modes 1-4 | 200 / 0-35 | 200 / 200 |

Every frame is decoded from every implementation in every mode. Transmitting
from the interrupt node: to `F` 200/200 in all modes, to `O` 200, 199, 200,
185 in modes 1-4 (mode 4 limit of the original receiver, as with `F`).

**2.2 Acknowledgement latency (mode 3, 29 bytes frame, `ack_latency.py`)**

The transmitter waits `SWBB_RESPONSE_OFFSET * length + SWBB_LATENCY` = 20 x 29
+ 8 = 588us after the end of the frame.

| Receiver | Response starts after the end of the frame |
|---|---|
| `F` polling | ~451us (PJON computes the CRC32 after the frame) |
| `I` (PJON from `bus.receive()`) | ~754us: too late, the transmitter gave up |
| `IA` (from the interrupt) | ~88us |

`I` is slower than `F` even with a tight loop: PJON checks the CRC after the
whole frame like with `F`, and the keep busy bits of the transmitter (2 edges
every ~47us) now trigger interrupts that slow it down. `I` needs
`SWBB_RESPONSE_OFFSET` >= 25 **on every transmitter** (tested: 50/50 with 25),
more if the application is busy, which is exactly what the Melvan I2C router review
(`REVIEW-melvan-router.md`) rejected. Short frames (CRC8, <= 15 bytes) are fast enough: a 9 bytes frame is
acknowledged by `I` 100/100, a 15 bytes frame 0/100.

**2.3 Busy application (mode 3, 100 packets, transmitter `F`, the receiver
does `bus.receive()` then busy waits, `irq_eval.sh busy`)**

| Busy time between `bus.receive()` | `F` polling | `I` delivered / ack | `IA` delivered / ack |
|---|---|---|---|
| 0 (`bus.receive()` in a loop) | 100 | 100 / 0 | 100 / 100 |
| 200us | 34 | 100 / 0 | 100 / 100 |
| 500us | 2 | 100 / 0 | 100 / 100 |
| 1ms | 3 | 100 / 0 | 100 / 100 |
| 3ms | 5 | 100 / 0 | 100 / 100 |
| 10ms | 0 | 73 / 0 | 67 / 67 |

A packet is sent every ~11.7ms: with 10ms of work the single frame buffer is
still occupied when the next frame starts and it is ignored (the transmitter
would retry). With retries (normal `bus.send`) the polling receiver also gets
the packets eventually, after several back-off delays.

**2.4 Other interrupts on the receiver (`irq_eval.sh load`: Timer2 interrupt
every 256us busy for 5/10/15us, 100 packets)**

| Receiver | Mode 1: 5 / 10 / 15us | Mode 3: 5 / 10 / 15us |
|---|---|---|
| `F` polling | 100 / 100 / 100 | 100 / 100 / 100 |
| `I` delivered | 100 / 98 / 88 | 100 / 82 / 9 |
| `IA` delivered and acknowledged | 100 / 100 / 88 | 100 / 80 / 5 |

AVR interrupts are not nested: an edge that occurs while another handler runs
is time stamped when that handler ends. The decoder rounds to whole bits, so
it tolerates up to half a bit (14us in mode 3, 22us in mode 1) minus the
transmitter's error. The Arduino Timer0 interrupt (~5us) is fine, longer ones
are not. The polling receiver is immune because it disables interrupts during
each byte. A hardware input capture pin (ICP1, pin 8) would remove this
limitation (the edge time is latched by the timer, the handler only has to
run before the next edge of the same kind).

**2.5 CPU, flash, RAM (mode 3)**

| | `F` | `I` | `IA` |
|---|---|---|---|
| Flash, transmitter / receiver test sketch | 5388 / 3870 | 6736 / 5404 | 7464 / 6154 |
| RAM, receiver test sketch | 542 | 655 | 665 |
| CPU while a frame is received | 100% (blocking) | ~10% (max 14% per ms) | ~17% (max 65% per ms, the ACK) |
| CPU with an idle bus | `bus.receive()` ~14us per call | 0 (no edges) | 0 |
| Interrupt duration per edge | - | 1-12us | 1-12us, 15-33us per byte (CRC), ~0.5ms per ACK |

### 3. Open risks and limits

- Timer1 is taken; INT0 or INT1 only (pins 2, 3). The ATmega88/168/328 only.
- Long interrupt handlers of the application (I2C slave callbacks, serial at
  high baud rates with heavy handlers) corrupt reception (2.4).
- `SWBB_INTERRUPT_ACK` blocks interrupts ~0.5ms per acknowledgement (as the
  polling implementation does for each byte) and acknowledges before the
  application sees the packet: if the application drops it later (port, packet
  id duplicates) it was acknowledged anyway, as PJON already does.
- No bus id check in the interrupt (shared mode not acknowledged there), no
  extended length, no PJON router mode.
- One frame buffer: frames arriving while the previous one was not read are
  ignored. A second buffer would cost `PJON_PACKET_MAX_LENGTH` bytes of RAM.
- Simulation only (ideal bus, simavr interrupt timing). Not run against the
  bootloader suite: the bootloader is unchanged and its ACK (a polling
  transmitter) is received like any other.
