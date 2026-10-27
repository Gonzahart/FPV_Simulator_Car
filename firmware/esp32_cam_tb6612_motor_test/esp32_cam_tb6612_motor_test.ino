#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "ota_config.h"

// Motor-only validation sketch for an AI-Thinker ESP32-CAM.
// Requires Arduino-ESP32 board package 3.x.
// Do not initialize or use the microSD card with this pin assignment.

const char *AP_SSID = "DriftCar1.0";
const char *AP_PASSWORD = "drift1234";  // Must be at least 8 characters.

WebServer server(80);

// TB6612FNG connections. PWMA, PWMB, and STBY are tied to ESP32-CAM 3.3 V.
// Channel A: rear drive motor. Channel B: front steering motor.
constexpr uint8_t DRIVE_IN1 = 12;  // AIN1
constexpr uint8_t DRIVE_IN2 = 13;  // AIN2
constexpr uint8_t STEER_IN1 = 14;  // BIN1
constexpr uint8_t STEER_IN2 = 15;  // BIN2

// ESP32 camera XCLK normally uses LEDC channel 0. Reserve it now so this motor
// assignment can be retained when camera streaming is added later.
constexpr uint8_t DRIVE_CH1 = 4;
constexpr uint8_t DRIVE_CH2 = 5;
constexpr uint8_t STEER_CH1 = 6;
constexpr uint8_t STEER_CH2 = 7;

constexpr uint32_t MOTOR_PWM_HZ = 18000;
constexpr uint8_t MOTOR_PWM_BITS = 8;
constexpr int MAX_PWM = 255;

// Change either to true if that motor runs opposite to the web controls.
constexpr bool DRIVE_REVERSED = false;
constexpr bool STEERING_REVERSED = false;

// Steering gets full power briefly, then reduced holding power to limit the
// stall current of the usual spring-centered toy steering mechanism.
constexpr int STEER_KICK_PWM = 255;
constexpr int STEER_HOLD_PWM = 150;
constexpr uint32_t STEER_KICK_MS = 120;

// If control packets stop arriving, shut both motors off.
constexpr uint32_t COMMAND_TIMEOUT_MS = 700;

