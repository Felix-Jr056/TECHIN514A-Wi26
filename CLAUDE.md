# CLAUDE.md
This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.
## Project Overview
**ErgoCompass** — a posture-tracking system with two embedded devices:
**Sensor device**: fabric back pad with 3 FSR pressure sensors, TI ADS1115 ADC, XIAO ESP32-C3. Reads pressure data, filters it, and transmits via BLE as a server (peripheral).
**Display device**: desk unit with an X27 stepper motor gauge needle, 3 NeoPixel LEDs, and a calibration button. XIAO ESP32-C3 acts as a BLE client (central), receives posture data, and shows status visually.
## Build & Flash (PlatformIO)
Both devices use PlatformIO with the Arduino framework targeting `seeed_xiao_esp32c3`.
```
# Build
pio run -d FinalProject/Code/Display
pio run -d FinalProject/Code/Sensor

# Upload to connected device
pio run -d FinalProject/Code/Display —target upload
pio run -d FinalProject/Code/Sensor —target upload

# Open serial monitor (115200 baud)
pio device monitor -d FinalProject/Code/Display -b 115200
pio device monitor -d FinalProject/Code/Sensor -b 115200
```
## Dependencies
### Sensor `platformio.ini`
`lib_deps`: `adafruit/Adafruit ADS1X15`
BLE is built into the ESP32 Arduino core — do NOT add a separate BLE library.
`monitor_speed = 115200`
### Display `platformio.ini`
`lib_deps`: `adafruit/Adafruit NeoPixel`
BLE is built into the ESP32 Arduino core — do NOT add a separate BLE library.
`monitor_speed = 115200`
## Code Architecture
### Sensor (`FinalProject/Code/Sensor/src/main.cpp`)
**FSR + ADC reading:**
3× Interlink FSR 406 sensors on ADS1115 channels **A0, A1, A2**.
ADS1115 connected via **default I2C** (SDA/SCL) on XIAO ESP32-C3.
Gain: **GAIN_TWO** (±2.048V). FSR 406 with 10kΩ pull-down at 3.3V produces ~0–2V for chair-leaning pressure.
Each channel read sequentially per loop cycle.
**Signal filtering:**
Exponential moving average (EMA) per channel, alpha = **0.1** (heavily smoothed — posture changes are slow).
Filtered values stored as floats, converted to int16 for BLE transmission.
**BLE (Server / Peripheral):**
Device name: `”ErgoCompass-Sensor”`
Service UUID: `”4fafc201-1fb5-459e-8fcc-c5c9c331914b”`
Characteristic UUID: `”beb5483e-36e1-4688-b7f5-ea07361b26a8”`
Properties: READ | NOTIFY
Payload: **6 bytes** — three int16 little-endian values (top, mid, bot filtered ADC readings).
Update rate: **200ms** (5 Hz).
Auto-restart advertising on disconnect.
**Debug output:** prints raw + filtered values and BLE connection status to Serial at 115200 baud.
### Display (`FinalProject/Code/Display/src/main.cpp`)
**BLE (Client / Central):**
Scans for `”ErgoCompass-Sensor”` by name or service UUID.
Connects, discovers characteristic, registers for notifications.
Parses 6-byte payload → three int16 values (top, mid, bot).
Auto-rescan and reconnect on disconnection.
On disconnect, holds last known LED/motor state (does not zero out).
**Calibration button:**
Momentary push button on pin **D6**, configured as `INPUT_PULLUP` (button connects D6 → GND).
Debounce: 50ms.
On press: captures current BLE-received FSR values as “good posture” reference (`refTop`, `refMid`, `refBot`).
Default placeholder values before calibration: `refTop = 8000`, `refMid = 10000`, `refBot = 12000`.
Prints `”Calibration captured: top=笑死我了 mid=Y bot=Z”` to Serial.
Values stored in RAM only (no persistent storage).
**3 NeoPixel LEDs (per-sensor posture indicator):**
SKC6812RV, one LED per strip instance on pins **D8** (top FSR), **D9** (mid FSR), **D10** (bot FSR).
Brightness: **50/255**.
Each LED shows how far its corresponding sensor deviates from the calibrated reference:
Deviation = `abs(current - ref) / ref`
≤10% deviation → pure green (0, 255, 0)
≥50% deviation → pure red (255, 0, 0)
Between 10%–50% → linear interpolation: `t = (deviation - 0.10) / 0.40`, R = t×255, G = (1−t)×255, B = 0
Updated immediately on each BLE notification.
**X27 stepper motor (spine pressure gauge needle):**
Motor driver code is pre-existing and must be preserved:
4-wire half-step on pins D0–D3, 6-step cycle, 630 steps = 315° full sweep.
Step delay ≥ 2ms.
Zero-reset on startup by driving backward past end stop.
Only the demo sweep in `loop()` is replaced with BLE-driven logic.
Needle operating range: **240°** (high pressure) to **300°** (low pressure).
**Spine pressure calculation model:**
Biomechanical principle: higher FSR readings = chair bearing more load = less compressive force on the spine. The relationship is inverse.
Sensor layout (top to bottom on chair back): top = upper back / shoulder blades, mid = mid-thoracic, bot = lower back / lumbar.
**Weighted total support** (lumbar-weighted for ergonomic relevance):
`totalSupport = 0.25 × top + 0.35 × mid + 0.40 × bot`
`refSupport = 0.25 × refTop + 0.35 × refMid + 0.40 × refBot`
**Support ratio**: `supportRatio = totalSupport / refSupport` (clamped to [0.2, 2.0])
**Distribution deviation** (how far current pressure distribution is from calibrated ideal):
Compute current fractions: `fracX = sensorX / (top + mid + bot)` for each sensor
Compute reference fractions: `refFracX = refX / (refTop + refMid + refBot)`
`distError = |fracTop − refFracTop| + |fracMid − refFracMid| + |fracBot − refFracBot|` (clamped to [0, 2.0])
**Combine into pressure score** (0.0 = good, 1.0 = bad):
`supportScore = 1.0 − constrain(supportRatio, 0.2, 1.5) / 1.5`
`distScore = constrain(distError / 1.0, 0.0, 1.0)`
`pressureScore = 0.65 × supportScore + 0.35 × distScore` (clamped to [0.0, 1.0])
**Map to motor position**:
pressureScore 0.0 (good) → 300°, pressureScore 1.0 (bad) → 240°
`targetDegrees = 300.0 − pressureScore × 60.0`
`targetSteps = (int)(targetDegrees / 315.0 × 630.0)`
Move incrementally: 1 step per loop iteration toward target (≥2ms delay), never jump.
**Edge cases:** if all FSR readings are 0, set pressureScore = 1.0 (worst). Avoid division by zero in fraction/ratio calculations.
## Key Hardware Notes
**X27 motor**: 1/3° per half-step, 630 half-steps ≈ 315° full sweep. Sequence is 6-step half-drive. Step delay ≥ 2 ms.
**NeoPixels**: model SKC6812RV, 1 LED per strip instance, brightness set to 50/255.
**FSRs**: Interlink FSR 406, read via 10kΩ pull-down voltage divider into ADS1115 ADC.
**ADS1115**: I2C default address (0x48), GAIN_TWO, channels A0/A1/A2.
**Calibration button**: momentary push button on Display device pin D6 (INPUT_PULLUP, active LOW).
**Power**: 1S LiPo (503035, 500 mAh) on both devices.
**BLE**: ESP32-C3 built-in radio. Sensor = server/peripheral, Display = client/central.
