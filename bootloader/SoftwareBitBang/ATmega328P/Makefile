
# PJON SoftwareBitBang bootloader for ATmega328P
#
# Build:   make PJON_PIN=12
# Flash:   make flash ISP=usbasp   (fuses, bootloader and a random serial)
#
# Options (all optional):
#   PJON_PIN          Arduino pin number of the PJON bus, 0-19 (A0=14)  [12]
#   DEVICE_ID         Device id used when none is passed by the application
#                     or saved in EEPROM (255 = not assigned)            [255]
#   MODE              PJDL/SoftwareBitBang mode 1-4 (must match the bus)  [1]
#   TIMEOUT_MS        Time spent in the bootloader after the application
#                     requested it, restarted by each valid command    [5000]
#   EXTRF_TIMEOUT_MS  Time spent in the bootloader after a reset pin reset,
#                     0 disables entering the bootloader this way       [1000]
#   F_CPU             Clock frequency in Hz                         [16000000]
#   BOOT_SIZE         Flash used by the bootloader: 3072 (2kB boot section
#                     + 1kB below it) or 4096; 1024 and 2048 are accepted
#                     but it does not fit                               [3072]
#   CHUNK_SIZE        Bytes of flash per write packet, 16/32/64/128       [32]
#                     (must match the uploader, see PJON_Bootloader.h)
#   EESAVE            1 keeps the EEPROM content on chip erase            [1]
#   BOOT_GUARD_S      Seconds the application has to confirm its start with
#                     pjon_boot_app_ok(): 0 (off), 1, 2, 4 or 8            [8]
#   EEPROM_ID_ADDR    EEPROM address of the device id, the 4 bytes serial
#                     follows it (must match the application)         [1019]
#   SERIAL_UPLOAD     1: Optiboot compatible serial upload (Arduino IDE,
#                     avrdude -c arduino) on pins 0/1                     [1]
#   SERIAL_BAUD       Serial upload baud rate                        [115200]
#   SERIAL            Serial written in EEPROM by "make flash": "random",
#                     8 hex digits (first byte not FF), or "none" to leave
#                     the EEPROM untouched                          [random]
#   ISP / ISP_PORT    avrdude programmer and port for "make flash"
#   AVRDUDE_FLAGS     extra avrdude options, e.g. "-b 19200" for ArduinoISP

SHELL := /bin/bash

PJON_PIN         ?= 12
DEVICE_ID        ?= 255
MODE             ?= 1
TIMEOUT_MS       ?= 5000
EXTRF_TIMEOUT_MS ?= 1000
F_CPU            ?= 16000000
BOOT_SIZE        ?= 3072
SERIAL_UPLOAD    ?= 1
SERIAL_BAUD      ?= 115200
CHUNK_SIZE       ?= 32
EESAVE           ?= 1
BOOT_GUARD_S     ?= 8
EEPROM_ID_ADDR   ?= 1019
SERIAL           ?= random
ISP              ?= usbasp
ISP_PORT         ?= usb
AVRDUDE_FLAGS    ?=

MCU     = atmega328p
TARGET  = pjon_swbb_bootloader
PJON_SRC = ../../../src

# BOOTSZ fuse bits and high fuse (SPIEN on, BOOTRST on, EESAVE as chosen).
# BOOT_START: first byte of the bootloader (writes refused from here up),
# ENTRY: start of the hardware boot section (reset vector, main()).
# 3072 is a split layout: 2kB boot section + 1kB below it (.bootlow).
LOW_SECTION = 0
ifeq ($(BOOT_SIZE),1024)
  BOOT_START = 0x7C00
  HFUSE_BASE = 0xDC
else ifeq ($(BOOT_SIZE),2048)
  BOOT_START = 0x7800
  HFUSE_BASE = 0xDA
else ifeq ($(BOOT_SIZE),3072)
  BOOT_START = 0x7400
  ENTRY      = 0x7800
  HFUSE_BASE = 0xDA
  LOW_SECTION = 1
else ifeq ($(BOOT_SIZE),4096)
  BOOT_START = 0x7000
  HFUSE_BASE = 0xD8
else
  $(error BOOT_SIZE must be 1024, 2048, 3072 or 4096)
endif
ENTRY ?= $(BOOT_START)

# EESAVE is bit 3 of the high fuse, programmed (0) keeps the EEPROM
ifeq ($(EESAVE),1)
  HFUSE = $(shell printf '0x%02X' $$(( $(HFUSE_BASE) & 0xF7 )))
else
  HFUSE = $(HFUSE_BASE)
endif

# External crystal >8MHz, slow rising power; brown-out at 2.7V
LFUSE ?= 0xFF
EFUSE ?= 0xFD

CC      = avr-gcc
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
SIZE    = avr-size

CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU)UL -Os -std=gnu99 -Wall -Wextra \
          -fno-split-wide-types -mrelax -I$(PJON_SRC) \
          -DPJON_PIN=$(PJON_PIN) -DDEVICE_ID=$(DEVICE_ID) -DMODE=$(MODE) \
          -DTIMEOUT_MS=$(TIMEOUT_MS) -DEXTRF_TIMEOUT_MS=$(EXTRF_TIMEOUT_MS) \
          -DBOOT_START=$(BOOT_START) -DPJON_BOOT_CHUNK_SIZE=$(CHUNK_SIZE) \
          -DBOOT_GUARD_S=$(BOOT_GUARD_S) \
          -DPJON_BOOT_EEPROM_ID_ADDR=$(EEPROM_ID_ADDR) \
          -DSERIAL_UPLOAD=$(SERIAL_UPLOAD) -DSERIAL_BAUD=$(SERIAL_BAUD) \
          -DLOW_SECTION=$(LOW_SECTION)
