#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <stdlib.h>

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
unsigned long previousMillis = 0;
const long interval = 1000;

// HC-SR04 Pin Definitions for XIAO ESP32C3
// D0 = GPIO2, D1 = GPIO3
#define TRIG_PIN 2  // D0 on XIAO ESP32C3
#define ECHO_PIN 3  // D1 on XIAO ESP32C3

// Sensor readings and processed data
float rawDistance = 0.0;
float denoisedDistance = 0.0;

// Moving Average Filter parameters
#define FILTER_SIZE 5
float distanceBuffer[FILTER_SIZE];
int bufferIndex = 0;
bool bufferFilled = false;

// Distance threshold for BLE transmission (in cm)
#define DISTANCE_THRESHOLD 30.0

// Custom UUIDs for this device
#define SERVICE_UUID        "471cb5b7-a5a9-4a19-a309-401ae5e95733"
#define CHARACTERISTIC_UUID "471cb5b7-a5a9-4a19-a309-401ae5e95733"

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
    }
};

// DSP Algorithm: Moving Average Filter
float movingAverageFilter(float newValue) {
    // Add new value to buffer
    distanceBuffer[bufferIndex] = newValue;
    bufferIndex = (bufferIndex + 1) % FILTER_SIZE;
    
    // Check if buffer is filled at least once
    if (bufferIndex == 0) {
        bufferFilled = true;
    }
    
    // Calculate average
    float sum = 0.0;
    int count = bufferFilled ? FILTER_SIZE : bufferIndex;
    for (int i = 0; i < count; i++) {
        sum += distanceBuffer[i];
    }
    
    return sum / count;
}

// Function to read distance from HC-SR04
float readUltrasonicDistance() {
    // Send trigger pulse
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    // Read echo pulse duration with longer timeout
    long duration = pulseIn(ECHO_PIN, HIGH, 50000); // 50ms timeout
    
    // Debug: print raw duration
    Serial.print("Duration: ");
    Serial.print(duration);
    Serial.print(" us | ");
    
    // If duration is 0, sensor didn't respond
    if (duration == 0) {
        Serial.print("[No echo received] ");
        return 0.0;
    }
    
    // Calculate distance in cm (speed of sound = 343m/s = 0.0343 cm/µs)
    // Distance = (duration * 0.0343) / 2
    float distance = (duration * 0.0343) / 2.0;
    
    return distance;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting BLE work!");

    // HC-SR04 sensor pin setup
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    
    // Initialize distance buffer
    for (int i = 0; i < FILTER_SIZE; i++) {
        distanceBuffer[i] = 0.0;
    }

    // Name your device to avoid conflicts
    BLEDevice::init("XIAO_ESP32C3_Ultrasonic");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pCharacteristic->addDescriptor(new BLE2902());
    pCharacteristic->setValue("Hello World");
    pService->start();
    // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.println("Characteristic defined! Now you can read it in your phone!");
}

void loop() {
    // Read raw distance from HC-SR04 sensor
    rawDistance = readUltrasonicDistance();
    
    // Apply moving average filter to denoise the data
    denoisedDistance = movingAverageFilter(rawDistance);
    
    // Print raw and denoised data to serial monitor
    Serial.print("Raw Distance: ");
    Serial.print(rawDistance, 2);
    Serial.print(" cm | Denoised Distance: ");
    Serial.print(denoisedDistance, 2);
    Serial.println(" cm");

    
    if (deviceConnected) {
        // Send data to client ONLY if denoised distance is less than 30cm
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;
            
            if (denoisedDistance < DISTANCE_THRESHOLD && denoisedDistance > 0) {
                // Convert denoised distance to string and send via BLE
                char distanceStr[20];
                snprintf(distanceStr, sizeof(distanceStr), "%.2f", denoisedDistance);
                pCharacteristic->setValue(distanceStr);
                pCharacteristic->notify();
                Serial.print("BLE Notify - Distance: ");
                Serial.print(distanceStr);
                Serial.println(" cm (< 30cm threshold)");
            } else {
                Serial.println("Distance >= 30cm or invalid, not transmitting via BLE");
            }
        }
    }
    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);  // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising();  // advertise again
        Serial.println("Start advertising");
        oldDeviceConnected = deviceConnected;
    }
    // connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
    }
    delay(1000);
}
