/**
 * Lab 5: Data Transmission Frequency
 * 
 * This program cycles through 5 different Firebase transmission rates:
 * 1. 2 Hz (2 times per second) - 500ms interval
 * 2. 1 Hz (1 time per second) - 1000ms interval
 * 3. 0.5 Hz (once every 2 seconds) - 2000ms interval
 * 4. 0.333 Hz (once every 3 seconds) - 3000ms interval
 * 5. 0.25 Hz (once every 4 seconds) - 4000ms interval
 * 
 * Each rate runs for 10 seconds to allow power measurement.
 * After completing all rates, the cycle repeats.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE
#include <FirebaseClient.h>

// ==================== Configuration ====================

// WiFi credentials
#define WIFI_SSID "UW MPSK"
#define WIFI_PASSWORD "6gTE9RJSeH5qpunL"

// Firebase credentials
#define API_KEY "AIzaSyD_m858j3clC9pyNG6icno5tr_qQMMRKec"
#define USER_EMAIL "dyx20020506@yahoo.com"
#define USER_PASSWORD "dyx20020506"
#define DATABASE_URL "https://techin514-lab5-30282-default-rtdb.firebaseio.com/"

// Ultrasonic sensor pins (HC-SR04)
#define TRIG_PIN D0
#define ECHO_PIN D1

// Duration for each transmission rate test (in milliseconds)
#define RATE_TEST_DURATION 10000  // 10 seconds per rate for power measurement

// ==================== Transmission Rate Definitions ====================

enum TransmissionRate {
    RATE_2HZ = 0,      // 2 times per second (500ms)
    RATE_1HZ = 1,      // 1 time per second (1000ms)
    RATE_0_5HZ = 2,    // Once every 2 seconds (2000ms)
    RATE_0_333HZ = 3,  // Once every 3 seconds (3000ms)
    RATE_0_25HZ = 4,   // Once every 4 seconds (4000ms)
    RATE_COUNT = 5
};

// Transmission intervals in milliseconds for each rate
const unsigned long transmissionIntervals[RATE_COUNT] = {
    500,   // 2 Hz
    1000,  // 1 Hz
    2000,  // 0.5 Hz
    3000,  // 0.333 Hz
    4000   // 0.25 Hz
};

// Rate names for display
const char* rateNames[RATE_COUNT] = {
    "2 Hz (500ms)",
    "1 Hz (1000ms)",
    "0.5 Hz (2000ms)",
    "0.333 Hz (3000ms)",
    "0.25 Hz (4000ms)"
};

// Current transmission rate index
int currentRateIndex = 0;

// ==================== Global Variables ====================

// Firebase
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);
FirebaseApp app;
WiFiClientSecure ssl_client1, ssl_client2;
using AsyncClient = AsyncClientClass;
AsyncClient async_client1(ssl_client1), async_client2(ssl_client2);
RealtimeDatabase Database;
AsyncResult dbResult;

// Timing
unsigned long rateStartTime = 0;
unsigned long lastTransmission = 0;
unsigned long transmissionCount = 0;

// ==================== Function Prototypes ====================

float readUltrasonic();
void setupWiFi();
void setupFirebase();
void processData(AsyncResult &aResult);
void sendDataToFirebase(float distance);

// ==================== Setup ====================

void setup() {
    Serial.begin(115200);
    delay(1000);  // Allow serial to initialize
    
    Serial.println("\n========================================");
    Serial.println("Lab 5: Transmission Rate vs Power Consumption");
    Serial.println("========================================\n");
    
    // Setup ultrasonic sensor pins
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    
    // Setup WiFi (stays connected throughout all tests)
    setupWiFi();
    
    // Setup Firebase
    setupFirebase();
    
    // Initialize timing
    rateStartTime = millis();
    lastTransmission = millis();
    transmissionCount = 0;
    
    Serial.println("\n========================================");
    Serial.printf(">>> STAGE 1: %s <<<\n", rateNames[currentRateIndex]);
    Serial.printf("Test Duration: %d seconds\n", RATE_TEST_DURATION / 1000);
    Serial.println("========================================\n");
    
    // Reset timing after setup is complete
    rateStartTime = millis();
    lastTransmission = rateStartTime;
}

// ==================== Main Loop ====================

void loop() {
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - rateStartTime;
    
    // Maintain Firebase connection
    app.loop();
    
    // Check if rate test duration has elapsed
    if (elapsed >= RATE_TEST_DURATION) {
        // Print summary for completed rate
        Serial.println("\n========================================");
        Serial.printf("Rate Test Complete: %s\n", rateNames[currentRateIndex]);
        Serial.printf("Total Transmissions: %lu\n", transmissionCount);
        Serial.printf("Actual Rate: %.3f Hz\n", (float)transmissionCount / (RATE_TEST_DURATION / 1000.0));
        Serial.println("========================================\n");
        
        // Move to next rate
        currentRateIndex = (currentRateIndex + 1) % RATE_COUNT;
        
        // Reset counters
        rateStartTime = millis();
        transmissionCount = 0;
        lastTransmission = rateStartTime;
        
        Serial.println("\n========================================");
        Serial.printf(">>> STAGE %d: %s <<<\n", currentRateIndex + 1, rateNames[currentRateIndex]);
        Serial.printf("Test Duration: %d seconds\n", RATE_TEST_DURATION / 1000);
        Serial.println("========================================\n");
        
        return;  // Start fresh on next loop iteration
    }
    
    // Check if it's time to transmit based on current rate
    unsigned long currentInterval = transmissionIntervals[currentRateIndex];
    if (currentTime - lastTransmission >= currentInterval) {
        // Read ultrasonic sensor
        float distance = readUltrasonic();
        
        // Send to Firebase
        sendDataToFirebase(distance);
        
        // Update timing
        lastTransmission = currentTime;
        transmissionCount++;
        
        // Print status (minimal to reduce timing interference)
        Serial.printf("[%s] #%lu | %.1fcm | %lus left\n", 
            rateNames[currentRateIndex], 
            transmissionCount, 
            distance, 
            (RATE_TEST_DURATION - elapsed) / 1000);
    }
    
    // Process Firebase results
    processData(dbResult);
    
    delay(10);  // Small delay to prevent overwhelming
}

// ==================== Helper Functions ====================

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

void setupFirebase() {
    Serial.println("Setting up Firebase...");
    
    ssl_client1.setInsecure();
    ssl_client2.setInsecure();
    ssl_client1.setHandshakeTimeout(5);
    ssl_client2.setHandshakeTimeout(5);
    
    initializeApp(async_client1, app, getAuth(user_auth), processData, "authTask");
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
    
    // Wait for Firebase to be ready
    Serial.print("Waiting for Firebase authentication");
    unsigned long startWait = millis();
    while (!app.ready() && millis() - startWait < 10000) {
        app.loop();
        Serial.print(".");
        delay(100);
    }
    
    if (app.ready()) {
        Serial.println("\nFirebase ready!");
    } else {
        Serial.println("\nFirebase not ready, continuing anyway...");
    }
}

void sendDataToFirebase(float distance) {
    if (app.ready()) {
        // Send to a path that identifies the current rate
        String path = "/Lab5/rate_" + String(currentRateIndex + 1) + "_" + String(transmissionIntervals[currentRateIndex]) + "ms/distance";
        Database.set<float>(async_client1, path.c_str(), distance, processData, "SetDistance");
    }
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
