# Change Log / Updates

## Summary

This version introduces major improvements in motor control, gyro integration, and web interface functionality. The system is more responsive, robust, and user-friendly.

---

## Key Changes

### 1. Web Interface
- Serves `index.html` via LittleFS.
- Fully functional Left, Right, Scan, Hold buttons.
- Dynamic configuration of Speed, MoveSteps, ScanSteps, ScanDelay, Hold Threshold, Hold Kp.

### 2. Motor Control
- Left/Right buttons now respect `speedPercent`.
- Added blocking rotate (`rotateStepsBlocking`) for manual movement.
- Added interruptible rotate (`rotateStepsInterruptible`) for Scan mode.

### 3. Scan Improvements
- Stops immediately when toggled OFF.
- Alternates scan direction automatically.
- Handles large scan step values without delay.

### 4. Gyroscope Integration
- Manual I2C handling for BMI160.
- Maintains `currentYaw` and `targetYaw`.
- Normalizes yaw to [-180°, +180°].

### 5. Hold Mode / Direction Correction
- Proportional control (P-controller) for auto-correction.
- Respects speed percentage.
- Debug output for each correction step.

### 6. Settings Save/Load
- All parameters saved in non-volatile memory.
- Parameters: speedPercent, moveSteps, scanSteps, scanDelay, holdThreshold, holdKp.

### 7. Serial Debug
- Full real-time logging for motor actions, yaw, scan, and hold.
- Updated every second for monitoring.

### 8. Bug Fixes
- Removed duplicate function definitions.
- Fixed missing `rotateStepsBlocking` references.
- Left/Right buttons now correctly respond to speedPercent.
- Scan stops immediately regardless of step size.
