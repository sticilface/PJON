## SoftwareBitBang simulation tests

Two simulated ATmega328P at 16MHz ([simavr](https://github.com/buserror/simavr)) connected by a single wire bus, running PJON SoftwareBitBang sketches compiled with avr-g++ and the minimal Arduino core of `../arduino`. They compare three implementations:

- `O`: the original library (`src` of git `ORIG_REF`, default `master`)
- `P`: this library, portable implementation (`digitalRead`, `digitalWrite`, `pinMode`)
- `F`: this library with `SWBB_PIN` (direct port access)
- `I` / `IA`: `SWBB_PIN` with `SWBB_INTERRUPT` (interrupt driven reception, see [REVIEW-swbb-interrupt.md](/REVIEW-swbb-interrupt.md)), `IA` with `SWBB_INTERRUPT_ACK`

Requirements: `apt install gcc-avr avr-libc binutils-avr libsimavr-dev libelf-dev`

| Script | What it does |
| ------ | ------------ |
| `./run_swbb_tests.sh "<A flags>" "<B flags>"` | A sends `COUNT` (200) packets of `LENGTH` (20) bytes to B, one attempt each (no retries), so the number of acknowledged packets is the raw link reliability. `SIM_FLAGS=-h` prints a histogram of the bit durations on the wire, `SIM_FLAGS=-e` every edge. |
| `./matrix.sh [modes]` | `run_swbb_tests.sh` for every mode and combination of `O`, `P` and `F`. `EXTRA=-DLOAD_ISR=15` adds an interrupt load to both devices (Timer2 interrupt every 256us busy for 15us, like a serial or I2C handler). |
| `./irq_eval.sh [smoke\|matrix\|busy\|load]` | Interrupt driven receiver against the polling ones: reception from every implementation, receiver busy between `bus.receive()` calls (`BUSY` us), interrupt load on the receiver (`ISR` us). |
| `./size_irq.sh` | Flash, RAM and idle polling cost of `F`, `I`, `IA`. |
| `python3 ack_latency.py <trace>` | Acknowledgement latency from a `SIM_FLAGS=-e` trace. |
| `python3 isr_time.py <trace>` | Interrupt time of B from a trace, B built with `-DSWBB_IRQ_PIN_DEBUG` (PD3 high in the interrupts). |
| `./bench.sh [modes]` | CPU time taken polling an idle bus, duration of `can_start()`, and how often `can_start()` wrongly reports the bus free while another device is waiting for a response (keep busy bits). |

The simulator (`swbb_sim.c`) drives the bus pin through the simavr port IRQ, so pin change and external interrupts work, traces PD3 of B (`-e`) and reports bus contention: one device driving the bus high while the other drives it low.
