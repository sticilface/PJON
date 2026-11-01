/* Simulation of two ATmega328P running PJON SoftwareBitBang sketches on a
   single wire bus (used to test SoftwareBitBang itself, see run_swbb_tests.sh)

   Usage: swbb_sim -a nodeA.hex -b nodeB.hex [options]
     -B pin      bus pin, Arduino numbering (default 2)
     -t seconds  maximum simulated time (default 10)
     -e          print every bus level change (microseconds, who drives)
     -h          print a histogram of high/low durations per driver
   The USART0 output of each node is printed prefixed by "A: " or "B: ".
   The simulation ends when both nodes printed a line starting with "DONE"
   or when the maximum time is reached. Exit code 0 if both printed DONE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_hex.h>
#include <simavr/sim_irq.h>
#include <simavr/avr_uart.h>
#include <simavr/avr_ioport.h>

#define MCU "atmega328p"

static avr_t *node[2];
static int pin_reg, ddr_reg, port_reg, mask, bus_bit;
static char bus_port;
static avr_irq_t *pin_irq[2];

/* Sets the bus level seen by a node: through the port pin IRQ, so that pin
   change and external interrupts (INT0/INT1) are triggered */
static void set_level(int n, int level) {
  avr_t *a = node[n];
  if(pin_irq[n]->value != (uint32_t)level) avr_raise_irq(pin_irq[n], level);
  if(level) a->data[pin_reg] |= mask;
  else a->data[pin_reg] &= ~mask;
}
static char line[2][256];
static int line_len[2], done[2];

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
    printf("%c: %s\n", 'A' + n, line[n]);
    if(!strncmp(line[n], "DONE", 4)) done[n] = 1;
    line_len[n] = 0;
    return;
  }
  line[n][line_len[n]++] = c;
}

static int drives_high(avr_t *a) {
  return (a->data[ddr_reg] & mask) && (a->data[port_reg] & mask);
}
static int drives_low(avr_t *a) {
  return (a->data[ddr_reg] & mask) && !(a->data[port_reg] & mask);
}

/* Histogram of pulse durations (0.5us buckets) per driver and level */
#define HMAX 600
static uint32_t hist[3][2][HMAX];

int main(int argc, char **argv) {
  const char *hex[2] = {NULL, NULL};
  int bus_pin = 2, trace = 0, histo = 0, opt;
  double max_seconds = 10;
  while((opt = getopt(argc, argv, "a:b:B:t:eh")) != -1) {
    switch(opt) {
      case 'a': hex[0] = optarg; break;
      case 'b': hex[1] = optarg; break;
      case 'B': bus_pin = atoi(optarg); break;
      case 't': max_seconds = atof(optarg); break;
      case 'e': trace = 1; break;
      case 'h': histo = 1; break;
      default: return 2;
    }
  }
  if(!hex[0] || !hex[1]) return 2;
  setvbuf(stdout, NULL, _IONBF, 0);
  if(bus_pin <= 7) { pin_reg = 0x29; bus_bit = bus_pin; bus_port = 'D'; }
  else if(bus_pin <= 13) { pin_reg = 0x23; bus_bit = bus_pin - 8; bus_port = 'B'; }
  else { pin_reg = 0x26; bus_bit = bus_pin - 14; bus_port = 'C'; }
  mask = 1 << bus_bit;
  ddr_reg = pin_reg + 1;
  port_reg = pin_reg + 2;

  for(int n = 0; n < 2; n++) {
    node[n] = avr_make_mcu_by_name(MCU);
    avr_init(node[n]);
    node[n]->frequency = 16000000;
    node[n]->log = LOG_WARNING;
    load(node[n], hex[n]);
    pin_irq[n] = avr_io_getirq(node[n], AVR_IOCTL_IOPORT_GETIRQ(bus_port), bus_bit);
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
    if((ha && drives_low(node[1])) || (hb && drives_low(node[0]))) {
      if(!contention_now) {
        contention++;
        if(trace) printf("%.2f contention\n", us(avr));
      }
      contention_now = 1;
    } else contention_now = 0;
    set_level(0, level);
    set_level(1, level);
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
