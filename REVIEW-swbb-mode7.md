## SoftwareBitBang mode 7: a faster link layer (prototype)

Mode 7 replaces the PJDL wire format with a frame based one, keeping the PJON
packet format and the application API. It is selected with `SWBB_MODE 7` and
`SWBB_PIN` (optionally `SWBB_INTERRUPT_WAKE`) and supported by the bootloader
(`MODE=7`). Every device of the bus must use it: it does not interoperate with
modes 1-6.

Everything below was measured in simulation (two ATmega328P at 16MHz, simavr,
cycle accurate) on the analog bus model of the real bus (1.5nF, 100 ohm series
resistors, 100k pull-down, see `REVIEW-swbb-cable.md`) unless stated otherwise.
Hardware results of the test plan below (steps 1-4 on a bench bus: 7.32 kB/s, 0 errors in 880,000 packets) are in
[REVIEW-swbb-hardware.md](REVIEW-swbb-hardware.md#6-mode-7-8ba4996a-the-hardware-test-plan-of-review-swbb-mode7md). Tools: `bootloader/SoftwareBitBang/ATmega328P/
test/swbb/m7_*.sh`.

### Design

```
 ___________________ ___ _______________________ ___________ ___
| PREAMBLE  | SLOT  |   | BYTE                  | BYTE      |...
|___________|_______|   |_      ___     _______ |_          |
| high              |low|S|d0 ... d7 (LSB first)|P| S ...    |
| 96us      | 0-28us| 4 |4|  8 x 4us            |4|          |
```

| Element | Value | Why |
|---|---|---|
| Bit | 4us (`SWBB_M7_BIT`) | 3us works as well at 16MHz, the 8MHz bootloader needs 4us |
| Byte | start bit (high), 8 data bits, stop bit (low): 40us | UART framing, the receiver resynchronizes on every start bit |
| Preamble | 96us high (24 bits) | longer than any high level inside a frame (9 bits), so a receiver can never start in the middle of a frame; at least 44us of it must be left when the receiver starts listening |
| Contention slot | preamble + random 0-7 x 4us, then the bus is driven low, released and read | if it is high another device started with a longer preamble: this one gives up at once and the other frame is not disturbed |
| Interrupt windows | every 4 bytes the bus stays low 48us (`SWBB_M7_GAP`) | both devices serve their interrupts there, interrupts are disabled for at most ~4 bytes (166us); the transmitter's interrupt time is compensated with Timer0 |
| Frame end | from the PJON length byte | the whole frame is handed to PJON in one `receive_frame` call |
| Idle (`can_start`) | bus low for 164us + random 0-16us | longest low level inside a frame: 9 bits + 112us (window made longer by interrupts) |
| Acknowledgement | keep busy pulses of 3us every ~22us; the receiver answers 6us after one: 16us high, 4us low, ACK byte | same scheme as PJDL, shorter |
| CRC | checked by PJON (polling) or by the interrupt (wake receiver) after the frame | |

### Line coding: UART framing vs. bit stuffing

| | UART framing (chosen) | NRZ with bit stuffing (not built) |
|---|---|---|
| Bits per byte | 10 | 8.3 on average, 9.6 worst case |
| Clock difference tolerated | measured -4%..+3% at 3 and 4us bits (theoretical limit about +-4.5% over 9.5 bits) | about +-9% (an edge at least every 5 bits) |
| Receiver | per byte: wait for the start bit, sample 8 bits at fixed cycles (the existing assembly loops) | must time every edge and decode run lengths with deterministic processing between edges 4us apart (64 cycles): much more complex |

Crystals are within +-0.005% and ceramic resonators (Uno, Pro Mini) within
+-0.5%, so +-3.5% is 7 times the worst real oscillator. Bit stuffing would
gain at most 4-17% of throughput, so it was not built. If a wider clock
tolerance is ever needed (e.g. an internal RC oscillator), the cheaper option
is a LIN style sync byte (0x55) after the preamble, from which the receiver
measures the transmitter's bit time.

### Results

**Throughput**, back to back packets with acknowledgement, one attempt each,
200 packets (`m7_speed.sh`), all 200/200 except mode 3 with 50 bytes (199):

| Payload | Mode 3 | Mode 6 | Mode 7 polling | Mode 7 wake receiver |
|---|---|---|---|---|
| 5 bytes | 193 packets/s, 0.96 kB/s | 436, 2.18 kB/s | 679, 3.40 kB/s | 703, 3.52 kB/s |
| 20 bytes | 87, 1.75 kB/s | 209, 4.17 kB/s | 361, 7.22 kB/s | 375, 7.50 kB/s |
| 50 bytes | 46, 2.27 kB/s | 112, 5.60 kB/s | 203, 10.13 kB/s | 211, 10.55 kB/s |

Mode 7 is 1.6-1.8 times mode 6 and 3.5-4.6 times mode 3. With the defaults
of the first prototype (24us windows) it reached 8.3 kB/s with 20 bytes, the
longer windows (needed for interrupt handlers up to 20us) cost ~13%.
Remaining per packet overhead (20 bytes payload, 2.8ms per packet, 1.4ms on
the wire): `can_start` ~0.2ms, receiver processing and CRC before the
acknowledgement ~0.37ms, the transmitter composing the packet.

**Clock difference** between the devices (receiver clock changed, 50
packets, `m7_sweep.sh`):

| Bit | -5% | -4% | -3% | -2% | 0 | +2% | +3% | +4% | +5% |
|---|---|---|---|---|---|---|---|---|---|
| 4us polling | 0 | 50 | 50 | 50 | 50 | 50 | 50 | 30 | 0 |
| 4us wake | | 50 | 50 | | 50 | | 50 | 29 | |
| 3us polling | 0 | 50 | 50 | 50 | 50 | 50 | 50 | 0 | 0 |
| 2.5us polling | 0 | 10/30 | 27/30 | | 30/30 | | 3/30 | 0 | 0 |
| 2us polling | | | | | fails | | | | |

**Interrupt load** on both devices (Timer2 handler every 256us, busy for the
given time), 100 packets (`m7_load.sh`):

| Handler | 5us | 10us | 15us | 20us | 25us |
|---|---|---|---|---|---|
| polling → polling | 100 | 100 | 100 | 100 | 82 |
| polling → wake | 100 | 100 | 100 | 100 | 89 |
| wake → polling | 100 | 100 | 100 | 100 | 86 |

(Mode 6 handled up to 15us polling, 25us with the wake receiver.) A receiver
whose interrupts take longer than the 48us window drops the frame
(the transmitter retries). `SWBB_M7_GAP` sets the window: it must be the same
on all devices and longer than the longest burst of interrupts of any
receiver. With 32us, 15us handlers gave 93-100/100; with 24us, 10us
handlers gave 100/100 and 15us handlers 89-98/100.

**Cable**: 100/100 with 1.5nF and 3nF and on the ideal bus, all combinations
of polling and wake receivers.

**Two devices sending to each other at the same time**, 200 packets each,
one attempt each, 200us of reception between packets (`m7_collide.sh`):

| | delivered A / B | contention events |
|---|---|---|
| mode 3 | 91 / 90 | 164 |
| mode 6 | 86 / 85 | 164 |
| mode 7 polling | 24 / 40 | 162 |
| mode 7 wake, before the contention slot | 194 / 194 | 327 |
| mode 7 wake | 193 / 192 | 3 |

A polling receiver only receives inside `bus.receive()`: while it waits in
`can_start` or transmits, the other device's packet is lost. Mode 7 makes the
exchanges shorter so the two devices miss each other more often (the result
depends on how their loops interleave; with or without the contention slot
it is 15-40 out of 200). Use the wake receiver for devices that transmit and
receive. The contention slot removed almost all bus contention (two
transmitters driving the bus at the same time) with the wake receiver.

**Read-back collision detection** during the data was not built: both
transmitters drive the bus (push-pull), so during a collision the bus is at
~2.5V and a transmitter can not read reliably whether another one is
transmitting. The contention slot detects the common case (two devices that
found the bus free at the same moment) before the data, when the bus can be
released and read.

**Bootloader** (`MODE=7`): 1258 bytes (PJDL modes ~1400). Link from a mode 7
uploader, 200 chunks of 32 bytes, one attempt each: 200/200 with the
bootloader at 16MHz and 8MHz, from a polling and a wake uploader, at 1.5nF
and 3nF. The 8MHz bootloader samples correctly only with `M7_ADVANCE` 19-24
cycles (21 was used): the margin at 8MHz is small, 4us is the shortest bit for
a bus with 8MHz bootloaders. Update (merge with the provisioning bootloader):
the bootloader now checks the stop bit a quarter bit in and keeps the code
between two bytes minimal, the window at 8MHz is 9-19 cycles (1-31 at 16MHz)
and `M7_ADVANCE` is 14. Simulation suite (`run_tests.sh`): 20/20 with
the portable uploader and 20/20 with `DIRECT_IO=1`, including the new `mode7`
tests (full update with polling, with wake receivers, 8MHz bootloader).

**Flash and RAM** (AppWithBootloaderHook, pin 2):

| | Mode 3 | Mode 6 | Mode 7 |
|---|---|---|---|
| polling | 4028 / 488 | 4034 / 488 | 3942 / 491 |
| wake receiver | | 5068 / 615 | 4652 / 602 |

**No regressions** in modes 1-6: the link matrices on the ideal bus and on
the analog model give the same results as before (all combinations with
`SWBB_PIN` 200/200 except mode 4 towards original or portable receivers).

### PJON core: table CRC32 (all modes)

While testing, receivers 2% slower than the transmitter lost most packets:
the receiver's processing between the end of the frame and the
acknowledgement (mostly PJON's bit by bit CRC32) took 520-580us for a 28
bytes frame, and the transmitter waits only 20us per byte (565us). The same
budget applies to every mode. `PJON_CRC32::compute` now uses a 16 entries
table (64 bytes of flash, in PROGMEM on AVR): same results (checked on 2000
random buffers), ~3 times faster. Turnaround for 28 bytes: 540us → 370us.
It costs ~100 bytes of flash; `PJON_CRC32_BITWISE` restores the old code.

### What the simulator can not show

- Noise and interference on 20m of untwisted star cable with a 100k pull-down,
  crosstalk with other conductors of the same cable, ground differences
  between devices.
- The real cable capacitance (estimated 1-2nF) and the real input thresholds.
- Real clock differences (expected to be well within the measured +-3.5%).
- Real interrupt loads (I2C callbacks of the Melvan I2C router can be long:
  they must fit in the 48us window).

### Hardware test plan

1. **Bench, two boards, short wire.** Flash
   [Mode7LinkTest](/examples/ARDUINO/Local/SoftwareBitBang/Mode7LinkTest/Mode7LinkTest.ino)
   on two boards, one with `#define TRANSMITTER`. Run 10 minutes: `failed`
   must be 0 and `received` (receiver) must equal `acknowledged`
   (transmitter). Expected ~7 kB/s with 20 bytes. Repeat with `MODE 6` and
   `MODE 3` for the comparison.
2. **Real bus.** Put the two boards at the two most distant points of the
   star, with all other nodes connected and powered (they may be idle or
   unpowered in any mode: they only load the bus). Same counts, 30 minutes.
3. **Scope** (at the receiver end of the cable):
   - bits 4.0us, preamble 96-124us, windows ~48us low every 4 bytes;
   - rising and falling edges (10-90%): must be well under 1us; the falling
     edge after the last bit is driven (no slow tail through the pull-down);
   - keep busy pulses 3us, acknowledgement 16us high + 4us + byte;
   - noise on the idle line (100k pull-down): spikes above ~1.5V are a risk,
     try 10-22k.
4. **Load and interrupts.** Flash the transmitter sketch on 2-3 boards
   (different `MY_ID`) sending to the same receiver: `failed` counts
   collisions and retries are now visible; run the wake receiver variant
   (`#define SWBB_INTERRUPT_WAKE`) on the receiver.
5. **Bootloader.** On one node that can be reached with an ISP programmer,
   flash the bootloader built with `make PJON_PIN=2 MODE=7`, and update it with
   an uploader in mode 7. Keep a mode 3 bootloader image to go back.

### Risks

- The whole bus must change at once (applications and bootloaders), and a
  mode 7 bootloader can only be updated by a mode 7 uploader (or an ISP).
- The timing values are duplicated in the library (`SWBB_M7_*`) and the
  bootloader (`M7_*`): they must match.
- Interrupt bursts longer than the window on any receiver drop frames.
- Interrupts are disabled up to ~0.3ms when a frame starts (preamble and first
  4 bytes) and ~0.17ms afterwards: a serial port receiving at 115200 baud
  during bus traffic can lose a character.
- A polling device misses packets while it transmits (use the wake receiver
  where devices transmit and receive).
- Prototype: no hardware validation, no range measurement.
