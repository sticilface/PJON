## SoftwareBitBang

| Medium | Pins used | Inclusion  |
|--------|-----------|--------------------|
| Wire   | 1 or 2    | `#include <PJONSoftwareBitBang.h>`|

`SoftwareBitBang` is a software implementation of [PJDL (Padded Jittering Data Link)](/src/strategies/SoftwareBitBang/specification/PJDL-specification-v5.0.md). It supports simplex and half-duplex asynchronous serial communication for up to 254 devices over a single wire. The maximum length of the bus can reach between 800 and 2000 meters depending on the mode used. It is a valid alternative to 1-Wire because of its flexibility and reliability. Fault tolerance schemes can be easily implemented because communication pins can be configured at runtime. Take a look at the [video introduction](https://www.youtube.com/watch?v=GWlhKD5lz5w) for a brief showcase of its features.
```cpp
PJDL SINGLE WIRE BUS                           ______
 ______    ______    ______    ______         |      |
|      |  |      |  |      |  |      |        |DEVICE|
|DEVICE|  |DEVICE|  |DEVICE|  |DEVICE|        |______|
|______|  |______|  |______|  |______|            |
___|__________|________|___________|______/\/\/\__| IO PIN
 ___|__    __|___    ___|__    ___|__  |  110-180 Ω
|      |  |      |  |      |  |      | |  
|DEVICE|  |DEVICE|  |DEVICE|  |DEVICE| |__/\/\/\__  GND
|______|  |______|  |______|  |______|    8 kΩ - 5 MΩ    
```
It is suggested to add 8kΩ-5MΩ pull-down resistor as shown in the graph above to reduce externally induced interference. The longer is the length of the cable and the higher is the amount of induced interference, the lower should be the resistance of the pull-down resistor. Pins can be optionally protected against overload adding a current limiting resistor to each connected pin. The resistor value can be obtained solving the following equation `R = (operating voltage / pin max current drain)`, for example to obtain the current limiting resistor value for an Arduino Uno simply substitute its characteristics: `R = (5v / 0.030A) = 166.66Ω`.

### Compatibility
| MCU              | Clock | Supported pins   |
| ---------------- |------ | ---------------- |
| ATtiny84/84A | 16MHz | 0, 1, 2, 3, 4 |
| ATtiny85 | 16MHz | 1, 2 |
| ATmega88/168/328 (Uno, Nano, Pro) | 16MHz | 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, A0, A1 |
| ATmega328PB | 16MHz | 10 |
| ATmega16u4/32u4 (Leonardo, Micro) | 16MHz | 2, 4, 8, 12 |
| ATmega2560 (Mega, Mega nano) | 16MHz | 3, 4, 7, 8, 9, 10, 12 |
| ATmega1284P | 16MHz | 18, 19, 20, 21, 22, 23, A0, A1, A2, A3, A4, A5, A6, A7 |
| SAMD (Arduino Zero) | 48MHz | D0, D1, D3, A0, A1 |
| STM32F1 | 72MHz | PB15, PB14, PB13, PB12, PB11, PB10, PB9, PB8, PB7, PB6, PB4, PB3, PA15, PA10 |
| MK20DX256 (Teensy 3.1) | 96MHz | All pins |
| ESP8266 (NodeMCU, AI-THINKER) | 80/160MHz | D1 or GPIO 5 |
| ESP32 (Heltech WiFi LoRa) | 160MHz | 12, 25 |

### Performance
`SWBB_MODE` can be configured in 4 different modes, `1`, `2`, `3` and `4`:

| Mode | Speed | Range | Supported MCUs   |
| ---- | ----- |------ | ---------------- |
| `1`  | 1.97kB/s | 2000m | ATtiny84/84A/85, ATmega88/168/328/328PB/16u4/32u4/2560/1284P, SAMD, STM32F1, MK20DX256, ESP8266, ESP32 |  
| `2`  | 2.21kB/s | 1600m | ATtiny84/84A/85, ATmega88/168/328/328PB/16u4/32u4/2560, STM32F1 |
| `3`  | 3.10kB/s | 1200m | ATtiny84/84A/85, ATmega88/168/328, STM32F1 |
| `4`  | 3.34kB/s |  800m | ATtiny84/84A/85, ATmega88/168/328, STM32F1 |
| `5`  | 6.3kB/s  | not measured | ATmega88/168/328 with `SWBB_PIN` only |
| `6`  | 8.2kB/s  | not measured | ATmega88/168/328 with `SWBB_PIN` only |

Modes 5 and 6 are not part of PJDL v5.0: they need the [direct port access](#direct-port-access-atmega88168328) implementation on every device of the bus (and the bootloader built with the same `MODE`). They were designed in simulation and tested on a short bench bus (mode 6: 0 errors in over 35,000 packets, about 2 times the message rate of mode 3 through a router, see [REVIEW-swbb-hardware.md](/REVIEW-swbb-hardware.md)); test them on the real cable before using them, see [REVIEW-swbb-fast-modes.md](/REVIEW-swbb-fast-modes.md).

#### Mode 7 (prototype, not PJDL)
`SWBB_MODE 7` uses a different wire format (UART like bytes: start bit, 8 data bits, stop bit at 4us per bit, after a 96us preamble, frames received and transmitted as a whole) with the same PJON packets and API. Every device of the bus must use mode 7 with `SWBB_PIN` (optionally `SWBB_INTERRUPT_WAKE`), and the bootloader built with `MODE=7`. In simulation it carries about 1.7-1.8 times the payload of mode 6 (7.2kB/s for 20 bytes payloads with acknowledgement, 10.1kB/s for 50 bytes); on a bench bus 7.32kB/s for 20 bytes with no errors in 880,000 packets ([REVIEW-swbb-hardware.md](/REVIEW-swbb-hardware.md)). It also has a contention slot that lets one of two devices starting at the same time win without disturbing the other. Clock difference between devices: up to about +-3.5%. Use [Mode7LinkTest](/examples/ARDUINO/Local/SoftwareBitBang/Mode7LinkTest/Mode7LinkTest.ino) on the real bus first, see [REVIEW-swbb-mode7.md](/REVIEW-swbb-mode7.md).
```cpp
#define SWBB_MODE 7
#define SWBB_PIN  2
// #define SWBB_INTERRUPT_WAKE // to receive in the INT0 interrupt
#include <PJONSoftwareBitBang.h>
```

When including and using the `SoftwareBitBang` strategy you have the complete access to the microcontroller. This happens because `SoftwareBitBang` runs a completely software-defined implementation, transforming a painful walk in a nice flight.

Communication over a single wire enables quick and creative experimentation. The first suggested test, at the tester's risk, is to let two Arduino boards communicate [through a living body](https://www.youtube.com/watch?v=caMit7nzJsM) touching with the left hand the digital pin of the first board and with the right the pin of the other one (should be harmless). It is stunning to see it working perfectly through the human body, although it also works through water and other conductors.

![PJDL communication over 2000m twisted pair](images/PJDL-2000m-mode4-twistedpair-8.2k-pulldown-60-series.png)

The picture above shows a [PJDL](/src/strategies/SoftwareBitBang/specification/PJDL-specification-v5.0.md) frame transmitted over a 800m twisted pair using mode `4`. Although bits are substantially deformed the exchange occurs nominally and performance is not affected. This experiment was done and published by [Jack Anderson](https://github.com/jdaandersj) in [LANC Video Camera Control](http://jda.tel/pdf/lanc_video_camera_control.pdf) - Department of Computer Science [Loughborough University](https://www.lboro.ac.uk/departments/compsci/) (UK).

### PJDL vs. 1-Wire

| Protocol | Communication speed | Range     | Communication mode     | Bus Control                |
| -------- | ------------------- | --------- | ---------------------- | -------------------------- |
| PJDL     | 1.97-3.34kB/s       | 800-2000m | Simplex or half-duplex | Multi-controller           |
| 1-Wire   | 2.03kB/s            | 300m      | Half-duplex            | Controller-peripheral only |

### Configuration
Before including the library it is possible to configure `SoftwareBitBang` using predefined constants:

| Constant                | Purpose                              | Supported value                            |
| ----------------------- |------------------------------------- | ------------------------------------------ |
| `SWBB_MODE`             | Data transmission mode               | 1, 2, 3, 4 (5, 6 with `SWBB_PIN`)          |
| `SWBB_BACK_OFF_DEGREE`  | Maximum back-off exponential degree  | Numeric value (4 by default)               |
| `SWBB_MAX_ATTEMPTS`     | Maximum transmission attempts        | Numeric value (20 by default)              |
| `SWBB_PREAMBLE`         | Preamble Length                      | Numeric value (1 by default), max 100      |
| `SWBB_MAX_PREAMBLE`     | Maximum preamble length              | Numeric value (1 by default), max 100      |
| `SWBB_PIN`              | Direct port access on this pin (ATmega88/168/328 only, see below) | Arduino pin number 0-19 |
| `SWBB_ATOMIC`           | With `SWBB_PIN`, disable interrupts while a byte is transmitted or received | 1 (default) or 0 |

`SoftwareBitBang` supports the use of input and output pins because separated signals may be required if additional circuitry is used for amplification or noise filtering. It also works if pins are directly connected as a simple point-to-point null-modem or cross-over serial link.

```cpp  
#include <PJONSoftwareBitBang.h>

PJONSoftwareBitBang bus;

void setup() {
  // Set the pin 12 as the communication pin
  bus.strategy.set_pin(12);
  // Set pin 11 as input pin and pin 12 as output pin
  bus.strategy.set_pins(11, 12);
}
```
After the PJON object is defined with its strategy, it is possible to set the communication pin by accessing the strategy present in the PJON instance. All the other necessary information is present in the general [Documentation](/documentation).

### Direct port access (ATmega88/168/328)
On the ATmega88/168/328 (Arduino Uno, Nano, Pro Mini) the pin can be selected at compile time defining `SWBB_PIN` before including the library. The pin is then read and written with single instructions instead of `digitalRead`, `digitalWrite` and `pinMode`, as the [bootloader](/bootloader/SoftwareBitBang/ATmega328P) does, and the timing is cycle exact:
```cpp
#define SWBB_MODE 3
#define SWBB_PIN  2 // set_pin and set_pins are then ignored
#include <PJONSoftwareBitBang.h>
```
- It is compatible with devices using the default implementation (also previous versions) and with the bootloader, in all modes.
- Less flash: ~630 bytes less in a sketch using PJON only, ~290 bytes less if the sketch uses `digitalRead`/`digitalWrite` elsewhere. Polling is ~2.8 times faster: `bus.receive()` with an idle bus takes ~14us instead of ~40us (16MHz).
- The padding bit is measured from its rising edge to its falling edge (with a resolution of 0.375us) and the data bits are sampled at a fixed number of cycles from the falling edge, so the receiver is not affected by the time spent in `bus.receive()` or by interrupts served between bytes.
- Interrupts are disabled while a byte is transmitted or received (~0.3ms in mode 3, ~0.5ms in mode 1) and served during the padding bits, the time spent in interrupts is measured with Timer0 and compensated, so an interrupt can not distort a byte. Define `SWBB_ATOMIC 0` to keep interrupts enabled.
- Timer0 must run as configured by the Arduino core (`millis`), it is read and never modified.

#### Interrupt triggered reception (prototype, pins 2 and 3)
With `SWBB_PIN 2` (INT0) or `3` (INT1), defining `SWBB_INTERRUPT_WAKE` makes the rising edge of an incoming frame trigger an interrupt that receives the whole frame with the same cycle exact code, checks it and transmits the acknowledgement at once. Frames are queued (`SWBB_WAKE_FRAMES`, default 2) and served by `bus.receive()`, that can be called whenever the application has time: frames are not missed while the application is busy. Modes 1-6, Timer1 is not used.
```cpp
#define SWBB_PIN 2
#define SWBB_INTERRUPT_WAKE
#include <PJONSoftwareBitBang.h>
```
- While a frame addressed to this device is received the application does not run (interrupts are served between bytes); a frame addressed to another device costs ~0.6ms in mode 3 plus ~5us for each of its rising edges.
- The acknowledgement is sent by the interrupt only for frames addressed to this device id (the one passed to `begin`) that `PJON::receive()` would acknowledge: the same header checks (packet id, port and MAC bits against the `PJON_INCLUDE_*` options, length, CRC32 above 15 bytes) are done before acknowledging ([SoftwareBitBangAccept.h](SoftwareBitBangAccept.h)), frames PJON would drop are neither acknowledged nor stored. As in PJON, frames for another port and repeated packet ids are acknowledged and then dropped by PJON. Not supported: shared mode, MAC addresses (stored without acknowledgement, PJON decides), router mode (acknowledging other device ids).
- The interrupt acknowledges only if `bus.receive()` was called in the last `SWBB_WAKE_ALIVE_MS` (default 100ms, 0 disables): a device whose application is stuck stops acknowledging. Keep the time between two `bus.receive()` calls below it.
- RAM: `SWBB_WAKE_FRAMES` x (`PJON_PACKET_MAX_LENGTH` + 2) bytes for the queue, ~14 bytes of state (+18 with `SWBB_WAKE_STATS`), and the interrupt uses ~40-50 bytes of stack (nested interrupts included) on top of whatever the application uses when a frame arrives. Check the headroom of the real application with [PJON_StackCheck.h](/src/utils/PJON_StackCheck.h) (`pjon_stack_paint()` at the start of `setup()`, `pjon_stack_free()` after heavy traffic). Lowering `PJON_MAX_PACKETS` (each buffered packet takes `PJON_PACKET_MAX_LENGTH` + 13 bytes, 5 by default) is usually the largest saving, `SWBB_WAKE_FRAMES 1` saves one frame.
- Frames that find the queue full are not acknowledged. Call `bus.receive()` regularly, also inside loops that only transmit: two devices sending to each other back to back without it fill each other's queue and both keep retrying (seen on hardware). `send_packet_blocking` receives during its back-off only with `PJON_RECEIVE_WHILE_SENDING_BLOCKING`.
- Projects with several source files that include PJON (not a single sketch): define `SWBB_WAKE_SHARED` for all of them and `SWBB_WAKE_IMPLEMENTATION` in exactly one (e.g. with `-D` build flags for the first, `#define` before the include for the second). Only that file then defines the interrupt handler and the receiver's state, otherwise the link fails with a duplicate `__vector_1`. The same applies to `SWBB_INTERRUPT` and to mode 7.
- See [REVIEW-swbb-interrupt-wake.md](/REVIEW-swbb-interrupt-wake.md) for the measurements.

### Why not interrupts?
In the Arduino environment the use of libraries is really extensive and often the end user is not able to go over collisions. Very often a library is using hardware resources of the microcontroller, colliding with other libraries. This happens because in general Arduino boards have limited hardware resources. Software-defined bit-banging, is a stable and reliable solution that leads to "more predictable" results than interrupt driven procedures coexisting on limited microcontrollers without the developer and the end user knowing about it.

![PJON - Michael Teeuw application example](http://33.media.tumblr.com/0065c3946a34191a2836c405224158c8/tumblr_inline_nvrbxkXo831s95p1z_500.gif)

PJON application example made by the user [Michael Teeuw](http://michaelteeuw.nl/post/130558526217/pjon-my-son)

### Known issues
- A 1-5 MΩ pull down resistor could be necessary to reduce interference, see [Mitigate interference](https://github.com/gioblu/PJON/wiki/Mitigate-interference).
- When using more than one instance of `SoftwareBitBang` in the same sketch use pins part of different port groups to avoid cross-talk.  
- During the execution of other tasks or delays a certain amount of packets could be potentially lost because transmitted out of the polling time of the receiver device. Thanks to the PJON packet handler after some retries the packet is received but a certain amount of bandwidth is wasted. If this situation occurs try to reduce the duration of other tasks and use a frame preamble setting `SWBB_MAX_PREAMBLE` and `SWBB_PREAMBLE` to a value between 1 and 100. The optimal preamble length is the maximum interval between each `bus.receive()` call divided by `SWBB_BIT_SPACER`
- `SoftwareBitBang` strategy can have compatibility issues with codebases that are using interrupts, reliability or bandwidth loss can occur because of the interruptions made by third party software. On the ATmega88/168/328 use [direct port access](#direct-port-access-atmega88168328) to avoid it.
- If you need to connect devices that operate at different voltages, for example when connecting an Arduino UNOs to an ESP32, consider that voltage level converters available in the market are often not compatible with PJDL, you can try this [alternative approach](http://www.falstad.com/circuit/circuitjs.html?ctz=CQAgjCAMB0l3BWEA2AzNA7ATi8gHACxhaKTJYgaohIHUICmAtGGAFABu4WATCKgj7E+PDH0hQQBCemoT50BJxpg+AvglUhR4ydJqSFSgO5SCefoO0Jkl8WwAm12z0gWeN8D3fgAcqgI2AAduETcvd3CIeTZTD1t1ULsoWJU1KwJzZMhU4XAtePz7UwJkAiK0ipySsorMi1Z7EM0wiwQMFyjDNgBnSp1+8PkQADMAQwAbHoY2ACdnbXDCxIlCeF6kgbzXCwkIcamZ+bzGzaGpVzhUwp2z3evPU-qqh9tTwtV7+eenrJXweDreYtCog24SS5sAAeICYPBcYDerm0xCk2nKqA4qAAOj0HAwOABLADGDFxAEkAPLQmiEFHlDBgDAo5nlHjlBAcXH4omkinUmHECCoOkISAUEWs8BIADKAHVyQAVADCAAkAKIy3EAQQASurcQBVGXqgAiuMVlNxMvJAFlDQAZbWKg09SmGxUABQ9bCAA) proposed by [alastaira](https://github.com/alastaira) instead. It isn't truly converting voltage levels but it is much quicker and cheaper than designs based on transistors.  

### Safety warning
In all cases, when installing or maintaining a PJON network, extreme care must be taken to avoid any danger. If devices are connected to AC power, you face a high risk of electrocution unless the hardware is installed carefully and correctly. If you lack sufficient experience, seek assistance from a qualified technician, and be aware that many countries prohibit uncertified installations. When a [SoftwareBitBang](/src/strategies/SoftwareBitBang) bus is installed [interference-mitigation](https://github.com/gioblu/PJON/wiki/Mitigate-interference) and [protective-circuitry](https://github.com/gioblu/PJON/wiki/Protective-circuitry) guidelines must be followed. If a common ground or power supply line is used its cable size and length must be carefully selected taking in consideration the overall application's power supply requirements and selected components' maximum rating. PJDL and its reference implementation [SoftwareBitBang](/src/strategies/SoftwareBitBang/README.md) are experimental, use them at your own risk.
