/* Simulation of ATmega328P devices connected by a single wire PJON bus.

   targets:  PJON SoftwareBitBang bootloader + optional application (1 to 4)
   uploader: Uploader sketch, its USART0 is exposed as a pty
             (/tmp/simavr-uart0) so that tools/pjon_boot_upload.py can be used

   All cores run in lock step. The bus is high if at least one core drives
   the pin high, otherwise it is low (pull-down). Contention (one core
   driving high while another drives low) is reported.

   Target EEPROMs start erased (0xFF). Serials and ids can be preloaded:
   the bootloader's random serial depends on the watchdog oscillator
   jitter, which the simulator does not reproduce (all simulated devices
   would get the same serial).

   Usage: sim -b bootloader.hex [-a app.hex] -u uploader.hex [options]
     -N count    number of targets (default 1)
     -s serial   preload the serial (8 hex digits) of the next target, can
                 be repeated (first -s for target 0, ...)
     -i id       preload the device id of the next target, can be repeated
     -A address  EEPROM address of the device id (default 1019), the serial
                 follows it
     -B pin      bus pin, Arduino numbering (default 12)
     -F hz       target clock frequency (default 16000000)
     -m value    exit with success when all targets wrote value in GPIOR1
     -w seconds  ignore GPIOR1 changes before this simulated time
     -t seconds  maximum simulated time (default 60), exit with failure
     -f file     dump the application flash of target 0 on exit
     -E prefix   dump the EEPROM of each target on exit in prefix<n>.bin
     -e          trace bus edges (microseconds, who drives) on stderr
     -n          do not create the pty (uploader serial unused) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_hex.h>
#include <simavr/avr_eeprom.h>
#include <simavr/parts/uart_pty.h>

#define MCU "atmega328p"
#define UPLOADER_FREQUENCY 16000000
#define GPIOR1_ADDR 0x4A
#define MCUSR_ADDR  0x54
#define RAMEND 0x8FF
#define EEPROM_SIZE 1024
#define MAX_TARGETS 4

typedef struct {
  avr_t *avr;
  uint8_t mailbox[3];
  uint32_t last_pc;
  int in_boot;
  int marker;
} target_t;

static target_t targets[MAX_TARGETS];
static int target_count = 1;
static avr_t *uploader;
static uart_pty_t pty;
static int pin_reg, ddr_reg, port_reg, mask;
static uint32_t boot_start;

/* Loads an Intel HEX file in flash, returns its start address */
static uint32_t load(avr_t *avr, const char *path, int verbose) {
  uint32_t size = 0, start = 0;
  uint8_t *data = read_ihex_file(path, &size, &start);
  if(!data) {
    fprintf(stderr, "sim: can not read %s\n", path);
    exit(2);
  }
  memcpy(avr->flash + start, data, size);
  free(data);
  if(verbose)
    printf("sim: loaded %s at 0x%04X (%u bytes)\n", path, start, size);
  return start;
}

static uint8_t *eeprom(avr_t *avr) {
  avr_eeprom_desc_t desc = { .ee = NULL, .offset = 0, .size = EEPROM_SIZE };
  avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &desc);
  return desc.ee;
}

static int drives_high(avr_t *avr) {
  return (avr->data[ddr_reg] & mask) && (avr->data[port_reg] & mask);
}

static int drives_low(avr_t *avr) {
  return (avr->data[ddr_reg] & mask) && !(avr->data[port_reg] & mask);
}

static void set_input(avr_t *avr, int level) {
  if(level) avr->data[pin_reg] |= mask;
  else avr->data[pin_reg] &= ~mask;
}

static double us(avr_t *avr) {
  return avr->cycle / (avr->frequency / 1000000.0);
}

/* Suffix identifying the target in the log, only with several targets */
static const char *tag(int n) {
  static char text[16];
  if(target_count == 1) return "";
  snprintf(text, sizeof(text), " [t%d]", n);
  return text;
}

