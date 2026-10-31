## PJON SoftwareBitBang bootloader for ATmega328P

A 1kB bootloader that updates the firmware of an ATmega328P (ATmega328P-AU / TQFP, or DIP) over a single wire [SoftwareBitBang](/src/strategies/SoftwareBitBang/README.md) PJON bus. The bus pin, device id and mode are chosen at build time. The running application can restart the device into the bootloader with a PJON packet. If nothing arrives, the bootloader times out and starts the application again.

```
 PC ── USB serial ── Uploader (Arduino) ── PJON SWBB bus ── target (ATmega328P)
 pjon_boot_upload.py   Uploader.ino          1 wire + GND   bootloader + application
```

- [Feasibility study](#feasibility-study)
- [How it works](#how-it-works)
- [Building and flashing the bootloader](#building-and-flashing-the-bootloader)
- [Preparing the application](#preparing-the-application)
- [Uploading firmware](#uploading-firmware)
- [Simulation tests](#simulation-tests)
- [Limitations](#limitations)

### Feasibility study

**Question:** can a bootloader receive firmware over PJON SoftwareBitBang (SWBB) and flash an ATmega328P within the limits of the boot section? **Answer: yes.** It takes 1000 bytes in a 1024 byte boot section (`BOOTSZ` = 512 words).

#### Constraints of the ATmega328P

| Resource | Limit | Used by this bootloader |
| -------- | ----- | ----------------------- |
| Boot section | 512, 1024, 2048 or 4096 bytes (`BOOTSZ` fuses) | 996–1000 bytes depending on mode and clock (1024 byte section) |
| Application space | 32768 − boot section | 31744 bytes (Optiboot leaves 32256) |
| SRAM | 2048 bytes | 144 byte packet buffer, 1 byte device id, a few bytes of stack |
| Flash page | 128 bytes, erase + write ≈ 9ms | one page per PJON packet |
| Interrupt vectors | none needed | interrupts stay disabled, polling only |

#### Why not reuse the PJON library?

The PJON library compiled with the SWBB strategy and the Arduino core takes about 4.1kB (the example application in this folder). It also depends on the Arduino core for `micros()`, `delay()`, `digitalRead()` and so on. That cannot fit in 1kB, and it would fill most of the largest 4kB boot section. The flashing task only needs a small subset of PJON, so the bootloader reimplements that subset in C (without the Arduino core) and drops the rest:

| Feature | Needed to flash? | Bootloader |
| ------- | ---------------- | ---------- |
| PJDL frame reception, one mode | yes | implemented, mode chosen at build time |
| PJDL synchronous acknowledgement | yes | implemented (only transmission it ever does) |
| Local mode, device id | yes | implemented |
| CRC32 | yes | implemented |
| Meta-data CRC8 | no, CRC32 covers the same bytes | not checked |
| Port | yes, to separate bootloader traffic from application traffic | implemented (fixed port 45063 / 0xB007) |
| Sender info, packet id | no | skipped if present |
| Shared mode, bus id, MAC, extended length, routing | no | rejected |
| Sending packets, medium access, retries | no, the uploader drives the exchange | not implemented |

#### Timing

SWBB is a software, timing based protocol, so timing accuracy was the main risk. The bootloader samples the pin with direct port reads, measures durations with Timer1 (0.5µs resolution at 16MHz) and waits with cycle exact delays. That is more precise than PJON running on the Arduino core, which uses `digitalRead()` and `micros()` (4µs resolution). Each byte resynchronizes on its padding bit, so clock tolerance only matters within one byte: a crystal or ceramic resonator is fine, while the uncalibrated internal RC oscillator (±10%) is not recommended.

Two PJON behaviours had to be handled. Both were found and checked in simulation against the real PJON code (see [Simulation tests](#simulation-tests)):
- PJON on Arduino transmits data bits of about 44.5µs and padding bits of about 115µs. That is longer than the nominal values but within PJDL tolerances. The bootloader samples at the nominal PJDL timing and waits for each padding bit's rising edge, so small differences do not accumulate.
- While waiting for the acknowledgement, PJON keeps the bus busy with short pulses, and after each pulse it drives the pin low for a few µs before releasing it. The bootloader answers 18µs after the falling edge, which is inside the window where the transmitter is listening, so the two sides never drive the bus against each other.

#### Flash write time vs acknowledgement timeout

The bootloader acknowledges a page only after it has been erased, written and verified. That gives end-to-end confirmation, at the cost of about 9ms. PJON's default response timeout for a 142 byte packet is about 2.8ms, so the uploader is compiled with `SWBB_RESPONSE_OFFSET 150` (≈21ms). This setting is only needed on the uploader; the application and the rest of the bus are not affected.

#### Throughput

Measured in simulation at 16MHz, mode 1: about 1.4kB/s. A full 31kB application flashes in about 23s, and a 4kB application in about 3s. Modes 3 and 4 are about 30% faster.

### How it works

The `BOOTRST` fuse makes the bootloader run after every reset. At startup it reads the reset cause and decides what to do:

| Reset cause | Behaviour | Device id |
| ----------- | --------- | --------- |
| Watchdog reset requested by the application (mailbox in SRAM) | stays active for `TIMEOUT_MS` (5s) | the id passed by the application |
| Reset pin (`EXTRF`) | stays active for `EXTRF_TIMEOUT_MS` (1s), 0 disables it | `DEVICE_ID` |
| No application in flash | stays active until a valid application is written and started | `DEVICE_ID` |
| Anything else (power on, brown out, plain watchdog reset) | starts the application immediately | |

Every valid command restarts the timeout. When it expires the application is started, or the bootloader keeps waiting if there is no application.

**Entering the bootloader from the application.** The application calls `pjon_boot_enter(id)` from [`PJON_Bootloader.h`](/src/utils/bootloader/PJON_Bootloader.h). It writes a 3 byte mailbox (magic + its device id) at the top of SRAM and triggers a 15ms watchdog reset. SRAM survives a watchdog reset, so the bootloader finds the mailbox, clears it and answers on the id the application was using. A board with several devices on the bus therefore needs only one bootloader build. `DEVICE_ID` is used only when the bootloader is entered with the reset pin or with no application in flash.

**Protocol.** PJON local mode packets with CRC32, on port 45063, acknowledged:

| Command | Payload | Sent to | Effect |
| ------- | ------- | ------- | ------ |
| Enter | `'B'` | application | application restarts into the bootloader |
| Ping | `'P'` | bootloader | acknowledged if the bootloader is active |
| Write | `'W'` address(2 bytes, MSB first) data(128 bytes) | bootloader | erases, writes and verifies the page, acknowledged only if the flash content is correct |
| Go | `'G'` | bootloader | starts the application |

Writes outside the application area (the bootloader section) or to unaligned addresses are refused (not acknowledged). Pages already holding the right content are not rewritten. The uploader erases page 0 first and writes it last. If an upload is interrupted, the reset vector is therefore erased, and the device stays in the bootloader instead of running a half written application.

The bootloader leaves the reset flags (`MCUSR`) in `GPIOR0` for the application, because it has to clear `MCUSR`.

### Building and flashing the bootloader

Requires `avr-gcc`, `avr-libc` and `avrdude` (`apt install gcc-avr avr-libc avrdude`).

```bash
cd bootloader/SoftwareBitBang/ATmega328P
make PJON_PIN=12 DEVICE_ID=44
```

| Option | Default | Description |
| ------ | ------- | ----------- |
| `PJON_PIN` | 12 | Arduino pin number of the bus, 0–19 (A0 = 14) |
| `DEVICE_ID` | 44 | id used after a reset pin reset or with no application |
| `MODE` | 1 | SoftwareBitBang mode 1–4, must match the bus (`SWBB_MODE`) |
| `TIMEOUT_MS` | 5000 | time in the bootloader after the application requested it |
| `EXTRF_TIMEOUT_MS` | 1000 | time in the bootloader after a reset pin reset, 0 = never |
| `F_CPU` | 16000000 | clock frequency |
| `BOOT_SIZE` | 1024 | boot section size; it fits in 1024, larger values are accepted |

The build fails if the bootloader does not fit in `BOOT_SIZE`. Flash it with an ISP programmer; this also sets the fuses:

```bash
make flash PJON_PIN=12 DEVICE_ID=44 ISP=usbasp
```

```bash
make flash ISP=arduino ISP_PORT=/dev/ttyACM0 AVRDUDE_FLAGS="-b 19200"
```

The second form uses an Arduino running the ArduinoISP sketch. The fuses are: high `0xDC` (1kB boot section, `BOOTRST`, `SPIEN`), low `0xFF` (external crystal), extended `0xFD` (brown-out at 2.7V). Override them with `LFUSE=` and `EFUSE=` if your board uses a different clock source.

Pin mapping on the ATmega328P-AU (TQFP-32):

| Arduino pin | Port | TQFP pin | | Arduino pin | Port | TQFP pin |
| ----------- | ---- | -------- |-| ----------- | ---- | -------- |
| 0 | PD0 | 30 | | 10 | PB2 | 14 |
| 1 | PD1 | 31 | | 11 | PB3 | 15 |
| 2 | PD2 | 32 | | 12 | PB4 | 16 |
| 3 | PD3 | 1 | | 13 | PB5 | 17 |
| 4 | PD4 | 2 | | 14 (A0) | PC0 | 23 |
| 5 | PD5 | 9 | | 15 (A1) | PC1 | 24 |
| 6 | PD6 | 10 | | 16 (A2) | PC2 | 25 |
| 7 | PD7 | 11 | | 17 (A3) | PC3 | 26 |
| 8 | PB0 | 12 | | 18 (A4) | PC4 | 27 |
| 9 | PB1 | 13 | | 19 (A5) | PC5 | 28 |

Avoid pin 13 if the board has an LED on it, and pins 0 and 1 if the application uses `Serial`. Wire the bus as described in the [SoftwareBitBang documentation](/src/strategies/SoftwareBitBang/README.md), including the suggested pull-down resistor: the bootloader releases the pin as an input without pull-up and relies on the bus being low when idle.

### Preparing the application

The application must use the same bus pin and mode, include the port feature and call `pjon_boot_enter()` when it receives the request. See [AppWithBootloaderHook](/examples/ARDUINO/Local/SoftwareBitBang/Bootloader/AppWithBootloaderHook/AppWithBootloaderHook.ino):

```cpp
#define PJON_INCLUDE_PORT
#include <utils/bootloader/PJON_Bootloader.h>
#include <PJONSoftwareBitBang.h>

PJONSoftwareBitBang bus(44);

void receiver_function(uint8_t *payload, uint16_t length, const PJON_Packet_Info &info) {
  if(pjon_boot_is_request(payload, length, info.port))
    pjon_boot_enter(bus.device_id()); // does not return
  // ... the application's own packets
};
```

The application must also call `bus.receive()` regularly. The application must be smaller than 31744 bytes; the Arduino IDE assumes the 512 byte Optiboot and allows 32256, and `pjon_boot_upload.py` refuses images that would overlap the bootloader. To get a HEX file, use *Sketch > Export compiled binary* in the Arduino IDE (take the file without `with_bootloader`), or `arduino-cli compile --output-dir`.

If an application without the hook was flashed, the device can still be recovered: pressing reset opens the bootloader for `EXTRF_TIMEOUT_MS` using `DEVICE_ID`. Use `--no-enter` and start the upload just before pressing reset; the uploader retries for several seconds.

### Uploading firmware

1. Flash the [Uploader](/examples/ARDUINO/Local/SoftwareBitBang/Bootloader/Uploader/Uploader.ino) sketch on any Arduino (same pin and `SWBB_MODE` as the bus). It is a serial to PJON bridge, and it uses a longer packet buffer and response timeout (see above).
2. Install pyserial (`pip install pyserial`) and run:

```bash
python3 tools/pjon_boot_upload.py --port /dev/ttyUSB0 --id 44 firmware.hex
```

```
firmware.hex: 4224 bytes, 33 pages
application restarted into the bootloader
bootloader found
written 33 pages in 3.2s
application started
```

Use `--no-enter` when the device is already in the bootloader (new chip or recovery). Use `--boot-start` if the bootloader was built with a larger `BOOT_SIZE`.

### Simulation tests

[`test/run_tests.sh`](test/run_tests.sh) runs end to end tests with two simulated ATmega328P ([simavr](https://github.com/buserror/simavr)) sharing one bus wire:
- the target runs the bootloader and the example application, built with the real PJON library;
- the uploader runs the Uploader sketch, with the real PJON library;
- `pjon_boot_upload.py` talks to the simulated uploader through a pty, exactly as it would with a real Arduino.

The sketches are compiled with `avr-g++` using a minimal Arduino core replacement ([`test/arduino`](test/arduino)) that keeps the timing behaviour of `digitalRead()`, `digitalWrite()`, `micros()` and `delayMicroseconds()`. The simulator also reports any moment where one side drives the bus high while the other drives it low.

```bash
sudo apt install gcc-avr avr-libc binutils-avr libsimavr-dev libelf-dev
pip install pyserial
./test/run_tests.sh
```

| Test | Checks |
| ---- | ------ |
| `hook_upload` | app v1 receives the request, restarts into the bootloader, app v2 is written, verified against the HEX file and started |
| `timeout` | after entering the bootloader with no further commands, app v1 restarts after 5s |
| `fresh_chip` | with no application the bootloader stays active (beyond the timeout) and accepts an upload |
| `interrupted` | an upload stopped after 10 pages leaves the device in the bootloader; a new upload completes |
| `pins` | pins 2 (PD2) and 17 (A3/PC3) |
| `modes` | SoftwareBitBang modes 2, 3 and 4 |
| `8mhz` | bootloader built for and running at 8MHz |
| `large_image` | 31744 byte image filling the whole application area |
| `protection` | writes to the bootloader section and to unaligned addresses are refused |

All tests pass. The simulator reports no bus contention while the target runs the bootloader; the few events it reports in some runs happen while the application (stock PJON library) exchanges the enter request with the uploader. Simulation covers the logic and the timing against the real PJON code. It does not cover the electrical side (cable length, pull-down value, noise) or clock deviations of real oscillators, so test on the real board before deploying.

### Limitations

- ATmega328P only (pin mapping, page size, fuses). The ATmega168/88 or ATmega328PB would need small changes.
- One mode, one pin, one clock frequency per build; local mode only; no shared mode bus id.
- The bootloader never transmits packets on its own, it only acknowledges. Verification is per page; there is no read-back of the whole image.
- EEPROM is not written.
- While a page is written (≈9ms) the bootloader does not listen to the bus. Other devices addressing the target in that moment are not acknowledged and retry.
