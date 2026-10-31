#!/usr/bin/env python3
"""Upload firmware to a device running the PJON SoftwareBitBang bootloader.

The PC talks to an Arduino running the Uploader sketch
(examples/ARDUINO/Local/SoftwareBitBang/Bootloader/Uploader), which sends the
firmware over the PJON bus to the target.

    python3 pjon_boot_upload.py --port /dev/ttyUSB0 --id 44 firmware.hex

Sequence:
 1. ask the running application to restart into the bootloader ('B')
 2. ping the bootloader until it answers ('P')
 3. erase page 0 first, so an interrupted upload leaves a device that stays
    in the bootloader instead of running a half written application
 4. write every other page, then page 0 last ('W', verified by the target)
 5. start the new application ('G')

Requires pyserial (pip install pyserial).
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

PAGE_SIZE = 128
CMD_ENTER = b"B"
CMD_PING = b"P"
CMD_WRITE = b"W"
CMD_GO = b"G"


def parse_hex(path):
    """Returns {address: byte} from an Intel HEX file."""
    data = {}
    base = 0
    with open(path) as f:
        for line_number, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith(":"):
                raise ValueError("line %d: not an Intel HEX record" % line_number)
            raw = bytes.fromhex(line[1:])
            if len(raw) < 5 or len(raw) != raw[0] + 5:
                raise ValueError("line %d: bad record length" % line_number)
            if sum(raw) & 0xFF:
                raise ValueError("line %d: bad checksum" % line_number)
            count, address, kind = raw[0], (raw[1] << 8) | raw[2], raw[3]
            payload = raw[4:4 + count]
            if kind == 0x00:
                for i, b in enumerate(payload):
                    data[base + address + i] = b
            elif kind == 0x01:
                break
            elif kind == 0x02:
                base = ((payload[0] << 8) | payload[1]) << 4
            elif kind == 0x04:
                base = ((payload[0] << 8) | payload[1]) << 16
            # 0x03 and 0x05 (start address) are not needed
    return data


def build_pages(data, boot_start):
    if not data:
        raise ValueError("the HEX file is empty")
    end = max(data) + 1
    if end > boot_start:
        raise ValueError(
            "the image ends at 0x%04X, it overlaps the bootloader at 0x%04X"
            % (end, boot_start))
    pages = []
    for address in range(0, end, PAGE_SIZE):
        page = bytes(data.get(address + i, 0xFF) for i in range(PAGE_SIZE))
        pages.append((address, page))
    return pages


class Uploader:
    def __init__(self, port, baud, reply_timeout):
        self.serial = serial.Serial(port, baud, timeout=reply_timeout)

    def handshake(self, attempts=20):
        # Opening the port resets most Arduino boards
        time.sleep(2)
        self.serial.reset_input_buffer()
        for _ in range(attempts):
            self.serial.write(bytes([0, 0]))
            reply = self.serial.read(1)
            if reply == b"R":
                return
            time.sleep(0.3)  # let the uploader discard a partial request
            self.serial.reset_input_buffer()
        raise RuntimeError("the Uploader sketch does not answer")

    def send(self, device_id, payload):
        self.serial.write(bytes([device_id, len(payload)]) + payload)
        reply = self.serial.read(1)
        if reply not in (b"K", b"E"):
            raise RuntimeError("no reply from the Uploader sketch")
        return reply == b"K"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("hex", help="firmware in Intel HEX format")
    parser.add_argument("--port", required=True, help="serial port of the Uploader")
    parser.add_argument("--id", type=int, required=True, help="target device id")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--boot-start", type=lambda v: int(v, 0), default=0x7C00,
                        help="bootloader start address (default 0x7C00)")
    parser.add_argument("--no-enter", action="store_true",
                        help="do not ask the application to enter the bootloader")
    parser.add_argument("--ping-attempts", type=int, default=5)
    parser.add_argument("--write-attempts", type=int, default=3)
    parser.add_argument("--reply-timeout", type=float, default=10.0,
                        help="seconds to wait for each Uploader reply")
    args = parser.parse_args()

    if not 1 <= args.id <= 253:
        sys.exit("--id must be between 1 and 253")

    pages = build_pages(parse_hex(args.hex), args.boot_start)
    print("%s: %d bytes, %d pages" % (args.hex, len(pages) * PAGE_SIZE, len(pages)))

    up = Uploader(args.port, args.baud, args.reply_timeout)
    up.handshake()

    if not args.no_enter:
        if up.send(args.id, CMD_ENTER):
            print("application restarted into the bootloader")
        else:
            print("the application did not answer, trying the bootloader anyway")

    for _ in range(args.ping_attempts):
        if up.send(args.id, CMD_PING):
            break
    else:
        sys.exit("the bootloader does not answer")
    print("bootloader found")

    def write(address, page):
        for _ in range(args.write_attempts):
            if up.send(args.id, CMD_WRITE + bytes([address >> 8, address & 0xFF]) + page):
                return
        sys.exit("\nwriting page 0x%04X failed" % address)

    write(0, b"\xFF" * PAGE_SIZE)  # invalidate the application first
    order = pages[1:] + pages[:1]  # page 0 (reset vector) last
    start = time.time()
    for n, (address, page) in enumerate(order, 1):
        write(address, page)
        sys.stdout.write("\rwriting %d/%d" % (n, len(order)))
        sys.stdout.flush()
    print("\rwritten %d pages in %.1fs" % (len(order), time.time() - start))

    if not up.send(args.id, CMD_GO):
        sys.exit("the bootloader did not acknowledge the start command")
    print("application started")


if __name__ == "__main__":
    main()
