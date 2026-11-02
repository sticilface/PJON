/* Simulation of two or three ATmega328P (or ATmega2560, see -M) running PJON
   SoftwareBitBang sketches on a single wire bus (used to test SoftwareBitBang
   itself, see run_swbb_tests.sh)

   Usage: swbb_sim -a nodeA.hex -b nodeB.hex [-c nodeC.hex] [options]
     -c hex      third node (C) on the same bus
     -G hz       node C clock frequency (default 16000000), e.g. 16008000 for
                 +500ppm: devices never have exactly the same clock
     -B pin[,pin[,pin]] bus pin of A[,B[,C]], Arduino numbering of each MCU
                 (default 2, a single value applies to every node)
     -M mcu[,mcu[,mcu]] MCU of A[,B[,C]]: atmega328p (default) or atmega2560
     -t seconds  maximum simulated time (default 10)
     -e          print every bus level change (microseconds, who drives)
     -C pF       analog bus model: total bus capacitance (cable + pins), the
                 bus is then a single RC node instead of an ideal wire
     -R ohm      series resistor of each device (default 100, with -C)
     -P ohm      pull-down resistor of the bus (default 100000, with -C)
     -N rate:nC[:us]  noise (with -C): charge injections coupled into the
                 bus (e.g. edges in a neighbouring conductor), Poisson with
                 `rate` per second, each `nC` nanocoulomb of random sign
                 spread over `us` microseconds (default 0.5): on an undriven
                 bus a jump of Q/C decaying with R(pull-down) x C
     -H uA       noise (with -C): 50Hz hum current of this amplitude (an
                 undriven bus sees ~ uA x R(pull-down))
     -O v[,v[,v]] ground offset of A[,B[,C]] in volts (the device sees the
                 bus voltage plus its offset)
     -s seed     seed of the noise generator (default 1)
                 The level seen by each device has the ATmega328P input
                 hysteresis (rising 2.7V, falling 2.2V at 5V).
     -h          print a histogram of high/low durations per driver
     -r address  node B starts at this address (a bootloader), it is not
                 expected to print DONE
     -F hz       node B clock frequency (default 16000000)
     -T          append the simulated time to the lines printed by the nodes
     -S          print the lowest stack pointer of each node at the end (with
                 __heap_start from avr-nm: free stack = min SP - heap start)
                 and the stack used by the wake interrupt (INTx of the bus
                 pin, nested interrupts included)
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

#define NMAX 3
static int nodes = 2;
static avr_t *node[NMAX];
/* Per node bus pin: data space addresses of PINx (DDRx, PORTx follow) */
static int pin_reg[NMAX], ddr_reg[NMAX], port_reg[NMAX], mask[NMAX];
static int bus_bit[NMAX];
static char bus_port[NMAX];
static avr_irq_t *pin_irq[NMAX];

/* Byte address of the external interrupt vector of a bus pin (0 if none):
   ATmega328P INT0/INT1 on pins 2/3, ATmega2560 INT4/INT5 on pins 2/3 and
   INT3..INT0 on pins 18..21 (4 bytes per vector) */
static int wake_vector(const char *mcu, int pin) {
  if(!strcmp(mcu, "atmega2560")) {
    if(pin == 2) return 5 * 4;
    if(pin == 3) return 6 * 4;
    if(pin >= 18 && pin <= 21) return (4 - (pin - 18)) * 4;
    return 0;
  }
  if(pin == 2) return 1 * 4;
  if(pin == 3) return 2 * 4;
  return 0;
}

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
static char line[NMAX][256];
static int line_len[NMAX], done[NMAX], stamp = 0;

