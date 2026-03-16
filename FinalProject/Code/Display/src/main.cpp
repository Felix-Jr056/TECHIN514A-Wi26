/*
 * ErgoCompass — Display Device
 *
 * Hardware:
 *   - XIAO ESP32-C3
 *   - X27 stepper motor gauge needle, 4-wire half-step, pins D0–D3
 *   - 3× NeoPixel (SKC6812RV) on D8 (top), D9 (mid), D10 (bot)
 *   - Calibration button on D6 (INPUT_PULLUP, active LOW)
 *
 * BLE role: Client / Central
 *   Connects to "ErgoCompass-Sensor"
 *   Service UUID:        4fafc201-1fb5-459e-8fcc-c5c9c331914b
 *   Characteristic UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8
 *   Payload: 6 bytes — three int16_t little-endian (top, mid, bot)
 */

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>

// ── BLE UUIDs (must match Sensor) ─────────────────────────────────────────
static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");
#define SENSOR_NAME "ErgoCompass-Sensor"

// ── X27 stepper motor pins (4-wire unipolar) ──────────────────────────────
#define MOTOR_PIN1  D0
#define MOTOR_PIN2  D1
#define MOTOR_PIN3  D2
#define MOTOR_PIN4  D3

// X27 full-step sequence: 4 steps per electrical cycle (2-phase bipolar).
const uint8_t stepSequence[4][4] = {
  {1, 0, 1, 0},
  {0, 1, 1, 0},
  {0, 1, 0, 1},
  {1, 0, 0, 1},
};

int currentStep    = 0;   // index into stepSequence (0–3)
int currentStepPos = 0;   // absolute motor position in steps
const int totalSteps = 600;  // approximate 315° sweep

// ── NeoPixel pins ─────────────────────────────────────────────────────────
#define NEO_PIN1   D8   // top FSR LED
#define NEO_PIN2   D9   // mid FSR LED
#define NEO_PIN3   D10  // bot FSR LED
#define NUM_LEDS   1

