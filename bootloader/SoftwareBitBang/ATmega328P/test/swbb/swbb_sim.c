/* Simulation of two ATmega328P running PJON SoftwareBitBang sketches on a
   single wire bus (used to test SoftwareBitBang itself, see run_swbb_tests.sh)

   Usage: swbb_sim -a nodeA.hex -b nodeB.hex [-c nodeC.hex] [options]
     -c hex      third node (C) on the same bus
     -G hz       node C clock frequency (default 16000000), e.g. 16008000 for
                 +500ppm: devices never have exactly the same clock
     -B pin      bus pin, Arduino numbering (default 2)
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
     -S          print the lowest stack pointer of each node at the end (with
                 __heap_start from avr-nm: free stack = min SP - heap start)
   The USART0 output of each node is printed prefixed by "A: ", "B: ", "C: ".
   The simulation ends when all nodes printed a line starting with "DONE"
   or when the maximum time is reached. Exit code 0 if all printed DONE. */

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

#define MCU "atmega328p"

#define NMAX 3
static int nodes = 2;
static avr_t *node[NMAX];
static int pin_reg, ddr_reg, port_reg, mask, bus_bit;
static char bus_port;
static avr_irq_t *pin_irq[NMAX];

/* Sets the bus level seen by a node: through the port pin IRQ, so that pin
   change and external interrupts (INT0/INT1) are triggered */
static void set_level(int n, int level) {
  avr_t *a = node[n];
  if(pin_irq[n]->value != (uint32_t)level) avr_raise_irq(pin_irq[n], level);
  if(level) a->data[pin_reg] |= mask;
  else a->data[pin_reg] &= ~mask;
}
static char line[NMAX][256];
static int line_len[NMAX], done[NMAX], stamp = 0;

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

static int drives_high(avr_t *a) {
  return (a->data[ddr_reg] & mask) && (a->data[port_reg] & mask);
}
static int drives_low(avr_t *a) {
  return (a->data[ddr_reg] & mask) && !(a->data[port_reg] & mask);
}

/* Histogram of pulse durations (0.5us buckets) per driver and level */
#define HMAX 600
static uint32_t hist[NMAX + 1][2][HMAX];

