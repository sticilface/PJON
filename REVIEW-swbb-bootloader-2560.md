## PJON SoftwareBitBang bootloader for the ATmega2560 (Arduino Mega)

The bootloader in `bootloader/SoftwareBitBang/ATmega328P` now also builds for the
ATmega2560 (`make MCU=atmega2560`). It has the same features as the ATmega328P
version: PJON updates through the application hook, reset pin entry, timeout,
provisioning (announcements, id assignment, serial number in EEPROM), boot
guard, modes 1-7, and serial upload from the Arduino IDE.

### Design

One source for both MCUs. With the default options the ATmega328P build is
unchanged apart from a few bytes (2942-2968 bytes, 3kB split layout).

| | ATmega328P | ATmega2560 |
|---|---|---|
| Bus pin | Arduino 0-19 | Mega pins of ports A-G, default 3 (PE5); same table as `SWBB_PIN` (`SoftwareBitBangAVRPins.h`). Pin 2 (PE4) is not touched |
| Flash page | 128 bytes, 4 chunks of 32 | 256 bytes, 8 chunks of 32 |
| PJON write command | `W` (16 bits address) | `W` below 64kB, `X` (24 bits address) above |
| Flash reads | LPM | ELPM (`pgm_read_byte_far`) |
| Layout | 3kB: 2kB boot section at 0x7800 + 1kB at 0x7400 | 4kB boot section at 0x3F000 (`BOOT_SIZE=8192`: 0x3E000) |
| Fuses | high 0xD2, low 0xFF, ext 0xFD | high 0xD2 (0xDA without EESAVE; 0xD0/0xD8 for 8kB), low 0xFF, ext 0xFD |
| Serial upload | Optiboot, STK500v1 (`avrdude -c arduino`) | Arduino Mega bootloader, STK500v2 (`avrdude -c wiring`) |
| Size | 2942-2968 bytes | 3756-3768 bytes |

The serial upload uses the protocol the Arduino IDE already uses for each
board, so on a Mega the board selection stays *Arduino Mega or Mega 2560*.

### Protocol change (backward compatible)

`X addrE addrH addrL data[CHUNK]` is the same as `W` with a 24 bits address.
The ATmega2560 bootloader accepts `W` (addresses below 64kB) and `X`. With 32
bytes chunks a packet with packet id and sender info is 49 bytes, so it still
fits `PJON_PACKET_MAX_LENGTH` 50. The bootloader does not report its page size:
the uploader must be told the target (`pjon_boot_upload.py --mcu atmega2560`,
which also sets the default bootloader start 0x3F000). `--sparse` writes only
the pages that contain data of the image.

### STK500v2 subset

The subset avrdude 7 uses with `-c wiring`:
- SIGN_ON ("AVRISP_2"), GET/SET_PARAMETER, ENTER/LEAVE_PROGMODE.
- LOAD_ADDRESS, including the extended address bit used above 128kB.
- PROGRAM_FLASH and READ_FLASH with 256 bytes pages. Pages are written and verified, and writes to the bootloader are refused.
- READ_SIGNATURE, READ_FUSE, READ_LOCK and SPI_MULTI.
- READ_EEPROM.
- CHIP_ERASE and PROGRAM_EEPROM are acknowledged without effect, as in the 328P version.

As on the 328P, a serial upload clears the boot guard mark: Arduino sketches do
not call `pjon_boot_app_ok()`.

### Traps found while porting

1. **Sign extension of `int` (16 bits on AVR).** `(payload[1] << 8) |
   payload[2]` is negative for addresses 0x8000-0xFFFF. Converted to the 32 bits
   address of the ATmega2560, it became 0xFFFF8xxx, so every chunk between 32kB
   and 64kB was refused. The fix is explicit `uint16_t` casts.
2. **Indirect jumps above 128kB.** `switch` jump tables use EIJMP with EIND,
   which nothing initialises with `-nostartfiles`. EIND is 0, so the jump lands
   in the lower 128kB. The fix is to build with `-fno-jump-tables` (Makefile).
3. **main() must not use the stack before reading the mailbox.** On the 2560
   build there is no `.bootlow` section, so the functions marked `LOWTEXT` were
   inlined into `main()`. `main()` then needed a stack frame, which the size
   check of the Makefile reports. The fix is that `LOWTEXT` always means
   `noinline`.
4. **CRC32 from flash above 64kB.** A table CRC would need ELPM reads, which
   are too slow for the mode 7 acknowledgement at 8MHz. The merged bootloader
   uses the bitwise assembly CRC32 by default (no table); `CRC32_TABLE=1` is
   refused for the 2560.

### Simulation results

`test/run_tests.sh` runs the ATmega2560 tests with an ATmega2560 target on Mega
pin 3 and an ATmega328P uploader on pin 2:

| Test | What it checks |
|---|---|
| `mega_hook_upload` | modes 1 and 3 |
| `mega_timeout` | |
| `mega_fresh_chip` | |
| `mega_interrupted` | |
| `mega_protection` | bootloader section, unaligned and beyond-flash addresses refused; far address accepted |
| `mega_chunks` | an 8-chunk page above 64kB |
| `mega_modes` | modes 6 and 7 |
| `mega_wake` | application with `SWBB_INTERRUPT_WAKE` on INT5 |
| `mega_large` | an 80kB image; a sparse image with pages at 0xFF00, 0x10000, 0x20000 and 0x3EF00 |
| `mega_provision` | a fresh device gets id 45 and app v1, then is updated to app v2 through the hook |
| `mega_serial` | `avrdude -c wiring` writes and verifies a 136kB image (above 128kB), then app v2 over PJON |

RESULTS_PLACEHOLDER