Adafruit_NeoPixel strip1(NUM_LEDS, NEO_PIN1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(NUM_LEDS, NEO_PIN2, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip3(NUM_LEDS, NEO_PIN3, NEO_GRB + NEO_KHZ800);

// ── Calibration button ────────────────────────────────────────────────────
#define BUTTON_PIN D6
#define DEBOUNCE_MS 50

// ── Calibration reference values ──────────────────────────────────────────
// Default placeholders — replaced when user presses calibration button.
// Lumbar (bot) typically reads highest in a well-supported sitting posture.
int16_t refTop = 8000;
int16_t refMid = 10000;
int16_t refBot = 12000;
bool calibrated = false;

// ── Latest BLE-received FSR values ────────────────────────────────────────
volatile int16_t bleTop = 0;
volatile int16_t bleMid = 0;
volatile int16_t bleBot = 0;
volatile bool    newBLEData = false;

// ── LED connection state ─────────────────────────────────────────────────
// 0 = scanning (blue flash), 1 = just connected (solid blue 3s), 2 = normal
uint8_t  ledState = 0;
uint32_t connectedAtMs = 0;

// ── BLE client state ──────────────────────────────────────────────────────
BLEClient*         pClient     = nullptr;
BLERemoteCharacteristic* pRemoteChar = nullptr;
BLEAdvertisedDevice* pTargetDevice  = nullptr;

bool doConnect    = false;  // set by scan callback when target is found
bool connected    = false;
bool doScan       = false;  // false: setup() starts the first scan directly;
                            // loop() uses a timer to rescan, not this flag.

// ── Motor helpers ─────────────────────────────────────────────────────────

void stepMotor(int step) {
  digitalWrite(MOTOR_PIN1, stepSequence[step][0]);
  digitalWrite(MOTOR_PIN2, stepSequence[step][1]);
  digitalWrite(MOTOR_PIN3, stepSequence[step][2]);
  digitalWrite(MOTOR_PIN4, stepSequence[step][3]);
}

// Move forward (increasing step position) by `steps` half-steps.
void moveForward(int steps, int delayMs = 3) {
  for (int i = 0; i < steps; i++) {
    currentStep = (currentStep + 1) % 4;
    stepMotor(currentStep);
    delay(delayMs);
  }
  currentStepPos += steps;
}

// Move backward (decreasing step position) by `steps` half-steps.
void moveBackward(int steps, int delayMs = 3) {
  for (int i = 0; i < steps; i++) {
    currentStep = (currentStep - 1 + 8) % 4;
    stepMotor(currentStep);
    delay(delayMs);
  }
  currentStepPos -= steps;
}

// Move incrementally toward targetStepPos, one step per call, ≥2ms delay.
// Returns true if already at target.
bool stepTowardTarget(int targetStepPos) {
  if (currentStepPos == targetStepPos) return true;
  if (currentStepPos < targetStepPos) {
    moveForward(1, 2);
  } else {
    moveBackward(1, 2);
  }
  return false;
}

// ── NeoPixel helpers ──────────────────────────────────────────────────────

// Good-posture reference values for each sensor.
// Top ~28000, Mid ~32000, Bot ~0.
const float idealTop = 28000.0f;
const float idealMid = 15000.0f;
const float idealBot = 0.0f;

// Map deviation from an ideal value to green (close) → red (far).
// maxDev is the deviation at which the LED is fully red.
uint32_t postureColor(Adafruit_NeoPixel& strip, float current, float ideal, float maxDev) {
  float deviation = fabsf(current - ideal) / maxDev;
  deviation = constrain(deviation, 0.0f, 1.0f);
  uint8_t r = (uint8_t)(deviation * 255.0f);
  uint8_t g = (uint8_t)((1.0f - deviation) * 255.0f);
  return strip.Color(r, g, 0);
}

void updateLEDs(int16_t top, int16_t mid, int16_t bot) {
  // Top: ideal 28000, fully red at 0 or 35000+ → maxDev = 28000
  strip1.setPixelColor(0, postureColor(strip1, (float)top, idealTop, 28000.0f));
  // Mid: ideal 15000, fully red at 0 or 32000 → maxDev = 15000
  strip2.setPixelColor(0, postureColor(strip2, (float)mid, idealMid, 15000.0f));
  // Bot: ideal 0, fully red at ~15000 → maxDev = 15000
  strip3.setPixelColor(0, postureColor(strip3, (float)bot, idealBot, 15000.0f));
  strip1.show();
  strip2.show();
  strip3.show();
}

// Set all 3 LEDs to the same solid color.
void setAllLEDs(uint8_t r, uint8_t g, uint8_t b) {
  strip1.setPixelColor(0, strip1.Color(r, g, b));
  strip2.setPixelColor(0, strip2.Color(r, g, b));
  strip3.setPixelColor(0, strip3.Color(r, g, b));
  strip1.show();
  strip2.show();
  strip3.show();
}

// ── Posture score → motor target step ────────────────────────────────────
// Good posture: top≈28000, mid≈15000, bot≈0 (all LEDs green = high score).
// Bad posture:  leaning forward (top+mid drop), slouching (bot rises),
//               or not sitting (all zero).
//
// Score = weighted deviation from ideal values (0 = perfect, 1 = worst).
int computeTargetSteps(int16_t top, int16_t mid, int16_t bot) {
  float fTop = (float)top;
  float fMid = (float)mid;
  float fBot = (float)bot;

  // Per-channel deviation from ideal, normalized to 0–1.
  float devTop = fabsf(fTop - idealTop) / 28000.0f;  // 0 at 28000, 1 at 0
  float devMid = fabsf(fMid - idealMid) / 15000.0f;  // 0 at 15000, 1 at 0 or 32000
  float devBot = fBot / 15000.0f;                     // 0 at 0, 1 at 15000+

  devTop = constrain(devTop, 0.0f, 1.0f);
  devMid = constrain(devMid, 0.0f, 1.0f);
  devBot = constrain(devBot, 0.0f, 1.0f);

  // Weighted combination: mid-back most important, then upper, then lumbar.
  float pressureScore = 0.35f * devTop + 0.40f * devMid + 0.25f * devBot;
  pressureScore = constrain(pressureScore, 0.0f, 1.0f);

  // Map to motor: 0.0 (good) → 300°, 1.0 (bad) → 240°
  float targetDegrees = 300.0f - pressureScore * 60.0f;
  int   targetSteps   = (int)(targetDegrees / 315.0f * (float)totalSteps);
  targetSteps = constrain(targetSteps, 0, totalSteps);

  Serial.printf("[Motor] score=%.2f deg=%.0f target=%d current=%d\n",
                pressureScore, targetDegrees, targetSteps, currentStepPos);


  return targetSteps;
}

// ── BLE scan + notification callbacks ────────────────────────────────────

// Receives notification data from the Sensor characteristic.
// Parses the 6-byte little-endian payload back into three int16_t values.
static void notifyCallback(BLERemoteCharacteristic* pChr,
                           uint8_t* pData, size_t length, bool isNotify) {
  if (length < 6) {
    return;
  }
  int16_t top = (int16_t)(pData[0] | (pData[1] << 8));
  int16_t mid = (int16_t)(pData[2] | (pData[3] << 8));
  int16_t bot = (int16_t)(pData[4] | (pData[5] << 8));

  bleTop = top;
  bleMid = mid;
  bleBot = bot;
  newBLEData = true;

  Serial.printf("[BLE]: %d  %d  %d\n", top, mid, bot);
}

// Scan callback: flag when target device is found.
class AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (advertisedDevice.getName() == SENSOR_NAME ||
        (advertisedDevice.haveServiceUUID() &&
         advertisedDevice.isAdvertisingService(serviceUUID))) {
      Serial.printf("[BLE] Found target: %s\n", advertisedDevice.toString().c_str());
      BLEDevice::getScan()->stop();
      pTargetDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

// Connect to pTargetDevice, discover service/char, register notification.
bool connectToServer() {
  Serial.printf("[BLE] Connecting to %s ...\n", pTargetDevice->getAddress().toString().c_str());

  // Disconnect and release any previous client to avoid resource exhaustion.
  if (pClient != nullptr) {
    if (pClient->isConnected()) {
      pClient->disconnect();
    }
    delete pClient;
    pClient = nullptr;
  }
  pClient = BLEDevice::createClient();

  if (!pClient->connect(pTargetDevice)) {
    Serial.println("[BLE] Connection failed.");
    return false;
  }
  Serial.println("[BLE] Connected.");

  BLERemoteService* pService = pClient->getService(serviceUUID);
  if (!pService) {
    Serial.println("[BLE] Service not found.");
    pClient->disconnect();
    return false;
  }

  pRemoteChar = pService->getCharacteristic(charUUID);
  if (!pRemoteChar) {
    Serial.println("[BLE] Characteristic not found.");
    pClient->disconnect();
    return false;
  }

  if (pRemoteChar->canNotify()) {
    pRemoteChar->registerForNotify(notifyCallback);
    Serial.println("[BLE] Registered for notifications.");
  }

  connected = true;
  ledState = 1;
  connectedAtMs = millis();
  setAllLEDs(0, 0, 255);  // solid blue on connect
  return true;
}

// ── setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== ErgoCompass Display — Starting ===");

  // ── Motor pins ──
  pinMode(MOTOR_PIN1, OUTPUT);
  pinMode(MOTOR_PIN2, OUTPUT);
  pinMode(MOTOR_PIN3, OUTPUT);
  pinMode(MOTOR_PIN4, OUTPUT);

  // ── NeoPixels ──
  strip1.begin(); strip1.setBrightness(50); strip1.show();
  strip2.begin(); strip2.setBrightness(50); strip2.show();
  strip3.begin(); strip3.setBrightness(50); strip3.show();

  // ── Calibration button ──
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // active LOW

  // ── Motor: assume zero position on boot (no sweep) ──
  currentStepPos = 0;
  currentStep    = 0;
  stepMotor(0);
  Serial.println("[Motor] Ready.");

  // ── BLE init ──
  BLEDevice::init("ErgoCompass-Display");

  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);

  Serial.println("[BLE] Scanning for \"" SENSOR_NAME "\"...");
  pScan->start(5, nullptr, false);  // 5s scan, async (non-blocking)
}

// ── loop ──────────────────────────────────────────────────────────────────
void loop() {
  // ── Periodic status print (every 1 s) ──
  // Shows BLE connection state and latest sensor readings regardless of
  // whether a new notification has arrived since the last print.
  static uint32_t lastStatusPrint = 0;
  if (millis() - lastStatusPrint >= 1000) {
    lastStatusPrint = millis();
    if (connected) {
      Serial.printf("[STATUS] BLE: CONNECTED | top=%5d  mid=%5d  bot=%5d | motor step=%d\n",
                    (int)bleTop, (int)bleMid, (int)bleBot, currentStepPos);
    } else {
      Serial.println("[STATUS] BLE: DISCONNECTED (scanning...)");
    }
  }

  // ── BLE: detect disconnection ──
  if (connected && pClient != nullptr && !pClient->isConnected()) {
    Serial.println("[BLE] Connection lost. Will rescan.");
    connected   = false;
    pRemoteChar = nullptr;
    ledState    = 0;  // back to blue flashing while scanning
  }

  // ── BLE: connect when scan finds the target ──
  if (doConnect && !connected) {
    doConnect = false;
    if (connectToServer()) {
      Serial.println("[BLE] *** Connected to ErgoCompass-Sensor ***");
    } else {
      Serial.println("[BLE] Connection attempt failed — will rescan.");
    }
  }

  // ── BLE: timer-based rescan when not connected ──
  // Restarts a 5-second scan every 7 seconds until the sensor is found.
  // Using a timer instead of a flag prevents the double-scan problem and
  // guarantees rescanning even if the scan window expires without a result.
  static uint32_t lastScanStart = 0;
  if (!connected && !doConnect) {
    uint32_t now = millis();
    if (now - lastScanStart >= 7000) {
      lastScanStart = now;
      BLEScan* pScan = BLEDevice::getScan();
      pScan->clearResults();  // avoid duplicate-device results from prior scans
      Serial.println("[BLE] Scanning for \"" SENSOR_NAME "\"...");
      pScan->start(5, nullptr, false);  // async (non-blocking)
    }
  }

  // ── Calibration button handling ──
  static bool     lastButtonState   = HIGH;
  static uint32_t lastDebounceTime  = 0;
  bool buttonReading = digitalRead(BUTTON_PIN);

  if (buttonReading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS && buttonReading == LOW) {
    // Button pressed (active LOW) and debounce settled.
    // Capture current BLE values as the good-posture reference.
    refTop = bleTop;
    refMid = bleMid;
    refBot = bleBot;
    calibrated = true;
    Serial.printf("[CAL] Calibration captured: top=%d mid=%d bot=%d\n",
                  refTop, refMid, refBot);
    // Flash white twice to confirm calibration.
    for (int i = 0; i < 2; i++) {
      setAllLEDs(255, 255, 255);
      delay(100);
      setAllLEDs(0, 0, 0);
      delay(100);
    }
    // Wait for release to avoid repeated triggering.
    while (digitalRead(BUTTON_PIN) == LOW) { delay(10); }
  }
  lastButtonState = buttonReading;

  // ── LED connection animation ──
  if (ledState == 0) {
    // Blue flashing (500ms on/off) while scanning for BLE
    bool on = (millis() / 500) % 2 == 0;
    if (on) {
      setAllLEDs(0, 0, 255);
    } else {
      setAllLEDs(0, 0, 0);
    }
  } else if (ledState == 1) {
    // Solid blue for 3 seconds after connection
    if (millis() - connectedAtMs >= 3000) {
      ledState = 2;  // transition to normal posture colors
    }
  }

  // ── Process new BLE data ──
  // motorTarget is file-scoped static so both the update branch and the
  // incremental-step branch always share the same value.
  static int motorTarget = 0;

  if (newBLEData) {
    newBLEData = false;

    // Snapshot volatile values safely (single reads are atomic on 32-bit).
    int16_t top = bleTop;
    int16_t mid = bleMid;
    int16_t bot = bleBot;

    // Update LEDs only in normal mode (skip during connection animation).
    if (ledState == 2) {
      updateLEDs(top, mid, bot);
    }

    // Recompute the target motor step for the new posture score.
    motorTarget = computeTargetSteps(top, mid, bot);
  }

  // Incremental motor step every loop iteration: moves one half-step toward
  // motorTarget (≥2ms delay inside stepTowardTarget). Keeps large traversals
  // from blocking the rest of the loop.
  stepTowardTarget(motorTarget);

  // Small yield delay (not the motor step delay — that's inside stepTowardTarget).
  delay(1);
}
