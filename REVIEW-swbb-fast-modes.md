## SoftwareBitBang: bootloader timing and fast modes 5 and 6

Hardware results for mode 6 (bench bus, two boards and a full system through a router): [REVIEW-swbb-hardware.md](REVIEW-swbb-hardware.md).

Follow-up of [REVIEW-swbb-atmega328p.md](REVIEW-swbb-atmega328p.md), now that the
bootloader may change too. Two changes:

1. The bootloader uses the same timing as the direct port access implementation
   (`SWBB_PIN`), so both have the same margins.
2. Two faster modes, 5 and 6, for `SWBB_PIN` devices and the bootloader.

Everything was measured on simulated ATmega328P (simavr, cycle accurate, ideal
wire). The tools are in
[`bootloader/SoftwareBitBang/ATmega328P/test/swbb`](bootloader/SoftwareBitBang/ATmega328P/test/swbb/README.md):
`run_boot_link.sh` (200 write chunks to the bootloader, one attempt each), `sweep.sh`
(receive windows), `grid.sh` (fast mode exploration). The bootloader suite gained
the `ORIG_SRC`, `BL_SOURCE` options and the `fast_modes` test.

### 1. Bootloader timing re-centred

Changes in `pjon_swbb_bootloader.c` (modes 1-4):
- Received data bits are expected to last bit + 0.5us, the ACK is transmitted
  with bits of bit + 1us: the same as `SWBB_RX_BIT` / `SWBB_TX_BIT`.
- The data bit loops (receive and ACK) are the cycle exact assembly loops of the
  library (the C loops had a 1-2 cycle data dependent overhead).
- Timing accepts fractions of microseconds.
- Each padding bit inside a frame is measured from the end of the previous byte
  (Timer1), as the library does with Timer0. The CRC32 update between bytes
  (~6us at 16MHz, ~12us at 8MHz) no longer eats into the padding bit, which is
  what made modes 5 and 6 possible at 8MHz.
- Size: +50-60 bytes (1392-1410 bytes, limit 2048).

Receive window of the bootloader (data bit duration accepted from a `SWBB_PIN`
sender, 50 write chunks per point, 0.5us steps; 16MHz and 8MHz give the same
results):

| Mode (bit) | Previous bootloader | New bootloader | `SWBB_PIN` sender | Stock sender |
| ---------- | ------------------- | -------------- | ----------------- | ------------ |
| 1 (44us) | at least 42-46.5us (edges outside the range swept) | 42.5-46.5us | 45us | ~44.2us |
| 3 (28us) | 27-29.5us | 27.5-30.5us | 29us | ~28.2us |
| 4 (26us) | 25-27.5us | 25.5-28us | 27us | ~26.3us |

Margins of the `SWBB_PIN` sender (shorter / longer side): mode 3 from 2.0 / 0.5us
to 1.5 / 1.5us, mode 4 from 2.0 / 0.5us to 1.5 / 1.0us. Margins of stock
senders (previous PJON versions, portable implementation) on the short side: mode
3 from ~1.2 to ~0.7us, mode 4 from ~1.3 to ~0.8us. They still pass every test
(below) but have less margin than before; the window is now centred between the
two kinds of sender.

Link with the new bootloader, 200 write chunks of 32 bytes, one attempt each:

| Sender | Modes 1-4, bootloader at 16MHz | Modes 1-4, bootloader at 8MHz | Interrupt load on the sender |
| ------ | ------------------------------ | ----------------------------- | ---------------------------- |
| original library (master) | 200/200 | 200/200 | 0/200 |
| portable implementation | 200/200 | 200/200 | 0/200 |
| `SWBB_PIN` | 200/200 | 200/200 | 200/200 |

(Interrupt load: a 15us handler every 256us. Senders without `SWBB_PIN`
corrupt their own frames under that load, see the previous review.)

Bootloader suite (`run_tests.sh`):

| Configuration | Result |
| ------------- | ------ |
| new bootloader, portable uploader and application (+ `fast_modes`) | 17/17 |
| new bootloader, `SWBB_PIN` uploader and application (+ `fast_modes`) | 17/17 |
| new bootloader, uploader and application built from the original library (`ORIG_SRC`) | 14/14 |
| previous bootloader (commit 1a91440), portable uploader and application | 14/14 |
| previous bootloader, `SWBB_PIN` uploader and application | 14/14 |

Already deployed nodes (stock PJON, previous bootloader) keep working with every
combination.

### 2. Fast modes 5 and 6

The bit durations of modes 1-4 were chosen for `digitalRead`/`digitalWrite`. With
cycle exact timing on both sides (the `SWBB_PIN` implementation and the
bootloader) much shorter bits work. Modes 5 and 6 are not part of PJDL v5.0:
every device of the bus must use `SWBB_PIN` (the portable implementation stops
with `#error "SWBB_MODE 5 and 6 require SWBB_PIN (direct port access)"`), the
bootloader must be built with the same `MODE`.

| Mode | Data bit | Padding bit | Acceptance | Keep busy | Byte | Speed | vs mode 4 |
| ---- | -------- | ----------- | ---------- | --------- | ---- | ----- | --------- |
| 4 | 26us | 65us | 30us | 6.5us | 299us | 3.34kB/s | 1x |
| 5 | 12us | 50us | 16us | 4us | 158us | 6.3kB/s | 1.9x |
| 6 | 8us | 50us | 12us | 4us | 122us | 8.2kB/s | 2.5x |

