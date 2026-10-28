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

## Body lights: proposed control wiring

The two white front LEDs share one 2-pin JST connector; the two red rear LEDs share a second. Both pairs were powered through the stock receiver board, which is planned for removal. The replacement firmware reserves **GPIO4 for front lights** and **GPIO2 for rear lights**, with separate browser switches. These pins only command external switches; they cannot safely supply the LED pairs directly.

For each pair, use a logic-level N-channel MOSFET as a low-side switch: GPIO → approximately 220 Ω gate resistor → gate; 10 kΩ from gate to common ground; source to common ground; drain to the LED pair's negative lead. Feed the positive lead from a switched supply **through verified current limiting appropriate to the actual LED wiring and supply voltage**. If the two LEDs in a pair are parallel, each LED needs its own current-limited branch. Do not assume the 2-pin harness includes resistors: the removed receiver board may have provided them. Confirm connector polarity, LED series/parallel arrangement, voltage, current, and resistor location before connecting power or choosing the supply rail.

GPIO4 also drives the ESP32-CAM's onboard white flash LED, which will turn on with the front lights. GPIO2 must not be pulled high at reset when entering the serial bootloader; the external gate pull-down keeps the proposed switch off and lets GPIO2 remain low. Test both normal boot and serial flashing with the finished lighting circuit attached. The lights default off at boot and turn off when an OTA upload begins. They are independent of the motor command timeout.

## Perfboard capacitor plan

| Rail to common GND | Components | Placement intent |
| --- | --- | --- |
| VIN | C1 680 µF electrolytic; C2 10 µF electrolytic; C3 0.1 µF ceramic | Input bulk and local bypass near boost module |
| 5V_OUT | C4 470 µF electrolytic; C5 0.1 µF ceramic | Output bulk and local bypass near converter/ESP feed |
| 3V3_LOGIC | C6 10 µF electrolytic; C7 0.1 µF ceramic | Driver logic supply bypass |

Electrolytic positive terminals connect to their named supply rails; negatives connect to common ground. Check capacitance, voltage rating, polarity, physical clearance, and the boost module manufacturer's stability recommendations before assembly. The illustrated values document the current plan, not measured minimum requirements.

## Scope of the drawing

The DIYLC file is an **illustrative interconnection layout**, not an electrical-rule-checked PCB design. Generic rectangles represent external boards, and their drawn connection locations may differ from physical headers. The perfboard size and hole routing need adjustment to the actual parts. Crossed lines without a junction marker are separate nets. Use a meter to confirm continuity and absence of shorts before applying battery power.
