/**
 * Lab 5: Power Consumption Stages
 * 
 * This program cycles through 5 different power consumption modes:
 * 1. Deep Sleep (10 seconds) - Lowest power consumption
 * 2. Idle (10 seconds) - ESP32 awake but doing nothing
 * 3. Ultrasonic readings (10 seconds) - Taking sensor readings
 * 4. WiFi + No Ultrasonic (10 seconds) - WiFi active but no sensor readings
 * 5. Ultrasonic + WiFi + Firebase (10 seconds) - Sensor + WiFi + Firebase
 * 
 * After completing all modes, the cycle repeats.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sleep.h>

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE
#include <FirebaseClient.h>

// Configuration

// WiFi credentials
#define WIFI_SSID "UW MPSK"
#define WIFI_PASSWORD ""

// Firebase credentials
#define API_KEY ""
#define USER_EMAIL ""
#define USER_PASSWORD ""
#define DATABASE_URL ""

// Ultrasonic sensor pins (HC-SR04)
#define TRIG_PIN D0
#define ECHO_PIN D1

// Mode duration in milliseconds
#define MODE_DURATION 10000  // 10 seconds per mode

// Deep sleep duration in microseconds
#define DEEP_SLEEP_DURATION 10 * 1000000  // 10 seconds

// Power Mode Definitions

enum PowerMode {
    MODE_DEEP_SLEEP = 0,
    MODE_IDLE = 1,
    MODE_ULTRASONIC = 2,
    MODE_WIFI_ONLY = 3,
    MODE_ULTRASONIC_WIFI_FIREBASE = 4,
    MODE_COUNT = 5
};

RTC_DATA_ATTR int currentMode = MODE_DEEP_SLEEP;
RTC_DATA_ATTR int bootCount = 0;

// Global Variables

// Firebase
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);
FirebaseApp app;
WiFiClientSecure ssl_client1, ssl_client2;
using AsyncClient = AsyncClientClass;
AsyncClient async_client1(ssl_client1), async_client2(ssl_client2);
RealtimeDatabase Database;
AsyncResult dbResult;

// Timing
unsigned long modeStartTime = 0;

// Function Prototypes

float readUltrasonic();
void setupWiFi();
void stopWiFi();
void setupFirebase();
void processData(AsyncResult &aResult);
void runDeepSleep();
void runIdleMode();
void runUltrasonicMode();
void runWiFiOnlyMode();
void runUltrasonicWiFiFirebaseMode();
const char* getModeName(PowerMode mode);

// Setup

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    bootCount++;
    Serial.println("\n========================================");
    Serial.printf("Boot count: %d\n", bootCount);
    Serial.printf("Wakeup cause: %d\n", esp_sleep_get_wakeup_cause());
    
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Woke up from deep sleep timer!");
        currentMode = (currentMode + 1) % MODE_COUNT;
    }
    
    Serial.printf("Current Mode: %d - %s\n", currentMode, getModeName((PowerMode)currentMode));
    Serial.println("========================================\n");
    
    modeStartTime = millis();
    
    if (currentMode == MODE_DEEP_SLEEP) {
        runDeepSleep();
    }
    
    if (currentMode == MODE_WIFI_ONLY || currentMode == MODE_ULTRASONIC_WIFI_FIREBASE) {
        setupWiFi();
    }
    
    if (currentMode == MODE_ULTRASONIC_WIFI_FIREBASE) {
        setupFirebase();
    }
}

// Main Loop

void loop() {
    unsigned long elapsed = millis() - modeStartTime;
    
    if (elapsed >= MODE_DURATION) {
        Serial.println("\n--- Mode duration complete ---");
        
        if (currentMode == MODE_WIFI_ONLY || currentMode == MODE_ULTRASONIC_WIFI_FIREBASE) {
            stopWiFi();
        }
        
        currentMode = (currentMode + 1) % MODE_COUNT;
        Serial.printf("\nSwitching to Mode: %d - %s\n", currentMode, getModeName((PowerMode)currentMode));
        Serial.println("========================================\n");
        
        if (currentMode == MODE_DEEP_SLEEP) {
            runDeepSleep();
        }
        
        if (currentMode == MODE_WIFI_ONLY || currentMode == MODE_ULTRASONIC_WIFI_FIREBASE) {
            setupWiFi();
        }
        if (currentMode == MODE_ULTRASONIC_WIFI_FIREBASE) {
            setupFirebase();
        }
        
        modeStartTime = millis();
    }
    
    // Run current mode
    switch (currentMode) {
        case MODE_IDLE:
            runIdleMode();
            break;
        case MODE_ULTRASONIC:
            runUltrasonicMode();
            break;
        case MODE_WIFI_ONLY:
            runWiFiOnlyMode();
            break;
        case MODE_ULTRASONIC_WIFI_FIREBASE:
            runUltrasonicWiFiFirebaseMode();
            break;
        default:
            break;
    }
    
    delay(100);
}

// Mode Implementations

void runDeepSleep() {
    Serial.println("=== MODE 0: DEEP SLEEP ===");
    Serial.flush();
    
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION);
    
    esp_deep_sleep_start();
}

void runIdleMode() {
    static unsigned long lastPrint = 0;
    
    if (millis() - lastPrint >= 1000) {
        Serial.println("=== MODE 1: IDLE ===");
        Serial.println("ESP32 is awake but doing nothing...");
        Serial.printf("Time remaining: %lu seconds\n", (MODE_DURATION - (millis() - modeStartTime)) / 1000);
        lastPrint = millis();
    }  
}

void runUltrasonicMode() {
    static unsigned long lastReading = 0;
    
    if (millis() - lastReading >= 500) {
        Serial.println("=== MODE 2: ULTRASONIC ONLY ===");
        float distance = readUltrasonic();
        Serial.printf("Distance: %.2f cm\n", distance);
        Serial.printf("Time remaining: %lu seconds\n", (MODE_DURATION - (millis() - modeStartTime)) / 1000);
        lastReading = millis();
    }
}

void runWiFiOnlyMode() {
    static unsigned long lastPrint = 0;
    
    if (millis() - lastPrint >= 1000) {
        Serial.println("=== MODE 3: WiFi ONLY (No Ultrasonic) ===");
        Serial.println("No readings");
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("WiFi Status: Connected\n");
            Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
        } else {
            Serial.println("WiFi Status: Disconnected");
        }
        
        Serial.printf("Time remaining: %lu seconds\n", (MODE_DURATION - (millis() - modeStartTime)) / 1000);
        lastPrint = millis();
    }
}

void runUltrasonicWiFiFirebaseMode() {
    static unsigned long lastReading = 0;
    
    app.loop();
    
    if (millis() - lastReading >= 2000) {
        Serial.println("=== MODE 4: ULTRASONIC + WiFi + FIREBASE ===");
        float distance = readUltrasonic();
        Serial.printf("Distance: %.2f cm\n", distance);
        
        if (app.ready()) {
            String path = "/Lab5/ultrasonic/distance";
            Database.set<float>(async_client1, path.c_str(), distance, processData, "SetDistance");
            
            String timestampPath = "/Lab5/ultrasonic/timestamp";
            Database.set<number_t>(async_client1, timestampPath.c_str(), number_t(millis()), dbResult);
            
            Serial.println("Firebase: Data sent!");
        } else {
            Serial.println("Firebase: Not ready yet...");
        }
        
        Serial.printf("WiFi RSSI: %d dBm\n", WiFi.RSSI());
        Serial.printf("Time remaining: %lu seconds\n", (MODE_DURATION - (millis() - modeStartTime)) / 1000);
        lastReading = millis();
    }
    
    processData(dbResult);
}

// Helper Functions

float readUltrasonic() {
    // Clear trigger
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    
    // Send 10us pulse
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    // Read echo pulse duration
    long duration = pulseIn(ECHO_PIN, HIGH, 30000);  // 30ms timeout
    
    // Calculate distance (speed of sound = 343 m/s = 0.0343 cm/us)
    // Distance = (duration * 0.0343) / 2
    float distance = (duration * 0.0343) / 2.0;
    
    // Return -1 if no echo received
    if (duration == 0) {
        return -1.0;
    }
    
    return distance;
}

void setupWiFi() {
    Serial.println("Setting up WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        Serial.print(".");
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println();
        Serial.println("WiFi connection failed!");
    }
}

void stopWiFi() {
    Serial.println("Stopping WiFi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi stopped");
}

void setupFirebase() {
    Serial.println("Setting up Firebase...");
    
    ssl_client1.setInsecure();
    ssl_client2.setInsecure();
    ssl_client1.setHandshakeTimeout(5);
    ssl_client2.setHandshakeTimeout(5);
    
    initializeApp(async_client1, app, getAuth(user_auth), processData, "authTask");
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
    
    Serial.println("Firebase initialized");
}

void processData(AsyncResult &aResult) {
    if (!aResult.isResult())
        return;

    if (aResult.isEvent())
        Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());

    if (aResult.isDebug())
        Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());

    if (aResult.isError())
        Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());

    if (aResult.available())
        Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
}

const char* getModeName(PowerMode mode) {
    switch (mode) {
        case MODE_DEEP_SLEEP: return "Deep Sleep";
        case MODE_IDLE: return "Idle";
        case MODE_ULTRASONIC: return "Ultrasonic Only";
        case MODE_WIFI_ONLY: return "WiFi Only (No Ultrasonic)";
        case MODE_ULTRASONIC_WIFI_FIREBASE: return "Ultrasonic + WiFi + Firebase";
        default: return "Unknown";
    }
}
