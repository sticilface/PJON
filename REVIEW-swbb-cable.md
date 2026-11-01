## SoftwareBitBang on the real cable: analog bus model

Setup to model: 15 ATmega328P nodes, star topology, ~20m of untwisted cable
in total, 100 ohm series resistor per node, one 100k pull-down.

The simulator used for the previous reviews treated the bus as an ideal wire:
when no device drives it, it is low at once. On this bus a released line is
discharged only by the 100k pull-down. With an estimated 1-2nF of cable and
pin capacitance (~50-100pF/m for untwisted conductors, ~10pF per pin), that
takes about 100us (time constant 100k x 1.5nF = 150us).

`swbb_sim -C <pF> -R <ohm> -P <ohm>` now models the bus as one RC node: each
driver (25 ohm output + series resistor) and the pull-down charge the bus
capacitance, and every device sees the level through the ATmega328P input
hysteresis (rising 2.7V, falling 2.2V). Driven edges take ~0.2us (125 ohm x
1.5nF), so at 20m the lumped model is adequate (propagation ~0.1us, no
significant reflections at these edge rates). `rc_eval.sh` runs the tests on
this model.

### Bug found: releasing the bus while it is high

Two changes made earlier in this branch (to avoid driving low against
another device) released the pin while the bus was high:
- the direct port access implementation (`SWBB_PIN`, also used by both
  interrupt receivers) after the keep busy bit and after a frame whose last
  bit is 1;
- the portable implementation after the keep busy bit.

On the modelled cable the line then falls in ~120us instead of 0.2us: the
transmitter takes its own slow falling edge for the start of the response
and gives up, and keep busy bits look like padding bits.

| 1.5nF, 100k, 50 packets, mode 3 | Before fix | After fix |
|---|---|---|
| `SWBB_PIN` → `SWBB_PIN` | 0/50 | 50/50 |
| `SWBB_PIN` → `SWBB_PIN`, mode 6 | 0/50 | 50/50 |
| portable → portable | 15/50 | 49-50/50 |
| original → original | 50/50 | 50/50 |

The original library was not affected: it always drives the pin low before
switching it to input.

Fix: `bus_release()` drives the bus low for `SWBB_DISCHARGE_US` (2us) before
switching to input, if this device was driving it. That discharges the
cable through the series resistor in well under 1us. Responders start at least
`SWBB_RESPONSE_DELAY` (18us) after the falling edge, so the short low level does
not collide with them. The portable implementation is back to the original
order (`digitalWrite(LOW)` then `pinMode(INPUT)`). The bootloader was not
affected: its acknowledgement ends with a 0 bit.

### Results on the modelled cable (after the fix)

100 packets of 20 bytes, one attempt each, 1.5nF, 100 ohm, 100k (`rc_eval.sh`):

| Mode | original↔original | portable↔portable | `SWBB_PIN`↔`SWBB_PIN` | mixed with `SWBB_PIN` | `SWBB_PIN` → wake / interrupt receiver |
|---|---|---|---|---|---|
| 1 | 100 | 100 | 100 | 100 | 100 / 100 |
| 2 | 100 | 100 | 100 | 100 | 100 / 100 |
| 3 | 100 (160 contention events) | 98 | 100 | 100 | 100 / 100 |
| 4 | 76 (96 contention events) | 92 | 100 | 97-100 | 100 / 100 |
| 5 | - | - | 100 (also at 3nF) | - | wake 100 both ways |
| 6 | - | - | 100 (also at 3nF) | - | wake 100 both ways |

Bootloader link on the same model (200 write chunks, one attempt each): 200/200
in modes 1, 3, 4, 5 and 6 with a `SWBB_PIN` uploader, and in modes 1, 3, 4 with a
portable one.

The contention events with original devices (25 in mode 1, 113 in mode 3 when
`SWBB_PIN` transmits to an original receiver) come from the original
responder, which switches its pin to output (low) before setting it high.

The bootloader suite on the ideal wire still passes: 17/17 portable, 17/17
`SWBB_PIN`.

### What the model says about the cable

- Driven edges are fast (~0.2us) even at 3nF: on this bus the edges are not
  what limits modes 5 and 6. The limit stays the CPU (bit loops, interrupt
  windows and the time between bytes).
- The 100k pull-down only matters when no device drives the bus. Once the fix
  is in, edges never rely on it. It does make the idle line high impedance: on
  an untwisted star cable it picks up interference more easily. If you see
  false frame starts or CRC errors on real hardware, try 10-22k (5V / 10k =
  0.5mA while a device drives high).
- Not modelled: noise, crosstalk between conductors, ground shifts between
  nodes and the exact cable capacitance. Measure the falling edge of a released
  line with a scope to calibrate (it gives the capacitance: time to 37% =
  100k x C).
