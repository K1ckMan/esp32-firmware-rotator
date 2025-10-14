#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Preferences.h>

// Motor pins
#define EN_PIN   6
#define DIR_PIN  5
#define STEP_PIN 4

// BMI160
#define BMI160_ADDR 0x69
#define BMI160_CMD_REG 0x7E
#define BMI160_GYRO_DATA 0x0C

const char* ssid = "GPOINTER";
const char* password = "12345678";

WebServer server(80);
Preferences preferences;

// Motor control
int speedPercent = 50;
int moveSteps = 200;
int scanSteps = 10;
float scanDelay = 1.0;
bool scanning = false;
unsigned long lastScanTime = 0;
bool scanDirection = true;

// Gyro Hold режим
bool holdMode = false;
float targetYaw = 0;          // Целевое направление
float currentYaw = 0;         // Текущее направление
float gyroOffsetZ = 0;
const float GYRO_SENSITIVITY = 16.384;
float holdThreshold = 5.0;    // Погрешность в градусах (более мягкая)
float holdKp = 0.8;           // Коэффициент корректировки (менее агрессивный)
unsigned long lastGyroUpdate = 0;

String currentFirmwareVersion = "beta v0.3";

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

// --- BMI160 Functions ---
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
  }
}

void initGyro() {
  pinMode(8, INPUT_PULLUP);
  pinMode(9, INPUT_PULLUP);
  delay(100);
  
  Wire.begin(8, 9);
  Wire.setClock(100000);
  delay(100);
  
  uint8_t chipId;
  readBMI160(0x00, &chipId, 1);
  Serial.printf("BMI160 Chip ID: 0x%02X\n", chipId);
  
  if (chipId != 0xD1) {
    Serial.println("BMI160 not found!");
    return;
  }
  
  // Инициализация BMI160
  writeBMI160(BMI160_CMD_REG, 0xB6); delay(100);
  writeBMI160(BMI160_CMD_REG, 0x11); delay(50);
  writeBMI160(BMI160_CMD_REG, 0x15); delay(100);
  writeBMI160(0x43, 0x03); delay(10);
  
  // Калибровка
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

float readGyroYaw() {
  unsigned long now = millis();
  float dt = (now - lastGyroUpdate) / 1000.0;
  lastGyroUpdate = now;
  
  uint8_t data[6];
  readBMI160(BMI160_GYRO_DATA, data, 6);
  
  int16_t gz = (data[5] << 8) | data[4];
  float gz_dps = (gz - gyroOffsetZ) / GYRO_SENSITIVITY;
  currentYaw += gz_dps * dt;
  
  // Нормализация 0-360
  while (currentYaw < 0) currentYaw += 360.0;
  while (currentYaw >= 360.0) currentYaw -= 360.0;
  
  return currentYaw;
}

// --- Motor Functions ---
void rotateSteps(bool dir, int steps, int delay_us) {
  digitalWrite(EN_PIN, LOW);
  digitalWrite(DIR_PIN, dir);
  for (int i = 0; i < steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(delay_us);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(delay_us);
  }
}

void holdDirectionCorrection() {
  float error = targetYaw - currentYaw;
  
  // Нормализация ошибки (-180 до 180)
  while (error > 180) error -= 360;
  while (error < -180) error += 360;
  
  if (fabs(error) > holdThreshold) {
    int correctionSteps = (int)(fabs(error) * holdKp);
    correctionSteps = constrain(correctionSteps, 1, 50);
    
    int delay_us = map(speedPercent, 0, 100, 2000, 200);
    bool dir = (error > 0);
    
    rotateSteps(dir, correctionSteps, delay_us);
    Serial.printf("Hold correction: error=%.1f°, steps=%d, dir=%s\n", 
                  error, correctionSteps, dir ? "CW" : "CCW");
  }
}

// --- Web Handlers ---
void handleLeft() {
  int delay_us = map(speedPercent, 0, 100, 2000, 200);
  rotateSteps(false, moveSteps, delay_us);
  Serial.printf("LEFT %d steps\n", moveSteps);
  server.send(200, "text/plain", "Left pressed");
}

void handleRight() {
  int delay_us = map(speedPercent, 0, 100, 2000, 200);
  rotateSteps(true, moveSteps, delay_us);
  Serial.printf("RIGHT %d steps\n", moveSteps);
  server.send(200, "text/plain", "Right pressed");
}

void handleSpeed() {
  if (server.hasArg("value")) {
    speedPercent = constrain(server.arg("value").toInt(),0,100);
    saveSettings();
  }
  server.send(200,"text/plain","OK");
}

void handleMoveSteps() {
  if (server.hasArg("value")) {
    moveSteps = constrain(server.arg("value").toInt(),1,2000);
    saveSettings();
  }
  server.send(200,"text/plain","OK");
}

void handleScanSteps() {
  if (server.hasArg("value")) {
    scanSteps = constrain(server.arg("value").toInt(),1,3000);
    saveSettings();
  }
  server.send(200,"text/plain","OK");
}

void handleScanDelay() {
  if (server.hasArg("value")) {
    scanDelay = constrain(server.arg("value").toFloat(),0,5.0);
    saveSettings();
  }
  server.send(200,"text/plain","OK");
}

void handleScan() {
  scanning = !scanning;
  if (scanning && holdMode) holdMode = false; // Выключаем Hold при Scan
  Serial.printf("Scan %s\n", scanning ? "started" : "stopped");
  server.send(200,"text/plain",scanning ? "Scan ON":"Scan OFF");
}

void handleHold() {
  holdMode = !holdMode;
  if (holdMode) {
    scanning = false; // Выключаем Scan при Hold
    targetYaw = currentYaw; // Запоминаем текущее направление
    Serial.printf("Hold ENABLED at %.1f°\n", targetYaw);
  } else {
    Serial.println("Hold DISABLED");
  }
  server.send(200,"text/plain", holdMode ? "Hold ON":"Hold OFF");
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
    Serial.printf("Hold Threshold set to %.1f\n", holdThreshold);
  }
  server.send(200, "text/plain", "OK");
}

