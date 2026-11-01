# PJON SoftwareBitBang bootloader for ATmega328P
#
# Build:   make PJON_PIN=12 DEVICE_ID=44
# Flash:   make flash ISP=usbasp          (sets fuses and writes the bootloader)
#
# Options (all optional):
#   PJON_PIN          Arduino pin number of the PJON bus, 0-19 (A0=14)  [12]
#   DEVICE_ID         Device id used when the application does not pass one,
#                     e.g. after a reset pin reset or with no application [44]
#   MODE              PJDL/SoftwareBitBang mode 1-4 (must match the bus)  [1]
#   TIMEOUT_MS        Time spent in the bootloader after the application
#                     requested it, restarted by each valid command    [5000]
#   EXTRF_TIMEOUT_MS  Time spent in the bootloader after a reset pin reset,
#                     0 disables entering the bootloader this way       [1000]
#   F_CPU             Clock frequency in Hz                         [16000000]
#   BOOT_SIZE         Bootloader section size in bytes, 2048 or 4096   [2048]
#                     (1024 is accepted but the bootloader no longer fits)
#   CHUNK_SIZE        Bytes of flash per write packet, 16/32/64/128       [32]
#                     (must match the uploader, see PJON_Bootloader.h)
#   EESAVE            1 keeps the EEPROM content on chip erase            [1]
#   ISP / ISP_PORT    avrdude programmer and port for "make flash"
#   AVRDUDE_FLAGS     extra avrdude options, e.g. "-b 19200" for ArduinoISP

PJON_PIN         ?= 12
DEVICE_ID        ?= 44
MODE             ?= 1
TIMEOUT_MS       ?= 5000
EXTRF_TIMEOUT_MS ?= 1000
F_CPU            ?= 16000000
BOOT_SIZE        ?= 2048
CHUNK_SIZE       ?= 32
EESAVE           ?= 1
ISP              ?= usbasp
ISP_PORT         ?= usb
AVRDUDE_FLAGS    ?=

MCU     = atmega328p
TARGET  = pjon_swbb_bootloader
PJON_SRC = ../../../src

# BOOTSZ fuse bits and high fuse (SPIEN on, BOOTRST on, EESAVE as chosen)
ifeq ($(BOOT_SIZE),1024)
  BOOT_START = 0x7C00
  HFUSE_BASE = 0xDC
else ifeq ($(BOOT_SIZE),2048)
  BOOT_START = 0x7800
  HFUSE_BASE = 0xDA
else ifeq ($(BOOT_SIZE),4096)
  BOOT_START = 0x7000
  HFUSE_BASE = 0xD8
else
  $(error BOOT_SIZE must be 1024, 2048 or 4096)
endif

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
          -DBOOT_START=$(BOOT_START) -DPJON_BOOT_CHUNK_SIZE=$(CHUNK_SIZE)
LDFLAGS = -nostartfiles -Wl,--section-start=.text=$(BOOT_START) -Wl,--relax

all: $(TARGET).hex size

fuses:
	@echo "lfuse $(LFUSE) hfuse $(HFUSE) efuse $(EFUSE) boot start $(BOOT_START)"

$(TARGET).elf: $(TARGET).c $(PJON_SRC)/utils/bootloader/PJON_Bootloader.h \
               Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

$(TARGET).lst: $(TARGET).elf
	$(OBJDUMP) -h -S $< > $@

size: $(TARGET).elf
	@bytes=$$($(SIZE) -A $< | awk '/^\.text/ {t=$$2} /^\.data/ {d=$$2} END {print t+d}'); \
	echo "Bootloader size: $$bytes of $(BOOT_SIZE) bytes"; \
	if [ $$bytes -gt $(BOOT_SIZE) ]; then \
	  echo "ERROR: bootloader does not fit in BOOT_SIZE=$(BOOT_SIZE)"; \
	  rm -f $(TARGET).hex; exit 1; \
	fi; \
	entry=$$(avr-nm $< | awk '$$3 == "main" {print $$1}'); \
	if [ $$((0x$$entry)) -ne $$(($(BOOT_START))) ]; then \
	  echo "ERROR: main() is at 0x$$entry, not at BOOT_START $(BOOT_START)"; \
	  rm -f $(TARGET).hex; exit 1; \
	fi

flash: $(TARGET).hex size
	avrdude -c $(ISP) -P $(ISP_PORT) $(AVRDUDE_FLAGS) -p $(MCU) -e \
	  -U lfuse:w:$(LFUSE):m -U hfuse:w:$(HFUSE):m -U efuse:w:$(EFUSE):m \
	  -U flash:w:$(TARGET).hex:i

clean:
	rm -f $(TARGET).elf $(TARGET).hex $(TARGET).lst

.PHONY: all size fuses flash clean
