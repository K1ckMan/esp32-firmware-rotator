#include <WiFi.h>
#include <WebServer.h> 

#define EN_PIN   6
#define DIR_PIN  5
#define STEP_PIN 4

const char* ssid = "GPOINTER";
const char* password = "12345678";

WebServer server(80);

// Motor control
int speedPercent = 50;        // Скорость 0–100%
int moveSteps = 200;          // Шагов для Left/Right
int scanSteps = 10;           // Шагов в режиме Scan
float scanDelay = 1.0;        // Задержка между циклами Scan (сек)
bool scanning = false;
unsigned long lastScanTime = 0;
bool scanDirection = true;

String currentFirmwareVersion = "beta v0.2";

// --- Motor ---
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

// --- Handlers ---
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
  if (server.hasArg("value")) speedPercent = constrain(server.arg("value").toInt(),0,100);
  server.send(200,"text/plain","OK");
}

void handleMoveSteps() {
  if (server.hasArg("value")) moveSteps = constrain(server.arg("value").toInt(),1,2000);
  server.send(200,"text/plain","OK");
}

void handleScanSteps() {
  if (server.hasArg("value")) scanSteps = constrain(server.arg("value").toInt(),1,3000);
  server.send(200,"text/plain","OK");
}

void handleScanDelay() {
  if (server.hasArg("value")) scanDelay = constrain(server.arg("value").toFloat(),0,5.0);
  server.send(200,"text/plain","OK");
}

void handleScan() {
  scanning = !scanning;
  Serial.printf("Scan %s\n", scanning ? "started" : "stopped");
  server.send(200,"text/plain",scanning ? "Scan ON":"Scan OFF");
}

// --- Web page ---
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>ESP Motor Control</title>
<style>
body{font-family:Arial,sans-serif;margin:0;padding:0;background:#000;color:#fff;-webkit-user-select:none;user-select:none;touch-action:none;}
.tabs{display:flex;background:#111;}
.tabs button{flex:1;padding:14px;background:#111;color:#fff;border:none;font-size:18px;}
.tabs button.active{background:#333;}
.tab-content{display:none;padding:20px;background:#000;min-height:80vh;}
.tab-content.active{display:block;}
.btn{display:block;width:100%;margin:15px 0;padding:24px;font-size:26px;font-weight:bold;border:none;border-radius:8px;background:#444;color:#fff;}
.btn:active{background:#666;}
.row{display:flex;gap:12px;margin:20px 0;}
.row .btn{flex:1;font-size:28px;padding:30px;}
.slider-container{margin:20px 0;text-align:center;}
.slider-container label{display:block;margin-bottom:8px;font-size:20px;}
input[type=range]{width:100%;}
.status{margin-top:20px;font-size:22px;font-weight:bold;text-align:center;}
.log{background:#111;padding:10px;border:1px solid #444;height:200px;overflow-y:auto;font-size:14px;}
</style>
</head>
<body>

<div class="tabs">
  <button class="tablink active" onclick="openTab(event,'remote')">Remote</button>
  <button class="tablink" onclick="openTab(event,'firmware')">Firmware</button>
  <button class="tablink" onclick="openTab(event,'debug')">Debug</button>
</div>

<div id="remote" class="tab-content active">
  <div class="row">
    <button class="btn" id="btnLeft">Left</button>
    <button class="btn" id="btnRight">Right</button>
  </div>
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
  <button class="btn" id="btnScan">Scan</button>
  <div class="status" id="statusText">Scan: OFF</div>
</div>

<div id="firmware" class="tab-content">
  <div>Current Version: <b id="currentFirmware">beta v0.2</b></div>
  <div>New Version: <b id="newFirmwareVersion">Checking...</b></div>
  <input type="file">
  <button class="btn" onclick="checkFirmwareVersion()">Check Update</button>
</div>

<div id="debug" class="tab-content">
  <h3>Events Log</h3>
  <div class="log" id="eventLog"></div>
</div>

<script>
let scanState=false;

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

// Buttons
document.getElementById("btnLeft").addEventListener("mousedown",()=>holdCommand("/left"));
document.getElementById("btnLeft").addEventListener("touchstart",()=>holdCommand("/left"));
document.getElementById("btnRight").addEventListener("mousedown",()=>holdCommand("/right"));
document.getElementById("btnRight").addEventListener("touchstart",()=>holdCommand("/right"));

// Sliders
document.getElementById("speed").oninput = (e)=>{document.getElementById("speedValue").innerText=e.target.value+"%";fetch("/speed?value="+e.target.value);logEvent("Speed "+e.target.value+"%");};
document.getElementById("moveRange").oninput = (e)=>{document.getElementById("moveValue").innerText=e.target.value;fetch("/moveSteps?value="+e.target.value);logEvent("Move "+e.target.value);};
document.getElementById("scanRange").oninput = (e)=>{document.getElementById("scanValue").innerText=e.target.value;fetch("/scanSteps?value="+e.target.value);logEvent("ScanRange "+e.target.value);};
document.getElementById("delayRange").oninput = (e)=>{document.getElementById("delayValue").innerText=e.target.value;fetch("/scanDelay?value="+e.target.value);logEvent("ScanDelay "+e.target.value+"s");};

// Scan button
document.getElementById("btnScan").onclick = ()=>{
  scanState=!scanState;
  document.getElementById("statusText").innerText="Scan: "+(scanState?"ON":"OFF");
  fetch("/scan"); logEvent("Scan "+(scanState?"ON":"OFF"));
}

// Firmware check
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
  pinMode(EN_PIN, OUTPUT); pinMode(DIR_PIN, OUTPUT); pinMode(STEP_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);

  WiFi.softAP(ssid,password);
  Serial.printf("AP started: SSID=%s, IP=%s\n", ssid, WiFi.softAPIP().toString().c_str());

  server.on("/", handleRoot);
  server.on("/left", handleLeft);
  server.on("/right", handleRight);
  server.on("/speed", handleSpeed);
  server.on("/moveSteps", handleMoveSteps);
  server.on("/scanSteps", handleScanSteps);
  server.on("/scanDelay", handleScanDelay);
  server.on("/scan", handleScan);
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();
  if(scanning && millis() - lastScanTime > (unsigned long)(scanDelay*1000)){
    int delay_us = map(speedPercent,0,100,2000,200);
    rotateSteps(scanDirection, scanSteps, delay_us);
    scanDirection = !scanDirection;
    lastScanTime = millis();
  }
}
