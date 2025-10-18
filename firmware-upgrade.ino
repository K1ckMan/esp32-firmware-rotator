#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ESPmDNS.h>

// Настройки Wi-Fi
const char* staSSID = "COVID 2G";
const char* staPassword = "password123";

// AP настройки
const char* apSSID = "ESP32_Update";
const char* apPassword = "12345678";

// URL для проверки версии и прошивки
const char* versionUrl = "http://gpointer.eu/firmware/version.json";
const char* firmwareUrl = "http://gpointer.eu/firmware/firmware.bin";

String currentVersion = "beta v0.2";
String latestVersion = "";
volatile int otaProgress = 0;
volatile bool startOTA = false;
bool otaInProgress = false;

WebServer server(80);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Dual mode
  WiFi.mode(WIFI_AP_STA);

  // AP
  WiFi.softAP(apSSID, apPassword);
  Serial.println("AP started: ESP32_Update");
  Serial.println("AP IP: " + WiFi.softAPIP().toString());

  // STA
  WiFi.begin(staSSID, staPassword);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to Wi-Fi, IP: " + WiFi.localIP().toString());
    checkForUpdate();
  } else {
    Serial.println("\nFailed to connect to Wi-Fi.");
  }

  // mDNS
  if (MDNS.begin("esp32")) {
    Serial.println("mDNS responder started: http://esp32.local");
  }

  // Веб-сервер
  server.on("/", handleRoot);
  server.on("/update", HTTP_POST, handleUpdate, handleUpload);
  server.on("/start-ota", handleStartOTA);
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  if (!otaInProgress) {
    server.handleClient();
  }

  // Запуск OTA если был флаг
  if (startOTA && !otaInProgress) {
    startOTA = false;
    otaInProgress = true;
    
    // Даём время веб-серверу завершить отправку ответа
    delay(500);
    
    Serial.println("=== Starting OTA from server ===");
    performOTA(firmwareUrl);
    
    otaInProgress = false;
  }
}

// Главная страница
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;} button{padding:10px;margin:5px;}</style>";
  html += "</head><body>";
  html += "<h2>ESP32 OTA Update</h2>";
  html += "<p><b>Current version:</b> " + currentVersion + "</p>";
  html += "<p><b>Latest version:</b> " + (latestVersion != "" ? latestVersion : "checking...") + "</p>";
  html += "<hr>";
  
  html += "<h3>Update from Server</h3>";
  html += "<button onclick=\"location.href='/start-ota'\">Start OTA Update</button>";
  html += "<p><small>Note: Device will reboot after update</small></p>";
  
  html += "<hr>";
  html += "<h3>Upload Firmware</h3>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='firmware' accept='.bin'><br><br>";
  html += "<input type='submit' value='Upload & Update'>";
  html += "</form>";

  html += "<hr>";
  html += "<p><b>OTA Progress:</b> " + String(otaProgress) + "%</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Отдельный обработчик для старта OTA
void handleStartOTA() {
  if (WiFi.status() != WL_CONNECTED) {
    server.send(503, "text/html", 
      "<html><body><h2>Error</h2><p>Not connected to WiFi. Cannot perform OTA.</p>"
      "<a href='/'>Back</a></body></html>");
    return;
  }
  
  server.send(200, "text/html", 
    "<html><body><h2>OTA Update Started</h2>"
    "<p>Device is downloading firmware. Please wait...</p>"
    "<p>Check Serial Monitor for progress.</p>"
    "<p><b>Do not disconnect power!</b></p>"
    "</body></html>");
  
  startOTA = true;
}

// Кнопка Update (для загрузки через браузер)
void handleUpdate() {
  server.send(200, "text/html", 
    "<html><body><h2>Upload Complete</h2>"
    "<p>Device will reboot now...</p></body></html>");
}

