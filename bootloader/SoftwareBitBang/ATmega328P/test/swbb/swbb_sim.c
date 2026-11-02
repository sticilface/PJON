/* Simulation of two ATmega328P (or ATmega2560, see -M) running PJON SoftwareBitBang sketches on a
   single wire bus (used to test SoftwareBitBang itself, see run_swbb_tests.sh)

   Usage: swbb_sim -a nodeA.hex -b nodeB.hex [options]
     -B pin[,pin] bus pin of A[,B], Arduino numbering of each MCU (default 2)
     -M mcu[,mcu] MCU of A[,B]: atmega328p (default) or atmega2560
     -t seconds  maximum simulated time (default 10)
     -e          print every bus level change (microseconds, who drives)
     -C pF       analog bus model: total bus capacitance (cable + pins), the
                 bus is then a single RC node instead of an ideal wire
     -R ohm      series resistor of each device (default 100, with -C)
     -P ohm      pull-down resistor of the bus (default 100000, with -C)
                 The level seen by each device has the ATmega328P input
                 hysteresis (rising 2.7V, falling 2.2V at 5V).
     -h          print a histogram of high/low durations per driver
     -r address  node B starts at this address (a bootloader), it is not
                 expected to print DONE
     -F hz       node B clock frequency (default 16000000)
     -T          append the simulated time to the lines printed by the nodes
   The USART0 output of each node is printed prefixed by "A: " or "B: ".
   The simulation ends when both nodes printed a line starting with "DONE"
   or when the maximum time is reached. Exit code 0 if both printed DONE. */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_hex.h>
#include <simavr/sim_irq.h>
#include <simavr/avr_uart.h>
#include <simavr/avr_ioport.h>

static avr_t *node[2];
/* Per node bus pin: data space addresses of PINx (DDRx, PORTx follow) */
static int pin_reg[2], ddr_reg[2], port_reg[2], mask[2], bus_bit[2];
static char bus_port[2];
static avr_irq_t *pin_irq[2];

/* Arduino pin number -> port letter and bit (-1 if unknown) */
static int map_pin(const char *mcu, int pin, char *port, int *bit) {
  if(!strcmp(mcu, "atmega2560")) {
    /* Arduino Mega variant (pins_arduino.h of the Arduino AVR core) */
    static const char ports[] =
      "EEEEGEHHHHBBBBJJHHDDDDAAAAAAAACCCCCCCCDGGGLLLLLLLLBBBBFFFFFFFFKKKKKKKK";
    static const int bits[] = {
      0,1,4,5,5,3,3,4,5,6,4,5,6,7,1,0,1,0,3,2,1,0,0,1,2,3,4,5,6,7,
      7,6,5,4,3,2,1,0,7,2,1,0,7,6,5,4,3,2,1,0,3,2,1,0,
      0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7};
    if(pin < 0 || pin > 69) return -1;
    *port = ports[pin]; *bit = bits[pin]; return 0;
  }
  if(pin < 0 || pin > 19) return -1;
  if(pin <= 7) { *port = 'D'; *bit = pin; }
  else if(pin <= 13) { *port = 'B'; *bit = pin - 8; }
  else { *port = 'C'; *bit = pin - 14; }
  return 0;
}

static int pin_address(char port) {
  static const char order[] = "ABCDEFG";
  const char *p = strchr(order, port);
  if(p) return 0x20 + 3 * (int)(p - order);
  return 0x100 + 3 * (int)(strchr("HJKL", port) - "HJKL");
}

/* Sets the bus level seen by a node: through the port pin IRQ, so that pin
   change and external interrupts (INT0/INT1) are triggered */
static void set_level(int n, int level) {
  avr_t *a = node[n];
  if(pin_irq[n]->value != (uint32_t)level) avr_raise_irq(pin_irq[n], level);
  if(level) a->data[pin_reg[n]] |= mask[n];
  else a->data[pin_reg[n]] &= ~mask[n];
}
static char line[2][256];
static int line_len[2], done[2], stamp = 0;

static void load(avr_t *avr, const char *path) {
  uint32_t size = 0, start = 0;
  uint8_t *data = read_ihex_file(path, &size, &start);
  if(!data) { fprintf(stderr, "can not read %s\n", path); exit(2); }
  memcpy(avr->flash + start, data, size);
  free(data);
}

