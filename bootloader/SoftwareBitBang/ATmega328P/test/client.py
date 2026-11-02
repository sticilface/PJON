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
    client.py large HEX OUT_HEX [END]  HEX followed by pseudo random data up to the bootloader
                                       (or up to END)
    client.py far HEX OUT_HEX          HEX plus pseudo random pages above 64kB, above 128kB
                                       and right below the bootloader (sparse image)

The target MCU is taken from PJON_BOOT_MCU (atmega328p by default, or
atmega2560): page size, bootloader start and protected addresses.
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

MCU = os.environ.get("PJON_BOOT_MCU", "atmega328p")
BOOT_START = up.MCUS[MCU]["boot_start"]
PAGE_SIZE = up.MCUS[MCU]["page_size"]
EEPROM_ID_ADDR = 1019
CHUNK = 32


def connect(port):
    uploader = up.Uploader(port, 115200, 60)
    uploader.handshake()
    return uploader


def chunk(uploader, device_id, address, data):
    return uploader.send(device_id, up.write_command(address, data))


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
    pages = up.build_pages(up.parse_hex(hex_path), BOOT_START, PAGE_SIZE)
    if not uploader.send(device_id, up.CMD_PING):
        sys.exit("ping: not acknowledged")
    if not page(uploader, device_id, 0, b"\xFF" * PAGE_SIZE):
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
    end = 0x8000 if MCU == "atmega328p" else 0x40000  # end of the flash
    checks = [  # chunk address, expected acknowledgement
        (BOOT_START - CHUNK, True),   # last chunk of the application area
        (BOOT_START, False),          # first chunk of the bootloader
        (end - CHUNK, False),         # last chunk of the bootloader
        (0x0010, False),              # not aligned to a chunk
    ]
    if MCU == "atmega2560":
        checks += [
            (0x1FFE0, True),          # far address ('X')
            (0x400000 - CHUNK, False),  # beyond the flash
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
    data = bytes(i & 0xFF for i in range(PAGE_SIZE))
    base = 0x1000 if MCU == "atmega328p" else 0x12000  # far page on the Mega
    n = PAGE_SIZE // CHUNK
    last = n - 1
    sequences = [  # chunk indexes sent, then the ping must be acknowledged?
        ("all chunks in order", list(range(n)), True),
        ("chunk 2 missing", [i for i in range(n) if i != 2], False),
        ("chunks 1 and 2 swapped", [0, 2, 1] + list(range(3, n)), False),
        ("first chunk missing", list(range(1, n)), False),
        ("chunk repeated (lost acknowledgement)",
         [0, 1, 1] + list(range(2, n)) + [last], True),
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


def write_hex(data, out_path):
    """Intel HEX with extended linear address records above 64kB"""
    with open(out_path, "w") as out:
        segment = 0
        for address in range(0, max(data) + 1, 16):
            if not any((address + i) in data for i in range(16)):
                continue
            if address >> 16 != segment:
                segment = address >> 16
                record = [2, 0, 0, 4, segment >> 8, segment & 0xFF]
                record.append(-sum(record) & 0xFF)
                out.write(":" + "".join("%02X" % b for b in record) + "\n")
            record = [16, (address >> 8) & 0xFF, address & 0xFF, 0] + \
                [data.get(address + i, 0xFF) for i in range(16)]
            record.append(-sum(record) & 0xFF)
            out.write(":" + "".join("%02X" % b for b in record) + "\n")
        out.write(":00000001FF\n")


def cmd_large(hex_path, out_path, end=None):
    end = int(end, 0) if end else BOOT_START
    data = up.parse_hex(hex_path)
    random.seed(1)
    for address in range(max(data) + 1, end):
        data[address] = random.randrange(256)
    write_hex(data, out_path)
    print("large image: %d bytes" % end)


def cmd_far(hex_path, out_path):
    data = up.parse_hex(hex_path)
    random.seed(2)
    starts = [0x10000 - PAGE_SIZE, 0x10000, 0x20000, BOOT_START - PAGE_SIZE]
    for start in starts:
        for address in range(start, start + PAGE_SIZE):
            data[address] = random.randrange(256)
    write_hex(data, out_path)
    print("far image: pages at %s" % ", ".join("0x%05X" % s for s in starts))


if __name__ == "__main__":
    commands = {
        "enter": cmd_enter, "partial": cmd_partial, "protect": cmd_protect,
        "chunks": cmd_chunks, "assign": cmd_assign, "eeprom": cmd_eeprom,
        "check": cmd_check, "large": cmd_large, "far": cmd_far,
    }
    if len(sys.argv) < 2 or sys.argv[1] not in commands:
        sys.exit(__doc__)
    commands[sys.argv[1]](*sys.argv[2:])