LDFLAGS = -nostartfiles -Wl,--section-start=.text=$(ENTRY) -Wl,--relax \
          $(if $(filter 1,$(LOW_SECTION)),-Wl$(comma)--section-start=.bootlow=$(BOOT_START))
comma := ,

all: $(TARGET).hex size

fuses:
	@echo "lfuse $(LFUSE) hfuse $(HFUSE) efuse $(EFUSE) boot start $(BOOT_START) entry $(ENTRY)"

$(TARGET).elf: $(TARGET).c $(PJON_SRC)/utils/bootloader/PJON_Bootloader.h \
               Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -j .bootlow -j .text -j .data -O ihex $< $@

$(TARGET).lst: $(TARGET).elf
	$(OBJDUMP) -h -S $< > $@

size: $(TARGET).elf
	@text=$$($(SIZE) -A $< | awk '/^\.text/ {t=$$2} /^\.data/ {d=$$2} END {print t+d}'); \
	low=$$($(SIZE) -A $< | awk '/^\.bootlow/ {print $$2}'); low=$${low:-0}; \
	room=$$(( 0x8000 - $(ENTRY) )); lowroom=$$(( $(ENTRY) - $(BOOT_START) )); \
	echo "Bootloader size: $$(( text + low )) of $(BOOT_SIZE) bytes"; \
	if [ $(LOW_SECTION) = 1 ]; then \
	  echo "  boot section: $$text of $$room, below it: $$low of $$lowroom"; fi; \
	if [ $$text -gt $$room ] || [ $$low -gt $$lowroom ]; then \
	  echo "ERROR: bootloader does not fit in BOOT_SIZE=$(BOOT_SIZE)"; \
	  rm -f $(TARGET).hex; exit 1; \
	fi; \
	entry=$$(avr-nm $< | awk '$$3 == "main" {print $$1}'); \
	if [ $$((0x$$entry)) -ne $$(($(ENTRY))) ]; then \
	  echo "ERROR: main() is at 0x$$entry, not at $(ENTRY)"; \
	  rm -f $(TARGET).hex; exit 1; \
	fi; \
	if $(OBJDUMP) -d $< | grep -A3 "<main>:" | grep -qE "rcall|push"; then \
	  echo "ERROR: main() uses the stack before reading the mailbox"; \
	  rm -f $(TARGET).hex; exit 1; \
	fi; \
	for spm in $$($(OBJDUMP) -d $< | awk '$$NF == "spm" {sub(":", "", $$1); print $$1}'); do \
	  if [ $$((0x$$spm)) -lt $$(($(ENTRY))) ]; then \
	    echo "ERROR: SPM at 0x$$spm, outside the boot section (SPM only works there)"; \
	    rm -f $(TARGET).hex; exit 1; \
	  fi; \
	done

# Intel HEX file with only the 4 serial bytes, written to EEPROM by "make
# flash" (avrdude writes only the addresses present in the file, so the
# device id and the rest of the EEPROM are kept). The bootloader also
# generates a random serial itself if it finds none.
serial.eep:
	@serial="$(SERIAL)"; \
	if [ "$$serial" = random ]; then \
	  serial=$$(od -An -N4 -tx1 /dev/urandom | tr -d ' \n'); \
	  serial=$$(printf '%02x' $$((0x$${serial:0:2} & 0x7F)))$${serial:2}; \
	fi; \
	case "$$serial" in \
	  [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]) ;; \
	  *) echo "ERROR: SERIAL must be random, none or 8 hex digits"; exit 1;; \
	esac; \
	if [ "$$(echo $${serial:0:2} | tr a-f A-F)" = FF ]; then \
	  echo "ERROR: the first serial byte can not be FF"; exit 1; fi; \
	addr=$$(( $(EEPROM_ID_ADDR) + 1 )); \
	sum=$$(( 4 + (addr >> 8) + (addr & 255) )); \
	for i in 0 2 4 6; do sum=$$(( sum + 0x$${serial:$$i:2} )); done; \
	printf ':04%04X00%s%02X\n:00000001FF\n' $$addr \
	  "$$(echo $$serial | tr a-f A-F)" $$(( (256 - (sum & 255)) & 255 )) \
	  > $@; \
	echo "Serial: $$serial"

ifeq ($(SERIAL),none)
  EEPROM_WRITE =
else
  EEPROM_WRITE = -U eeprom:w:serial.eep:i
endif

flash: $(TARGET).hex size $(if $(EEPROM_WRITE),serial.eep)
	avrdude -c $(ISP) -P $(ISP_PORT) $(AVRDUDE_FLAGS) -p $(MCU) -e \
	  -U lfuse:w:$(LFUSE):m -U hfuse:w:$(HFUSE):m -U efuse:w:$(EFUSE):m \
	  -U flash:w:$(TARGET).hex:i $(EEPROM_WRITE)
	@rm -f serial.eep

clean:
	rm -f $(TARGET).elf $(TARGET).hex $(TARGET).lst serial.eep

.PHONY: all size fuses flash clean serial.eep
