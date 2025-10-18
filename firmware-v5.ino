#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Wire.h>

// --- Motor pins ---
#define EN_PIN   6
#define DIR_PIN  5
#define STEP_PIN 4

// --- BMI160 ---
#define BMI160_ADDR 0x69
#define BMI160_CMD_REG 0x7E
#define BMI160_GYRO_DATA 0x0C

// --- WiFi ---
const char* ssid = "GPOINTER";
const char* password = "12345678";

WebServer server(80);
Preferences preferences;

// --- Motor control ---
int speedPercent = 50;
int moveSteps = 200;
int scanSteps = 10;
float scanDelay = 1.0;
bool scanning = false;
bool scanDirection = true;
unsigned long lastScanTime = 0;

// --- Gyro Hold режим ---
bool holdMode = false;
float targetYaw = 0;          // Целевое направление
float currentYaw = 0;         // Текущее направление
float gyroOffsetZ = 0;
const float GYRO_SENSITIVITY = 16.384; // same as before
float holdThreshold = 5.0;
float holdKp = 0.8;
unsigned long lastGyroUpdate = 0;

// --- Settings Save/Load ---
void saveSettings() {
  preferences.begin("settings", false);
  preferences.putInt("speed", speedPercent);
  preferences.putInt("moveSteps", moveSteps);
  preferences.putInt("scanSteps", scanSteps);
  preferences.putFloat("scanDelay", scanDelay);
  preferences.putFloat("holdThresh", holdThreshold);
  preferences.putFloat("holdKp", holdKp);
  preferences.end();
  Serial.println("Settings saved");
}

void loadSettings() {
  preferences.begin("settings", true);
  speedPercent = preferences.getInt("speed", 50);
  moveSteps = preferences.getInt("moveSteps", 200);
  scanSteps = preferences.getInt("scanSteps", 10);
  scanDelay = preferences.getFloat("scanDelay", 1.0);
  holdThreshold = preferences.getFloat("holdThresh", 5.0);
  holdKp = preferences.getFloat("holdKp", 0.8);
  preferences.end();
  Serial.println("Settings loaded");
}

