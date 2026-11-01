## SoftwareBitBang simulation tests

Two simulated ATmega328P at 16MHz ([simavr](https://github.com/buserror/simavr)) connected by a single wire bus, running PJON SoftwareBitBang sketches compiled with avr-g++ and the minimal Arduino core of `../arduino`. They compare three implementations:

- `O`: the original library (`src` of git `ORIG_REF`, default `master`)
- `P`: this library, portable implementation (`digitalRead`, `digitalWrite`, `pinMode`)
- `F`: this library with `SWBB_PIN` (direct port access)

Requirements: `apt install gcc-avr avr-libc binutils-avr libsimavr-dev libelf-dev`

| Script | What it does |
| ------ | ------------ |
| `./run_swbb_tests.sh "<A flags>" "<B flags>"` | A sends `COUNT` (200) packets of `LENGTH` (20) bytes to B, one attempt each (no retries), so the number of acknowledged packets is the raw link reliability. `SIM_FLAGS=-h` prints a histogram of the bit durations on the wire, `SIM_FLAGS=-e` every edge. |
| `./matrix.sh [modes]` | `run_swbb_tests.sh` for every mode and combination of `O`, `P` and `F`. `EXTRA=-DLOAD_ISR=15` adds an interrupt load to both devices (Timer2 interrupt every 256us busy for 15us, like a serial or I2C handler). |
| `./run_boot_link.sh <mode> "<flags>" [bootloader F_CPU]` | A sketch built with `<flags>` sends 200 write chunks to the bootloader (no application), one attempt each: link reliability with the bootloader. `BL_SRC` selects another bootloader source, `BL_FLAGS` adds bootloader flags. |
| `./sweep.sh <receiver> <mode> [offsets]` | Receive window: a `SWBB_PIN` sender transmits data bits of the mode's bit + offset (us), for receiver `bl`/`bl8` (bootloader at 16/8MHz), `F`, `O` or `P`. |
| `./grid.sh <tests> "<bit pad acc>"...` | Explores fast mode timing values (mode 5 with overridden values): `ff`, `load`, `bl`, `bl8` and transmitter bit offsets in % (`tx+5`, `bl-3`, `b8+3`). |
| `./bench.sh [modes]` | CPU time taken polling an idle bus, duration of `can_start()`, and how often `can_start()` wrongly reports the bus free while another device is waiting for a response (keep busy bits). |

The simulator (`swbb_sim.c`) reports bus contention: one device driving the bus high while the other drives it low.
