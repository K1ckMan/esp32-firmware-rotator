# Motor Control with Gyro & Web Interface

This project provides a Wi-Fi controlled motor system with gyroscope-based hold, scanning, and manual control via a web interface. The system is based on an ESP32, a stepper motor, and a BMI160 gyroscope (manual I2C handling).  

---

## Features

- **Manual Control**: Left and Right motor movement via web interface.
- **Scan Mode**: Automatic back-and-forth rotation with adjustable steps and speed.
- **Hold Mode**: Gyro-based direction hold using proportional control.
- **Web Interface**: Configure speed, steps, scan delay, and hold parameters.
- **Persistent Settings**: Configuration is saved in non-volatile memory.
- **Serial Debug**: Full real-time debugging for motor movement, yaw, scan, and hold.

---

## Hardware

- **ESP32**
- **Stepper Motor**
- **Stepper Driver** (e.g., A4988 or DRV8825)
- **BMI160 Gyroscope**
- **Wi-Fi AP for web control**

### Pins Used

| Function | Pin |
|----------|-----|
| STEP     | 4   |
| DIR      | 5   |
| ENABLE   | 6   |
| SDA      | 8   |
| SCL      | 9   |

---

## Software Setup

1. Install [Arduino IDE](https://www.arduino.cc/en/software) with ESP32 board support.
2. Install required libraries: `WiFi.h`, `WebServer.h`, `LittleFS.h`, `Preferences.h`, `Wire.h`.
3. Copy `index.html` to `data/` folder for LittleFS upload.
4. Configure Wi-Fi SSID and password in the sketch.
5. Upload the sketch to ESP32 and upload LittleFS.

---

## Web Interface Usage

- **Left / Right**: Move motor manually. Speed respects configured percentage.
- **Scan**: Toggle automatic scanning. Stops immediately on toggle OFF.
- **Hold**: Toggle gyro hold mode to maintain direction.
- **Speed**: Set motor speed percentage.
- **MoveSteps / ScanSteps**: Adjust steps for manual and scan movements.
- **ScanDelay**: Adjust delay between scan cycles.
- **HoldThreshold / HoldKp**: Fine-tune gyro hold correction.
- **ResetYaw**: Reset current yaw to 0.

---

## Serial Debug Output

Serial prints useful information every second:

DBG: yaw=XX.XX target=XX.XX hold=ON/OFF scan=ON/OFF speed=XX moveSteps=XX scanSteps=XX


Left/Right presses, scan toggle, and hold toggle are logged immediately.

---

## Notes

- Gyroscope is handled manually via I2C, no external library required.
- Blocking rotations are used for Left/Right manual movement.
- Interruptible rotations are used for scan mode to allow immediate stop.