// ---------------- LittleFS ----------------
void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (!f) {
    server.send(500, "text/plain", "File not found");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

// ---------------- BMI160 I2C helpers (manual) ----------------
void writeBMI160(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BMI160_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

void readBMI160(uint8_t reg, uint8_t *buffer, uint8_t len) {
  Wire.beginTransmission(BMI160_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(BMI160_ADDR, len);
  for (int i = 0; i < len; i++) {
    if (Wire.available()) buffer[i] = Wire.read();
    else buffer[i] = 0;
  }
}

void initGyro() {
  pinMode(8, INPUT_PULLUP);
  pinMode(9, INPUT_PULLUP);
  delay(100);
  Wire.begin(8, 9);
  Wire.setClock(100000);
  delay(100);

  uint8_t chipId = 0;
  readBMI160(0x00, &chipId, 1);
  Serial.printf("BMI160 Chip ID: 0x%02X\n", chipId);

  if (chipId != 0xD1) {
    Serial.println("BMI160 not found!");
  } else {
    writeBMI160(BMI160_CMD_REG, 0xB6); delay(100);
    writeBMI160(BMI160_CMD_REG, 0x11); delay(50);
    writeBMI160(BMI160_CMD_REG, 0x15); delay(100);
    writeBMI160(0x43, 0x03); delay(10);

    Serial.println("Calibrating gyro...");
    long sumZ = 0;
    for (int i = 0; i < 100; i++) {
      uint8_t data[6];
      readBMI160(BMI160_GYRO_DATA, data, 6);
      int16_t gz = (data[5] << 8) | data[4];
      sumZ += gz;
      delay(10);
    }
    gyroOffsetZ = sumZ / 100.0;
    Serial.printf("Gyro offset Z: %.2f\n", gyroOffsetZ);
    Serial.println("BMI160 ready!");
  }

  lastGyroUpdate = millis();
}

float readGyroYaw() {
  unsigned long now = millis();
  float dt = (now - lastGyroUpdate) / 1000.0;
  if (dt <= 0) dt = 0.001;
  lastGyroUpdate = now;

  uint8_t data[6] = {0};
  readBMI160(BMI160_GYRO_DATA, data, 6);
  int16_t gz = (data[5] << 8) | data[4];

  float gz_dps = (gz - gyroOffsetZ) / GYRO_SENSITIVITY;
  currentYaw += gz_dps * dt;

  while (currentYaw > 180.0) currentYaw -= 360.0;
  while (currentYaw <= -180.0) currentYaw += 360.0;

  return currentYaw;
}

// ---------------- Motor functions ----------------
void rotateStepsBlocking(bool dir, int steps, int speed) {
  digitalWrite(EN_PIN, LOW);
  digitalWrite(DIR_PIN, dir);
  int delay_us = map(speed, 0, 100, 2000, 80);
  if (delay_us < 80) delay_us = 80;

  for (int i = 0; i < steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(delay_us);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(delay_us);
  }
  digitalWrite(EN_PIN, HIGH);
}

void rotateStepsInterruptible(bool dir, int steps, int speed) {
  digitalWrite(EN_PIN, LOW);
  digitalWrite(DIR_PIN, dir);
  int delay_us = map(speed, 0, 100, 2000, 80);
  if (delay_us < 80) delay_us = 80;

  for (int i = 0; i < steps; i++) {
    if (!scanning) break; // stop immediately
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(delay_us);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(delay_us);

    server.handleClient();
    yield();
  }
  digitalWrite(EN_PIN, HIGH);
}

// ---------------- Hold Correction ----------------
void holdDirectionCorrection() {
  float error = targetYaw - currentYaw;
  while (error > 180) error -= 360;
  while (error < -180) error += 360;
  if (fabs(error) > holdThreshold) {
    int correctionSteps = (int)(fabs(error) * holdKp);
    correctionSteps = constrain(correctionSteps, 1, 50);
    bool dir = (error > 0);
    rotateStepsBlocking(dir, correctionSteps, speedPercent);
    Serial.printf("Hold correction: error=%.1f°, steps=%d, dir=%s\n",
                  error, correctionSteps, dir ? "CW" : "CCW");
  }
}

// ---------------- Web Handlers ----------------
void handleSpeed() {
  if (server.hasArg("value")) {
    speedPercent = constrain(server.arg("value").toInt(), 0, 100);
    saveSettings();
    Serial.printf("Speed set via web: %d%%\n", speedPercent);
  }
  server.send(200, "text/plain", "OK");
}

void handleMoveSteps() {
  if (server.hasArg("value")) {
    moveSteps = constrain(server.arg("value").toInt(), 1, 2000);
    saveSettings();
    Serial.printf("MoveSteps set: %d\n", moveSteps);
  }
  server.send(200, "text/plain", "OK");
}

void handleScanSteps() {
  if (server.hasArg("value")) {
    scanSteps = constrain(server.arg("value").toInt(), 1, 3000);
    saveSettings();
    Serial.printf("ScanSteps set: %d\n", scanSteps);
  }
  server.send(200, "text/plain", "OK");
}

void handleScanDelay() {
  if (server.hasArg("value")) {
    scanDelay = constrain(server.arg("value").toFloat(), 0.0, 10.0);
    saveSettings();
    Serial.printf("ScanDelay set: %.2f\n", scanDelay);
  }
  server.send(200, "text/plain", "OK");
}

void handleLeft() {
  Serial.println("WEB: LEFT pressed");
  rotateStepsBlocking(false, moveSteps, speedPercent);
  server.send(200, "text/plain", "Left pressed");
}

void handleRight() {
  Serial.println("WEB: RIGHT pressed");
  rotateStepsBlocking(true, moveSteps, speedPercent);
  server.send(200, "text/plain", "Right pressed");
}

void handleScan() {
  scanning = !scanning;
  if (scanning && holdMode) holdMode = false;
  Serial.printf("Scan toggled -> %s\n", scanning ? "ON" : "OFF");
  server.send(200, "text/plain", scanning ? "Scan ON" : "Scan OFF");
}

void handleHold() {
  holdMode = !holdMode;
  if (holdMode) {
    targetYaw = currentYaw;
    scanning = false;
  }
  Serial.printf("Hold toggled -> %s\n", holdMode ? "ON" : "OFF");
  server.send(200, "text/plain", holdMode ? "Hold ON" : "Hold OFF");
}

void handleGyroStatus() {
  String json = "{";
  json += "\"yaw\":" + String(currentYaw, 1) + ",";
  json += "\"target\":" + String(targetYaw, 1) + ",";
  json += "\"hold\":" + String(holdMode ? "true" : "false") + ",";
  json += "\"threshold\":" + String(holdThreshold, 1) + ",";
  json += "\"kp\":" + String(holdKp, 2);
  json += "}";
  server.send(200, "application/json", json);
}

void handleHoldThreshold() {
  if (server.hasArg("value")) {
    holdThreshold = constrain(server.arg("value").toFloat(), 0.5, 20.0);
    saveSettings();
    Serial.printf("HoldThreshold set: %.2f\n", holdThreshold);
  }
  server.send(200, "text/plain", "OK");
}

void handleHoldKp() {
  if (server.hasArg("value")) {
    holdKp = constrain(server.arg("value").toFloat(), 0.1, 5.0);
    saveSettings();
    Serial.printf("HoldKp set: %.2f\n", holdKp);
  }
  server.send(200, "text/plain", "OK");
}

void handleResetYaw() {
  currentYaw = 0;
  targetYaw = 0;
  Serial.println("Yaw reset to 0");
  server.send(200, "text/plain", "Yaw reset");
}

void handleGetSettings() {
  String json = "{";
  json += "\"speed\":" + String(speedPercent) + ",";
  json += "\"moveSteps\":" + String(moveSteps) + ",";
  json += "\"scanSteps\":" + String(scanSteps) + ",";
  json += "\"scanDelay\":" + String(scanDelay, 1) + ",";
  json += "\"threshold\":" + String(holdThreshold, 1) + ",";
  json += "\"kp\":" + String(holdKp, 2);
  json += "}";
  server.send(200, "application/json", json);
}

// ---------------- Setup ----------------
unsigned long lastSerialDebug = 0;

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(EN_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);

  loadSettings();
  initGyro();

  if (!LittleFS.begin()) Serial.println("LittleFS mount failed");
  else Serial.println("LittleFS mounted");

  WiFi.softAP(ssid, password);
  Serial.printf("AP started: SSID=%s, IP=%s\n", ssid, WiFi.softAPIP().toString().c_str());

  server.on("/", handleRoot);
  server.on("/speed", handleSpeed);
  server.on("/moveSteps", handleMoveSteps);
  server.on("/scanSteps", handleScanSteps);
  server.on("/scanDelay", handleScanDelay);
  server.on("/left", handleLeft);
  server.on("/right", handleRight);
  server.on("/scan", handleScan);
  server.on("/hold", handleHold);
  server.on("/gyro", handleGyroStatus);
  server.on("/holdThreshold", handleHoldThreshold);
  server.on("/holdKp", handleHoldKp);
  server.on("/resetYaw", handleResetYaw);
  server.on("/getSettings", handleGetSettings);
  server.begin();

  lastSerialDebug = millis();
  Serial.println("System ready!");
}

// ---------------- Loop ----------------
void loop() {
  server.handleClient();
  readGyroYaw();

  if (holdMode) holdDirectionCorrection();

  if (scanning) {
    unsigned long now = millis();
    if (now - lastScanTime >= (unsigned long)(scanDelay * 1000.0)) {
      lastScanTime = now;
      rotateStepsInterruptible(scanDirection, scanSteps, speedPercent);
      scanDirection = !scanDirection;
    }
  }

  if (millis() - lastSerialDebug >= 1000) {
    lastSerialDebug = millis();
    Serial.printf("DBG: yaw=%.2f target=%.2f hold=%s scan=%s speed=%d moveSteps=%d scanSteps=%d\n",
                  currentYaw, targetYaw,
                  holdMode ? "ON" : "OFF",
                  scanning ? "ON" : "OFF",
                  speedPercent, moveSteps, scanSteps);
  }
}
