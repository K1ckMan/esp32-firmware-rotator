#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Adafruit_NeoPixel.h>

// Настройки LED
#define LED_PIN 48      // Пин с WS2812
#define NUM_LEDS 1
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// BLE
int scanTime = 5; // время сканирования в секундах
BLEScan* pBLEScan;
BLEClient* pClient;
BLERemoteCharacteristic* notifyChar;

// Колбэк для уведомлений
void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  
  Serial.print("Notification from ");
  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(": ");
  for (size_t i = 0; i < length; i++) {
    Serial.print(pData[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Мигаем зелёным при уведомлении
  strip.setPixelColor(0, strip.Color(0, 255, 0)); // Зеленый
  strip.show();
  delay(500);
  strip.setPixelColor(0, strip.Color(0, 0, 0));   // Выключить
  strip.show();
}

void setup() {
  Serial.begin(115200);

  // Настройка LED
  strip.begin();
  strip.show(); 
  // Загореть красным при старте
  strip.setPixelColor(0, strip.Color(255, 0, 0)); 
  strip.show();

  Serial.println("Starting BLE Client...");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);

  Serial.println("Scanning for BLE devices...");
  BLEScanResults* results = pBLEScan->start(scanTime, false);

  for (int i = 0; i < results->getCount(); i++) {
    BLEAdvertisedDevice device = results->getDevice(i);
    Serial.print(i);
    Serial.print(": ");
    Serial.print(device.getAddress().toString().c_str());
    Serial.print(" (");
    Serial.print(device.getName().c_str());
    Serial.println(")");
  }

  Serial.println("Enter device number to connect:");
  int index = -1;
  while (index < 0 || index >= results->getCount()) {
    while (!Serial.available()) {}
    index = Serial.parseInt();
  }

  BLEAdvertisedDevice device = results->getDevice(index);
  Serial.print("Connecting to ");
  Serial.print(device.getAddress().toString().c_str());
  Serial.print(" (");
  Serial.print(device.getName().c_str());
  Serial.println(")...");

  pClient = BLEDevice::createClient();
  pClient->connect(&device);
  Serial.println("✅ Connected!");

  // Ищем сервисы и характеристики
  std::map<std::string, BLERemoteService*>* services = pClient->getServices();
  for (auto &s : *services) {
    BLERemoteService* pService = s.second;
    std::map<std::string, BLERemoteCharacteristic*>* chars = pService->getCharacteristics();
    for (auto &c : *chars) {
      BLERemoteCharacteristic* pChar = c.second;
      // Подписываемся на уведомления, если есть
      if (pChar->canNotify()) {
        notifyChar = pChar;
        notifyChar->registerForNotify(notifyCallback);
        Serial.print("Subscribed to notifications: ");
        Serial.println(notifyChar->getUUID().toString().c_str());
      }
    }
  }

  Serial.println("BLE client ready. Waiting for notifications...");
}

void loop() {
  delay(1000);
}
