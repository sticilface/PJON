#!/usr/bin/env python3
"""Helpers used by run_tests.sh (not needed to flash a device).

    client.py enter PORT ID            only ask the application to enter the bootloader
    client.py partial PORT ID HEX N    enter, invalidate page 0, write N pages and
                                       half of the next one, stop
    client.py protect PORT ID          bootloader already active: check that only
                                       valid chunk addresses are accepted
    client.py chunks PORT ID           bootloader already active: check that missing,
                                       out of order and repeated chunks are handled
    client.py assign PORT SERIAL ID EXPECTED
                                       assign ID to the unprovisioned device with
                                       SERIAL, EXPECTED 1 if it must be accepted
    client.py check HEX FLASH_BIN      compare a HEX file with a flash dump
    client.py eeprom EEPROM_BIN ID     check that the EEPROM dump holds device ID
    client.py large HEX OUT_HEX        HEX followed by pseudo random data up to the bootloader
"""

import importlib.util
import os
import random
import sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "pjon_boot_upload", os.path.join(here, "..", "tools", "pjon_boot_upload.py"))
up = importlib.util.module_from_spec(spec)
spec.loader.exec_module(up)

BOOT_START = 0x7000
EEPROM_ID_ADDR = 1019
CHUNK = 32


def connect(port):
    uploader = up.Uploader(port, 115200, 60)
    uploader.handshake()
    return uploader


def chunk(uploader, device_id, address, data):
    return uploader.send(
        device_id, up.CMD_WRITE + bytes([address >> 8, address & 0xFF]) + data)


def page(uploader, device_id, address, data):
    return up.write_page(uploader, device_id, address, data, CHUNK)


def enter(uploader, device_id):
    if not uploader.send(device_id, up.CMD_ENTER):
        sys.exit("enter: not acknowledged")
    print("enter: acknowledged")


def cmd_enter(port, device_id):
    enter(connect(port), int(device_id))


def cmd_partial(port, device_id, hex_path, count):
    device_id = int(device_id)
    uploader = connect(port)
    enter(uploader, device_id)
    pages = up.build_pages(up.parse_hex(hex_path), BOOT_START)
    if not uploader.send(device_id, up.CMD_PING):
        sys.exit("ping: not acknowledged")
    if not page(uploader, device_id, 0, b"\xFF" * up.PAGE_SIZE):
        sys.exit("invalidate page 0: not acknowledged")
    for address, data in pages[1:1 + int(count)]:
        if not page(uploader, device_id, address, data):
            sys.exit("write 0x%04X: not acknowledged" % address)
    address, data = pages[1 + int(count)]
    for offset in (0, CHUNK):  # half of the next page
        if not chunk(uploader, device_id, address + offset, data[offset:offset + CHUNK]):
            sys.exit("chunk 0x%04X: not acknowledged" % (address + offset))
    print("partial: stopped after %s pages and 2 chunks" % count)


def cmd_protect(port, device_id):
    device_id = int(device_id)
    uploader = connect(port)
    checks = [  # chunk address, expected acknowledgement
        (0x6FE0, True),    # last chunk of the application area
        (0x7000, False),   # first chunk of the bootloader
        (0x7FE0, False),   # last chunk of the bootloader
        (0x0010, False),   # not aligned to a chunk
    ]
    failed = 0
    for address, expected in checks:
        acked = chunk(uploader, device_id, address, b"\x00" * CHUNK)
        failed += acked != expected
        print("write 0x%04X: %s (%s)" % (
            address, "acknowledged" if acked else "refused",
            "ok" if acked == expected else "WRONG"))
    sys.exit(1 if failed else 0)


def cmd_chunks(port, device_id):
    device_id = int(device_id)
    uploader = connect(port)
    data = bytes(range(128))
    base = 0x1000
    sequences = [  # chunk indexes sent, then the ping must be acknowledged?
        ("all chunks in order", [0, 1, 2, 3], True),
        ("chunk 2 missing", [0, 1, 3], False),
        ("chunks 1 and 2 swapped", [0, 2, 1, 3], False),
        ("first chunk missing", [1, 2, 3], False),
        ("chunk repeated (lost acknowledgement)", [0, 1, 1, 2, 3, 3], True),
    ]
    failed = 0
    for name, indexes, expected in sequences:
        for i in indexes:
            if not chunk(uploader, device_id, base + i * CHUNK,
                         data[i * CHUNK:(i + 1) * CHUNK]):
                print("%s: chunk %d not acknowledged (WRONG)" % (name, i))
                failed += 1
        acked = uploader.send(device_id, up.CMD_PING)
        failed += acked != expected
        print("%s: page %s (%s)" % (
            name, "confirmed" if acked else "failed",
            "ok" if acked == expected else "WRONG"))
    sys.exit(1 if failed else 0)


def cmd_assign(port, serial_hex, device_id, expected):
    acked = up.assign(connect(port), serial_hex, int(device_id))
    ok = acked == (expected == "1")
    print("assign id %s to serial %s: %s (%s)" % (
        device_id, serial_hex, "accepted" if acked else "refused",
        "ok" if ok else "WRONG"))
    sys.exit(0 if ok else 1)


def cmd_eeprom(eeprom_path, device_id):
    data = open(eeprom_path, "rb").read()
    found = data[EEPROM_ID_ADDR]
    serial = data[EEPROM_ID_ADDR + 1:EEPROM_ID_ADDR + 5].hex()
    print("EEPROM: device id %d, serial %s" % (found, serial))
    sys.exit(0 if found == int(device_id) else 1)


def cmd_check(hex_path, flash_path):
    data = up.parse_hex(hex_path)
    flash = open(flash_path, "rb").read()
    bad = sum(1 for address, b in data.items() if flash[address] != b)
    print("flash check: %d bytes, %d different" % (len(data), bad))
    sys.exit(1 if bad else 0)


def cmd_large(hex_path, out_path):
    data = up.parse_hex(hex_path)
    random.seed(1)
    for address in range(max(data) + 1, BOOT_START):
        data[address] = random.randrange(256)
    with open(out_path, "w") as out:
        for address in range(0, BOOT_START, 16):
            record = [16, address >> 8, address & 0xFF, 0] + \
                [data.get(address + i, 0xFF) for i in range(16)]
            record.append(-sum(record) & 0xFF)
            out.write(":" + "".join("%02X" % b for b in record) + "\n")
        out.write(":00000001FF\n")
    print("large image: %d bytes" % BOOT_START)


if __name__ == "__main__":
    commands = {
        "enter": cmd_enter, "partial": cmd_partial, "protect": cmd_protect,
        "chunks": cmd_chunks, "assign": cmd_assign, "eeprom": cmd_eeprom,
        "check": cmd_check, "large": cmd_large,
    }
    if len(sys.argv) < 2 or sys.argv[1] not in commands:
        sys.exit(__doc__)
    commands[sys.argv[1]](*sys.argv[2:])
