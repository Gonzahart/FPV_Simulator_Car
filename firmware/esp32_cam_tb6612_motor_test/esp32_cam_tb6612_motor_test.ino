#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "esp_camera.h"
// WebServer and ESP-IDF both define HTTP_ANY; neither handler uses it here.
#undef HTTP_ANY
#include "esp_http_server.h"
#include "ota_config.h"

// Motor control, OTA updates, and MJPEG video for an AI-Thinker ESP32-CAM.
// Requires Arduino-ESP32 board package 3.x.
// Do not initialize or use the microSD card with this pin assignment.

const char *AP_SSID = "DriftCar1.0";
const char *AP_PASSWORD = "drift1234";  // Must be at least 8 characters.

WebServer server(80);
httpd_handle_t streamServer = nullptr;
bool cameraReady = false;

// TB6612FNG connections. PWMA, PWMB, and STBY are tied to ESP32-CAM 3.3 V.
// Channel A: rear drive motor. Channel B: front steering motor.
constexpr uint8_t DRIVE_IN1 = 12;  // AIN1
constexpr uint8_t DRIVE_IN2 = 13;  // AIN2
constexpr uint8_t STEER_IN1 = 14;  // BIN1
constexpr uint8_t STEER_IN2 = 15;  // BIN2

// Use one 1 kΩ resistor per body-light harness. GPIO3 is UART RX: disconnect
// the serial programmer's TX lead before running this sketch.
// GPIO4 drives only the onboard flash LED; GPIO2 is a boot strap pin.
constexpr uint8_t FRONT_LIGHT_PIN = 3;
constexpr uint8_t REAR_LIGHT_PIN = 2;
constexpr uint8_t FLASH_LIGHT_PIN = 4;

// Camera XCLK uses LEDC channel 0 / timer 0; keep motors on channels 4-7.
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
constexpr int DEFAULT_STEER_HOLD_PWM = 210;
constexpr int MIN_STEER_HOLD_PWM = 150;
constexpr uint32_t STEER_KICK_MS = 120;

// If control packets stop arriving, shut both motors off.
constexpr uint32_t COMMAND_TIMEOUT_MS = 700;