static double us(avr_t *avr) {
  return avr->cycle / (avr->frequency / 1000000.0);
}

static void uart_out(struct avr_irq_t *irq, uint32_t value, void *param) {
  (void)irq;
  int n = (int)(intptr_t)param;
  char c = (char)value;
  if(c == '\r') return;
  if(c == '\n' || line_len[n] == 255) {
    line[n][line_len[n]] = 0;
    if(stamp) printf("%c: %s [sim %.3f ms]\n", 'A' + n, line[n],
      node[n]->cycle / (node[n]->frequency / 1000.0));
    else printf("%c: %s\n", 'A' + n, line[n]);
    if(!strncmp(line[n], "DONE", 4)) done[n] = 1;
    line_len[n] = 0;
    return;
  }
  line[n][line_len[n]++] = c;
}

static int node_index(avr_t *a) { return a == node[1]; }
static int drives_high(avr_t *a) {
  int n = node_index(a);
  return (a->data[ddr_reg[n]] & mask[n]) && (a->data[port_reg[n]] & mask[n]);
}
static int drives_low(avr_t *a) {
  int n = node_index(a);
  return (a->data[ddr_reg[n]] & mask[n]) && !(a->data[port_reg[n]] & mask[n]);
}

/* Histogram of pulse durations (0.5us buckets) per driver and level */
#define HMAX 600
static uint32_t hist[3][2][HMAX];

