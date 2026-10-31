#!/usr/bin/env python3
"""Helpers used by run_tests.sh (not needed to flash a device).

    client.py enter PORT ID            only ask the application to enter the bootloader
    client.py partial PORT ID HEX N    enter, invalidate page 0, write N pages, stop
    client.py protect PORT ID          bootloader already active: check that only
                                       valid page addresses are accepted
    client.py check HEX FLASH_BIN      compare a HEX file with a flash dump
    client.py large HEX OUT_HEX        HEX followed by pseudo random data up to 0x7BFF
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

BOOT_START = 0x7C00


def connect(port):
    uploader = up.Uploader(port, 115200, 60)
    uploader.handshake()
    return uploader


def write(uploader, device_id, address, page):
    return uploader.send(
        device_id, up.CMD_WRITE + bytes([address >> 8, address & 0xFF]) + page)


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
    if not write(uploader, device_id, 0, b"\xFF" * up.PAGE_SIZE):
        sys.exit("invalidate page 0: not acknowledged")
    for address, page in pages[1:1 + int(count)]:
        if not write(uploader, device_id, address, page):
            sys.exit("write 0x%04X: not acknowledged" % address)
    print("partial: stopped after %s pages" % count)


def cmd_protect(port, device_id):
    device_id = int(device_id)
    uploader = connect(port)
    checks = [  # address, expected acknowledgement
        (0x7B80, True),    # last application page
        (0x7C00, False),   # first bootloader page
        (0x7F80, False),   # last bootloader page
        (0x0010, False),   # not aligned to a page
    ]
    failed = 0
    for address, expected in checks:
        acked = write(uploader, device_id, address, b"\x00" * up.PAGE_SIZE)
        failed += acked != expected
        print("write 0x%04X: %s (%s)" % (
            address, "acknowledged" if acked else "refused",
            "ok" if acked == expected else "WRONG"))
    sys.exit(1 if failed else 0)


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
        "check": cmd_check, "large": cmd_large,
    }
    if len(sys.argv) < 2 or sys.argv[1] not in commands:
        sys.exit(__doc__)
    commands[sys.argv[1]](*sys.argv[2:])