int requestedDrive = 0;  // -255 to +255
int requestedSteer = 0;  // -1, 0, or +1
int steeringHoldPwm = DEFAULT_STEER_HOLD_PWM;
bool frontLightsOn = false;
bool rearLightsManualOn = false;
bool rearLightsOn = false;
bool flashLightOn = false;
bool brakeModeOn = false;
int previousSteer = 0;
uint32_t steerStartedAt = 0;
uint32_t lastCommandAt = 0;
volatile bool otaInProgress = false;
bool otaStarted = false;
bool otaFailed = false;
bool otaComplete = false;
bool otaForbidden = false;
volatile bool rebootPending = false;
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
    main { max-width: 640px; margin: auto; padding: 22px; }
    h1 { font-size: 1.45rem; margin: 0 0 18px; }
    .video-wrap { position: relative; }
    .video { display: block; width: 100%; aspect-ratio: 4 / 3; object-fit: contain;
             background: #000; border-radius: 12px; }
    #cameraStatus { color: #9ad; min-height: 1.4em; margin: 8px 0 18px; }
    .controls { display: grid; grid-template-columns: 1fr 1fr; gap: 24px; }
    .lights { position: absolute; inset: auto 8px 8px; display: grid;
              grid-template-columns: 1fr 1fr; gap: 8px; }
    .lights button { min-height: 46px; padding: 6px; font-size: .88rem;
                     background: rgba(20, 20, 20, .85); border: 1px solid #777; }
    .lights button[aria-pressed="true"] { background: rgba(18, 100, 65, .94); }
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
  <h1>ESP32 Drift Car</h1>
  <div class="video-wrap">
    <img id="camera" class="video" alt="Live view from the car camera">
    <div class="lights" aria-label="Light controls">
      <button id="frontLights" type="button" aria-pressed="false">Front: Off</button>
      <button id="rearLights" type="button" aria-pressed="false">Rear: Off</button>
      <button id="brakeMode" type="button" aria-pressed="false">Brake mode: Off</button>
      <button id="flashLight" type="button" aria-pressed="false">Flash: Off</button>
    </div>
  </div>
  <p id="cameraStatus">Connecting to camera…</p>
  <div class="speed">
    <label>Drive power: <span id="speedValue">180</span>/255</label>
    <input id="speed" type="range" min="80" max="255" value="180">
  </div>
  <div class="speed">
    <label>Steering hold power: <span id="holdValue">210</span>/255</label>
    <input id="hold" type="range" min="150" max="255" step="5" value="210">
    <small>Use the lowest setting that keeps the wheels turned.</small>
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
  <p id="lightStatus"></p>
  <p id="status">Connect to the car, then hold a button.</p>
  <p><a href="/update" style="color:#9ad">Firmware update</a></p>
</main>
<script>
  let driveDirection = 0;
  let steerDirection = 0;
  let drivePower = 180;
  let steerHoldPower = 210;
  let requestPending = false;

  const speed = document.getElementById('speed');
  const speedValue = document.getElementById('speedValue');
  const hold = document.getElementById('hold');
  const holdValue = document.getElementById('holdValue');
  const status = document.getElementById('status');
  const camera = document.getElementById('camera');
  const cameraStatus = document.getElementById('cameraStatus');
  const frontLights = document.getElementById('frontLights');
  const rearLights = document.getElementById('rearLights');
  const flashLight = document.getElementById('flashLight');
  const brakeMode = document.getElementById('brakeMode');
  const lightStatus = document.getElementById('lightStatus');
  const lightButtons = [frontLights, rearLights, brakeMode, flashLight];
  let lights = { front: false, rear: false, flash: false, brake: false, rearEffective: false };
  let lightsBusy = false;

  function renderLights() {
    frontLights.textContent = `Front: ${lights.front ? 'On' : 'Off'}`;
    rearLights.textContent = lights.rear ? 'Rear: On' :
      (lights.rearEffective ? 'Rear: Auto' : 'Rear: Off');
    flashLight.textContent = `Flash: ${lights.flash ? 'On' : 'Off'}`;
    brakeMode.textContent = `Brake mode: ${lights.brake ? 'On' : 'Off'}`;
    frontLights.setAttribute('aria-pressed', String(lights.front));
    rearLights.setAttribute('aria-pressed', String(lights.rear));
    flashLight.setAttribute('aria-pressed', String(lights.flash));
    brakeMode.setAttribute('aria-pressed', String(lights.brake));
    lightStatus.textContent = lights.rearEffective && !lights.rear
      ? 'Rear lights on automatically while reversing.' : '';
  }

  async function setLights(changes) {
    if (lightsBusy) return;
    lightsBusy = true;
    lightButtons.forEach(button => button.disabled = true);
    try {
      const next = { ...lights, ...changes };
      const query = `front=${Number(next.front)}&rear=${Number(next.rear)}` +
                    `&flash=${Number(next.flash)}&brake=${Number(next.brake)}`;
      const response = await fetch(`/lights?${query}`,
                                   { cache: 'no-store' });
      if (!response.ok) throw new Error('Light command failed');
      lights = await response.json();
      renderLights();
    } catch (_) {
      lightStatus.textContent = 'Could not change lights. Check the connection.';
    } finally {
      lightsBusy = false;
      lightButtons.forEach(button => button.disabled = false);
    }
  }

  frontLights.addEventListener('click', () => setLights({ front: !lights.front }));
  rearLights.addEventListener('click', () => setLights({ rear: !lights.rear }));
  flashLight.addEventListener('click', () => setLights({ flash: !lights.flash }));
  brakeMode.addEventListener('click', () => setLights({ brake: !lights.brake }));
  fetch('/lights', { cache: 'no-store' })
    .then(response => { if (!response.ok) throw new Error('Light status failed'); return response.json(); })
    .then(state => { lights = state; renderLights(); })
    .catch(() => lightStatus.textContent = 'Could not read light status.');

  // Video uses a separate HTTP server so its long-lived connection cannot
  // block motor-control requests or the OTA upload page on port 80.
  camera.onload = () => cameraStatus.textContent = 'Live camera';
  camera.onerror = () => cameraStatus.textContent = 'Camera unavailable — check Serial output';
  camera.src = `${location.protocol}//${location.hostname}:81/stream`;

  speed.addEventListener('input', () => {
    drivePower = Number(speed.value);
    speedValue.textContent = drivePower;
  });

  hold.addEventListener('input', () => {
    steerHoldPower = Number(hold.value);
    holdValue.textContent = steerHoldPower;
    sendState();
  });

  function sendState(force = false) {
    if (requestPending && !force) return;
    requestPending = true;
    const drive = driveDirection * drivePower;
    fetch(`/control?drive=${drive}&steer=${steerDirection}&hold=${steerHoldPower}`,
          { cache: 'no-store' })
      .then(async r => {
        if (!r.ok) throw new Error('request failed');
        const state = await r.json();
        lights.rearEffective = state.rearEffective;
        renderLights();
        status.textContent = `Drive ${drive}, steer ${steerDirection}`;
      })
      .catch(() => status.textContent = 'Control connection lost — motors will stop')
      .finally(() => requestPending = false);
  }

  document.querySelectorAll('button[data-axis]').forEach(button => {
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

bool initCamera() {
  // AI-Thinker ESP32-CAM pin map. GPIO12-15 remain dedicated to the motor driver.
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;
  config.pin_pwdn = 32;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;  // 320x240 limits Wi-Fi and memory load.
  config.jpeg_quality = 14;
  const bool hasPsram = psramFound();
  config.fb_count = hasPsram ? 2 : 1;
  config.fb_location = hasPsram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = config.fb_count > 1 ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("ERROR: Camera init failed (0x%x). Controls and OTA remain available.\n", err);
    return false;
  }
  return true;
}

esp_err_t handleStream(httpd_req_t *req) {
  if (!cameraReady || otaInProgress || rebootPending) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Camera unavailable");
  }

  static const char *CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
  static const char *BOUNDARY = "\r\n--frame\r\n";
  if (httpd_resp_set_type(req, CONTENT_TYPE) != ESP_OK) return ESP_FAIL;
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  esp_err_t result = ESP_OK;
  while (!otaInProgress && !rebootPending) {
    camera_fb_t *frame = esp_camera_fb_get();
    if (!frame) {
      Serial.println("ERROR: Camera capture failed.");
      result = ESP_FAIL;
      break;
    }

    char header[64];
    const int length = snprintf(header, sizeof(header),
                                "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                                static_cast<unsigned>(frame->len));
    if (length < 0 || length >= static_cast<int>(sizeof(header))) {
      result = ESP_FAIL;
    } else {
      result = httpd_resp_send_chunk(req, BOUNDARY, strlen(BOUNDARY));
      if (result == ESP_OK) result = httpd_resp_send_chunk(req, header, length);
      if (result == ESP_OK) {
        result = httpd_resp_send_chunk(req,
                                       reinterpret_cast<const char *>(frame->buf),
                                       frame->len);
      }
    }
    esp_camera_fb_return(frame);
    if (result != ESP_OK) break;  // Browser disconnected or send failed.
    delay(1);  // Give control and Wi-Fi tasks time between frames.
  }

  if (result == ESP_OK) httpd_resp_send_chunk(req, nullptr, 0);
  return result;
}

bool startStreamServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81;
  config.stack_size = 8192;
  if (httpd_start(&streamServer, &config) != ESP_OK) return false;

  httpd_uri_t streamUri = {};
  streamUri.uri = "/stream";
  streamUri.method = HTTP_GET;
  streamUri.handler = handleStream;
  if (httpd_register_uri_handler(streamServer, &streamUri) != ESP_OK) {
    httpd_stop(streamServer);
    streamServer = nullptr;
    return false;
  }
  return true;
}

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

void updateRearLight() {
  rearLightsOn = rearLightsManualOn || (brakeModeOn && requestedDrive < 0);
  digitalWrite(REAR_LIGHT_PIN, rearLightsOn ? HIGH : LOW);
}

void stopAllMotors() {
  requestedDrive = 0;
  requestedSteer = 0;
  previousSteer = 0;
  writeBridge(DRIVE_IN1, DRIVE_IN2, 0, DRIVE_REVERSED);
  writeBridge(STEER_IN1, STEER_IN2, 0, STEERING_REVERSED);
  updateRearLight();
}

void setLights(bool front, bool rear, bool flash, bool brake) {
  frontLightsOn = front;
  rearLightsManualOn = rear;
  flashLightOn = flash;
  brakeModeOn = brake;
  digitalWrite(FRONT_LIGHT_PIN, front ? HIGH : LOW);
  digitalWrite(FLASH_LIGHT_PIN, flash ? HIGH : LOW);
  updateRearLight();
}

void sendLightState() {
  server.sendHeader("Cache-Control", "no-store");
  const String state = String("{\"front\":") + (frontLightsOn ? "true" : "false") +
                       ",\"rear\":" + (rearLightsManualOn ? "true" : "false") +
                       ",\"flash\":" + (flashLightOn ? "true" : "false") +
                       ",\"brake\":" + (brakeModeOn ? "true" : "false") +
                       ",\"rearEffective\":" + (rearLightsOn ? "true" : "false") + "}";
  server.send(200, "application/json", state);
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
      (millis() - steerStartedAt < STEER_KICK_MS) ? STEER_KICK_PWM : steeringHoldPwm;
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
  if (server.hasArg("hold")) {
    steeringHoldPwm = constrain(server.arg("hold").toInt(),
                                MIN_STEER_HOLD_PWM, MAX_PWM);
  }
  lastCommandAt = millis();
  applyMotorCommands();
  updateRearLight();
  sendLightState();
}

void handleLights() {
  const bool hasFront = server.hasArg("front");
  const bool hasRear = server.hasArg("rear");
  const bool hasFlash = server.hasArg("flash");
  const bool hasBrake = server.hasArg("brake");
  const bool hasAny = hasFront || hasRear || hasFlash || hasBrake;
  if (hasAny && !(hasFront && hasRear && hasFlash && hasBrake)) {
    server.send(400, "text/plain", "Specify front, rear, flash, and brake states");
    return;
  }

  if (hasAny) {
    if (otaInProgress || rebootPending) {
      server.send(503, "text/plain", "Firmware update in progress; lights are off");
      return;
    }
    const String front = server.arg("front");
    const String rear = server.arg("rear");
    const String flash = server.arg("flash");
    const String brake = server.arg("brake");
    if ((front != "0" && front != "1") || (rear != "0" && rear != "1") ||
        (flash != "0" && flash != "1") || (brake != "0" && brake != "1")) {
      server.send(400, "text/plain", "Light states must be 0 or 1");
      return;
    }
    setLights(front == "1", rear == "1", flash == "1", brake == "1");
  }

  sendLightState();
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
      setLights(false, false, false, false);
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

  // Body lights and onboard flash default off. GPIO2's LED/resistor path to
  // ground does not pull this boot strap pin high during serial flashing.
  digitalWrite(FRONT_LIGHT_PIN, LOW);
  digitalWrite(REAR_LIGHT_PIN, LOW);
  digitalWrite(FLASH_LIGHT_PIN, LOW);
  pinMode(FRONT_LIGHT_PIN, OUTPUT);
  pinMode(REAR_LIGHT_PIN, OUTPUT);
  pinMode(FLASH_LIGHT_PIN, OUTPUT);

  bool pwmOk = true;
  pwmOk &= ledcAttachChannel(DRIVE_IN1, MOTOR_PWM_HZ, MOTOR_PWM_BITS, DRIVE_CH1);
  pwmOk &= ledcAttachChannel(DRIVE_IN2, MOTOR_PWM_HZ, MOTOR_PWM_BITS, DRIVE_CH2);
  pwmOk &= ledcAttachChannel(STEER_IN1, MOTOR_PWM_HZ, MOTOR_PWM_BITS, STEER_CH1);
  pwmOk &= ledcAttachChannel(STEER_IN2, MOTOR_PWM_HZ, MOTOR_PWM_BITS, STEER_CH2);

  stopAllMotors();
  if (!pwmOk) {
    Serial.println("ERROR: Could not attach one or more motor PWM pins.");
  }

  const bool cameraInitialized = initCamera();

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("ERROR: Could not start Wi-Fi access point.");
  }

  server.collectHeaders(OTA_REQUEST_HEADERS, 2);
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/control", HTTP_GET, handleControl);
  server.on("/lights", HTTP_GET, handleLights);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateComplete, handleUpdateUpload);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not found");
  });
  server.begin();

  if (cameraInitialized) {
    cameraReady = startStreamServer();
    if (!cameraReady) {
      Serial.println("ERROR: Could not start camera stream server on port 81.");
      esp_camera_deinit();
    }
  }

  lastCommandAt = millis();
  Serial.println();
  Serial.print("Connect to Wi-Fi: ");
  Serial.println(AP_SSID);
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());
  Serial.print("Firmware update: http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/update");
  if (cameraReady) {
    Serial.print("Camera stream: http://");
    Serial.print(WiFi.softAPIP());
    Serial.println(":81/stream");
  }
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
