const int motorPwmPin = D2;
const int motorIn1Pin = D3;
const int motorIn2Pin = D4;
const int motorStbyPin = D5;

const int pwmFreq = 20000;
const int pwmResolution = 10;
const int pwmMax = 1023;

String inputString = "";
bool inputComplete = false;

void setMotorPercent(int percent) {
  if (percent > 100) percent = 100;
  if (percent < -100) percent = -100;

  if (percent > 0) {
    digitalWrite(motorIn1Pin, HIGH);
    digitalWrite(motorIn2Pin, LOW);
  } else if (percent < 0) {
    digitalWrite(motorIn1Pin, LOW);
    digitalWrite(motorIn2Pin, HIGH);
  } else {
    digitalWrite(motorIn1Pin, LOW);
    digitalWrite(motorIn2Pin, LOW);
  }

  int duty = (abs(percent) * pwmMax) / 100;
  ledcWrite(motorPwmPin, duty);

  Serial.print("Motor percent = ");
  Serial.print(percent);
  Serial.print("%  | raw duty = ");
  Serial.println(duty);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(motorIn1Pin, OUTPUT);
  pinMode(motorIn2Pin, OUTPUT);
  pinMode(motorStbyPin, OUTPUT);

  digitalWrite(motorStbyPin, HIGH);

  bool ok = ledcAttach(motorPwmPin, pwmFreq, pwmResolution);
  if (!ok) {
    Serial.println("PWM attach failed");
    while (1) {}
  }

  setMotorPercent(0);

  Serial.println("Enter motor percent from -100 to 100");
  Serial.println("Positive = forward, negative = reverse, 0 = stop");
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n' || c == '\r') {
      if (inputString.length() > 0) {
        inputComplete = true;
      }
    } else {
      inputString += c;
    }
  }

  if (inputComplete) {
    int percent = inputString.toInt();
    setMotorPercent(percent);

    inputString = "";
    inputComplete = false;
  }
}