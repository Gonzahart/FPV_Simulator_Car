# ESP32-CAM motor-control test

[`esp32_cam_tb6612_motor_test.ino`](esp32_cam_tb6612_motor_test.ino) is the current sketch in this repository. It is a **motor-only** test for an AI-Thinker ESP32-CAM and TB6612FNG using Arduino-ESP32 **3.x**. The ESP32 serves a browser interface from its own Wi-Fi access point. Camera capture and microSD are not part of this sketch.

## Flash and control

1. Open the `.ino` in Arduino IDE and select the AI-Thinker ESP32-CAM board with Arduino-ESP32 3.x installed.
2. Edit `AP_SSID` and `AP_PASSWORD` near the top of the sketch if desired. The password must contain at least eight characters.
3. Flash using the board's serial-programming procedure. Disconnect programming power before connecting the intended battery supply.
4. With the verified 5 V and common ground connections in place, open Serial at 115200 baud to read the access point and IP address. Join that Wi-Fi network and visit the printed address.
5. Hold a touch button or use W/A/S/D. The drive-power slider selects 80–255 out of 255; release commands zero. The UI sends a state heartbeat while connected.

## Behavior and limits

| Feature | Implementation |
| --- | --- |
| Rear motor | PWM on GPIO12/13, up to 18 kHz, 8-bit duty |
| Steering motor | PWM on GPIO14/15, 120 ms full-power kick followed by hold duty 150/255 |
| Lost commands | Both motor commands set to zero after 700 ms without a request |
| LEDC allocation | Channels 4–7 reserved for motors; channel 0 kept clear for prospective camera XCLK |
| Driver setup | PWMA/PWMB/STBY tied high to 3.3 V; driver VCC to 3.3 V, VM to motor battery |

The browser uses GET requests for a local development control API. It is not an authenticated network control protocol; use it only on a network you control. The configured timeout is a firmware failsafe, not a replacement for the physical switches. The current UI can issue overlapping requests under some press/release timing; verify stop behavior on the assembled car before floor testing. Do not initialize microSD with GPIO12–15 assigned to the driver.

The two sketches under [`../legacy/`](../legacy/) record earlier experiments and have different pin mappings. They are not wiring instructions for this build.