void handleHoldKp() {
  if (server.hasArg("value")) {
    holdKp = constrain(server.arg("value").toFloat(), 0.1, 5.0);
    saveSettings();
    Serial.printf("Hold Kp set to %.2f\n", holdKp);
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

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>ESP Motor + Gyro</title>
<style>
body{font-family:Arial,sans-serif;margin:0;padding:0;background:#000;color:#fff;-webkit-user-select:none;user-select:none;touch-action:none;}
.tabs{display:flex;background:#111;}
.tabs button{flex:1;padding:14px;background:#111;color:#fff;border:none;font-size:18px;}
.tabs button.active{background:#333;}
.tab-content{display:none;padding:20px;background:#000;min-height:80vh;}
.tab-content.active{display:block;}
.btn{display:block;width:100%;margin:15px 0;padding:24px;font-size:26px;font-weight:bold;border:none;border-radius:8px;background:#444;color:#fff;}
.btn:active{background:#666;}
.btn.hold-active{background:#00aa00;}
.row{display:flex;gap:12px;margin:20px 0;}
.row .btn{flex:1;font-size:28px;padding:30px;}
.slider-container{margin:20px 0;text-align:center;}
.slider-container label{display:block;margin-bottom:8px;font-size:20px;}
input[type=range]{width:100%;}
.status{margin-top:20px;font-size:22px;font-weight:bold;text-align:center;}
.gyro-info{background:#111;padding:15px;margin:20px 0;border-radius:8px;font-size:18px;}
.log{background:#111;padding:10px;border:1px solid #444;height:200px;overflow-y:auto;font-size:14px;}
</style>
</head>
<body>

<div class="tabs">
  <button class="tablink active" onclick="openTab(event,'remote')">Remote</button>
  <button class="tablink" onclick="openTab(event,'gyro')">Gyro</button>
  <button class="tablink" onclick="openTab(event,'firmware')">Firmware</button>
  <button class="tablink" onclick="openTab(event,'debug')">Debug</button>
</div>

<div id="remote" class="tab-content active">
  <div class="row">
    <button class="btn" id="btnLeft">Left</button>
    <button class="btn" id="btnRight">Right</button>
  </div>
  
  <button class="btn" id="btnHold">Hold Direction</button>
  <button class="btn" id="btnScan">Scan</button>
  
  <div class="slider-container">
    <label>Speed: <span id="speedValue">50%</span></label>
    <input type="range" id="speed" min="0" max="100" value="50">
  </div>
  <div class="slider-container">
    <label>Move Range: <span id="moveValue">200</span></label>
    <input type="range" id="moveRange" min="1" max="2000" value="200">
  </div>
  <div class="slider-container">
    <label>Scan Range: <span id="scanValue">10</span></label>
    <input type="range" id="scanRange" min="1" max="3000" value="10">
  </div>
  <div class="slider-container">
    <label>Scan Delay: <span id="delayValue">1.0</span></label>
    <input type="range" id="delayRange" min="0" max="5" step="0.1" value="1.0">
  </div>
  
  <div class="status" id="statusText">Standby</div>
</div>

<div id="gyro" class="tab-content">
  <h2>Gyroscope Settings</h2>
  <div class="gyro-info">
    <div>Current Yaw: <b id="gyroCurrentYaw">0.0</b></div>
    <div>Target Yaw: <b id="gyroTargetYaw">0.0</b></div>
    <div>Error: <b id="gyroYawError">0.0</b></div>
    <div>Status: <b id="gyroStatus">Standby</b></div>
  </div>
  
  <div class="slider-container">
    <label>Hold Threshold: <span id="thresholdValue">5.0</span></label>
    <input type="range" id="threshold" min="0.5" max="20" step="0.5" value="5.0">
    <small style="color:#888;">Minimum deviation to trigger correction</small>
  </div>
  
  <div class="slider-container">
    <label>Hold Correction Power (Kp): <span id="kpValue">0.80</span></label>
    <input type="range" id="kp" min="0.1" max="5.0" step="0.1" value="0.8">
    <small style="color:#888;">Strength of direction correction</small>
  </div>
  
  <button class="btn" id="btnResetYaw">Reset Yaw to 0</button>
</div>

<div id="firmware" class="tab-content">
  <div>Current Version: <b>beta v0.3</b></div>
  <div>New Version: <b id="newFirmwareVersion">Checking...</b></div>
  <button class="btn" onclick="checkFirmwareVersion()">Check Update</button>
</div>

<div id="debug" class="tab-content">
  <h3>Events Log</h3>
  <div class="log" id="eventLog"></div>
  
  <h3 style="margin-top:30px;">Gyroscope Data</h3>
  <div class="gyro-info">
    <div>Current Yaw: <b id="debugYaw">0.0</b></div>
    <div>Target Yaw: <b id="debugTarget">0.0</b></div>
    <div>Error: <b id="debugError">0.0</b></div>
    <div>Threshold: <b id="debugThreshold">5.0</b></div>
    <div>Kp: <b id="debugKp">0.80</b></div>
    <div>Hold Active: <b id="debugHold">No</b></div>
  </div>
</div>

<script>
let scanState=false, holdState=false;

function openTab(evt,id){
  document.querySelectorAll('.tab-content').forEach(e=>e.classList.remove('active'));
  document.querySelectorAll('.tablink').forEach(e=>e.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  evt.currentTarget.classList.add('active');
}

function logEvent(t){
  let l=document.getElementById("eventLog"), ts=new Date().toLocaleTimeString();
  l.innerHTML=`<div>[${ts}] ${t}</div>`+l.innerHTML;
}

function sendCommand(cmd){fetch(cmd).then(r=>r.text()).then(t=>logEvent(t)).catch(e=>logEvent("Error:"+e));}

function holdCommand(cmd){
  sendCommand(cmd);
  let holdInterval=setInterval(()=>sendCommand(cmd),100);
  const clearHold=()=>clearInterval(holdInterval);
  document.addEventListener("mouseup",clearHold,{once:true});
  document.addEventListener("touchend",clearHold,{once:true});
}

// Gyro status update
async function updateGyroStatus(){
  try{
    const resp = await fetch("/gyro");
    const data = await resp.json();
    
    let error = data.target - data.yaw;
    while(error > 180) error -= 360;
    while(error < -180) error += 360;
    
    // Gyro tab
    document.getElementById("gyroCurrentYaw").innerText = data.yaw.toFixed(1);
    document.getElementById("gyroTargetYaw").innerText = data.target.toFixed(1);
    document.getElementById("gyroYawError").innerText = error.toFixed(1);
    document.getElementById("gyroStatus").innerText = data.hold ? "HOLDING" : "Standby";
    
    // Debug tab
    document.getElementById("debugYaw").innerText = data.yaw.toFixed(1);
    document.getElementById("debugTarget").innerText = data.target.toFixed(1);
    document.getElementById("debugError").innerText = error.toFixed(1);
    document.getElementById("debugThreshold").innerText = data.threshold.toFixed(1);
    document.getElementById("debugKp").innerText = data.kp.toFixed(2);
    document.getElementById("debugHold").innerText = data.hold ? "Yes" : "No";
  }catch(e){}
}
setInterval(updateGyroStatus, 200);

// Load saved settings on startup
async function loadSavedSettings(){
  try{
    const resp = await fetch("/getSettings");
    const data = await resp.json();
    document.getElementById("speed").value = data.speed;
    document.getElementById("speedValue").innerText = data.speed+"%";
    document.getElementById("moveRange").value = data.moveSteps;
    document.getElementById("moveValue").innerText = data.moveSteps;
    document.getElementById("scanRange").value = data.scanSteps;
    document.getElementById("scanValue").innerText = data.scanSteps;
    document.getElementById("delayRange").value = data.scanDelay;
    document.getElementById("delayValue").innerText = data.scanDelay;
    document.getElementById("threshold").value = data.threshold;
    document.getElementById("thresholdValue").innerText = data.threshold;
    document.getElementById("kp").value = data.kp;
    document.getElementById("kpValue").innerText = data.kp.toFixed(2);
  }catch(e){}
}

// Buttons
document.getElementById("btnLeft").addEventListener("mousedown",()=>holdCommand("/left"));
document.getElementById("btnLeft").addEventListener("touchstart",()=>holdCommand("/left"));
document.getElementById("btnRight").addEventListener("mousedown",()=>holdCommand("/right"));
document.getElementById("btnRight").addEventListener("touchstart",()=>holdCommand("/right"));

// Hold button
document.getElementById("btnHold").onclick = ()=>{
  holdState=!holdState;
  scanState=false;
  document.getElementById("btnHold").classList.toggle("hold-active", holdState);
  document.getElementById("statusText").innerText=holdState?"Hold: ON":"Standby";
  fetch("/hold"); 
  logEvent("Hold "+(holdState?"ON":"OFF"));
}

// Scan button
document.getElementById("btnScan").onclick = ()=>{
  scanState=!scanState;
  holdState=false;
  document.getElementById("btnHold").classList.remove("hold-active");
  document.getElementById("statusText").innerText=scanState?"Scan: ON":"Standby";
  fetch("/scan"); 
  logEvent("Scan "+(scanState?"ON":"OFF"));
}

// Sliders
document.getElementById("speed").oninput = (e)=>{document.getElementById("speedValue").innerText=e.target.value+"%";fetch("/speed?value="+e.target.value);logEvent("Speed "+e.target.value+"%");};
document.getElementById("moveRange").oninput = (e)=>{document.getElementById("moveValue").innerText=e.target.value;fetch("/moveSteps?value="+e.target.value);logEvent("Move "+e.target.value);};
document.getElementById("scanRange").oninput = (e)=>{document.getElementById("scanValue").innerText=e.target.value;fetch("/scanSteps?value="+e.target.value);logEvent("ScanRange "+e.target.value);};
document.getElementById("delayRange").oninput = (e)=>{document.getElementById("delayValue").innerText=e.target.value;fetch("/scanDelay?value="+e.target.value);logEvent("ScanDelay "+e.target.value+"s");};

// Gyro settings sliders
document.getElementById("threshold").oninput = (e)=>{
  document.getElementById("thresholdValue").innerText=e.target.value+"°";
  fetch("/holdThreshold?value="+e.target.value);
  logEvent("Threshold "+e.target.value+"°");
};

document.getElementById("kp").oninput = (e)=>{
  document.getElementById("kpValue").innerText=parseFloat(e.target.value).toFixed(2);
  fetch("/holdKp?value="+e.target.value);
  logEvent("Kp "+e.target.value);
};

// Reset yaw button
document.getElementById("btnResetYaw").onclick = ()=>{
  fetch("/resetYaw");
  logEvent("Yaw reset to 0°");
};
async function checkFirmwareVersion(){
  try{
    const resp = await fetch("https://gpointer.eu/firmware/version.json");
    const data = await resp.json();
    document.getElementById("newFirmwareVersion").innerText=data.latest;
    logEvent("Firmware check: latest="+data.latest);
  }catch(e){
    document.getElementById("newFirmwareVersion").innerText="Error";
    logEvent("Firmware check error");
  }
}
document.addEventListener("DOMContentLoaded",()=>{checkFirmwareVersion();});
</script>
</body>
</html>
)rawliteral";
  server.send(200,"text/html",html);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Load saved settings
  loadSettings();
  
  // Motor pins
  pinMode(EN_PIN, OUTPUT); 
  pinMode(DIR_PIN, OUTPUT); 
  pinMode(STEP_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);
  
  // Initialize gyro
  initGyro();
  lastGyroUpdate = millis();
  
  // WiFi AP
  WiFi.softAP(ssid, password);
  Serial.printf("AP started: SSID=%s, IP=%s\n", ssid, WiFi.softAPIP().toString().c_str());
  
  // Web server
  server.on("/", handleRoot);
  server.on("/left", handleLeft);
  server.on("/right", handleRight);
  server.on("/speed", handleSpeed);
  server.on("/moveSteps", handleMoveSteps);
  server.on("/scanSteps", handleScanSteps);
  server.on("/scanDelay", handleScanDelay);
  server.on("/scan", handleScan);
  server.on("/hold", handleHold);
  server.on("/gyro", handleGyroStatus);
  server.on("/holdThreshold", handleHoldThreshold);
  server.on("/holdKp", handleHoldKp);
  server.on("/resetYaw", handleResetYaw);
  server.on("/getSettings", handleGetSettings);
  server.begin();
  
  Serial.println("System ready!");
}

void loop() {
  server.handleClient();
  
  // Обновляем показания гироскопа
  readGyroYaw();
  
  // Hold режим - корректируем направление
  if (holdMode) {
    holdDirectionCorrection();
  }
  
  // Scan режим
  if (scanning && millis() - lastScanTime > (unsigned long)(scanDelay * 1000)) {
    int delay_us = map(speedPercent, 0, 100, 2000, 200);
    rotateSteps(scanDirection, scanSteps, delay_us);
    scanDirection = !scanDirection;
    lastScanTime = millis();
  }
  
  delay(20); // Небольшая задержка для стабильности
}