## Review: using this bootloader behind the Melvan I2C→PJON router

Target setup: ESP32 server → I2C → ATmega328P router (PJON id 254, SWBB **mode 3**, pin 2) → SWBB bus → ATmega328P nodes (bus pin 2). The router is stock PJON 13.x (only an `SWBB_RESPONSE_PULSE` tweak), built with `PJON_PACKET_MAX_LENGTH=50`, packet id + port on, router mode, I2C buffer 64 bytes, 2 KB SRAM. Checked: the bootloader builds at 996/1024 bytes with `PJON_PIN=2 MODE=3`.

### Problems with the current protocol on this path
1. **142-byte write packets don't fit.** The router refuses payload + overhead ≥ 50 (overhead ≈ 13: id, header, length, CRC8, sender id, packet id ×2, port ×2, CRC32 ×4). Raising the router's buffers to about 160 doesn't fit its 2 KB SRAM, and the I2C transfers are limited to 64 bytes.
2. **The ACK comes after the 9 ms page write.** That needs `SWBB_RESPONSE_OFFSET=150` on the sender. On the router that setting applies to every send, so a missing node would block it much longer. We don't want that.
3. **The bootloader accepts only CRC32.** PJON switches to CRC32 automatically only for packets over 15 bytes, so the 1-byte `P` and `G` commands from a normal sender go out with CRC8 and are dropped. (We will make the router force CRC32 on port 0xB007. Note it in the README.)

### Requested changes
**A. Chunked page writes**
- `'W' addrH addrL data[PJON_BOOT_CHUNK_SIZE]`, with a default chunk size of **32** (payload 35 bytes, which fits a 50-byte packet with packet id + port + sender info + CRC32).
- `addr` is the byte address of the chunk and must be aligned to the chunk size.
- The bootloader copies each chunk into its 128-byte SRAM page buffer at `addr & (SPM_PAGESIZE-1)`.
- When the last chunk of a page arrives (`(addr & (SPM_PAGESIZE-1)) == SPM_PAGESIZE - CHUNK`), it does the erase / write / verify. Require all chunks of the page to have arrived first, for example with a bitmask of received chunks for the current page address. On a page mismatch or a missing chunk, the page fails.
- Make `PJON_BOOT_CHUNK_SIZE` configurable at build time (16 / 32 / 64 / 128). 128 keeps the current behaviour.

**B. Don't hold the ACK across the flash write**
- Acknowledge the last chunk as soon as it is received and checked, then write and verify the page, then store the result (`last_page_ok`).
- `P` (ping) is acknowledged only when no write is pending and `last_page_ok` is set (or no page has been written yet). While the bootloader is writing it doesn't listen, so the sender sees no ACK and retries with normal PJON attempts and back-off.
- The uploader sends a `P` after each page and treats a missing ACK (after its retries) as a failed page, then resends the page's chunks.
- Result: the sender can keep the default `SWBB_RESPONSE_OFFSET` and still gets confirmation that each page is in flash. Drop `PJON_BOOT_UPLOADER_RESPONSE_OFFSET` / `PACKET_MAX_LENGTH`, or keep them only for chunk 128.
- `G` should also be acknowledged only after the pending write has finished.

**C. Size**
- A 2 KB bootloader is acceptable: make `BOOT_SIZE=2048` the default (high fuse `0xDA`, `BOOT_START` 0x7800, app ≤ 30720 bytes). Don't spend effort squeezing into 1 KB, and keep the reset-pin entry and the skip for pages that already match. Update `pjon_boot_upload.py` (default boot start / max image size), the Makefile and the README. Report the size for MODE 1–4, pins 2 and 12, 16 MHz.
- Consider defaulting the high fuse to keep EEPROM (EESAVE: `0xD2` for the 2 KB section). Our nodes keep their device id in EEPROM, and `make flash` does a chip erase.

**D. Tools and tests**
- Update `pjon_boot_upload.py`, `Uploader.ino` and the simavr tests (`run_tests.sh`) for chunked writes + ping-after-page.
- Add simulation tests:
  - mode 3, pin 2, sender with `PJON_PACKET_MAX_LENGTH=50`, packet id + port included, and *default* `SWBB_RESPONSE_OFFSET`
  - a missing or out-of-order chunk is detected
  - an interrupted page leaves the device in the bootloader
- Keep the behaviour where page 0 is erased first and written last.

**E. README**
- Document the chunked protocol, the CRC32 requirement for every packet, and that applications must handle port 0xB007 (`pjon_boot_is_request`).
- Document that flashing the bootloader removes Optiboot, so serial uploads stop working on that board.