int main(int argc, char **argv) {
  const char *hex[NMAX] = {NULL, NULL, NULL};
  int bus_pin = 2, trace = 0, histo = 0, stack = 0, opt;
  uint16_t min_sp[NMAX] = {0xFFFF, 0xFFFF, 0xFFFF};
  uint16_t isr_depth[NMAX] = {0, 0, 0};
  long b_reset = -1, b_frequency = 16000000, c_frequency = 16000000;
  double max_seconds = 10;
  double cap_pf = 0, r_series = 100, r_pull = 100000;
  while((opt = getopt(argc, argv, "a:b:c:B:t:ehr:F:G:TSC:R:P:")) != -1) {
    switch(opt) {
      case 'a': hex[0] = optarg; break;
      case 'b': hex[1] = optarg; break;
      case 'c': hex[2] = optarg; nodes = 3; break;
      case 'B': bus_pin = atoi(optarg); break;
      case 't': max_seconds = atof(optarg); break;
      case 'e': trace = 1; break;
      case 'T': stamp = 1; break;
      case 'S': stack = 1; break;
      case 'h': histo = 1; break;
      case 'r': b_reset = strtol(optarg, NULL, 0); break;
      case 'F': b_frequency = atol(optarg); break;
      case 'G': c_frequency = atol(optarg); break;
      case 'C': cap_pf = atof(optarg); break;
      case 'R': r_series = atof(optarg); break;
      case 'P': r_pull = atof(optarg); break;
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

  for(int n = 0; n < nodes; n++) {
    node[n] = avr_make_mcu_by_name(MCU);
    avr_init(node[n]);
    node[n]->frequency = 16000000;
    node[n]->log = LOG_WARNING;
    load(node[n], hex[n]);
    if(n == 2) node[n]->frequency = c_frequency;
    if(n == 1) {
      node[n]->frequency = b_frequency;
      if(b_reset >= 0) {
        node[n]->reset_pc = node[n]->pc = b_reset;
        done[1] = 1;
      }
    }
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

  int last_level = 0, last_driver = NMAX, contention = 0, contention_now = 0;
  double last_edge = 0;
  for(;;) {
    avr_t *avr = node[0];
    for(int n = 1; n < nodes; n++) if(us(node[n]) < us(avr)) avr = node[n];
    int state = avr_run(avr);
    if(stack) {
      int n = 0;
      while(node[n] != avr) n++;
      uint16_t sp = avr->data[0x5D] | (avr->data[0x5E] << 8);
      if(sp >= 0x100 && sp < min_sp[n] && avr->cycle > 1000) min_sp[n] = sp;
      /* Stack used by the INT0 interrupt (vector at 0x0004), including the
         interrupts nested in it: from its entry (return address pushed)
         to the lowest stack pointer before it returns */
      static int in_isr[NMAX];
      static uint16_t entry_sp[NMAX], isr_min[NMAX];
      if(!in_isr[n] && avr->pc == 0x0004) {
        in_isr[n] = 1;
        entry_sp[n] = isr_min[n] = sp;
      } else if(in_isr[n]) {
        if(sp < isr_min[n]) isr_min[n] = sp;
        if(sp > entry_sp[n]) {
          in_isr[n] = 0;
          uint16_t d = entry_sp[n] - isr_min[n] + 2;
          if(d > isr_depth[n]) isr_depth[n] = d;
        }
      }
    }
    if(state == cpu_Done || state == cpu_Crashed) {
      int n = 0;
      while(node[n] != avr) n++;
      printf("sim: node %c stopped (state %d)\n", 'A' + n, state);
      break;
    }
    int high[NMAX], low[NMAX], any_high = 0, any_low = 0, first_high = -1;
    for(int n = 0; n < nodes; n++) {
      high[n] = drives_high(node[n]);
      low[n] = drives_low(node[n]);
      if(high[n] && first_high < 0) first_high = n;
      any_high |= high[n];
      any_low |= low[n];
    }
    int level = any_high;
    int seen[NMAX];
    for(int n = 0; n < nodes; n++) seen[n] = level;
    if(cap_pf > 0) {
      /* Analog bus: drivers (25 ohm output + series resistor) and the
         pull-down charge the bus capacitance, integrated exactly over the
         time elapsed since the last step */
      static double v = 0, t_last = 0;
      static int state_seen[NMAX] = {0, 0, 0};
      double now = us(avr), dt = (now - t_last) * 1e-6;
      t_last = now;
      double g = 1.0 / r_pull, i = 0, gd = 1.0 / (r_series + 25);
      for(int n = 0; n < nodes; n++) {
        if(high[n]) { g += gd; i += gd * 5.0; }
        else if(low[n]) g += gd;
      }
      double v_end = i / g, tau = cap_pf * 1e-12 / g;
      if(dt > 0) v = v_end + (v - v_end) * exp(-dt / tau);
      for(int n = 0; n < nodes; n++) {
        if(!state_seen[n] && v > 2.7) state_seen[n] = 1;
        else if(state_seen[n] && v < 2.2) state_seen[n] = 0;
        seen[n] = state_seen[n];
      }
      level = seen[0];
    }
    if(any_high && any_low) {
      if(!contention_now) {
        contention++;
        if(trace) printf("%.2f contention\n", us(avr));
      }
      contention_now = 1;
    } else contention_now = 0;
    for(int n = 0; n < nodes; n++) set_level(n, seen[n]);
    if(level != last_level) {
      double now = us(avr);
      if(trace) {
        printf("%.2f %d ", now, level);
        for(int n = 0; n < nodes; n++) putchar(high[n] ? 'A' + n : '-');
        putchar('\n');
      }
      int b = (int)((now - last_edge) * 2 + 0.5);
      if(b < HMAX) hist[last_driver][last_level][b]++;
      last_edge = now;
      last_driver = level ? (first_high >= 0 ? first_high : NMAX) : last_driver;
      last_level = level;
    }
    /* Debug pin of node B (PORTD bit 3): printed in the trace */
    static int last_dbg = 0;
    int dbg = (node[1]->data[0x2B] >> 3) & 1;
    if(trace && dbg != last_dbg)
      printf("%.2f dbg %d\n", us(node[1]), dbg);
    last_dbg = dbg;
    int all_done = 1;
    for(int n = 0; n < nodes; n++) all_done &= done[n];
    if(all_done) break;
    if(us(avr) > max_seconds * 1e6) {
      printf("sim: maximum simulated time reached\n");
      break;
    }
  }
  if(histo) {
    for(int d = 0; d < nodes; d++)
      for(int l = 1; l >= 0; l--) {
        printf("sim: %s pulses driven by %c:", l ? "high" : "low", 'A' + d);
        for(int b = 0; b < HMAX; b++)
          if(hist[d][l][b] > 2) printf(" %.1f(%u)", b / 2.0, hist[d][l][b]);
        printf("\n");
      }
  }
  if(stack)
    for(int n = 0; n < nodes; n++)
      printf("sim: %c lowest stack pointer 0x%04X INT0 stack %u\n", 'A' + n,
        min_sp[n], isr_depth[n]);
  printf("sim: simulated %.3fs, bus contention events: %d\n",
    us(node[0]) / 1e6, contention);
  int all_done = 1;
  for(int n = 0; n < nodes; n++) all_done &= done[n];
  _exit(all_done ? 0 : 1);
}
