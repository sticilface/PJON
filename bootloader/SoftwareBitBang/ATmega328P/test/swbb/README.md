## SoftwareBitBang simulation tests

Two simulated ATmega328P (or ATmega2560: `swbb_sim -M`, `MCU_A`/`MCU_B`, `PIN_A`/`PIN_B` in `run_swbb_tests.sh`) at 16MHz ([simavr](https://github.com/buserror/simavr)) connected by a single wire bus, running PJON SoftwareBitBang sketches compiled with avr-g++ and the minimal Arduino core of `../arduino`. They compare three implementations:

- `O`: the original library (`src` of git `ORIG_REF`, default `master`)
- `P`: this library, portable implementation (`digitalRead`, `digitalWrite`, `pinMode`)
- `F`: this library with `SWBB_PIN` (direct port access)
- `I` / `IA`: `SWBB_PIN` with `SWBB_INTERRUPT` (interrupt driven reception, see [REVIEW-swbb-interrupt.md](/REVIEW-swbb-interrupt.md)), `IA` with `SWBB_INTERRUPT_ACK`
- `W`: `SWBB_PIN` with `SWBB_INTERRUPT_WAKE` (interrupt triggered reception, see [REVIEW-swbb-interrupt-wake.md](/REVIEW-swbb-interrupt-wake.md))

Requirements: `apt install gcc-avr avr-libc binutils-avr libsimavr-dev libelf-dev`

| Script | What it does |
| ------ | ------------ |
| `./run_swbb_tests.sh "<A flags>" "<B flags>"` | A sends `COUNT` (200) packets of `LENGTH` (20) bytes to B, one attempt each (no retries), so the number of acknowledged packets is the raw link reliability. `SIM_FLAGS=-h` prints a histogram of the bit durations on the wire, `SIM_FLAGS=-e` every edge. |
| `./matrix.sh [modes]` | `run_swbb_tests.sh` for every mode and combination of `O`, `P` and `F`. `EXTRA=-DLOAD_ISR=15` adds an interrupt load to both devices (Timer2 interrupt every 256us busy for 15us, like a serial or I2C handler). |
| `./run_boot_link.sh <mode> "<flags>" [bootloader F_CPU]` | A sketch built with `<flags>` sends 200 write chunks to the bootloader (no application), one attempt each: link reliability with the bootloader. `BL_SRC` selects another bootloader source, `BL_FLAGS` adds bootloader flags. |
| `./sweep.sh <receiver> <mode> [offsets]` | Receive window: a `SWBB_PIN` sender transmits data bits of the mode's bit + offset (us), for receiver `bl`/`bl8` (bootloader at 16/8MHz), `F`, `O` or `P`. |
| `./grid.sh <tests> "<bit pad acc>"...` | Explores fast mode timing values (mode 5 with overridden values): `ff`, `load`, `bl`, `bl8` and transmitter bit offsets in % (`tx+5`, `bl-3`, `b8+3`). |
| `./irq_eval.sh [smoke\|matrix\|busy\|load]` | Interrupt driven receiver against the polling ones: reception from every implementation, receiver busy between `bus.receive()` calls (`BUSY` us), interrupt load on the receiver (`ISR` us). |
| `./size_irq.sh` | Flash, RAM and idle polling cost of `F`, `I`, `IA`. |
| `python3 ack_latency.py <trace>` | Acknowledgement latency from a `SIM_FLAGS=-e` trace. |
| `python3 isr_time.py <trace>` | Interrupt time of B from a trace, B built with `-DSWBB_IRQ_PIN_DEBUG` (PD3 high in the interrupts). |
| `./wake_eval.sh <section>` | Interrupt triggered receiver `W`: `matrix` (every implementation to and from `W`, modes 1-6), `busy`, `gap` (busy receiver, packets every `GAP_MS`), `load` (`ISR` us), `foreign` (packets to another device interleaved), `cpu` (interrupt time, uses `wake_isr.py`), `millis`, `pair <A> <B> [mode]`. |
| `./size_wake.sh` | Flash and RAM of `F`, `IA`, `W`. |
| `./rc_eval.sh [matrix\|fast\|boot]` | Tests on the analog bus model (`swbb_sim -C <pF> -R <ohm> -P <ohm>`): `CAP` (default 1500pF), series 100 ohm, `PULL` (default 100k). |
| `./m7_eval.sh <tx> <rx> [flags]` | Mode 7 link test (`F` polling, `W` wake receiver; `3`, `6` for comparison) on the analog bus model. `CAP`, `PULL`, `BFREQ` (clock of the receiver, for clock error tests), `M7` (flags for both). |
| `./m7_sweep.sh "<bit us>" "<clock %>"` | Mode 7 receiver clock tolerance for each bit time. |
| `./m7_speed.sh` | Throughput with acknowledgement (back to back packets) of modes 3, 6 and 7, payloads of 5, 20, 50 bytes. |
| `./m7_load.sh`, `./m7_gap.sh` | Mode 7 under interrupt load (handler every 256us on both devices), interrupt window length. |
| `./m7_collide.sh` | Both devices transmitting to each other at the same time (modes 3, 6, 7, 7 wake). |
| `./m7_boot.sh` | Mode 7 uploader to bootloader link (200 chunks), bootloader at 16/8MHz. |
| `./mega_eval.sh <O\|P\|F\|W> "<modes>" [ideal\|cable]` | ATmega2560 (pin 3) with ATmega328P (pin 2) nodes, every direction (`PAIRS`), `IMPL_U` for another implementation on the 328P, `EXTRA_M`/`EXTRA_U` for extra flags. |
| `./mega_load.sh [load\|busy]` | Interrupt load and busy loops on the Mega. |
| `./mega_size.sh`, `./mega_compile.sh` | Flash/RAM on both MCUs, compile check of every MCU / pin / implementation / mode. |
| `./three.sh "<tx flags>" "<rx flags>"`, `./three_eval.sh <mode> <tx> <rx>` | Three devices (`swbb_sim -c`): two transmitters (ids 1 and 3, `TX2_ID` to change the second) sending back to back to one receiver: fairness, acknowledged vs received per sender. `SIM_FLAGS="-G 16008000"` gives the second transmitter a 500ppm faster clock. |
| `./accept_eval.sh [modes]` | Wake receiver acknowledgements match what PJON accepts (packet id, port, MAC with and without the `PJON_INCLUDE_*` option). `LIB=<src dir>` runs it on another library version. |
| `./alive_eval.sh [modes]` | `SWBB_WAKE_ALIVE_MS`: the receiver's application stops calling `bus.receive()` (also with the queue looking empty, as after a stack overflow); busy loops of 20/50/150ms. |
| `./stack_eval.sh` | Static RAM, deepest stack use, stack used by the INT0 interrupt (nested interrupts included) and `pjon_stack_free()`, for polling and wake receivers with 1 and 2 frames (`swbb_sim -S`). |
| `./bench.sh [modes]` | CPU time taken polling an idle bus, duration of `can_start()`, and how often `can_start()` wrongly reports the bus free while another device is waiting for a response (keep busy bits). |

The simulator (`swbb_sim.c`) drives the bus pin through the simavr port IRQ, so pin change and external interrupts work, traces PD3 of B (`-e`) and reports bus contention: one device driving the bus high while the other drives it low.
