# Bring-up and validation log

This is a checklist for the assembled design. Leave a dated result, meter/scope setup, and measured value beside each item as testing progresses; an unchecked item is not evidence that it passed.

| Check | Method / expected observation | Result |
| --- | --- | --- |
| Battery and rail isolation | With both batteries removed, verify BT1+ and BT2+ are not shorted to each other or GND. | ☐ Pending |
| Shared ground | Continuity from both battery negatives to converter GND, ESP32-CAM GND, and driver GND. | ☐ Pending |
| Polarity | Inspect every electrolytic and battery connector against the named rails. | ☐ Pending |
| Boost output, unloaded | Power BT2 only; measure VIN and 5V_OUT at the perfboard. | ☐ Pending |
| Camera rail under load | Connect ESP32-CAM; measure 5V_OUT during boot and Wi-Fi transmit, ideally with a scope. | ☐ Pending |
| Motor supply | Power BT1; measure VM at driver while rear motor starts and while steering operates. | ☐ Pending |
| Boot and GPIO12 | Cold boot with driver connected and both switches on; confirm serial boot log and access point. | ☐ Pending |
| Direction and steering | With wheels safely lifted, check forward/reverse and left/right mapping; then test on ground. | ☐ Pending |
| Command timeout | Drive motor, interrupt control traffic, measure time until outputs turn off (configured for 700 ms). | ☐ Pending |
| Runtime and heat | Record battery capacity, current, runtime, and driver/converter temperatures. | ☐ Pending |
| FPV stream | Add camera firmware, measure frame rate/latency and concurrent motor control. | ☐ Pending |

Record a short demonstration video and a wiring photo once the assembled system passes the first nine checks. Link them from the main README with a brief note on test conditions.