int requestedDrive = 0;  // -255 to +255
int requestedSteer = 0;  // -1, 0, or +1
int previousSteer = 0;
uint32_t steerStartedAt = 0;
uint32_t lastCommandAt = 0;
bool otaInProgress = false;
bool otaStarted = false;
bool otaFailed = false;
bool otaComplete = false;
bool otaForbidden = false;
bool rebootPending = false;
const char *OTA_REQUEST_HEADERS[] = {"Origin", "Host"};

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
  <title>ESP32 Drift Car</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; }
    body { margin: 0; background: #111; color: #eee; text-align: center; touch-action: none; }
    main { max-width: 520px; margin: auto; padding: 22px; }
    h1 { font-size: 1.45rem; margin: 0 0 18px; }
    .controls { display: grid; grid-template-columns: 1fr 1fr; gap: 24px; }
    .group { display: grid; gap: 12px; align-content: center; }
    button { min-height: 92px; border: 0; border-radius: 18px; font-size: 1.2rem;
             font-weight: 700; background: #333; color: white; }
    button.active { background: #e53935; transform: scale(.98); }
    input { width: 100%; }
    .speed { margin: 25px 0 15px; }
    #status { color: #9ad; min-height: 1.4em; }
  </style>
</head>
<body>
<main>
  <h1>ESP32 Drift Car — Motor Test</h1>
  <div class="speed">
    <label>Drive power: <span id="speedValue">180</span>/255</label>
    <input id="speed" type="range" min="80" max="255" value="180">
  </div>
  <div class="controls">
    <div class="group">
      <button data-axis="steer" data-value="-1">LEFT</button>
      <button data-axis="steer" data-value="1">RIGHT</button>
    </div>
    <div class="group">
      <button data-axis="drive" data-value="1">FORWARD</button>
      <button data-axis="drive" data-value="-1">REVERSE</button>
    </div>
  </div>
  <p id="status">Connect to the car, then hold a button.</p>
  <p><a href="/update" style="color:#9ad">Firmware update</a></p>
</main>
<script>
  let driveDirection = 0;
  let steerDirection = 0;
  let drivePower = 180;
  let requestPending = false;

  const speed = document.getElementById('speed');
  const speedValue = document.getElementById('speedValue');
  const status = document.getElementById('status');

  speed.addEventListener('input', () => {
    drivePower = Number(speed.value);
    speedValue.textContent = drivePower;
  });

  function sendState(force = false) {
    if (requestPending && !force) return;
    requestPending = true;
    const drive = driveDirection * drivePower;
    fetch(`/control?drive=${drive}&steer=${steerDirection}`, { cache: 'no-store' })
      .then(r => {
        if (!r.ok) throw new Error('request failed');
        status.textContent = `Drive ${drive}, steer ${steerDirection}`;
      })
      .catch(() => status.textContent = 'Control connection lost — motors will stop')
      .finally(() => requestPending = false);
  }

  document.querySelectorAll('button').forEach(button => {
    const axis = button.dataset.axis;
    const value = Number(button.dataset.value);

    const press = event => {
      event.preventDefault();
      button.setPointerCapture?.(event.pointerId);
      if (axis === 'drive') driveDirection = value;
      else steerDirection = value;
      button.classList.add('active');
      sendState(true);
    };

    const release = event => {
      event.preventDefault();
      if (axis === 'drive' && driveDirection === value) driveDirection = 0;
      if (axis === 'steer' && steerDirection === value) steerDirection = 0;
      button.classList.remove('active');
      sendState(true);
    };

    button.addEventListener('pointerdown', press);
    button.addEventListener('pointerup', release);
    button.addEventListener('pointercancel', release);
    button.addEventListener('lostpointercapture', release);
  });

  const keys = { w: ['drive', 1], s: ['drive', -1], a: ['steer', -1], d: ['steer', 1] };
  window.addEventListener('keydown', event => {
    if (event.repeat || !keys[event.key.toLowerCase()]) return;
    const [axis, value] = keys[event.key.toLowerCase()];
    if (axis === 'drive') driveDirection = value;
    else steerDirection = value;
    sendState(true);
  });
  window.addEventListener('keyup', event => {
    if (!keys[event.key.toLowerCase()]) return;
    const [axis, value] = keys[event.key.toLowerCase()];
    if (axis === 'drive' && driveDirection === value) driveDirection = 0;
    if (axis === 'steer' && steerDirection === value) steerDirection = 0;
    sendState(true);
  });

  window.addEventListener('blur', () => {
    driveDirection = 0;
    steerDirection = 0;
    sendState(true);
  });

  // Heartbeat keeps the car moving only while this page remains connected.
  setInterval(sendState, 150);
</script>
</body>
</html>
)HTML";

void writeBridge(uint8_t in1, uint8_t in2, int command, bool reversed) {
  command = constrain(command, -MAX_PWM, MAX_PWM);
  if (reversed) command = -command;

  if (command > 0) {
    ledcWrite(in1, command);
    ledcWrite(in2, 0);
  } else if (command < 0) {
    ledcWrite(in1, 0);
    ledcWrite(in2, -command);
  } else {
    // Both low with PWM held high gives high-impedance stop (coast).
    ledcWrite(in1, 0);
    ledcWrite(in2, 0);
  }
}

void stopAllMotors() {
  requestedDrive = 0;
  requestedSteer = 0;
  previousSteer = 0;
  writeBridge(DRIVE_IN1, DRIVE_IN2, 0, DRIVE_REVERSED);
  writeBridge(STEER_IN1, STEER_IN2, 0, STEERING_REVERSED);
}

void applyMotorCommands() {
  writeBridge(DRIVE_IN1, DRIVE_IN2, requestedDrive, DRIVE_REVERSED);

  if (requestedSteer == 0) {
    writeBridge(STEER_IN1, STEER_IN2, 0, STEERING_REVERSED);
    previousSteer = 0;
    return;
  }

  if (requestedSteer != previousSteer) {
    previousSteer = requestedSteer;
    steerStartedAt = millis();
  }

  const int steeringPower =
      (millis() - steerStartedAt < STEER_KICK_MS) ? STEER_KICK_PWM : STEER_HOLD_PWM;
  writeBridge(STEER_IN1, STEER_IN2, requestedSteer * steeringPower, STEERING_REVERSED);
}

void handleControl() {
  if (otaInProgress || rebootPending) {
    server.send(503, "text/plain", "Firmware update in progress; motors are stopped");
    return;
  }

  if (!server.hasArg("drive") || !server.hasArg("steer")) {
    server.send(400, "text/plain", "Missing drive or steer value");
    return;
  }

  requestedDrive = constrain(server.arg("drive").toInt(), -MAX_PWM, MAX_PWM);
  requestedSteer = constrain(server.arg("steer").toInt(), -1, 1);
  lastCommandAt = millis();
  applyMotorCommands();
  server.send(200, "text/plain", "OK");
}

// The update page is reachable through the car's existing Wi-Fi access point.
// Authentication is checked in BOTH the page handler and the upload callback:
// an unauthenticated POST must never reach Update.begin() or Update.write().
bool otaAuthorized() {
  if (server.authenticate(OTA_USER, OTA_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

void handleUpdatePage() {
  if (!otaAuthorized()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html",
      "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Car firmware update</title></head><body style='font-family:system-ui;max-width:38rem;"
      "margin:2rem auto;padding:0 1rem'>"
      "<h1>Car firmware update</h1><p>Park the car, turn off the motor battery, and keep the "
      "camera battery charged. Motor outputs are disabled when the upload begins.</p>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<input type='file' name='firmware' accept='.bin' required>"
      "<button type='submit'>Upload firmware</button></form>"
      "<p>Upload the compiled sketch .bin built for this board and its OTA partition scheme.</p>"
      "<p><a href='/'>Back to controls</a></p></body></html>");
}

void handleUpdateUpload() {
  // WebServer invokes this callback while parsing the multipart POST body.
  if (!server.authenticate(OTA_USER, OTA_PASSWORD)) return;

  HTTPUpload &upload = server.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START:
      otaForbidden = server.header("Origin") != String("http://") + server.header("Host");
      if (otaForbidden) {
        otaFailed = true;
        Serial.println("OTA rejected: request origin does not match the car's update page.");
        return;
      }
      otaInProgress = true;
      otaStarted = false;
      otaFailed = false;
      otaComplete = false;
      stopAllMotors();
      otaStarted = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      if (!otaStarted) {
        otaFailed = true;
        Update.printError(Serial);
      }
      break;

    case UPLOAD_FILE_WRITE:
      if (otaStarted && !otaFailed &&
          Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        otaFailed = true;
        Update.printError(Serial);
        Update.abort();
      }
      break;

    case UPLOAD_FILE_END:
      if (otaStarted && !otaFailed) {
        otaComplete = Update.end(true);
        if (!otaComplete) {
          otaFailed = true;
          Update.printError(Serial);
        }
      }
      break;

    case UPLOAD_FILE_ABORTED:
      if (otaStarted) Update.abort();
      otaFailed = true;
      otaInProgress = false;
      break;
  }
}

void handleUpdateComplete() {
  if (!otaAuthorized()) return;
  if (otaForbidden) {
    server.send(403, "text/plain", "Update rejected: origin mismatch.");
    return;
  }
  if (!otaComplete || otaFailed || Update.hasError()) {
    server.send(500, "text/plain", "Update failed. Check Serial output and the OTA partition scheme.");
    otaInProgress = false;
    return;
  }

  stopAllMotors();
  server.send(200, "text/plain", "Update complete. Rebooting now.");
  rebootPending = true;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  bool pwmOk = true;
  pwmOk &= ledcAttachChannel(DRIVE_IN1, MOTOR_PWM_HZ, MOTOR_PWM_BITS, DRIVE_CH1);
  pwmOk &= ledcAttachChannel(DRIVE_IN2, MOTOR_PWM_HZ, MOTOR_PWM_BITS, DRIVE_CH2);
  pwmOk &= ledcAttachChannel(STEER_IN1, MOTOR_PWM_HZ, MOTOR_PWM_BITS, STEER_CH1);
  pwmOk &= ledcAttachChannel(STEER_IN2, MOTOR_PWM_HZ, MOTOR_PWM_BITS, STEER_CH2);

  stopAllMotors();
  if (!pwmOk) {
    Serial.println("ERROR: Could not attach one or more motor PWM pins.");
  }

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("ERROR: Could not start Wi-Fi access point.");
  }

  server.collectHeaders(OTA_REQUEST_HEADERS, 2);
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/control", HTTP_GET, handleControl);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateComplete, handleUpdateUpload);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  server.begin();

  lastCommandAt = millis();
  Serial.println();
  Serial.print("Connect to Wi-Fi: ");
  Serial.println(AP_SSID);
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
  Serial.print("Firmware update: http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/update");
}

void loop() {
  server.handleClient();

  if (otaInProgress || rebootPending) {
    stopAllMotors();
    if (rebootPending) {
      delay(200);  // Let the HTTP response leave before restarting.
      ESP.restart();
    }
    delay(2);
    return;
  }

  if (millis() - lastCommandAt > COMMAND_TIMEOUT_MS) {
    stopAllMotors();
  } else {
    applyMotorCommands();
  }

  delay(2);
}
