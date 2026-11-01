## PJON SoftwareBitBang bootloader for ATmega328P

A 2kB bootloader that updates the firmware of an ATmega328P (ATmega328P-AU / TQFP, or DIP) over a single wire [SoftwareBitBang](/src/strategies/SoftwareBitBang/README.md) PJON bus. The bus pin, device id and mode are chosen at build time. The running application can restart the device into the bootloader with a PJON packet. If nothing arrives, the bootloader times out and starts the application again.

Firmware is sent in small chunks (32 bytes by default), and every acknowledgement is immediate. So the sender can be a stock PJON device with default settings (`PJON_PACKET_MAX_LENGTH` 50, default `SWBB_RESPONSE_OFFSET`), including a PJON router.

```
 PC ── USB serial ── Uploader (Arduino) ── PJON SWBB bus ── target (ATmega328P)
 pjon_boot_upload.py   Uploader.ino          1 wire + GND   bootloader + application
```

- [Feasibility study](#feasibility-study)
- [How it works](#how-it-works)
- [Building and flashing the bootloader](#building-and-flashing-the-bootloader)
- [Preparing the application](#preparing-the-application)
- [Uploading firmware](#uploading-firmware)
- [Sending firmware from other PJON devices](#sending-firmware-from-other-pjon-devices)
- [Simulation tests](#simulation-tests)
- [Limitations](#limitations)

### Feasibility study

**Question:** can a bootloader receive firmware over PJON SoftwareBitBang (SWBB) and flash an ATmega328P within the limits of the boot section? **Answer: yes.** It takes 1346–1350 bytes in a 2048 byte boot section (`BOOTSZ` = 1024 words).

#### Constraints of the ATmega328P

| Resource | Limit | Used by this bootloader |
| -------- | ----- | ----------------------- |
| Boot section | 512, 1024, 2048 or 4096 bytes (`BOOTSZ` fuses) | 1346–1350 bytes (2048 byte section) |
| Application space | 32768 − boot section | 30720 bytes (Optiboot leaves 32256) |
| SRAM | 2048 bytes | 48 byte packet buffer, 128 byte page buffer, a few bytes of state and stack |
| Flash page | 128 bytes, erase + write ≈ 9ms | assembled from 4 chunks of 32 bytes |
| Interrupt vectors | none needed | interrupts stay disabled, polling only |

Sizes measured with avr-gcc 7.3.0 at 16MHz:

| | pin 2 | pin 12 | pin 2, 8MHz |
| - | ----- | ------ | ----------- |
| mode 1 | 1410 | 1410 | 1404 |
| mode 2 | 1402 | 1402 | 1392 |
| mode 3 | 1398 | 1398 | 1392 |
| mode 4 | 1404 | 1404 | 1398 |
| mode 5 | 1400 | 1400 | 1394 |
| mode 6 | 1406 | 1406 | 1396 |

An earlier version without chunks fitted in 1kB (1000 bytes). Chunked writes need a separate page buffer and chunk tracking, so a 1kB build no longer fits. The Makefile refuses `BOOT_SIZE=1024`.

#### Why not reuse the PJON library?

The PJON library compiled with the SWBB strategy and the Arduino core takes about 4.1kB (the example application in this folder). It also depends on the Arduino core for `micros()`, `delay()`, `digitalRead()` and so on. That fills most of the largest 4kB boot section. The flashing task only needs a small subset of PJON, so the bootloader reimplements that subset in C (without the Arduino core) and drops the rest:

| Feature | Needed to flash? | Bootloader |
| ------- | ---------------- | ---------- |
| PJDL frame reception, one mode | yes | implemented, mode chosen at build time |
| PJDL synchronous acknowledgement | yes | implemented (only transmission it ever does) |
| Local mode, device id | yes | implemented |
| CRC32 | yes | implemented, required on every packet |
| Meta-data CRC8 | no, CRC32 covers the same bytes | not checked |
| Port | yes, to separate bootloader traffic from application traffic | implemented (fixed port 45063 / 0xB007) |
| Sender info, packet id | no | accepted and skipped |
| Shared mode, bus id, MAC, extended length, routing | no | rejected |
| Sending packets, medium access, retries | no, the sender drives the exchange | not implemented |

#### Timing

SWBB is a software, timing based protocol, so timing accuracy was the main risk. The bootloader samples the pin with direct port reads, measures durations with Timer1 (0.5µs resolution at 16MHz) and waits with cycle exact delays. That is more precise than PJON running on the Arduino core, which uses `digitalRead()` and `micros()` (4µs resolution). Each byte resynchronizes on its padding bit, so clock tolerance only matters within one byte: a crystal or ceramic resonator is fine, while the uncalibrated internal RC oscillator (±10%) is not recommended.

Three PJON behaviours had to be handled. All were found and checked in simulation against the real PJON code (see [Simulation tests](#simulation-tests)):
- PJON on Arduino transmits data bits of about 44.5µs and padding bits of about 115µs. That is longer than the nominal values but within PJDL tolerances. The bootloader samples at the nominal PJDL timing and waits for each padding bit's rising edge, so small differences do not accumulate.
- While waiting for the acknowledgement, PJON keeps the bus busy with short pulses, and after each pulse it drives the pin low for a few µs before releasing it. The bootloader answers 18µs after the falling edge, which is inside the window where the sender is listening, so the two sides never drive the bus against each other.
- The default response timeout is short: 20µs per byte, so about 1ms for a 48 byte chunk. The bootloader updates the CRC32 while the frame is received, one byte at a time at the start of the following padding bit. It acknowledges about 30µs after the end of the frame, at 8MHz and at 16MHz.

#### Flash write time vs acknowledgement timeout

A page takes about 9ms to erase and write, far longer than PJON's default response timeout. So the bootloader acknowledges the last chunk of a page *before* writing it. The page result is then confirmed with a ping: the ping is not answered while the page is being written (the sender's PJON retries it with its normal back-off), and it is answered only if the page was written and verified. The sender keeps its default settings and still gets end-to-end confirmation for every page.

#### Timing

The bootloader times bits by counting cycles, as the SoftwareBitBang direct port access implementation (`SWBB_PIN`) does, and uses the same values: it expects data bits 0.5us longer than specified and transmits its acknowledgement with bits 1us longer (modes 1-4). Senders using `digitalRead`/`digitalWrite` (the portable implementation and previous PJON versions) transmit bits ~0.2us longer than specified and receive best bits ~1us longer, `SWBB_PIN` senders transmit bits 1us longer: the bootloader's window is centred between them. Each padding bit is measured from the end of the previous byte, so the time spent updating the CRC32 between bytes (~12us at 8MHz) is not lost. Measured windows (data bit duration accepted from a `SWBB_PIN` sender, 50 packets each, same at 8MHz):

| Mode (bit) | Previous version | This version | `SWBB_PIN` sender | Stock sender |
| ---------- | ---------------- | ------------ | ----------------- | ------------ |
| 1 (44us) | at least 42-46.5us | 42.5-46.5us | 45us | ~44.2us |
| 3 (28us) | 27-29.5us | 27.5-30.5us | 29us | ~28.2us |
| 4 (26us) | 25-27.5us | 25.5-28us | 27us | ~26.3us |

#### Fast modes 5 and 6

Modes 5 and 6 are not part of PJDL v5.0: they are supported only by this bootloader and by the SoftwareBitBang direct port access implementation (`#define SWBB_PIN`), every device of the bus must use them. See [REVIEW-swbb-fast-modes.md](/REVIEW-swbb-fast-modes.md) for the measurements.

| Mode | Data bit | Padding bit | Acceptance | Byte | Speed |
| ---- | -------- | ----------- | ---------- | ---- | ----- |
| 4 | 26us | 65us | 30us | 299us | 3.34kB/s |
| 5 | 12us | 50us | 16us | 158us | 6.3kB/s |
| 6 | 8us | 50us | 12us | 122us | 8.2kB/s |

The range of the bus in modes 5 and 6 was not measured: the simulation does not model the cable (capacitance, reflections, noise). Test them on the real bus with an oscilloscope before using them.

#### Throughput

Measured in simulation at 16MHz, mode 1: about 0.9kB/s. A full 30kB application flashes in about 33s, and a 4kB application in about 5s. Modes 3 and 4 are about 30% faster. Sending 200 chunks of 32 bytes (one attempt each, excluding the page write waits) takes 3.2s in mode 4, 1.76s in mode 5 and 1.41s in mode 6.

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

**Protocol.** PJON local mode packets on port 45063 (0xB007), with an acknowledgement requested. **Every packet must use CRC32.** PJON switches to CRC32 automatically only for packets longer than 15 bytes, so senders must force it (`bus.set_crc_32(true)`, at least for the bootloader port) or the short commands are dropped.

| Command | Payload | Sent to | Acknowledged when |
| ------- | ------- | ------- | ----------------- |
| Enter | `'B'` | application | received; the application then restarts into the bootloader |
| Write chunk | `'W'` address (2 bytes, MSB first) data (`CHUNK_SIZE` bytes) | bootloader | the address is valid. After the last chunk of a page, the page is erased, written and verified |
| Ping | `'P'` | bootloader | the last page was written and verified (or no page was written yet). Not answered while a page is being written |
| Go | `'G'` | bootloader | the last page is correct; the application is then started |

Chunk rules:
- The address is the byte address of the chunk, aligned to `CHUNK_SIZE` (32 by default; 16, 64 and 128 are also possible).
- The chunks of a page must be sent in order, starting from its first chunk.
- Repeating the chunk just sent is allowed (an acknowledgement got lost).
- A missing or out of order chunk makes the page fail: the ping after it is not acknowledged and the page must be sent again.

To write a page, send its chunks and then a ping. Chunks outside the application area (the bootloader section) or with unaligned addresses are refused (not acknowledged). Pages already holding the right content are not rewritten. The uploader erases page 0 first and writes it last. If an upload is interrupted, the reset vector is therefore erased, and the device stays in the bootloader instead of running a half written application.

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
| `MODE` | 1 | SoftwareBitBang mode 1–6, must match the bus (`SWBB_MODE`). Modes 5 and 6 are faster modes, every device of the bus must use the SoftwareBitBang direct port access (`SWBB_PIN`), see below |
| `TIMEOUT_MS` | 5000 | time in the bootloader after the application requested it |
| `EXTRF_TIMEOUT_MS` | 1000 | time in the bootloader after a reset pin reset, 0 = never |
| `F_CPU` | 16000000 | clock frequency |
| `CHUNK_SIZE` | 32 | bytes of flash per write packet: 16, 32, 64 or 128, same as the sender |
| `BOOT_SIZE` | 2048 | boot section size, 2048 or 4096 |
| `EESAVE` | 1 | 1 keeps the EEPROM content when the chip is erased |

The build fails if the bootloader does not fit in `BOOT_SIZE`, or if `main()` is not at the start of the boot section. Flash it with an ISP programmer; this erases the chip and sets the fuses:

```bash
make flash PJON_PIN=12 DEVICE_ID=44 ISP=usbasp
```

```bash
make flash ISP=arduino ISP_PORT=/dev/ttyACM0 AVRDUDE_FLAGS="-b 19200"
```

The second form uses an Arduino running the ArduinoISP sketch. `make fuses` prints the fuse values. The defaults are:
- high `0xD2`: 2kB boot section, `BOOTRST`, `SPIEN` and `EESAVE`. `EESAVE` keeps the EEPROM, e.g. a device id stored there, through the chip erase done by `make flash`. With `EESAVE=0` the value is `0xDA`.
- low `0xFF`: external crystal.
- extended `0xFD`: brown-out at 2.7V.

Override `LFUSE=` and `EFUSE=` if your board uses a different clock source.

**Flashing this bootloader replaces Optiboot** (the Arduino bootloader). Afterwards the board can no longer be programmed over the serial port from the Arduino IDE; use PJON (this bootloader) or an ISP programmer.

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

To be updatable over PJON, the application must:
- use the same bus pin and mode as the bootloader;
- define `PJON_INCLUDE_PORT`. Otherwise PJON drops every packet carrying a port, including the bootloader request;
- define `PJON_INCLUDE_PACKET_ID` if the sender (e.g. a router) adds packet ids, for the same reason;
- handle port 0xB007: call `pjon_boot_enter()` when `pjon_boot_is_request()` is true;
- call `bus.receive()` regularly.

See [AppWithBootloaderHook](/examples/ARDUINO/Local/SoftwareBitBang/Bootloader/AppWithBootloaderHook/AppWithBootloaderHook.ino):

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

The application must be smaller than 30720 bytes. The Arduino IDE assumes the 512 byte Optiboot and allows 32256, but `pjon_boot_upload.py` refuses images that would overlap the bootloader. To get a HEX file, use *Sketch > Export compiled binary* in the Arduino IDE (take the file without `with_bootloader`), or `arduino-cli compile --output-dir`.

If an application without the hook was flashed, the device can still be recovered: pressing reset opens the bootloader for `EXTRF_TIMEOUT_MS` using `DEVICE_ID`. Use `--no-enter` and start the upload just before pressing reset; the uploader retries for several seconds.

### Uploading firmware

1. Flash the [Uploader](/examples/ARDUINO/Local/SoftwareBitBang/Bootloader/Uploader/Uploader.ino) sketch on any Arduino (same pin and `SWBB_MODE` as the bus). It is a serial to PJON bridge using default PJON settings, with CRC32 forced.
2. Install pyserial (`pip install pyserial`) and run:

```bash
python3 tools/pjon_boot_upload.py --port /dev/ttyUSB0 --id 44 firmware.hex
```

```
firmware.hex: 4224 bytes, 33 pages
application restarted into the bootloader
bootloader found
written 33 pages in 4.7s
application started
```

Use `--no-enter` when the device is already in the bootloader (new chip or recovery). Use `--chunk-size` if the bootloader was built with another `CHUNK_SIZE`, and `--boot-start` if it was built with another `BOOT_SIZE` (`0x7000` for 4096).

### Sending firmware from other PJON devices

Any PJON device (or router) can flash a node by following the protocol above. Requirements for the sender:
- force CRC32 on the bootloader port;
- request acknowledgements;
- for each page: send the chunks in order, then a ping. A ping without acknowledgement (after PJON's own retries) means the page failed, so send its chunks again.

With the default chunk size the longest packet is 48 bytes, sender info and packet id included, so it fits the default `PJON_PACKET_MAX_LENGTH` of 50 and an I2C transfer of 64 bytes. No timing setting needs to change. Chunk sizes 64 and 128 need a larger `PJON_PACKET_MAX_LENGTH` (the Uploader sketch sets it automatically).

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
| `interrupted` | an upload stopped in the middle of a page leaves the device in the bootloader; a new upload completes |
| `pins` | pins 2 (PD2) and 17 (A3/PC3) |
| `modes` | SoftwareBitBang modes 2, 3 and 4 |
| `fast_modes` | modes 5 and 6 (uploader and application with `SWBB_PIN`), mode 5 with the bootloader at 8MHz |
| `8mhz` | bootloader built for and running at 8MHz |
| `large_image` | 30720 byte image filling the whole application area |
| `protection` | chunks in the bootloader section and unaligned chunks are refused |
| `router` | mode 3, pin 2, sender with `PJON_PACKET_MAX_LENGTH` 50, packet id, port, sender info and the default `SWBB_RESPONSE_OFFSET` |
| `chunks` | a page with a missing chunk, swapped chunks or without its first chunk fails; a repeated chunk is accepted |

`DIRECT_IO=1 ./run_tests.sh` builds the uploader and the application with `SWBB_PIN`, `ORIG_SRC=<path to a PJON src folder>` builds them with another PJON version (e.g. the one already on deployed devices), `BL_SOURCE=<path>` tests another bootloader source (e.g. the one already flashed). All tests pass with the portable implementation, with `SWBB_PIN`, with an uploader built from the previous PJON version, and the previous bootloader passes all tests (except `fast_modes`) with this library.

All tests pass. The simulator reports no bus contention while the target runs the bootloader; the few events it reports in some runs happen while the application (stock PJON library) exchanges the enter request with the uploader. Simulation covers the logic and the timing against the real PJON code. It does not cover the electrical side (cable length, pull-down value, noise) or clock deviations of real oscillators, so test on the real board before deploying.

### Limitations

- ATmega328P only (pin mapping, page size, fuses). The ATmega168/88 or ATmega328PB would need small changes.
- One mode, one pin, one clock frequency per build; local mode only; no shared mode bus id.
- Modes 5 and 6 are not PJDL v5.0 modes and their bus range is unknown (see above).
- The bootloader never transmits packets on its own, it only acknowledges. Verification is per page; there is no read-back of the whole image.
- EEPROM is not written.
- While a page is written (≈9ms) the bootloader does not listen to the bus. Other devices addressing the target in that moment are not acknowledged and retry.