int main(int argc, char **argv) {
  const char *hex[2] = {NULL, NULL};
  int bus_pin[2] = {2, 2}, trace = 0, histo = 0, opt;
  char mcu[2][16] = {"atmega328p", "atmega328p"};
  long b_reset = -1, b_frequency = 16000000;
  double max_seconds = 10;
  double cap_pf = 0, r_series = 100, r_pull = 100000;
  while((opt = getopt(argc, argv, "a:b:B:M:t:ehr:F:TC:R:P:")) != -1) {
    switch(opt) {
      case 'a': hex[0] = optarg; break;
      case 'b': hex[1] = optarg; break;
      case 'B': {
        char *c = strchr(optarg, ',');
        bus_pin[0] = bus_pin[1] = atoi(optarg);
        if(c) bus_pin[1] = atoi(c + 1);
        break;
      }
      case 'M': {
        char *c = strchr(optarg, ',');
        snprintf(mcu[0], 16, "%.*s", c ? (int)(c - optarg) : 15, optarg);
        snprintf(mcu[1], 16, "%s", c ? c + 1 : mcu[0]);
        break;
      }
      case 't': max_seconds = atof(optarg); break;
      case 'e': trace = 1; break;
      case 'T': stamp = 1; break;
      case 'h': histo = 1; break;
      case 'r': b_reset = strtol(optarg, NULL, 0); break;
      case 'F': b_frequency = atol(optarg); break;
      case 'C': cap_pf = atof(optarg); break;
      case 'R': r_series = atof(optarg); break;
      case 'P': r_pull = atof(optarg); break;
      default: return 2;
    }
  }
  if(!hex[0] || !hex[1]) return 2;
  setvbuf(stdout, NULL, _IONBF, 0);
  for(int n = 0; n < 2; n++) {
    if(map_pin(mcu[n], bus_pin[n], &bus_port[n], &bus_bit[n])) {
      fprintf(stderr, "unknown pin %d on %s\n", bus_pin[n], mcu[n]);
      return 2;
    }
    pin_reg[n] = pin_address(bus_port[n]);
    mask[n] = 1 << bus_bit[n];
    ddr_reg[n] = pin_reg[n] + 1;
    port_reg[n] = pin_reg[n] + 2;
  }

  for(int n = 0; n < 2; n++) {
    node[n] = avr_make_mcu_by_name(mcu[n]);
    if(!node[n]) { fprintf(stderr, "unknown MCU %s\n", mcu[n]); return 2; }
    avr_init(node[n]);
    node[n]->frequency = 16000000;
    node[n]->log = LOG_WARNING;
    load(node[n], hex[n]);
    if(n == 1) {
      node[n]->frequency = b_frequency;
      if(b_reset >= 0) {
        node[n]->reset_pc = node[n]->pc = b_reset;
        done[1] = 1;
      }
    }
    pin_irq[n] = avr_io_getirq(node[n], AVR_IOCTL_IOPORT_GETIRQ(bus_port[n]), bus_bit[n]);
    avr_irq_t *irq = avr_io_getirq(node[n], AVR_IOCTL_UART_GETIRQ('0'),
      UART_IRQ_OUTPUT);
    avr_irq_register_notify(irq, uart_out, (void *)(intptr_t)n);
    /* Do not wait for the transmission of each character */
    uint32_t f = 0;
    avr_ioctl(node[n], AVR_IOCTL_UART_GET_FLAGS('0'), &f);
    f &= ~(AVR_UART_FLAG_POLL_SLEEP | AVR_UART_FLAG_STDIO);
    avr_ioctl(node[n], AVR_IOCTL_UART_SET_FLAGS('0'), &f);
  }

  int last_level = 0, last_driver = 2, contention = 0, contention_now = 0;
  double last_edge = 0;
  for(;;) {
    avr_t *avr = (us(node[0]) <= us(node[1])) ? node[0] : node[1];
    int state = avr_run(avr);
    if(state == cpu_Done || state == cpu_Crashed) {
      printf("sim: node %c stopped (state %d)\n", avr == node[0] ? 'A':'B', state);
      break;
    }
    int ha = drives_high(node[0]), hb = drives_high(node[1]);
    int level = ha || hb;
    if(cap_pf > 0) {
      /* Analog bus: drivers (25 ohm output + series resistor) and the
         pull-down charge the bus capacitance, integrated exactly over the
         time elapsed since the last step */
      static double v = 0, t_last = 0;
      static int seen[2] = {0, 0};
      double now = us(avr), dt = (now - t_last) * 1e-6;
      t_last = now;
      double g = 1.0 / r_pull, i = 0, gd = 1.0 / (r_series + 25);
      for(int n = 0; n < 2; n++) {
        if(drives_high(node[n])) { g += gd; i += gd * 5.0; }
        else if(drives_low(node[n])) g += gd;
      }
      double v_end = i / g, tau = cap_pf * 1e-12 / g;
      if(dt > 0) v = v_end + (v - v_end) * exp(-dt / tau);
      for(int n = 0; n < 2; n++) {
        if(!seen[n] && v > 2.7) seen[n] = 1;
        else if(seen[n] && v < 2.2) seen[n] = 0;
      }
      set_level(1, seen[1]);
      level = seen[0];
    }
    if((ha && drives_low(node[1])) || (hb && drives_low(node[0]))) {
      if(!contention_now) {
        contention++;
        if(trace) printf("%.2f contention\n", us(avr));
      }
      contention_now = 1;
    } else contention_now = 0;
    set_level(0, level);
    if(cap_pf <= 0) set_level(1, level);
    if(level != last_level) {
      double now = us(avr);
      if(trace) printf("%.2f %d %c%c\n", now, level, ha ? 'A' : '-', hb ? 'B' : '-');
      int b = (int)((now - last_edge) * 2 + 0.5);
      if(b < HMAX) hist[last_driver][last_level][b]++;
      last_edge = now;
      last_driver = level ? (ha ? 0 : 1) : last_driver;
      last_level = level;
    }
    /* Debug pin of node B (PORTD bit 3): printed in the trace */
    static int last_dbg = 0;
    int dbg = (node[1]->data[0x2B] >> 3) & 1;
    if(trace && dbg != last_dbg)
      printf("%.2f dbg %d\n", us(node[1]), dbg);
    last_dbg = dbg;
    if(done[0] && done[1]) break;
    if(us(avr) > max_seconds * 1e6) {
      printf("sim: maximum simulated time reached\n");
      break;
    }
  }
  if(histo) {
    for(int d = 0; d < 2; d++)
      for(int l = 1; l >= 0; l--) {
        printf("sim: %s pulses driven by %c:", l ? "high" : "low", 'A' + d);
        for(int b = 0; b < HMAX; b++)
          if(hist[d][l][b] > 2) printf(" %.1f(%u)", b / 2.0, hist[d][l][b]);
        printf("\n");
      }
  }
  printf("sim: simulated %.3fs, bus contention events: %d\n",
    us(node[0]) / 1e6, contention);
  _exit((done[0] && done[1]) ? 0 : 1);
}