// Загрузка прошивки через браузер
void handleUpload() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Upload Start: %s\n", upload.filename.c_str());
    otaProgress = 0;
    otaInProgress = true;
    
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
    
    size_t totalSize = Update.progress();
    otaProgress = (totalSize > 0) ? (totalSize * 100 / Update.size()) : 0;
    
    if (totalSize % 51200 == 0) { // Каждые ~50KB
      Serial.printf("Upload progress: %d bytes\n", totalSize);
    }
  } 
  else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.println("Upload finished. Rebooting in 2 seconds...");
      delay(2000);
      ESP.restart();
    } else {
      Update.printError(Serial);
      otaInProgress = false;
    }
  }
}

// Проверка версии через JSON
void checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("Fetching version JSON...");
  HTTPClient http;
  http.begin(versionUrl);
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    payload.trim();
    Serial.println("Version JSON: " + payload);

    int start = payload.indexOf("\"latest\":");
    if (start >= 0) {
      start = payload.indexOf("\"", start + 9);
      int end = payload.indexOf("\"", start + 1);
      if (start >= 0 && end > start) {
        latestVersion = payload.substring(start + 1, end);
        Serial.println("Latest version: " + latestVersion);
      }
    }
  } else {
    Serial.printf("Failed to fetch version, HTTP code: %d\n", httpCode);
  }

  http.end();
}

// OTA через сервер
void performOTA(const char* url) {
  Serial.println("\n=== OTA Update Started ===");
  
  // Останавливаем сервисы
  server.close();
  server.stop();
  WiFi.softAPdisconnect(true);
  
  delay(500);
  
  Serial.println("Services stopped, starting download...");

  HTTPClient http;
  http.begin(url);
  http.setTimeout(30000); // 30 секунд
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  Serial.println("Connecting to server...");
  int httpCode = http.GET();
  Serial.printf("HTTP Response: %d\n", httpCode);
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Download failed, error: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    restoreServices();
    return;
  }

  int contentLength = http.getSize();
  Serial.printf("Firmware size: %d bytes\n", contentLength);

  if (contentLength <= 0) {
    Serial.println("Invalid firmware size");
    http.end();
    restoreServices();
    return;
  }

  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    Serial.printf("Not enough space. Need: %d, Free: %d\n", 
                  contentLength, ESP.getFreeSketchSpace());
    Update.printError(Serial);
    http.end();
    restoreServices();
    return;
  }

  Serial.println("Starting firmware download...");
  
  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  uint8_t buff[1024];
  
  unsigned long lastPrint = 0;
  
  while (http.connected() && (written < contentLength)) {
    size_t available = stream->available();
    
    if (available) {
      size_t bytesToRead = (available > sizeof(buff)) ? sizeof(buff) : available;
      size_t bytesRead = stream->readBytes(buff, bytesToRead);
      
      size_t bytesWritten = Update.write(buff, bytesRead);
      
      if (bytesWritten != bytesRead) {
        Serial.println("Write failed!");
        Update.printError(Serial);
        break;
      }
      
      written += bytesWritten;
      
      // Печать прогресса каждые 500мс
      if (millis() - lastPrint > 500) {
        otaProgress = (written * 100) / contentLength;
        Serial.printf("Progress: %d%% (%d/%d bytes)\n", 
                      otaProgress, written, contentLength);
        lastPrint = millis();
      }
    }
    
    delay(1);
  }

  Serial.printf("\nTotal written: %d bytes\n", written);

  if (written != contentLength) {
    Serial.printf("Download incomplete! Expected %d, got %d\n", contentLength, written);
    Update.abort();
    http.end();
    restoreServices();
    return;
  }

  if (Update.end(true)) {
    Serial.println("\n=== OTA SUCCESS ===");
    Serial.println("Firmware updated successfully!");
    Serial.println("Rebooting in 3 seconds...");
    http.end();
    delay(3000);
    ESP.restart();
  } else {
    Serial.println("\n=== OTA FAILED ===");
    Update.printError(Serial);
    http.end();
    restoreServices();
  }
}

// Восстановление сервисов после неудачной OTA
void restoreServices() {
  Serial.println("Restoring services...");
  WiFi.softAP(apSSID, apPassword);
  delay(500);
  server.begin();
  Serial.println("Services restored. AP and web server running.");
}