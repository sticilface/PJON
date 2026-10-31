/* Simulation of two ATmega328P connected by a single wire PJON bus.

   target:   PJON SoftwareBitBang bootloader + optional application
   uploader: Uploader sketch, its USART0 is exposed as a pty
             (/tmp/simavr-uart0) so that tools/pjon_boot_upload.py can be used

   Both cores run in lock step. The bus is high if at least one of the two
   cores drives the pin high, otherwise it is low (pull-down). Contention
   (one core driving high while the other drives low) is reported.

   Usage: sim -b bootloader.hex [-a app.hex] -u uploader.hex [options]
     -B pin      bus pin, Arduino numbering (default 12)
     -F hz       target clock frequency (default 16000000)
     -m value    exit with success when the target writes value in GPIOR1
     -w seconds  ignore GPIOR1 changes before this simulated time
     -t seconds  maximum simulated time (default 60), exit with failure
     -f file     dump the target application flash (0 - boot start) on exit
     -e          trace bus edges (microseconds, who drives) on stderr
     -n          do not create the pty (uploader serial unused) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_hex.h>
#include <simavr/parts/uart_pty.h>

#define MCU "atmega328p"
#define UPLOADER_FREQUENCY 16000000
#define GPIOR0_ADDR 0x3E
#define GPIOR1_ADDR 0x4A
#define MCUSR_ADDR  0x54
#define RAMEND 0x8FF

static avr_t *target, *uploader;
static uart_pty_t pty;
static int pin_reg, ddr_reg, port_reg, mask;
static int trace = 0;
static uint32_t boot_start;

/* Loads an Intel HEX file in flash, returns its start address */
static uint32_t load(avr_t *avr, const char *path) {
  uint32_t size = 0, start = 0;
  uint8_t *data = read_ihex_file(path, &size, &start);
  if(!data) {
    fprintf(stderr, "sim: can not read %s\n", path);
    exit(2);
  }
  memcpy(avr->flash + start, data, size);
  free(data);
  printf("sim: loaded %s at 0x%04X (%u bytes)\n", path, start, size);
  return start;
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

int main(int argc, char **argv) {
  const char *boot = NULL, *app = NULL, *up = NULL, *dump = NULL;
  int bus_pin = 12, marker = -1, use_pty = 1, opt;
  double max_seconds = 60, marker_after = 0;
  uint32_t target_frequency = 16000000;
  while((opt = getopt(argc, argv, "b:a:u:B:F:m:w:t:f:en")) != -1) {
    switch(opt) {
      case 'b': boot = optarg; break;
      case 'a': app = optarg; break;
      case 'u': up = optarg; break;
      case 'B': bus_pin = atoi(optarg); break;
      case 'F': target_frequency = atol(optarg); break;
      case 'm': marker = strtol(optarg, NULL, 0); break;
      case 'w': marker_after = atof(optarg); break;
      case 't': max_seconds = atof(optarg); break;
      case 'f': dump = optarg; break;
      case 'e': trace = 1; break;
      case 'n': use_pty = 0; break;
      default: return 2;
    }
  }
  if(!boot || !up) {
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

  target = avr_make_mcu_by_name(MCU);
  uploader = avr_make_mcu_by_name(MCU);
  if(!target || !uploader) return 2;
  avr_init(target);
  avr_init(uploader);
  target->frequency = target_frequency;
  uploader->frequency = UPLOADER_FREQUENCY;
  target->log = uploader->log = LOG_WARNING;

  boot_start = load(target, boot);
  if(app) load(target, app);
  load(uploader, up);

  /* BOOTRST fuse: reset vector in the bootloader section */
  target->reset_pc = boot_start;
  target->pc = boot_start;
  /* Power on reset flag */
  target->data[MCUSR_ADDR] = 1;

  if(use_pty) {
    uart_pty_init(uploader, &pty);
    uart_pty_connect(&pty, '0');
  }

  uint8_t mailbox[3] = {0, 0, 0};
  uint32_t last_pc = target->pc;
  int last_level = 0, contention_boot = 0, contention_app = 0;
  int last_marker = 0, result = 1;
  int in_boot = 1, contention_now = 0;
  printf("sim: target starts in the bootloader at 0x%04X\n", boot_start);

  for(;;) {
    /* Run the core that is behind in time */
    avr_t *avr = (us(target) <= us(uploader)) ? target : uploader;
    int state = avr_run(avr);
    if(state == cpu_Done || state == cpu_Crashed) {
      printf("sim: %s stopped (state %d)\n",
        avr == target ? "target" : "uploader", state);
      break;
    }

    if(avr == target) {
      uint32_t pc = target->pc;
      /* A reset is the only way into the bootloader entry point */
      if(pc == boot_start && last_pc != boot_start) {
        uint8_t *ram = target->data + RAMEND - 2;
        printf("sim: %.3fs target reset, MCUSR=0x%02X\n",
          us(target) / 1e6, target->data[MCUSR_ADDR]);
        /* Real hardware keeps SRAM content on a watchdog reset */
        if(memcmp(ram, mailbox, 3)) {
          printf("sim: restoring SRAM mailbox cleared by the simulator\n");
          memcpy(ram, mailbox, 3);
        }
      }
      if(pc < boot_start) memcpy(mailbox, target->data + RAMEND - 2, 3);
      if((pc < boot_start) == in_boot) {
        in_boot = !in_boot;
        printf("sim: %.3fs target running %s (pc 0x%04X)\n",
          us(target) / 1e6, in_boot ? "bootloader" : "application", pc);
      }
      last_pc = pc;
      int m = target->data[GPIOR1_ADDR];
      if(m != last_marker) {
        last_marker = m;
        printf("sim: %.3fs target GPIOR1 = 0x%02X\n", us(target) / 1e6, m);
        if(m == marker && us(target) >= marker_after * 1e6 && result) {
          result = 0;
          /* keep running a little so the uploader can answer the PC */
          max_seconds = us(target) / 1e6 + 0.3;
        }
      }
    }

    /* Wired bus */
    int level = drives_high(target) || drives_high(uploader);
    if(
      (drives_high(target) && drives_low(uploader)) ||
      (drives_high(uploader) && drives_low(target))
    ) {
      if(!contention_now) {
        if(in_boot) contention_boot++;
        else contention_app++;
        if(trace)
          fprintf(stderr, "%.2f contention start %c\n", us(avr),
            drives_high(target) ? 'T' : 'U');
      }
      contention_now = 1;
    } else contention_now = 0;
    set_input(target, level);
    set_input(uploader, level);
    if(trace && level != last_level)
      fprintf(stderr, "%.2f %d %c%c\n", us(avr), level,
        drives_high(target) ? 'T' : '-', drives_high(uploader) ? 'U' : '-');
    static int last_udir = 0;
    int udir = (uploader->data[ddr_reg] & mask) ? 1 : 0;
    if(trace && udir != last_udir)
      fprintf(stderr, "%.2f uploader pin %s\n", us(uploader), udir ? "output" : "input");
    last_udir = udir;
    last_level = level;

    if(us(avr) > max_seconds * 1e6) {
      if(result) printf("sim: maximum simulated time reached\n");
      break;
    }
  }

  printf("sim: simulated %.3fs, bus contention events: %d while the target "
    "runs the bootloader, %d while it runs the application\n",
    us(target) / 1e6, contention_boot, contention_app);
  if(dump) {
    FILE *f = fopen(dump, "wb");
    if(f) {
      fwrite(target->flash, 1, boot_start, f);
      fclose(f);
    }
  }
  printf("sim: %s\n", result ? "FAIL" : "PASS");
  fflush(stdout);
  _exit(result); /* the pty thread is not stopped, it can block */
}
