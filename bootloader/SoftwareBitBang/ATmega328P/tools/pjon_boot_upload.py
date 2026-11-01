#!/usr/bin/env python3
"""Upload firmware to a device running the PJON SoftwareBitBang bootloader.

The PC talks to an Arduino running the Uploader sketch
(examples/ARDUINO/Local/SoftwareBitBang/Bootloader/Uploader), which sends the
firmware over the PJON bus to the target.

    python3 pjon_boot_upload.py --port /dev/ttyUSB0 --id 44 firmware.hex
    python3 pjon_boot_upload.py --port /dev/ttyUSB0 --provision 45 firmware.hex
    python3 pjon_boot_upload.py --port /dev/ttyUSB0 --listen

--id: update a device. Sequence:
 1. ask the running application to restart into the bootloader ('B')
 2. ping the bootloader until it answers ('P')
 3. erase page 0 first, so an interrupted upload leaves a device that stays
    in the bootloader instead of running a half written application
 4. write every other page, then page 0 last. A page is sent as chunks
    ('W', --chunk-size bytes each, must match the bootloader build) followed
    by a ping, acknowledged only once the page is written and verified; if
    it is not, the page is sent again
 5. start the new application ('G')

--provision ID: fresh install. Waits for the announcement of a device that
has no id yet (or the one selected with --serial), assigns ID to it ('I',
saved in its EEPROM) and flashes the firmware on ID.

--listen: prints the announcements of devices waiting in the bootloader.

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
CMD_ASSIGN = b"I"
CMD_ANNOUNCE = ord("b")
UNASSIGNED = 255
REASONS = {1: "no application", 2: "application did not start (boot guard)"}


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


def build_pages(data, boot_start=0x7000):
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


def write_page(up, device_id, address, page, chunk_size):
    """Sends the chunks of a page, then a ping that is acknowledged only if
    the page was written and verified. Returns True if it was."""
    for offset in range(0, PAGE_SIZE, chunk_size):
        chunk_address = address + offset
        command = CMD_WRITE + bytes([chunk_address >> 8, chunk_address & 0xFF])
        if not up.send(device_id, command + page[offset:offset + chunk_size]):
            return False
    return up.send(device_id, CMD_PING)


class Announcement:
    def __init__(self, sender, payload):
        self.sender = sender
        self.reason = payload[1]
        self.serial = payload[2:6].hex()
        self.version = payload[6]
        self.chunk_size = payload[7]

    def __str__(self):
        return "device %s serial %s: %s (bootloader v%d, chunk size %d)" % (
            "without id" if self.sender == UNASSIGNED else "id %d" % self.sender,
            self.serial, REASONS.get(self.reason, "reason %d" % self.reason),
            self.version, self.chunk_size)


class Uploader:
    """Talks to the Uploader sketch. Packets received by the Uploader on the
    bootloader port ('#' frames) can arrive at any time, they are queued."""

    def __init__(self, port, baud, reply_timeout):
        self.serial = serial.Serial(port, baud, timeout=reply_timeout)
        self.events = []

    def _read(self, count):
        data = self.serial.read(count)
        if len(data) != count:
            raise RuntimeError("no reply from the Uploader sketch")
        return data

    def _reply(self):
        """Returns the next reply byte, queueing forwarded packets."""
        while True:
            reply = self._read(1)
            if reply != b"#":
                return reply
            sender, length = self._read(2)
            self.events.append((sender, self._read(length)))

    def handshake(self, attempts=20):
        # Opening the port resets most Arduino boards
        time.sleep(2)
        self.serial.reset_input_buffer()
        for _ in range(attempts):
            self.serial.write(bytes([0, 0]))
            try:
                if self._reply() == b"R":
                    return
            except RuntimeError:
                pass
            time.sleep(0.3)  # let the uploader discard a partial request
            self.serial.reset_input_buffer()
        raise RuntimeError("the Uploader sketch does not answer")

    def send(self, device_id, payload):
        self.serial.write(bytes([device_id, len(payload)]) + payload)
        reply = self._reply()
        if reply not in (b"K", b"E"):
            raise RuntimeError("unexpected reply from the Uploader sketch")
        return reply == b"K"

    def wait_announcement(self, timeout, match=lambda a: True):
        """Returns the first announcement for which match() is true, or None
        after timeout seconds."""
        end = time.time() + timeout
        while True:
            while self.events:
                sender, payload = self.events.pop(0)
                if len(payload) >= 8 and payload[0] == CMD_ANNOUNCE:
                    announcement = Announcement(sender, payload)
                    if match(announcement):
                        return announcement
            if time.time() > end:
                return None
            if self.serial.in_waiting:
                if self._read(1) == b"#":
                    sender, length = self._read(2)
                    self.events.append((sender, self._read(length)))
            else:
                time.sleep(0.01)


def assign(up, serial_hex, device_id):
    """Assigns device_id to the unprovisioned device with this serial."""
    command = CMD_ASSIGN + bytes.fromhex(serial_hex) + bytes([device_id])
    return up.send(UNASSIGNED, command)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="\n".join(__doc__.split("\n")[1:]))
    parser.add_argument("hex", nargs="?", help="firmware in Intel HEX format")
    parser.add_argument("--port", required=True, help="serial port of the Uploader")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--id", type=int, help="update the device with this id")
    mode.add_argument("--provision", type=int, metavar="ID",
                      help="assign ID to a device without id, then flash it")
    mode.add_argument("--listen", action="store_true",
                      help="print announcements of devices in the bootloader")
    parser.add_argument("--serial", help="with --provision: only this device "
                        "(8 hex digits, as printed by --listen)")
    parser.add_argument("--wait", type=float, default=120,
                        help="seconds to wait for an announcement (default 120, "
                             "with --listen: 0 = forever)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--boot-start", type=lambda v: int(v, 0), default=0x7000,
                        help="bootloader start address (default 0x7000, 4kB "
                             "boot section)")
    parser.add_argument("--chunk-size", type=int, default=32,
                        choices=[16, 32, 64, 128],
                        help="bytes per write packet, as CHUNK_SIZE of the "
                             "bootloader build (default 32)")
    parser.add_argument("--no-enter", action="store_true",
                        help="do not ask the application to enter the bootloader")
    parser.add_argument("--ping-attempts", type=int, default=5)
    parser.add_argument("--write-attempts", type=int, default=3)
    parser.add_argument("--reply-timeout", type=float, default=10.0,
                        help="seconds to wait for each Uploader reply")
    args = parser.parse_args()

    if args.listen:
        up = Uploader(args.port, args.baud, args.reply_timeout)
        up.handshake()
        end = time.time() + args.wait if args.wait else None
        while end is None or time.time() < end:
            announcement = up.wait_announcement(1)
            if announcement:
                print(announcement)
                sys.stdout.flush()
        return

    if not args.hex:
        parser.error("the firmware HEX file is required")
    target = args.provision if args.provision is not None else args.id
    if not 1 <= target <= 253:
        sys.exit("the device id must be between 1 and 253")
    pages = build_pages(parse_hex(args.hex), args.boot_start)
    print("%s: %d bytes, %d pages" % (args.hex, len(pages) * PAGE_SIZE, len(pages)))

    up = Uploader(args.port, args.baud, args.reply_timeout)
    up.handshake()

    if args.provision is not None:
        wanted = args.serial.lower() if args.serial else None
        print("waiting for a device without id%s..." % (
            " with serial " + wanted if wanted else ""))
        announcement = up.wait_announcement(
            args.wait, lambda a: a.sender == UNASSIGNED and
            (wanted is None or a.serial == wanted))
        if not announcement:
            sys.exit("no device announced itself")
        print(announcement)
        if announcement.chunk_size != args.chunk_size:
            sys.exit("the bootloader uses chunk size %d, use --chunk-size %d" % (
                announcement.chunk_size, announcement.chunk_size))
        if not assign(up, announcement.serial, target):
            sys.exit("assigning id %d failed" % target)
        print("id %d assigned to serial %s" % (target, announcement.serial))
        args.no_enter = True

    if not args.no_enter:
        if up.send(target, CMD_ENTER):
            print("application restarted into the bootloader")
        else:
            print("the application did not answer, trying the bootloader anyway")

    for _ in range(args.ping_attempts):
        if up.send(target, CMD_PING):
            break
    else:
        sys.exit("the bootloader does not answer")
    print("bootloader found")

    def write(address, page):
        for _ in range(args.write_attempts):
            if write_page(up, target, address, page, args.chunk_size):
                return
        sys.exit("\nwriting page 0x%04X failed (is --chunk-size the same as "
                 "the bootloader's CHUNK_SIZE?)" % address)

    write(0, b"\xFF" * PAGE_SIZE)  # invalidate the application first
    order = pages[1:] + pages[:1]  # page 0 (reset vector) last
    start = time.time()
    for n, (address, page) in enumerate(order, 1):
        write(address, page)
        sys.stdout.write("\rwriting %d/%d" % (n, len(order)))
        sys.stdout.flush()
    print("\rwritten %d pages in %.1fs" % (len(order), time.time() - start))

    if not up.send(target, CMD_GO):
        sys.exit("the bootloader did not acknowledge the start command")
    print("application started")


if __name__ == "__main__":
    main()
