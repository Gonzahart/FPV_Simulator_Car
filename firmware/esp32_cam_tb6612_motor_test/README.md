# ESP32-CAM motor control, camera stream, and OTA

[`esp32_cam_tb6612_motor_test.ino`](esp32_cam_tb6612_motor_test.ino) is the current sketch for an AI-Thinker ESP32-CAM and TB6612FNG using Arduino-ESP32 **3.x**. It serves a browser controller with a QVGA MJPEG camera view from its own Wi-Fi access point. This firmware compiles, but camera performance and power stability still need validation on the assembled car. The microSD card is not used.

## Flash and control

1. Copy `ota_config.example.h` to `ota_config.h` in this folder. Set a **unique** `OTA_PASSWORD`, then remove the `#error` line. The real file is gitignored. Also change `AP_SSID` and `AP_PASSWORD` near the top of the sketch if desired; the Wi-Fi password must contain at least eight characters.
2. Open the `.ino` in Arduino IDE with Arduino-ESP32 3.x installed and select the AI-Thinker ESP32-CAM board. In **Tools → Partition Scheme**, choose an option that supports OTA (two app slots); verify the compiled firmware fits one slot. Do not choose a no-OTA or huge-app layout.
3. Flash this first OTA-capable build using the board's serial-programming procedure. Disconnect programming power before connecting the intended battery supply.
4. With the verified 5 V and common ground connections in place, open Serial at 115200 baud to read the access point and IP address. Join that Wi-Fi network and visit the printed address.
5. The page displays the live camera view above the controls. The stream is also available at `http://192.168.4.1:81/stream` (substitute the printed AP IP if different). Hold a touch button or use W/A/S/D. The drive-power slider selects 80–255 out of 255. The steering-hold slider starts at 210/255 and adjusts the power used after the initial steering kick. The front and rear light buttons control separate GPIO outputs once external LED switches are wired. Release commands zero. The UI sends a state heartbeat while connected.
6. If camera initialization fails, Serial prints an error; motor control and OTA remain available. Test motor response and the 700 ms timeout **while video is open**, then measure the ESP32-CAM 5 V rail for dips during streaming and motor starts.

## Later firmware updates over Wi-Fi

1. Park the car, switch **off** the motor battery, ensure the camera battery is charged, and connect your computer to the car's Wi-Fi access point.
2. Compile the next version using the **same board and OTA partition scheme**. In Arduino IDE, choose **Sketch → Export Compiled Binary** and locate the sketch's `.bin` file.
3. Open `http://192.168.4.1/update` (or the AP IP printed in Serial), sign in with `OTA_USER` and `OTA_PASSWORD`, choose the compiled sketch `.bin`, and upload it.
4. The motors and body-light outputs are commanded off before flash writes, and any active video stream ends. On success the board reboots. Reconnect to the access point and verify the camera, controller, lights, and motor timeout. If the upload fails, read Serial output; the current firmware normally remains bootable, but keep serial-programming access available for recovery.

The upload page uses HTTP Basic authentication and checks that a browser POST originated from the car's own page. It runs on the local access point, **not HTTPS**. Do not reuse an important password or expose the access point to untrusted users. The update handler will refuse to start if the selected partition scheme has no spare OTA app slot.

## Behavior and limits

| Feature | Implementation |
| --- | --- |
| Rear motor | PWM on GPIO12/13, up to 18 kHz, 8-bit duty |
| Steering motor | PWM on GPIO14/15, 120 ms full-power kick followed by adjustable hold duty (default 210/255, range 150–255) |
| Body lights | GPIO4 for front, GPIO2 for rear; separate on/off buttons and `/lights` state endpoint; both default off |
| Lost commands | Both motor commands set to zero after 700 ms without a request |
| Camera | AI-Thinker pin map; JPEG QVGA (320 × 240), MJPEG at port 81 `/stream`; two frame buffers with PSRAM, one without |
| LEDC allocation | Channel 0 / timer 0 for camera XCLK; channels 4–7 for motors |
| Driver setup | PWMA/PWMB/STBY tied high to 3.3 V; driver VCC to 3.3 V, VM to motor battery |
| Firmware update | Authenticated browser upload at `/update`; motors stopped during flash writes |

The camera runs on a separate HTTP server so its long-lived video response does not occupy the control and OTA server. Only one video client is recommended at a time. The browser uses GET requests for a local development control API; neither that API nor the video stream has separate authentication beyond the Wi-Fi password. Use the access point only with trusted clients. The configured timeout is a firmware failsafe, not a replacement for the physical switches. The current UI can issue overlapping requests under some press/release timing; verify stop behavior on the assembled car before floor testing. Do not initialize microSD with GPIO12–15 assigned to the driver.

## Body-light outputs

The front and rear JST connectors each carry a pair of LEDs that was powered through the original receiver board. The GPIOs are **control signals only**. Each pair needs a suitable external transistor/MOSFET switch and confirmed current limiting; do not connect an LED pair directly to GPIO2 or GPIO4, or assume the removed receiver board's resistors are still in circuit. The [hardware notes](../../docs/hardware.md) describe the proposed switching arrangement and checks needed before connecting the LED harnesses.

GPIO4 also drives the ESP32-CAM's onboard white flash LED, so it will light along with the front pair. GPIO2 affects serial bootloader entry if an external circuit pulls it high at reset; the switch gate needs a pull-down and the finished wiring needs a boot/flash test. Both outputs start low and are turned off when an OTA upload starts. Light state is independent of the 700 ms motor-command timeout.

## Steering hold adjustment

The original hold setting of 150/255 let the spring-centered wheels return to center about 120 ms after pressing A or D. The new default is 210/255. With the car safely raised, hold a turn and adjust the slider to the **lowest** value that keeps the wheels turned. Do not leave the steering motor stalled for long while tuning; stop if the motor or driver becomes hot. Measure steering current and temperature before extended driving. If the wheels still return at 255/255, check motor-battery voltage and steering wiring rather than increasing the duty further.

The two sketches under [`../legacy/`](../legacy/) record earlier experiments and have different pin mappings. They are not wiring instructions for this build.
