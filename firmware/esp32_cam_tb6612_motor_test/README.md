# ESP32-CAM motor-control test

[`esp32_cam_tb6612_motor_test.ino`](esp32_cam_tb6612_motor_test.ino) is the current sketch in this repository. It is a **motor-only** test for an AI-Thinker ESP32-CAM and TB6612FNG using Arduino-ESP32 **3.x**. The ESP32 serves a browser interface from its own Wi-Fi access point. Camera capture and microSD are not part of this sketch.

## Flash and control

1. Copy `ota_config.example.h` to `ota_config.h` in this folder. Set a **unique** `OTA_PASSWORD`, then remove the `#error` line. The real file is gitignored. Also change `AP_SSID` and `AP_PASSWORD` near the top of the sketch if desired; the Wi-Fi password must contain at least eight characters.
2. Open the `.ino` in Arduino IDE with Arduino-ESP32 3.x installed and select the AI-Thinker ESP32-CAM board. In **Tools → Partition Scheme**, choose an option that supports OTA (two app slots); verify the compiled firmware fits one slot. Do not choose a no-OTA or huge-app layout.
3. Flash this first OTA-capable build using the board's serial-programming procedure. Disconnect programming power before connecting the intended battery supply.
4. With the verified 5 V and common ground connections in place, open Serial at 115200 baud to read the access point and IP address. Join that Wi-Fi network and visit the printed address.
5. Hold a touch button or use W/A/S/D. The drive-power slider selects 80–255 out of 255; release commands zero. The UI sends a state heartbeat while connected.

## Later firmware updates over Wi-Fi

1. Park the car, switch **off** the motor battery, ensure the camera battery is charged, and connect your computer to the car's Wi-Fi access point.
2. Compile the next version using the **same board and OTA partition scheme**. In Arduino IDE, choose **Sketch → Export Compiled Binary** and locate the sketch's `.bin` file.
3. Open `http://192.168.4.1/update` (or the AP IP printed in Serial), sign in with `OTA_USER` and `OTA_PASSWORD`, choose the compiled sketch `.bin`, and upload it.
4. The motors are commanded off before flash writes. On success the board reboots. Reconnect to the access point and verify both the controller and motor timeout. If the upload fails, read Serial output; the current firmware normally remains bootable, but keep serial-programming access available for recovery.

The upload page uses HTTP Basic authentication and checks that a browser POST originated from the car's own page. It runs on the local access point, **not HTTPS**. Do not reuse an important password or expose the access point to untrusted users. The update handler will refuse to start if the selected partition scheme has no spare OTA app slot.

## Behavior and limits

| Feature | Implementation |
| --- | --- |
| Rear motor | PWM on GPIO12/13, up to 18 kHz, 8-bit duty |
| Steering motor | PWM on GPIO14/15, 120 ms full-power kick followed by hold duty 150/255 |
| Lost commands | Both motor commands set to zero after 700 ms without a request |
| LEDC allocation | Channels 4–7 reserved for motors; channel 0 kept clear for prospective camera XCLK |
| Driver setup | PWMA/PWMB/STBY tied high to 3.3 V; driver VCC to 3.3 V, VM to motor battery |
| Firmware update | Authenticated browser upload at `/update`; motors stopped during flash writes |

The browser uses GET requests for a local development control API. It is not an authenticated network control protocol; use it only on a network you control. The configured timeout is a firmware failsafe, not a replacement for the physical switches. The current UI can issue overlapping requests under some press/release timing; verify stop behavior on the assembled car before floor testing. Do not initialize microSD with GPIO12–15 assigned to the driver.

The two sketches under [`../legacy/`](../legacy/) record earlier experiments and have different pin mappings. They are not wiring instructions for this build.
