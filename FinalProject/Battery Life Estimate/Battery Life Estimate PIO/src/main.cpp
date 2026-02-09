/**
 * Power Consumption Test: ESP32
 * 
 * This program cycles through 3 power consumption modes:
 * 1. Active (10 seconds) - CPU running at full speed with active tasks
 * 2. Idle (10 seconds) - CPU idle, peripherals on, light sleep
 * 3. Deep Sleep (10 seconds) - ESP32 in deep sleep mode
 * 
 * After completing all modes, the cycle repeats.
 */

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_pm.h>
#include <driver/rtc_io.h>

// Mode duration in milliseconds
#define MODE_DURATION 10000  // 10 seconds per mode

// RTC memory to persist mode across deep sleep
RTC_DATA_ATTR int currentMode = 0;
RTC_DATA_ATTR int bootCount = 0;

// Power Mode Definitions
enum PowerMode {
    MODE_ACTIVE = 0,
    MODE_IDLE = 1,
    MODE_DEEP_SLEEP = 2,
    MODE_COUNT = 3
};

// Timing
unsigned long modeStartTime = 0;

// Function Prototypes
void runActiveMode();
void runIdleMode();
void enterDeepSleep();
const char* getModeName(PowerMode mode);

// Setup
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    bootCount++;
    
    Serial.println("\n========================================");
    Serial.println("ESP32 Power Consumption Test");
    Serial.println("========================================\n");
    Serial.printf("Boot count: %d\n", bootCount);
    
    // Check wake up reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Wakeup caused by timer");
            // After deep sleep, move to next mode
            currentMode = (currentMode + 1) % MODE_COUNT;
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("Wakeup caused by external signal using RTC_IO");
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("Wakeup caused by external signal using RTC_CNTL");
            break;
        default:
            Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
            currentMode = MODE_ACTIVE;  // Start fresh
            break;
    }
    
    Serial.printf("Current Mode: %d - %s\n", currentMode, getModeName((PowerMode)currentMode));
    Serial.println("========================================\n");
    
    modeStartTime = millis();
}

// Main Loop
void loop() {
    unsigned long elapsed = millis() - modeStartTime;
    
    // Check if mode duration is complete (except for deep sleep which handles itself)
    if (elapsed >= MODE_DURATION && currentMode != MODE_DEEP_SLEEP) {
        Serial.println("\n--- Mode duration complete ---");
        
        // Switch to next mode
        currentMode = (currentMode + 1) % MODE_COUNT;
        Serial.printf("\nSwitching to Mode: %d - %s\n", currentMode, getModeName((PowerMode)currentMode));
        Serial.println("========================================\n");
        
        modeStartTime = millis();
    }
    
    // Run current mode
    switch (currentMode) {
        case MODE_ACTIVE:
            runActiveMode();
            break;
        case MODE_IDLE:
            runIdleMode();
            break;
        case MODE_DEEP_SLEEP:
            enterDeepSleep();
            break;
        default:
            break;
    }
}

// Mode Implementations

void runActiveMode() {
    static unsigned long lastPrint = 0;
    static volatile uint32_t dummyCalc = 0;
    
    // Keep CPU busy with calculations
    for (int i = 0; i < 10000; i++) {
        dummyCalc += i * i;
        dummyCalc ^= (dummyCalc << 3);
    }
    
    if (millis() - lastPrint >= 1000) {
        Serial.println("=== MODE 0: ACTIVE ===");
        Serial.println("CPU running at full speed");
        Serial.printf("CPU Freq: %d MHz\n", getCpuFrequencyMhz());
        Serial.printf("Time remaining: %lu seconds\n", (MODE_DURATION - (millis() - modeStartTime)) / 1000);
        Serial.printf("Dummy calc: %lu\n", dummyCalc);
        lastPrint = millis();
    }
}

void runIdleMode() {
    static unsigned long lastPrint = 0;
    
    if (millis() - lastPrint >= 1000) {
        Serial.println("=== MODE 1: IDLE ===");
        Serial.println("CPU idle, using light sleep between tasks");
        Serial.printf("CPU Freq: %d MHz\n", getCpuFrequencyMhz());
        Serial.printf("Time remaining: %lu seconds\n", (MODE_DURATION - (millis() - modeStartTime)) / 1000);
        lastPrint = millis();
    }
    
    // Light sleep for 100ms intervals
    // This puts the CPU in a low-power state while still maintaining WiFi/BT if needed
    esp_sleep_enable_timer_wakeup(100000);  // 100ms in microseconds
    esp_light_sleep_start();
}

void enterDeepSleep() {
    Serial.println("=== MODE 2: DEEP SLEEP ===");
    Serial.println("Entering deep sleep for 10 seconds...");
    Serial.println("CPU and most peripherals will be powered off");
    Serial.println("RTC memory preserves mode state");
    Serial.flush();  // Make sure all serial data is sent
    
    // Configure wake up timer
    esp_sleep_enable_timer_wakeup(MODE_DURATION * 1000);  // Convert ms to us
    
    // Enter deep sleep
    esp_deep_sleep_start();
    
    // Code below this line will never execute
    // ESP32 will restart from setup() after deep sleep
}

// Helper Functions

const char* getModeName(PowerMode mode) {
    switch (mode) {
        case MODE_ACTIVE: return "Active";
        case MODE_IDLE: return "Idle (Light Sleep)";
        case MODE_DEEP_SLEEP: return "Deep Sleep";
        default: return "Unknown";
    }
}