int main(int argc, char **argv) {
  const char *boot = NULL, *app = NULL, *up = NULL, *dump = NULL;
  const char *eeprom_dump = NULL, *serials[MAX_TARGETS] = {0};
  int ids[MAX_TARGETS] = {0}, serial_count = 0, id_count = 0;
  int bus_pin = 12, marker = -1, use_pty = 1, trace = 0, opt;
  int id_address = 1019;
  double max_seconds = 60, marker_after = 0;
  uint32_t target_frequency = 16000000;
  while((opt = getopt(argc, argv, "b:a:u:N:s:i:A:B:F:m:w:t:f:E:en")) != -1) {
    switch(opt) {
      case 'b': boot = optarg; break;
      case 'a': app = optarg; break;
      case 'u': up = optarg; break;
      case 'N': target_count = atoi(optarg); break;
      case 's': if(serial_count < MAX_TARGETS) serials[serial_count++] = optarg; break;
      case 'i': if(id_count < MAX_TARGETS) ids[id_count++] = atoi(optarg); break;
      case 'A': id_address = atoi(optarg); break;
      case 'B': bus_pin = atoi(optarg); break;
      case 'F': target_frequency = atol(optarg); break;
      case 'm': marker = strtol(optarg, NULL, 0); break;
      case 'w': marker_after = atof(optarg); break;
      case 't': max_seconds = atof(optarg); break;
      case 'f': dump = optarg; break;
      case 'E': eeprom_dump = optarg; break;
      case 'e': trace = 1; break;
      case 'n': use_pty = 0; break;
      default: return 2;
    }
  }
  if(!boot || !up || target_count < 1 || target_count > MAX_TARGETS) {
    fprintf(stderr, "usage: sim -b bootloader.hex [-a app.hex] -u uploader.hex\n");
    return 2;
  }
  setvbuf(stdout, NULL, _IONBF, 0);

  /* Arduino pin -> data space addresses of PINx, DDRx, PORTx */
  if(bus_pin <= 7) { pin_reg = 0x29; mask = 1 << bus_pin; }
  else if(bus_pin <= 13) { pin_reg = 0x23; mask = 1 << (bus_pin - 8); }
  else { pin_reg = 0x26; mask = 1 << (bus_pin - 14); }
  ddr_reg = pin_reg + 1;
  port_reg = pin_reg + 2;

  for(int n = 0; n < target_count; n++) {
    target_t *t = &targets[n];
    t->avr = avr_make_mcu_by_name(MCU);
    if(!t->avr) return 2;
    avr_init(t->avr);
    t->avr->frequency = target_frequency;
    t->avr->log = LOG_WARNING;
    boot_start = load(t->avr, boot, !n);
    if(app) load(t->avr, app, !n);
    /* BOOTRST fuse: reset vector in the bootloader section */
    t->avr->reset_pc = boot_start;
    t->avr->pc = boot_start;
    t->avr->data[MCUSR_ADDR] = 1; // power on reset flag
    t->last_pc = boot_start;
    t->in_boot = 1;
    uint8_t *ee = eeprom(t->avr);
    memset(ee, 0xFF, EEPROM_SIZE);
    if(n < id_count) ee[id_address] = ids[n];
    if(n < serial_count)
      for(int i = 0; i < 4; i++) {
        unsigned int b;
        sscanf(serials[n] + 2 * i, "%2x", &b);
        ee[id_address + 1 + i] = b;
      }
  }
  uploader = avr_make_mcu_by_name(MCU);
  if(!uploader) return 2;
  avr_init(uploader);
  uploader->frequency = UPLOADER_FREQUENCY;
  uploader->log = LOG_WARNING;
  load(uploader, up, 1);

  if(use_pty) {
    uart_pty_init(uploader, &pty);
    uart_pty_connect(&pty, '0');
  }

  int last_level = 0, contention_boot = 0, contention_app = 0;
  int result = 1, contention_now = 0;
  printf("sim: %d target(s) start in the bootloader at 0x%04X\n",
    target_count, boot_start);

  for(;;) {
    /* Run the core that is behind in time */
    avr_t *avr = uploader;
    target_t *current = NULL;
    for(int n = 0; n < target_count; n++)
      if(us(targets[n].avr) <= us(avr)) {
        avr = targets[n].avr;
        current = &targets[n];
      }
    int state = avr_run(avr);
    if(state == cpu_Done || state == cpu_Crashed) {
      printf("sim: %s stopped (state %d)\n",
        current ? "target" : "uploader", state);
      break;
    }

    if(current) {
      int n = current - targets;
      uint32_t pc = avr->pc;
      /* A reset is the only way into the bootloader entry point */
      if(pc == boot_start && current->last_pc != boot_start) {
        uint8_t *ram = avr->data + RAMEND - 2;
        printf("sim: %.3fs target reset, MCUSR=0x%02X%s\n",
          us(avr) / 1e6, avr->data[MCUSR_ADDR], tag(n));
        /* Real hardware keeps SRAM content on a watchdog reset */
        if(memcmp(ram, current->mailbox, 3)) {
          printf("sim: restoring SRAM mailbox cleared by the simulator\n");
          memcpy(ram, current->mailbox, 3);
        }
      }
      memcpy(current->mailbox, avr->data + RAMEND - 2, 3);
      if((pc < boot_start) == current->in_boot) {
        current->in_boot = !current->in_boot;
        printf("sim: %.3fs target running %s (pc 0x%04X)%s\n", us(avr) / 1e6,
          current->in_boot ? "bootloader" : "application", pc, tag(n));
      }
      current->last_pc = pc;
      int m = avr->data[GPIOR1_ADDR];
      if(m != current->marker) {
        current->marker = m;
        printf("sim: %.3fs target GPIOR1 = 0x%02X%s\n", us(avr) / 1e6, m, tag(n));
        int all = us(avr) >= marker_after * 1e6;
        for(int k = 0; k < target_count; k++)
          all = all && (targets[k].marker == marker);
        if(all && result) {
          result = 0;
          /* keep running a little so the uploader can answer the PC */
          max_seconds = us(avr) / 1e6 + 0.3;
        }
      }
    }

    /* Wired bus */
    int high = drives_high(uploader), low = drives_low(uploader);
    int high_in_boot = 0;
    for(int n = 0; n < target_count; n++) {
      if(drives_high(targets[n].avr)) {
        high = 1;
        high_in_boot |= targets[n].in_boot;
      }
      if(drives_low(targets[n].avr)) low = 1;
    }
    if(high && low) {
      if(!contention_now) {
        int in_boot = 0;
        for(int n = 0; n < target_count; n++) in_boot |= targets[n].in_boot;
        if(in_boot) contention_boot++;
        else contention_app++;
        if(trace) fprintf(stderr, "%.2f contention start\n", us(avr));
      }
      contention_now = 1;
    } else contention_now = 0;
    for(int n = 0; n < target_count; n++) set_input(targets[n].avr, high);
    set_input(uploader, high);
    if(trace && high != last_level) {
      fprintf(stderr, "%.2f %d %c%c\n", us(avr), high,
        (high && !drives_high(uploader)) ? 'T' : '-',
        drives_high(uploader) ? 'U' : '-');
    }
    (void)high_in_boot;
    last_level = high;

    if(us(avr) > max_seconds * 1e6) {
      if(result) printf("sim: maximum simulated time reached\n");
      break;
    }
  }

  printf("sim: simulated %.3fs, bus contention events: %d while a target "
    "runs the bootloader, %d while targets run the application\n",
    us(targets[0].avr) / 1e6, contention_boot, contention_app);
  if(dump) {
    FILE *f = fopen(dump, "wb");
    if(f) {
      fwrite(targets[0].avr->flash, 1, boot_start, f);
      fclose(f);
    }
  }
  if(eeprom_dump)
    for(int n = 0; n < target_count; n++) {
      char path[512];
      snprintf(path, sizeof(path), "%s%d.bin", eeprom_dump, n);
      FILE *f = fopen(path, "wb");
      if(f) {
        fwrite(eeprom(targets[n].avr), 1, EEPROM_SIZE, f);
        fclose(f);
      }
    }
  printf("sim: %s\n", result ? "FAIL" : "PASS");
  fflush(stdout);
  _exit(result); /* the pty thread is not stopped, it can block */
}