Measured, 200 packets of 20 bytes with acknowledgement (including `can_start`,
the ACK and the loop): mode 4 2.23s, mode 5 1.28s (1.75x), mode 6 1.05s (2.13x).
200 write chunks to the bootloader (excluding the 0.8s spent waiting for page
writes): mode 4 3.22s, mode 5 1.76s (1.83x), mode 6 1.41s (2.29x).

**How the values were chosen** (`grid.sh`, 50 packets per point):

| Data bit / padding bit (us) | No load | Interrupt load on both | Notes |
| --- | --- | --- | --- |
| 8-12 / 20-30 | 34-49/50 | 0-39/50 | padding bit too short |
| 14 / 35 | 50/50 | 48/50 | |
| 8-16 / 40 | 50/50 | 47-50/50 | |
| 6-14 / 45 | 50/50 | 50/50, but 199/200 over 200 packets | |
| 8-12 / 50 | 50/50 | 200/200 (200 packets) | chosen |
| 5 / 45 | 50/50 | 50/50 | bootloader fails (15/50 at 16MHz, 0/50 at 8MHz) |

- The **padding bit** can not shrink with the data bit. Between two bytes the
  receiver serves pending interrupts and PJON processes the byte just received;
  the padding bit must still be high when the receiver is ready. Its duration is
  compensated (measured from the end of the previous byte), but if the padding
  bit is already over the byte is lost. Measured latency: up to ~15us without
  load, up to ~45us with the 15us interrupt load. 50us gives 200/200 under load.
- The **data bit** is limited by the bootloader: 5us fails, 6us works but
  8us leaves margin at 8MHz. The shorter the bit, the larger the share of the
  fixed processing time between the edge and the first sample. Without
  compensation the receivers sampled ~2.7us late, so 8us bits worked only from
  −1% to +9%. In modes 5 and 6 both receivers now start sampling earlier by a
  fixed number of cycles (`SWBB_SAMPLE_ADVANCE` 50, bootloader `SAMPLE_ADVANCE`
  40). Modes 1-4 are unchanged.
- The **acceptance** is data bit + 4us, the **keep busy bit** 4us (the 8MHz
  bootloader detects a high level every ~1.25us).
- In modes 5 and 6 there is no +1us / +0.5us offset: only exact implementations
  are involved, transmitter and receiver use the nominal bit.

**Margins of the chosen values** (sender data bit changed by a percentage, 50
packets; all 50/50 unless stated):

| Receiver | Mode 5 (12us) | Mode 6 (8us) |
| -------- | ------------- | ------------ |
| `SWBB_PIN` | −5% .. +5% | −5% .. +5% |
| bootloader 16MHz | −5% .. +5% | −5% .. +5% |
| bootloader 8MHz | −5% .. +5% | −5% .. +3% (2/50 at +5%) |

With ±5% of 8 data bits the last sample moves by ±3.4us (mode 5) / ±2.3us
(mode 6). Clock differences between devices are much smaller (crystals 50ppm,
ceramic resonators ±0.5%).

**Results with the chosen values** (200 packets, one attempt each):

| Test | Mode 5 | Mode 6 |
| ---- | ------ | ------ |
| `SWBB_PIN` → `SWBB_PIN` (20 bytes) | 200/200, no contention | 200/200, no contention |
| same, interrupt load on both | 200/200 | 200/200 |
| `SWBB_PIN` → bootloader 16MHz (35 byte chunks) | 200/200 | 200/200 |
| `SWBB_PIN` → bootloader 8MHz | 200/200 | 200/200 |
| `SWBB_PIN` with interrupt load → bootloader | 200/200 | 200/200 |
| bootloader suite `fast_modes` (full update mode 5, mode 6, mode 5 with the bootloader at 8MHz) | pass | pass |

### 3. Limits and risks

- **The cable is not simulated.** The simulator's bus is ideal: no capacitance,
  no reflections, no noise, no slow edges. Data bits are driven both ways, so
  their edges are fast (series resistor × cable capacitance, roughly 1us per
  few nF at 150Ω), but the last falling edge of each frame and of each ACK is
  produced only by the pull-down resistor. With 8us bits an edge slower than
  ~2us already uses most of the margin. Rough, unmeasured expectation: tens of
  metres in mode 5, less in mode 6, far below the 800m of mode 4. **Test on the
  real bus with an oscilloscope** (check that a 0 bit after a padding bit is
  clearly low at its middle) before using modes 5 and 6, keep a way back to mode
  3 or 4 (the bootloader is built for one mode).
- Every device of the bus must be rebuilt: there is no automatic fallback, a
  device in mode 4 does not hear a device in mode 5.
- The ±4us accuracy of the padding bit when interrupts are compensated (Timer0
  resolution) is a larger fraction of the shorter acceptance margin; it is
  included in the tests above.
- Stock senders reach the new bootloader with ~0.5us less margin than before on
  the short side (still 200/200 in all modes).
- Only the ATmega328P at 16MHz (library) and 16/8MHz (bootloader) was tested.
