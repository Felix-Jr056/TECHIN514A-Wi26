/*
 * ErgoCompass — Sensor Device
 *
 * Hardware:
 *   - XIAO ESP32-C3
 *   - 3× Interlink FSR 406 (10 kΩ pull-down) → ADS1115 A0/A1/A2
 *   - ADS1115 via default I2C (SDA/SCL), address 0x48
 *
 * BLE role: Server / Peripheral
 *   Service UUID:        4fafc201-1fb5-459e-8fcc-c5c9c331914b
 *   Characteristic UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8
 *   Payload: 6 bytes — three int16_t little-endian (top, mid, bot filtered ADC)
 *   Update rate: 200 ms (5 Hz)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── BLE UUIDs ──────────────────────────────────────────────────────────────
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define DEVICE_NAME         "ErgoCompass-Sensor"

// ── ADS1115 ────────────────────────────────────────────────────────────────
Adafruit_ADS1115 ads;

// ── EMA filter state ───────────────────────────────────────────────────────
// Alpha = 0.1: heavily smoothed; posture changes are slow.
// new_filtered = alpha * raw + (1 - alpha) * prev_filtered
const float EMA_ALPHA = 0.1f;
const float NOISE_FLOOR = 200.0f;  // readings below this are noise → clamp to 0

float filteredTop = 0.0f;
float filteredMid = 0.0f;
float filteredBot = 0.0f;

// No-pressure baseline offsets (measured at boot).
float baselineTop = 0.0f;
float baselineMid = 0.0f;
float baselineBot = 0.0f;

// ── BLE state ──────────────────────────────────────────────────────────────
BLEServer*         pServer         = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool               deviceConnected = false;

// Restart advertising after disconnect
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pSvr) override {
    deviceConnected = true;
    Serial.println("[BLE] Client connected.");
  }

  void onDisconnect(BLEServer* pSvr) override {
    deviceConnected = false;
    Serial.println("[BLE] Client disconnected — restarting advertising...");
    pSvr->startAdvertising();
  }
};

// ── Helpers ────────────────────────────────────────────────────────────────

// Pack three int16 values into a 6-byte little-endian payload and notify.
void sendBLEPayload(int16_t top, int16_t mid, int16_t bot) {
  uint8_t payload[6];
  payload[0] = top & 0xFF;
  payload[1] = (top >> 8) & 0xFF;
  payload[2] = mid & 0xFF;
  payload[3] = (mid >> 8) & 0xFF;
  payload[4] = bot & 0xFF;
  payload[5] = (bot >> 8) & 0xFF;

  pCharacteristic->setValue(payload, sizeof(payload));
  if (deviceConnected) {
    pCharacteristic->notify();
  }
}

// ── setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== ErgoCompass Sensor — Starting ===");

  // ── ADS1115 init ──
  Wire.begin();
  if (!ads.begin()) {
    Serial.println("[ADS] ERROR: ADS1115 not found! Check wiring.");
    while (true) { delay(1000); }
  }
  // GAIN_TWO: ±2.048V range — FSR 406 with 10 kΩ pull-down at 3.3V
  // produces ~0–2V, so this gain gives good resolution without clipping.
  ads.setGain(GAIN_TWO);
  Serial.println("[ADS] ADS1115 initialised (GAIN_TWO, ±2.048V).");

  // Read baseline (no-pressure offset) — average 10 samples per channel.
  float baseTop = 0, baseMid = 0, baseBot = 0;
  for (int i = 0; i < 10; i++) {
    baseTop += ads.readADC_SingleEnded(0);
    baseMid += ads.readADC_SingleEnded(1);
    baseBot += ads.readADC_SingleEnded(2);
    delay(20);
  }
  baselineTop = baseTop / 10.0f;
  baselineMid = baseMid / 10.0f;
  baselineBot = baseBot / 10.0f;
  Serial.printf("[ADS] Baselines: top=%.0f mid=%.0f bot=%.0f\n",
                baselineTop, baselineMid, baselineBot);

  // Seed EMA at zero (after baseline subtraction).
  filteredTop = 0.0f;
  filteredMid = 0.0f;
  filteredBot = 0.0f;

  // ── BLE init ──
  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  // BLE2902 descriptor is required for notifications to work.
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising as \"" DEVICE_NAME "\".");
  Serial.println("[BLE] Service UUID:        " SERVICE_UUID);
  Serial.println("[BLE] Characteristic UUID: " CHARACTERISTIC_UUID);
}

// ── loop ───────────────────────────────────────────────────────────────────
void loop() {
  // ── Read raw ADC values and subtract baseline ──
  float rawTop = max(0.0f, (float)ads.readADC_SingleEnded(0) - baselineTop);
  float rawMid = max(0.0f, (float)ads.readADC_SingleEnded(1) - baselineMid);
  float rawBot = max(0.0f, (float)ads.readADC_SingleEnded(2) - baselineBot);
  if (rawTop < NOISE_FLOOR) rawTop = 0.0f;
  if (rawMid < NOISE_FLOOR) rawMid = 0.0f;
  if (rawBot < NOISE_FLOOR) rawBot = 0.0f;

  // ── Apply EMA filter ──
  filteredTop = EMA_ALPHA * rawTop + (1.0f - EMA_ALPHA) * filteredTop;
  filteredMid = EMA_ALPHA * rawMid + (1.0f - EMA_ALPHA) * filteredMid;
  filteredBot = EMA_ALPHA * rawBot + (1.0f - EMA_ALPHA) * filteredBot;

  int16_t fTop = (int16_t)filteredTop;
  int16_t fMid = (int16_t)filteredMid;
  int16_t fBot = (int16_t)filteredBot;

  // ── Debug output ──
  Serial.printf("[ADC] raw  top=%5.0f  mid=%5.0f  bot=%5.0f\n", rawTop, rawMid, rawBot);
  Serial.printf("[ADC] filt top=%5d  mid=%5d  bot=%5d | BLE=%s\n",
                fTop, fMid, fBot, deviceConnected ? "connected" : "advertising");

  // ── BLE transmit (READ value always updated; NOTIFY only when connected) ──
  sendBLEPayload(fTop, fMid, fBot);

  delay(200);  // 5 Hz update rate
}
