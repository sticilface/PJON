#!/usr/bin/env python3
"""Upload firmware to a device running the PJON SoftwareBitBang bootloader.

The PC talks to an Arduino running the Uploader sketch
(examples/ARDUINO/Local/SoftwareBitBang/Bootloader/Uploader), which sends the
firmware over the PJON bus to the target.

    python3 pjon_boot_upload.py --port /dev/ttyUSB0 --id 44 firmware.hex
    python3 pjon_boot_upload.py --port /dev/ttyUSB0 --provision 45 firmware.hex
    python3 pjon_boot_upload.py --port /dev/ttyUSB0 --listen
    python3 pjon_boot_upload.py --mcu atmega2560 --port /dev/ttyUSB0 \
        --id 44 firmware.hex                              (Arduino Mega)

--id: update a device. Sequence:
 1. ask the running application to restart into the bootloader ('B')
 2. ping the bootloader until it answers ('P')
 3. erase page 0 first, so an interrupted upload leaves a device that stays
    in the bootloader instead of running a half written application
 4. write every other page, then page 0 last. A page is sent as chunks
    ('W', --chunk-size bytes each, must match the bootloader build; 'X' with
    a 24 bits address above 64kB on the ATmega2560) followed
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

PAGE_SIZE = 128  # ATmega328P, see MCUS
CMD_ENTER = b"B"
CMD_PING = b"P"
CMD_WRITE = b"W"
CMD_WRITE_FAR = b"X"
CMD_GO = b"G"
CMD_ASSIGN = b"I"
CMD_ANNOUNCE = ord("b")
UNASSIGNED = 255
REASONS = {1: "no application", 2: "application did not start (boot guard)",
           3: "entered on request"}

# Target MCUs: flash page size and default bootloader start (default
# BOOT_SIZE of the Makefile: 3072 on the atmega328p, 4096 on the atmega2560)
MCUS = {
    "atmega328p": {"page_size": 128, "boot_start": 0x7400},
    "atmega2560": {"page_size": 256, "boot_start": 0x3F000},
}


def write_command(address, data):
    """Write command of a chunk: 'W' with a 16 bits address, 'X' with a 24
    bits address above 64kB (only bootloaders of MCUs with more than 64kB of
    flash accept it)."""
    if address > 0xFFFF:
        return CMD_WRITE_FAR + bytes(
            [address >> 16, (address >> 8) & 0xFF, address & 0xFF]) + data
    return CMD_WRITE + bytes([address >> 8, address & 0xFF]) + data


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


def build_pages(data, boot_start=0x7400, page_size=PAGE_SIZE, sparse=False):
    """Pages from address 0 to the end of the image. With sparse, only the
    pages that contain data of the image (the others keep their content)."""
    if not data:
        raise ValueError("the HEX file is empty")
    end = max(data) + 1
    if end > boot_start:
        raise ValueError(
            "the image ends at 0x%04X, it overlaps the bootloader at 0x%04X"
            % (end, boot_start))
    used = {address - address % page_size for address in data}
    pages = []
    for address in range(0, end, page_size):
        if sparse and address and address not in used:
            continue
        page = bytes(data.get(address + i, 0xFF) for i in range(page_size))
        pages.append((address, page))
    return pages


def write_page(up, device_id, address, page, chunk_size):
    """Sends the chunks of a page, then a ping that is acknowledged only if
    the page was written and verified. Returns True if it was."""
    for offset in range(0, len(page), chunk_size):
        command = write_command(
            address + offset, page[offset:offset + chunk_size])
        if not up.send(device_id, command):
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
    parser.add_argument("--mcu", default="atmega328p", choices=sorted(MCUS),
                        help="target MCU: flash page size and default "
                             "--boot-start (default atmega328p)")
    parser.add_argument("--boot-start", type=lambda v: int(v, 0), default=None,
                        help="bootloader start address (default: 0x7400 on "
                             "the atmega328p, 3kB layout, 0x7000 for "
                             "BOOT_SIZE=4096; 0x3F000 on the atmega2560, "
                             "0x3E000 for BOOT_SIZE=8192)")
    parser.add_argument("--sparse", action="store_true",
                        help="write only the pages that contain data of the "
                             "image (the others keep their content)")
    parser.add_argument("--chunk-size", type=int, default=32,
                        choices=[16, 32, 64, 128],
                        help="bytes per write packet, as CHUNK_SIZE of the "
                             "bootloader build (default 32)")
    parser.add_argument("--no-enter", action="store_true",
                        help="the device is already in the bootloader: do not "
                             "ask the application to enter it")
    parser.add_argument("--enter-wait", type=float, default=5,
                        help="seconds to wait for the bootloader announcement "
                             "after the request (default 5)")
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
    page_size = MCUS[args.mcu]["page_size"]
    boot_start = args.boot_start
    if boot_start is None:
        boot_start = MCUS[args.mcu]["boot_start"]
    pages = build_pages(parse_hex(args.hex), boot_start, page_size, args.sparse)
    print("%s: %d bytes, %d pages" % (args.hex, len(pages) * page_size, len(pages)))

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
        if not up.send(target, CMD_ENTER):
            print("the application did not answer, trying the bootloader anyway")
        # The application's PJON acknowledges bootloader commands too: wait
        # for the bootloader's announcement to know it is really running
        if not up.wait_announcement(args.enter_wait, lambda a: a.sender == target):
            sys.exit("device %d did not announce the bootloader: is the "
                     "application handling the bootloader request?" % target)
        print("bootloader entered")

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
        sys.exit("\nwriting page 0x%04X failed (are --chunk-size and --mcu "
                 "the same as the bootloader's?)" % address)

    write(0, b"\xFF" * page_size)  # invalidate the application first
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
