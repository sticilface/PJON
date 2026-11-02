## PJON SoftwareBitBang bootloader for ATmega328P

A 3kB bootloader that updates the firmware of an ATmega328P (ATmega328P-AU / TQFP, or DIP) over a single wire [SoftwareBitBang](/src/strategies/SoftwareBitBang/README.md) PJON bus, and also over the serial port like Optiboot (Arduino IDE upload, `avrdude -c arduino`). The bus pin and mode are chosen at build time.
- **Updates:** the running application can restart the device into the bootloader with a PJON packet. If nothing arrives, the bootloader times out and starts the application again.
- **Fresh install:** a new device, with no application and no device id, announces itself to the master. The master assigns it an id, saved in EEPROM, and flashes the application.
- **Boot guard:** after an update, a new application that does not confirm its start within a few seconds is reset by the watchdog. The bootloader then stays active and announces the failure, so the master can flash a working application.
- **Serial upload:** after a reset pin reset (the Arduino IDE resets the board through DTR) the bootloader also listens on the serial port for 1 second and accepts uploads with the Optiboot (STK500v1) protocol.

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
- [Fresh install (provisioning)](#fresh-install-provisioning)
- [Serial upload (Arduino IDE, avrdude)](#serial-upload-arduino-ide-avrdude)
- [Sending firmware from other PJON devices](#sending-firmware-from-other-pjon-devices)
- [Simulation tests](#simulation-tests)
- [Limitations](#limitations)

### Feasibility study

**Question:** can a bootloader receive firmware over PJON SoftwareBitBang (SWBB) and flash an ATmega328P within the limits of the boot section? **Answer: yes.** The flashing part alone fits in 2kB (about 1350 bytes). With provisioning (announcements, id assignment, serial number), the boot guard and Optiboot compatible serial upload it takes 2942–2968 bytes depending on the mode, laid out in 3kB (see below).

#### Constraints of the ATmega328P

| Resource | Limit | Used by this bootloader |
| -------- | ----- | ----------------------- |
| Boot section | 512, 1024, 2048 or 4096 bytes (`BOOTSZ` fuses) | 2kB boot section + 1kB below it: 2942–2968 bytes |
| Application space | 32768 − bootloader | 29696 bytes (Optiboot leaves 32256) |
| SRAM | 2048 bytes | 48 byte packet buffer, 128 byte page buffer, a few bytes of state and stack |
| EEPROM | 1024 bytes | 6 bytes at the end: boot guard flag, device id, serial |
| Flash page | 128 bytes, erase + write ≈ 9ms | assembled from 4 chunks of 32 bytes |
| Interrupt vectors | none needed | interrupts stay disabled, polling only |

Sizes measured with avr-gcc 7.3.0 at 16MHz:

| | pin 2 | pin 12 | pin 2, 8MHz |
| - | ----- | ------ | ----------- |
| mode 1 | 2960 (1964 + 996) | 2960 (1964 + 996) | 2952 (1956 + 996) |
| mode 2 | 2954 (1958 + 996) | 2954 (1958 + 996) | 2942 (1946 + 996) |
| mode 3 | 2950 (1954 + 996) | 2950 (1954 + 996) | 2942 (1946 + 996) |
| mode 4 | 2956 (1960 + 996) | 2956 (1960 + 996) | 2948 (1952 + 996) |
| mode 5 | 2952 (1956 + 996) | 2952 (1956 + 996) | 2944 (1948 + 996) |
| mode 6 | 2958 (1962 + 996) | 2958 (1962 + 996) | 2946 (1950 + 996) |
| mode 7 | 2968 (2024 + 944) | 2968 (2024 + 944) | 2962 (2022 + 940) |

Bytes in total (boot section + below it, of 2048 + 1024).

In mode 1 that is 1964 bytes in the boot section (of 2048) and 996 below it (of 1024). Without serial upload (`SERIAL_UPLOAD=0`) it is 2440 bytes.

The bootloader transmits announcements with the same timing as its acknowledgements (data bits 1us longer than specified in modes 1-4, mode 7 frames in mode 7), and after transmitting it drives the bus low for 2us before releasing it (also after each keep busy bit while it waits for the master's acknowledgement): with a long cable and a weak pull-down a released bus falls slowly and would be taken for a padding bit. The CRC32 is computed bit by bit by a 34 bytes assembly routine (~100 cycles per byte); `-DCRC32_TABLE=1` selects a 16 entries table instead (faster, ~40 bytes larger).

**3kB layout.** The ATmega328P boot section can only be 512, 1024, 2048 or 4096 bytes, so the default `BOOT_SIZE=3072` is a split layout:
- **Boot section (0x7800–0x7FFF, 2kB):** selected by the fuses. It holds the reset entry (`main()`) and all the flash writing code, because the `SPM` instruction only works when executed from the boot section.
- **Below it (0x7400–0x77FF, 1kB):** functions that do not write flash are placed here (`LOWTEXT` in the source: frame reception, CRC8, serial number, serial upload session).
- **Protection:** the application can not write flash (it has no access to `SPM`), and the bootloader refuses writes from 0x7400 up.
- **Checks:** the Makefile checks that each half fits, that `main()` is at the reset address, and that every `SPM` instruction is in the boot section.

`BOOT_SIZE=4096` uses a normal 4kB boot section instead: more headroom (about 1.1kB free) and 1kB less for the application (28672 bytes). An earlier version without chunks, provisioning or serial upload fitted in 1kB (1000 bytes).

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
| Sending packets, medium access | only for announcements | implemented for announcements to the master, with retries every 5–60s |

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

#### Mode 7 (not PJDL)

`MODE=7` is the frame based link layer of `src/strategies/SoftwareBitBang/SoftwareBitBangM7.h` (UART like bytes of 10 bits of 4us, a 96us preamble, a contention slot, interrupt windows of the transmitter every 4 bytes, a short acknowledgement). The bootloader receives the whole frame, then checks the CRC32 while the transmitter keeps the bus busy. It announces itself and receives the acknowledgement in mode 7 too (preamble, contention slot, keep busy pulses as the library). Its timing values (`M7_*`) must match the library's (`SWBB_M7_*`). See [REVIEW-swbb-mode7.md](/REVIEW-swbb-mode7.md).

#### Throughput

Measured in simulation at 16MHz, mode 1: about 0.9kB/s. A full 30kB application flashes in about 33s, and a 4kB application in about 5s. Modes 3 and 4 are about 30% faster. Sending 200 chunks of 32 bytes (one attempt each, excluding the page write waits) takes 3.2s in mode 4, 1.76s in mode 5 and 1.41s in mode 6.

### How it works

The `BOOTRST` fuse makes the bootloader run after every reset. At startup it reads the reset cause and decides what to do:

| Reset cause | Behaviour | Device id |
| ----------- | --------- | --------- |
| Watchdog reset requested by the application (mailbox in SRAM) | stays active for `TIMEOUT_MS` (5s), **announces itself once** so the sender knows the bootloader (not the application) is answering | the id passed by the application |
| Watchdog reset while the updated application is pending (boot guard) | stays active until a working application is written and started, **announces itself** | saved id |
| Reset pin (`EXTRF`) | stays active for `EXTRF_TIMEOUT_MS` (1s), 0 disables it; listens to PJON and to the serial port | saved id |
| No application in flash | stays active until a valid application is written and started, **announces itself** | saved id |
| Anything else (power on, brown out, plain watchdog reset) | starts the application immediately | |

"Saved id" is the id saved in EEPROM by the `'I'` command, or `DEVICE_ID` (default 255, not assigned) if there is none. Every valid command restarts the timeout. When it expires the application is started, or the bootloader keeps waiting if there is no application.

**Entering the bootloader from the application.** The application calls `pjon_boot_enter(id)` from [`PJON_Bootloader.h`](/src/utils/bootloader/PJON_Bootloader.h). It writes a 3 byte mailbox (magic + its device id) at the top of SRAM and triggers a 15ms watchdog reset. SRAM survives a watchdog reset, so the bootloader finds the mailbox, clears it and answers on the id the application was using.

**Announcements.** While it waits without timeout (no application, or failed boot), the bootloader sends `'b'` reason serial[4] version chunk_size to the master (id 254), from its saved id (255 if unassigned).
- **When:** the first announcement goes out within about 1s, then every 5s, doubling up to 60s, with random jitter seeded from the serial so devices that power up together do not collide.
- **Bus access:** it waits for the bus to be idle before sending and asks for an acknowledgement.
- **Postponed:** any valid command from the master postpones the next announcement by 60s, so announcements do not interfere with flashing.

**Boot guard.** The guard covers the first start after an update:
- **Marking:** when `'G'` follows a successful page write, the bootloader marks the application as pending (a flag in EEPROM) and starts it with the watchdog running (`BOOT_GUARD_S`, 8s by default).
- **Confirming:** the application calls `pjon_boot_app_ok()` once it works. That disables the watchdog and clears the flag: one EEPROM write per update, nothing on normal starts.
- **Failure:** if the application hangs or crashes before confirming, the watchdog resets the device, the bootloader sees the pending flag, stays active and announces reason 2.
- **Power cycle:** a power cycle while pending gives the application another supervised try.

The flag is in EEPROM because the application's stack overwrites the top of SRAM. Once confirmed, the application starts without the watchdog and may use it for its own purposes.

**Protocol.** PJON local mode packets on port 45063 (0xB007), with an acknowledgement requested. **Every packet must use CRC32.** PJON switches to CRC32 automatically only for packets longer than 15 bytes, so senders must force it (`bus.set_crc_32(true)`, at least for the bootloader port) or the short commands are dropped.

| Command | Payload | Sent to | Acknowledged when |
| ------- | ------- | ------- | ----------------- |
| Enter | `'B'` | application | received; the application then restarts into the bootloader |
| Write chunk | `'W'` address (2 bytes, MSB first) data (`CHUNK_SIZE` bytes) | bootloader | the address is valid. After the last chunk of a page, the page is erased, written and verified |
| Ping | `'P'` | bootloader | the last page was written and verified (or no page was written yet). Not answered while a page is being written |
| Go | `'G'` | bootloader | the last page is correct; the application is then started (pending, with boot guard, if pages were written) |
| Assign id | `'I'` serial (4 bytes) id | bootloader, usually at id 255 | the serial is the device's: the id (1–253) is saved in EEPROM and used from then on. Other unprovisioned devices ignore it, so several can wait on id 255 at once |
| Announce | `'b'` reason serial (4 bytes) version chunk size | sent by the bootloader to the master (254) | — (the master's PJON acknowledges). Reason 1: no application, 2: application did not start, 3: entered on request |

**Why the announcement on request.** The application's PJON acknowledges every packet addressed to it, bootloader commands included, before the receiver function runs. An acknowledgement alone therefore does not prove that the bootloader is answering. After `'B'` the sender should wait for the announcement (reason 3) before flashing; `pjon_boot_upload.py` does, and stops with an error if the application did not enter the bootloader.

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
make PJON_PIN=12
```

| Option | Default | Description |
| ------ | ------- | ----------- |
| `PJON_PIN` | 12 | Arduino pin number of the bus, 0–19 (A0 = 14) |
| `DEVICE_ID` | 255 | id used when none is passed by the application or saved in EEPROM (255 = not assigned) |
| `MODE` | 1 | SoftwareBitBang mode 1–7, must match the bus (`SWBB_MODE`). Modes 5 and 6 are faster modes, mode 7 is a different (not PJDL) link layer: every device of the bus must use the SoftwareBitBang direct port access (`SWBB_PIN`), see above |
| `TIMEOUT_MS` | 5000 | time in the bootloader after the application requested it |
| `EXTRF_TIMEOUT_MS` | 1000 | time in the bootloader after a reset pin reset, 0 = never |
| `F_CPU` | 16000000 | clock frequency |
| `CHUNK_SIZE` | 32 | bytes of flash per write packet: 16, 32, 64 or 128, same as the sender |
| `BOOT_SIZE` | 3072 | flash used by the bootloader: 3072 (split layout, 2kB boot section) or 4096 |
| `SERIAL_UPLOAD` | 1 | Optiboot compatible serial upload on pins 0/1 (the bus can not use them then) |
| `SERIAL_BAUD` | 115200 | serial upload baud rate |
| `EESAVE` | 1 | 1 keeps the EEPROM content when the chip is erased |
| `BOOT_GUARD_S` | 8 | seconds an updated application has to call `pjon_boot_app_ok()`: 0 (off), 1, 2, 4 or 8 |
| `EEPROM_ID_ADDR` | 1019 | EEPROM address of the device id; the guard flag is just before it and the 4 byte serial just after it, so 1018–1023 by default. Must match the application (`PJON_BOOT_EEPROM_ID_ADDR`) |
| `SERIAL` | random | serial written to EEPROM by `make flash`: `random`, 8 hex digits (first byte not `FF`), or `none` to leave the EEPROM alone |

The build fails if the bootloader does not fit in `BOOT_SIZE`, or if `main()` is not at the start of the boot section. Flash it with an ISP programmer; this erases the chip and sets the fuses:

```bash
make flash PJON_PIN=12 ISP=usbasp
```

```bash
make flash ISP=arduino ISP_PORT=/dev/ttyACM0 AVRDUDE_FLAGS="-b 19200"
```

The second form uses an Arduino running the ArduinoISP sketch.

**Serial.** `make flash` also writes the device serial: 4 random bytes (or `SERIAL=...`) at the serial's EEPROM address, and nothing else.
- **Existing id kept:** avrdude writes only the bytes present in the file, so a device id already in EEPROM survives. Check this on avrdude versions older than 7.0, or use `SERIAL=none` when re-flashing a provisioned device.
- **Fallback:** if a chip is flashed without a serial, the bootloader generates a random one itself on its first start, from the jitter between the watchdog oscillator and the CPU clock.

`make fuses` prints the fuse values. The defaults are:
- high `0xD2`: 2kB boot section (of the 3kB layout), `BOOTRST`, `SPIEN` and `EESAVE`. `EESAVE` keeps the EEPROM (device id, serial) through the chip erase done by `make flash`. With `EESAVE=0` the value is `0xDA`; with `BOOT_SIZE=4096` it is `0xD0`.
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
- use the device id assigned during provisioning: `bus.set_id(pjon_boot_saved_id(fallback))`;
- call `pjon_boot_app_ok()` once it works, e.g. at the end of `setup()`, on every start (it only writes the EEPROM after an update);
- leave EEPROM bytes 1018–1023 alone (or the range set with `EEPROM_ID_ADDR`);
- define `PJON_INCLUDE_PORT`. Otherwise PJON drops every packet carrying a port, including the bootloader request;
- define `PJON_INCLUDE_PACKET_ID` if the sender (e.g. a router) adds packet ids, for the same reason;
- handle port 0xB007: call `pjon_boot_enter()` when `pjon_boot_is_request()` is true;
- call `bus.receive()` regularly.

See [AppWithBootloaderHook](/examples/ARDUINO/Local/SoftwareBitBang/Bootloader/AppWithBootloaderHook/AppWithBootloaderHook.ino):

```cpp
#define PJON_INCLUDE_PORT
#include <utils/bootloader/PJON_Bootloader.h>
#include <PJONSoftwareBitBang.h>

PJONSoftwareBitBang bus;

void receiver_function(uint8_t *payload, uint16_t length, const PJON_Packet_Info &info) {
  if(pjon_boot_is_request(payload, length, info.port))
    pjon_boot_enter(bus.device_id()); // does not return
  // ... the application's own packets
};

void setup() {
  bus.set_id(pjon_boot_saved_id(44));
  bus.strategy.set_pin(12);
  bus.set_receiver(receiver_function);
  bus.begin();
  pjon_boot_app_ok(); // confirms the start after an update
};
```

The example also answers `'Y'` (identify) on the bootloader port by blinking its LED quickly for 3 seconds.

The application must be smaller than 29696 bytes. The Arduino IDE assumes the 512 byte Optiboot and allows 32256, but `pjon_boot_upload.py` refuses images that would overlap the bootloader. To get a HEX file, use *Sketch > Export compiled binary* in the Arduino IDE (take the file without `with_bootloader`), or `arduino-cli compile --output-dir`.

If an application without the hook was flashed, the device can still be recovered: pressing reset opens the bootloader for `EXTRF_TIMEOUT_MS` using the saved id. Use `--no-enter` and start the upload just before pressing reset; the uploader retries for several seconds.

### Uploading firmware

1. Flash the [Uploader](/examples/ARDUINO/Local/SoftwareBitBang/Bootloader/Uploader/Uploader.ino) sketch on any Arduino (same pin and `SWBB_MODE` as the bus). It is a serial to PJON bridge using default PJON settings, with CRC32 forced. It uses id 254 and forwards the announcements it receives to the PC.
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

After `'B'` the tool waits for the bootloader's announcement, so it never "flashes" an application that did not enter the bootloader. Use `--no-enter` when the device is already in the bootloader (recovery, failed boot): the tool then trusts the ping, which a running application would also acknowledge. Use `--chunk-size` if the bootloader was built with another `CHUNK_SIZE`, and `--boot-start` if it was built with another `BOOT_SIZE` (default `0x7400`, `0x7000` for 4096).

### Fresh install (provisioning)

1. Flash the bootloader with `make flash` (fuses, bootloader and a random serial).
2. Connect the device to the bus. With no application, the bootloader announces itself from id 255 with its serial.
3. Assign an id and flash the application in one go:

```bash
python3 tools/pjon_boot_upload.py --port /dev/ttyUSB0 --provision 45 firmware.hex
```

```
waiting for a device without id...
device without id serial 40e0e02c: no application (bootloader v2, chunk size 32)
id 45 assigned to serial 40e0e02c
bootloader found
written 35 pages in 4.8s
application started
```

`--provision` takes the first device without id that announces itself; use `--serial` to pick a specific one. `--listen` prints every announcement, including devices whose updated application did not start (reason 2, sent from their id):

```
device id 44 serial 0a0b0c0d: application did not start (boot guard) (bootloader v2, chunk size 32)
```

For a failed boot, flash a working application with `--id 44 --no-enter`.

A server (e.g. an ESP32 behind a router) does the same: on an announcement from id 255 it picks a free id, sends `'I'` serial id to id 255, then flashes the application on the new id. On an announcement with reason 2 from a known id, it flashes that device again.

### Serial upload (Arduino IDE, avrdude)

With `SERIAL_UPLOAD=1` (default) the bootloader speaks the Optiboot protocol (STK500v1) on the serial port (pins 0/1, 115200 baud):
- **Entering:** after a reset pin reset it listens on the serial port during `EXTRF_TIMEOUT_MS` (1s). The Arduino IDE and `avrdude -c arduino` reset the board through DTR before uploading, so the board needs the usual auto-reset circuit (DTR → 100nF → RESET), or press reset just before uploading. It also listens whenever it waits on PJON (no application, failed boot, request).
- **Commands:** signature, page write, read back for verification, start. Pages are written and verified like PJON uploads, and writes to the bootloader are refused, so a sketch that is too large fails cleanly.
- **Not supported:** EEPROM is not written over serial.
- **Boot guard:** serial uploads do not arm it, since Arduino sketches normally do not call `pjon_boot_app_ok()`. A pending mark left by a PJON update is cleared.
- **Reset cause:** like Optiboot, the reset flags are passed to the application in register r2 (also in `GPIOR0`).

```bash
avrdude -c arduino -p m328p -P /dev/ttyUSB0 -b 115200 -D -U flash:w:firmware.hex:i
```

In the Arduino IDE, choose a board using Optiboot at 115200 baud (e.g. Arduino Uno), but note that the IDE assumes 32256 bytes are available while this bootloader leaves 29696. To get a warning in the IDE, add a board to `boards.txt` (or a `boards.local.txt`) copying the Uno with `upload.maximum_size=29696`. Do not use *Burn Bootloader* in the IDE: it would replace this bootloader with Optiboot.

### Sending firmware from other PJON devices

Any PJON device (or router) can flash a node by following the protocol above. Requirements for the sender:
- force CRC32 on the bootloader port;
- request acknowledgements;
- for each page: send the chunks in order, then a ping. A ping without acknowledgement (after PJON's own retries) means the page failed, so send its chunks again;
- to receive announcements: use id 254 (`PJON_BOOT_MASTER_ID`), define `PJON_INCLUDE_PORT` and handle port 0xB007. The announcement is 19 bytes long with CRC32, sender info and port.

With the default chunk size the longest packet is 48 bytes, sender info and packet id included, so it fits the default `PJON_PACKET_MAX_LENGTH` of 50 and an I2C transfer of 64 bytes. No timing setting needs to change. Chunk sizes 64 and 128 need a larger `PJON_PACKET_MAX_LENGTH` (the Uploader sketch sets it automatically).

### Simulation tests

[`test/run_tests.sh`](test/run_tests.sh) runs end to end tests with simulated ATmega328P ([simavr](https://github.com/buserror/simavr)) sharing one bus wire:
- one or two targets run the bootloader and the example application, built with the real PJON library; the first target's serial port can be exposed as a pty for avrdude;
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
| `mode7` | mode 7 (uploader and application with `SWBB_PIN`, then with `SWBB_INTERRUPT_WAKE`), mode 7 with the bootloader at 8MHz |
| `8mhz` | bootloader built for and running at 8MHz |
| `large_image` | 29696 byte image filling the whole application area |
| `protection` | chunks in the bootloader section and unaligned chunks are refused |
| `router` | mode 3, pin 2, sender with `PJON_PACKET_MAX_LENGTH` 50, packet id, port, sender info and the default `SWBB_RESPONSE_OFFSET` |
| `chunks` | a page with a missing chunk, swapped chunks or without its first chunk fails; a repeated chunk is accepted |
| `provision` | a fresh device (erased EEPROM, serial generated by the bootloader) announces itself, gets id 45 and app v1; app v1 uses id 45 and is updated to app v2 through the hook |
| `provision_two` | two fresh devices on the bus at once: an `'I'` with a wrong serial is refused, they get ids 45 and 46 and are flashed one after the other |
| `boot_guard` | an update with an application that never confirms its start: reset by the watchdog after 8s, the bootloader announces the failure, a working application is flashed and runs without further resets |
| `serial_fresh` | fresh chip: app v1 uploaded over the serial port with avrdude (Optiboot protocol, written and verified), then updated to app v2 over PJON through the hook |
| `serial_reset` | app v1 running, reset pin (as the Arduino IDE does with DTR), avrdude uploads an application that never calls `pjon_boot_app_ok()`: it keeps running, serial uploads do not arm the boot guard |

`DIRECT_IO=1 ./run_tests.sh` builds the uploader and the application with `SWBB_PIN`, `ORIG_SRC=<path to a PJON src folder>` builds them with another PJON version (e.g. the one already on deployed devices), `BL_SOURCE=<path>` tests another bootloader source (e.g. the one already flashed). All tests pass with the portable implementation, with `SWBB_PIN`, with an uploader built from the previous PJON version, and the previous bootloader passes all tests (except `fast_modes`) with this library.

All tests pass. The simulator reports no bus contention while the target runs the bootloader; the few events it reports in some runs happen while the application (stock PJON library) exchanges the enter request with the uploader. Simulation covers the logic and the timing against the real PJON code. It does not cover the electrical side (cable length, pull-down value, noise) or clock deviations of real oscillators, so test on the real board before deploying.

### Limitations

- ATmega328P only (pin mapping, page size, fuses). The ATmega168/88 or ATmega328PB would need small changes.
- One mode, one pin, one clock frequency per build; local mode only; no shared mode bus id.
- Modes 5 and 6 are not PJDL v5.0 modes and their bus range is unknown (see above).
- The bootloader transmits only announcements and acknowledgements. Verification is per page; there is no read-back of the whole image.
- Application EEPROM data is not written by the bootloader; it only uses its 6 bytes.
- The boot guard covers the first start after an update. Crashes after `pjon_boot_app_ok()` are the application's business (e.g. its own watchdog). An application that hangs without watchdog can only be recovered with a reset or power cycle.
- While a page is written (≈9ms) the bootloader does not listen to the bus. Other devices addressing the target in that moment are not acknowledged and retry.