static void load(avr_t *avr, const char *path) {
  /* every chunk: a bootloader with the split layout has two */
  ihex_chunk_p chunks = NULL;
  int count = read_ihex_chunks(path, &chunks);
  if(count <= 0) { fprintf(stderr, "can not read %s\n", path); exit(2); }
  for(int i = 0; i < count; i++)
    memcpy(avr->flash + chunks[i].baseaddr, chunks[i].data, chunks[i].size);
  free_ihex_chunks(chunks);
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

static int node_index(avr_t *a) {
  int n = 0;
  while(n < nodes - 1 && node[n] != a) n++;
  return n;
}
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
static uint32_t hist[NMAX + 1][2][HMAX];
static uint32_t noise_events = 0, noise_edges[NMAX];

int main(int argc, char **argv) {
  const char *hex[NMAX] = {NULL, NULL, NULL};
  int bus_pin[NMAX] = {2, 2, 2}, trace = 0, histo = 0, stack = 0, opt;
  char mcu[NMAX][16] = {"atmega328p", "atmega328p", "atmega328p"};
  uint16_t min_sp[NMAX] = {0xFFFF, 0xFFFF, 0xFFFF};
  uint16_t isr_depth[NMAX] = {0, 0, 0};
  long b_reset = -1, b_frequency = 16000000, c_frequency = 16000000;
  double max_seconds = 10;
  double cap_pf = 0, r_series = 100, r_pull = 100000;
  double noise_rate = 0, noise_q = 0, noise_w = 0.5, hum_ua = 0;
  double offset[NMAX] = {0, 0, 0};
  unsigned long seed = 1;
  while((opt = getopt(argc, argv, "a:b:c:B:M:t:ehr:F:G:TSC:R:P:N:H:O:s:")) != -1) {
    switch(opt) {
      case 'a': hex[0] = optarg; break;
      case 'b': hex[1] = optarg; break;
      case 'c': hex[2] = optarg; nodes = 3; break;
      case 'B': { /* pin[,pin[,pin]]: missing values repeat the last one */
        char *p = optarg;
        for(int n = 0; n < NMAX; n++) {
          bus_pin[n] = atoi(p);
          char *c = strchr(p, ',');
          if(c) p = c + 1;
        }
        break;
      }
      case 'M': { /* mcu[,mcu[,mcu]]: missing values repeat the last one */
        char *p = optarg;
        for(int n = 0; n < NMAX; n++) {
          char *c = strchr(p, ',');
          snprintf(mcu[n], 16, "%.*s", c ? (int)(c - p) : 15, p);
          if(c) p = c + 1;
        }
        break;
      }
      case 'S': stack = 1; break;
      case 'G': c_frequency = atol(optarg); break;
      case 't': max_seconds = atof(optarg); break;
      case 'e': trace = 1; break;
      case 'T': stamp = 1; break;
      case 'h': histo = 1; break;
      case 'r': b_reset = strtol(optarg, NULL, 0); break;
      case 'F': b_frequency = atol(optarg); break;
      case 'C': cap_pf = atof(optarg); break;
      case 'R': r_series = atof(optarg); break;
      case 'P': r_pull = atof(optarg); break;
      case 'N': {
        noise_rate = atof(optarg);
        char *c = strchr(optarg, ':');
        if(c) {
          noise_q = atof(c + 1);
          c = strchr(c + 1, ':');
          if(c) noise_w = atof(c + 1);
        }
        break;
      }
      case 'H': hum_ua = atof(optarg); break;
      case 'O': {
        char *p = optarg;
        for(int n = 0; n < NMAX; n++) {
          offset[n] = atof(p);
          char *c = strchr(p, ',');
          if(c) p = c + 1;
        }
        break;
      }
      case 's': seed = strtoul(optarg, NULL, 0); break;
      default: return 2;
    }
  }
  if(!hex[0] || !hex[1]) return 2;
  setvbuf(stdout, NULL, _IONBF, 0);
  for(int n = 0; n < nodes; n++) {
    if(map_pin(mcu[n], bus_pin[n], &bus_port[n], &bus_bit[n])) {
      fprintf(stderr, "unknown pin %d on %s\n", bus_pin[n], mcu[n]);
      return 2;
    }
    pin_reg[n] = pin_address(bus_port[n]);
    mask[n] = 1 << bus_bit[n];
    ddr_reg[n] = pin_reg[n] + 1;
    port_reg[n] = pin_reg[n] + 2;
  }

  for(int n = 0; n < nodes; n++) {
    node[n] = avr_make_mcu_by_name(mcu[n]);
    if(!node[n]) { fprintf(stderr, "unknown MCU %s\n", mcu[n]); return 2; }
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

  int last_level = 0, last_driver = NMAX, contention = 0, contention_now = 0;
  double last_edge = 0;
  for(;;) {
    avr_t *avr = node[0];
    for(int n = 1; n < nodes; n++) if(us(node[n]) < us(avr)) avr = node[n];
    int state = avr_run(avr);
    if(stack) {
      int n = node_index(avr);
      uint16_t sp = avr->data[0x5D] | (avr->data[0x5E] << 8);
      if(sp >= 0x100 && sp < min_sp[n] && avr->cycle > 1000) min_sp[n] = sp;
      /* Stack used by the wake interrupt (vector of the bus pin), including
         the interrupts nested in it: from its entry (return address pushed)
         to the lowest stack pointer before it returns */
      static int in_isr[NMAX];
      static uint16_t entry_sp[NMAX], isr_min[NMAX];
      int vector = wake_vector(mcu[n], bus_pin[n]);
      if(!in_isr[n] && vector && avr->pc == (uint32_t)vector) {
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
      printf("sim: node %c stopped (state %d)\n", 'A' + node_index(avr), state);
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
      static double next_noise = -1, noise_end = -1, noise_i = 0;
      double now = us(avr), dt = (now - t_last) * 1e-6;
      t_last = now;
      double g = 1.0 / r_pull, i = 0, gd = 1.0 / (r_series + 25);
      for(int n = 0; n < nodes; n++) {
        if(high[n]) { g += gd; i += gd * 5.0; }
        else if(low[n]) g += gd;
      }
      /* Noise current sources */
      if(noise_rate > 0) {
        if(next_noise < 0) {
          srand48(seed);
          next_noise = now - log(1 - drand48()) * 1e6 / noise_rate;
        }
        if(now >= next_noise) {
          noise_i = (drand48() < 0.5 ? -1 : 1) * noise_q * 1e-9 /
            (noise_w * 1e-6);
          noise_end = now + noise_w;
          next_noise = now - log(1 - drand48()) * 1e6 / noise_rate;
          noise_events++;
        }
        if(now < noise_end) i += noise_i;
      }
      if(hum_ua > 0) i += hum_ua * 1e-6 * sin(2 * M_PI * 50 * now * 1e-6);
      double v_end = i / g, tau = cap_pf * 1e-12 / g;
      if(dt > 0) v = v_end + (v - v_end) * exp(-dt / tau);
      /* Input protection diodes and drivers clamp the bus */
      if(v > 5.6) v = 5.6;
      if(v < -0.6) v = -0.6;
      int driven = any_high || any_low;
      for(int n = 0; n < nodes; n++) {
        double vn = v + offset[n];
        int before = state_seen[n];
        if(!state_seen[n] && vn > 2.7) state_seen[n] = 1;
        else if(state_seen[n] && vn < 2.2) state_seen[n] = 0;
        if(!driven && state_seen[n] != before) noise_edges[n]++;
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
  if(noise_rate > 0 || hum_ua > 0) {
    printf("sim: noise injections %u, level changes seen on the undriven bus:",
      noise_events);
    for(int n = 0; n < nodes; n++) printf(" %c %u", 'A' + n, noise_edges[n]);
    printf("\n");
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
