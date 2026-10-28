# Hardware and wiring

The [editable DIYLC project](../hardware/RC_car_complete_system.diy) shows the entire planned system on one canvas. It combines off-board parts with the converter/capacitor perfboard. Use this document to interpret its named rails and signals.

## Power paths

| Source | Switched positive goes to | Negative goes to | Purpose |
| --- | --- | --- | --- |
| BT1, 1S Li-ion motor battery | SW1A → TB6612FNG **VM** | Driver GND and common ground | Rear drive and steering motors |
| BT2, 1S LiPo camera battery | SW1B → **VIN** → TPS61023 module IN and EN | Common ground | Boosted **5V_OUT** to ESP32-CAM 5 V input |
| ESP32-CAM 3.3 V output | **3V3_LOGIC** → TB6612FNG VCC, STBY, PWMA, PWMB | Common ground | Driver logic and enabled, full-duty outputs |

SW1A and SW1B can be separate switches or two poles of one mechanically linked DPST switch. Each pole switches only its battery positive. **Do not connect BT1+ to BT2+ or connect either battery directly to the ESP32-CAM 5 V pin.** Verify the specific boost module's IN, EN, GND, and OUT pad order against its actual board before soldering.

## Signals

| ESP32-CAM | TB6612FNG | Function |
| --- | --- | --- |
| GPIO12 | AIN1 | Rear drive direction/PWM |
| GPIO13 | AIN2 | Rear drive direction/PWM |
| GPIO14 | BIN1 | Steering direction/PWM |
| GPIO15 | BIN2 | Steering direction/PWM |
| 3.3 V | VCC, STBY, PWMA, PWMB | Driver logic supply and enables |
| GND | GND | Shared signal reference |
| — | AO1/AO2 | Rear motor leads |
| — | BO1/BO2 | Steering motor leads |

The current firmware applies PWM to the IN pins while PWMA/PWMB remain high. If direction is reversed relative to the controls, change the `DRIVE_REVERSED` or `STEERING_REVERSED` constant or swap the corresponding motor leads after power-off. GPIO12–15 conflict with microSD on the ESP32-CAM. GPIO12's boot strapping behavior also requires a real boot test with the driver attached.

## Body lights: control wiring

The two white front LEDs share one 2-pin JST connector; the two red rear LEDs share a second. Both pairs were powered through the stock receiver board, which is planned for removal. The firmware reserves **GPIO3 for front body lights**, **GPIO2 for rear body lights**, and **GPIO4 solely for the ESP32-CAM's onboard flash**. The browser has independent front, rear, and flash buttons plus a brake-mode toggle. In brake mode, the rear pair turns on while the reverse command is active, regardless of the manual rear switch; it turns off on release or command timeout unless the manual rear switch remains on.

Photos of the shell show each two-wire harness branching to two lamp positions, with no resistor visible along the exposed wire. Photos of both sides of the receiver board show resistors, but do not establish which components limit current for either light connector. A battery test with a 1 kΩ series resistor established the connector polarity and lit both LEDs in each pair:

| Harness | Positive lead | Negative lead |
| --- | --- | --- |
| Front white lights | Red | White |
| Rear red lights | Red | Yellow |

With the battery at 3.74 V while lit, the front JST measured 2.64 V and the rear JST measured 1.81 V. The 1 kΩ resistor therefore dropped 1.10 V for the front pair (about **1.10 mA total**) and 1.93 V for the rear pair (about **1.93 mA total**). These are pair currents at this test point, not current ratings for the individual LEDs. The exact series/parallel arrangement and any hidden resistors are still unconfirmed.

For the simple low-current hookup, connect **GPIO3 → 1 kΩ → front red lead**, and connect the front white lead to ESP32-CAM GND. Connect **GPIO2 → a separate 1 kΩ → rear red lead**, and connect the rear yellow lead to the same GND. The firmware drives each pin HIGH to turn its pair on and LOW to turn it off. Supply the body lights only through these GPIOs; do not also connect their red leads to either battery or the 3.3 V rail. A 3.3 V GPIO with 1 kΩ in series limits each harness to at most 3.3 mA even for a shorted harness; a standard 1/4 W resistor is more than sufficient. The lit currents will be lower than the 3.74 V battery test, especially for the white pair. Verify brightness on the assembled car. The onboard flash is already wired to GPIO4 on the ESP32-CAM board and needs no external connection.

The 1 kΩ resistors are a conservative starting point and the lamps may be dim. A single resistor at a JST limits the **total** harness current; it does not guarantee equal sharing if its two LEDs are parallel. To increase brightness, first identify the individual LED branches and add a suitable current-limiting resistor to **each** branch. If a higher current is required, use external switches and a suitably current-limited supply rather than increasing GPIO load based only on these low-current measurements.

GPIO3 is the ESP32-CAM's UART RX pin. It is available during normal use without a connected USB-to-serial programmer, as in the intended OTA setup. Disconnect any programmer's TX lead before running the firmware; a future wired recovery flash requires checking that the front LED load does not interfere with serial reception, and the wire can be unplugged temporarily if needed. GPIO2 must remain low or floating at reset when entering the serial bootloader; the proposed LED-plus-resistor path to ground does not pull it high. Test normal boot and any later serial flashing with the finished wiring. The lights default off at boot and turn off when an OTA upload begins. Brake-mode rear lights follow the accepted reverse command and the 700 ms motor-command timeout.

## Perfboard capacitor plan

| Rail to common GND | Components | Placement intent |
| --- | --- | --- |
| VIN | C1 680 µF electrolytic; C2 10 µF electrolytic; C3 0.1 µF ceramic | Input bulk and local bypass near boost module |
| 5V_OUT | C4 470 µF electrolytic; C5 0.1 µF ceramic | Output bulk and local bypass near converter/ESP feed |
| 3V3_LOGIC | C6 10 µF electrolytic; C7 0.1 µF ceramic | Driver logic supply bypass |

Electrolytic positive terminals connect to their named supply rails; negatives connect to common ground. Check capacitance, voltage rating, polarity, physical clearance, and the boost module manufacturer's stability recommendations before assembly. The illustrated values document the current plan, not measured minimum requirements.

## Scope of the drawing

The DIYLC file is an **illustrative interconnection layout**, not an electrical-rule-checked PCB design. Generic rectangles represent external boards, and their drawn connection locations may differ from physical headers. The perfboard size and hole routing need adjustment to the actual parts. Crossed lines without a junction marker are separate nets. Use a meter to confirm continuity and absence of shorts before applying battery power.
