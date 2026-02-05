/**
 * Power Management Strategy:
 * - Deep Sleep (20s) → Ultrasonic Only (5s) → Deep Sleep (loop)
 * - If ultrasonic detects object < 50cm: activate WiFi + Firebase
 * - WiFi + Firebase turns OFF after distance > 50cm for 10 seconds
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

// Power Management Settings

// Deep sleep duration (20 seconds)
#define DEEP_SLEEP_DURATION_US 20 * 1000000ULL

// Ultrasonic-only sensing duration (5 seconds)
#define ULTRASONIC_ONLY_DURATION 5000

// Distance threshold for activating WiFi
#define PROXIMITY_THRESHOLD 50.0

// Time distance must be > threshold before turning off WiFi (10 seconds)
#define WIFI_OFF_DELAY 10000

// Ultrasonic reading interval during active modes
#define READING_INTERVAL 500

// State Machine

enum PowerState {
    STATE_DEEP_SLEEP,
    STATE_ULTRASONIC_ONLY,
    STATE_WIFI_FIREBASE
};
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR PowerState currentState = STATE_DEEP_SLEEP;

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
unsigned long stateStartTime = 0;
unsigned long lastReading = 0;
unsigned long lastAboveThreshold = 0; 
bool wifiConnected = false;
bool firebaseReady = false;
bool objectDetected = false;

// Function Prototypes
float readUltrasonic();
void enterDeepSleep();
void setupWiFi();
void stopWiFi();
void setupFirebase();
void processData(AsyncResult &aResult);
void sendDataToFirebase(float distance);
const char* getStateName(PowerState state);

// Setup
void setup() {
    Serial.begin(115200);
    delay(500);
    
    bootCount++;
    
    Serial.println("Power Management System");
    Serial.printf("Boot count: %d\n", bootCount);
    Serial.printf("Wake cause: %d\n", esp_sleep_get_wakeup_cause());
    
    // Setup ultrasonic sensor pins
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    
    // After deep sleep, transition to ultrasonic-only mode
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        currentState = STATE_ULTRASONIC_ONLY;
        Serial.println("Woke from deep sleep → Ultrasonic Only mode");
    } else {
        // First boot - start with ultrasonic only
        currentState = STATE_ULTRASONIC_ONLY;
        Serial.println("First boot → Ultrasonic Only mode");
    }
    
    stateStartTime = millis();
    lastReading = 0;
    lastAboveThreshold = 0;
    objectDetected = false;
}

// Main Loop

void loop() {
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - stateStartTime;
    
    switch (currentState) {
        
        // TATE: ULTRASONIC ONLY
        case STATE_ULTRASONIC_ONLY: {
            // Take readings at regular intervals
            if (currentTime - lastReading >= READING_INTERVAL) {
                float distance = readUltrasonic();
                lastReading = currentTime;
                
                Serial.printf("[ULTRASONIC ONLY] Distance: %.1f cm | %lu/%d ms\n", 
                    distance, elapsed, ULTRASONIC_ONLY_DURATION);
                
                // Check if object detected (distance < threshold)
                if (distance > 0 && distance < PROXIMITY_THRESHOLD) {
                    Serial.println("*** OBJECT DETECTED! Activating WiFi + Firebase ***");
                    objectDetected = true;
                    
                    // Transition to WiFi+Firebase mode
                    currentState = STATE_WIFI_FIREBASE;
                    stateStartTime = millis();
                    
                    // Setup WiFi and Firebase
                    setupWiFi();
                    if (wifiConnected) {
                        setupFirebase();
                    }
                    return;
                }
            }
            
            // After 5 seconds of no detection, go back to deep sleep
            if (elapsed >= ULTRASONIC_ONLY_DURATION) {
                Serial.println("No object detected → Deep Sleep (20s)");
                enterDeepSleep();
            }
            break;
        }
        
        // STATE: WIFI + FIREBASE
        case STATE_WIFI_FIREBASE: {
            // Maintain Firebase connection
            if (firebaseReady) {
                app.loop();
            }
            
            // Take readings at regular intervals
            if (currentTime - lastReading >= READING_INTERVAL) {
                float distance = readUltrasonic();
                lastReading = currentTime;
                
                // Send to Firebase
                if (firebaseReady) {
                    sendDataToFirebase(distance);
                }
                
                Serial.printf("[WIFI+FIREBASE] Distance: %.1f cm | WiFi: %s\n", 
                    distance, wifiConnected ? "ON" : "OFF");
                
                // Check if object is still within threshold
                if (distance > 0 && distance < PROXIMITY_THRESHOLD) {
                    // Object still detected - reset the "above threshold" timer
                    objectDetected = true;
                    lastAboveThreshold = 0;
                    Serial.println("  → Object still in range");
                } else {
                    // Object moved away
                    if (lastAboveThreshold == 0) {
                        // Just started being above threshold
                        lastAboveThreshold = currentTime;
                        Serial.println("  → Object moved away, starting 10s countdown");
                    } else {
                        unsigned long aboveTime = currentTime - lastAboveThreshold;
                        Serial.printf("  → Above threshold for %lu/%d ms\n", aboveTime, WIFI_OFF_DELAY);
                        
                        // If above threshold for 10 seconds, turn off WiFi
                        if (aboveTime >= WIFI_OFF_DELAY) {
                            Serial.println("*** 10s elapsed - Turning OFF WiFi → Deep Sleep ***");
                            stopWiFi();
                            enterDeepSleep();
                        }
                    }
                }
            }
            
            // Process Firebase results
            processData(dbResult);
            break;
        }
        
        default:
            enterDeepSleep();
            break;
    }
    
    delay(10);
}

// Power Management Functions

void enterDeepSleep() {
    Serial.println("Entering DEEP SLEEP for 20 seconds...");
    Serial.flush();
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Configure timer wakeup
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_US);
    
    // Enter deep sleep
    esp_deep_sleep_start(); 
}

// WiFi & Firebase Functions

void setupWiFi() {
    Serial.println("Setting up WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    Serial.print("Connecting");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        Serial.print(".");
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
        wifiConnected = true;
    } else {
        Serial.println("\nWiFi connection failed!");
        wifiConnected = false;
    }
}

void stopWiFi() {
    Serial.println("Stopping WiFi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiConnected = false;
    firebaseReady = false;
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
    
    Serial.print("Authenticating");
    unsigned long startWait = millis();
    while (!app.ready() && millis() - startWait < 10000) {
        app.loop();
        Serial.print(".");
        delay(100);
    }
    
    if (app.ready()) {
        Serial.println("\nFirebase ready!");
        firebaseReady = true;
    } else {
        Serial.println("\nFirebase not ready");
        firebaseReady = false;
    }
}

void sendDataToFirebase(float distance) {
    if (app.ready()) {
        String path = "/Lab5/proximity/distance";
        Database.set<float>(async_client1, path.c_str(), distance, processData, "SetDistance");
        
        // Also send timestamp
        String tsPath = "/Lab5/proximity/timestamp";
        Database.set<number_t>(async_client1, tsPath.c_str(), number_t(millis()), dbResult);
    }
}

// Sensor Functions

float readUltrasonic() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    
    if (duration == 0) {
        return -1.0;
    }
    
    float distance = (duration * 0.0343) / 2.0;
    return distance;
}

// Utility Functions

void processData(AsyncResult &aResult) {
    if (!aResult.isResult())
        return;

    if (aResult.isEvent())
        Firebase.printf("Event: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.eventLog().message().c_str(), aResult.eventLog().code());

    if (aResult.isError())
        Firebase.printf("Error: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
}

const char* getStateName(PowerState state) {
    switch (state) {
        case STATE_DEEP_SLEEP: return "Deep Sleep";
        case STATE_ULTRASONIC_ONLY: return "Ultrasonic Only";
        case STATE_WIFI_FIREBASE: return "WiFi + Firebase";
        default: return "Unknown";
    }
}
