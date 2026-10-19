#include <WiFi.h>
#include <WebServer.h>

WebServer server(80);

const char* ap_ssid = "Porsche_RC";
const char* ap_pass = "12345678";

const int REAR_IN1 = 13;
const int REAR_IN2 = 12;

const int STEER_IN1 = 15;
const int STEER_IN2 = 14;

unsigned long lastCmdMs = 0;
const unsigned long timeoutMs = 2000;

void motorStop(int in1, int in2) {
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
}

void motorForward(int in1, int in2) {
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);
}

void motorReverse(int in1, int in2) {
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);
}

void setMotor(int in1, int in2, int dir) {
  if (dir > 0) motorForward(in1, in2);
  else if (dir < 0) motorReverse(in1, in2);
  else motorStop(in1, in2);
}

const char PAGE[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 RC</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      margin: 20px;
    }
    .row {
      margin: 10px 0;
    }
    button {
      width: 90px;
      height: 60px;
      font-size: 24px;
      margin: 6px;
    }
  </style>
</head>
<body>
  <h2>ESP32 RC Controller</h2>
  <p>Keyboard: W A S D</p>
  <p>Touch buttons also work</p>

  <div class="row">
    <button onmousedown="press('w')" onmouseup="release('w')" ontouchstart="press('w')" ontouchend="release('w')">W</button>
  </div>
  <div class="row">
    <button onmousedown="press('a')" onmouseup="release('a')" ontouchstart="press('a')" ontouchend="release('a')">A</button>
    <button onmousedown="press('s')" onmouseup="release('s')" ontouchstart="press('s')" ontouchend="release('s')">S</button>
    <button onmousedown="press('d')" onmouseup="release('d')" ontouchstart="press('d')" ontouchend="release('d')">D</button>
  </div>

<script>
let rear = 0;
let steer = 0;
let sendTimer = null;

async function send() {
  try {
    await fetch("/drive?rear=" + rear + "&steer=" + steer, { cache: "no-store" });
  } catch (e) {}
}

function startSending() {
  if (!sendTimer) {
    send();
    sendTimer = setInterval(send, 100);
  }
}

function stopSendingIfIdle() {
  if (rear === 0 && steer === 0 && sendTimer) {
    clearInterval(sendTimer);
    sendTimer = null;
    send();
  }
}

function press(k) {
  if (k === "w") rear = 1;
  if (k === "s") rear = -1;
  if (k === "a") steer = -1;
  if (k === "d") steer = 1;
  startSending();
}

function release(k) {
  if (k === "w" && rear === 1) rear = 0;
  if (k === "s" && rear === -1) rear = 0;
  if (k === "a" && steer === -1) steer = 0;
  if (k === "d" && steer === 1) steer = 0;

  if (rear !== 0 || steer !== 0) {
    send();
  } else {
    stopSendingIfIdle();
  }
}

window.addEventListener("keydown", (e) => {
  const k = e.key.toLowerCase();
  if ("wasd".includes(k)) {
    e.preventDefault();
    press(k);
  }
});

window.addEventListener("keyup", (e) => {
  const k = e.key.toLowerCase();
  if ("wasd".includes(k)) {
    e.preventDefault();
    release(k);
  }
});

window.addEventListener("blur", () => {
  rear = 0;
  steer = 0;
  stopSendingIfIdle();
});
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send(200, "text/html", PAGE);
}

void handleDrive() {
  int rear = 0;
  int steer = 0;

  if (server.hasArg("rear")) rear = server.arg("rear").toInt();
  if (server.hasArg("steer")) steer = server.arg("steer").toInt();

  setMotor(REAR_IN1, REAR_IN2, rear);
  setMotor(STEER_IN1, STEER_IN2, steer);

  lastCmdMs = millis();
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(REAR_IN1, OUTPUT);
  pinMode(REAR_IN2, OUTPUT);
  pinMode(STEER_IN1, OUTPUT);
  pinMode(STEER_IN2, OUTPUT);

  motorStop(REAR_IN1, REAR_IN2);
  motorStop(STEER_IN1, STEER_IN2);

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  bool ok = WiFi.softAP(ap_ssid, ap_pass, 1, 0, 4);

  Serial.println();
  Serial.print("softAP() = ");
  Serial.println(ok ? "OK" : "FAIL");
  Serial.print("AP SSID: ");
  Serial.println(ap_ssid);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/drive", handleDrive);
  server.begin();

  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  if (millis() - lastCmdMs > timeoutMs) {
    motorStop(REAR_IN1, REAR_IN2);
    motorStop(STEER_IN1, STEER_IN2);
  }
}