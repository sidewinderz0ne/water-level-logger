// src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include "FS.h"
#include "LittleFS.h"

// Pin definitions
#define TRIG_PIN 2
#define ECHO_PIN 4
#define SWITCH_PIN 13

// Constants
const char* ssid = "water_level";
const char* password = "sulungresearch";
const char* PARAM_INTERVAL = "interval";
const char* PARAM_DATE = "date";
const char* PARAM_TIME = "time";
const char* PARAM_CALIBRATION = "calibration";

// Global variables
AsyncWebServer server(80);
RTC_DS3231 rtc;
unsigned long lastMeasurement = 0;
int measurementInterval = 60000; // Default 1 minute
float calibrationOffset = 0;
int failedReadings = 0;
bool isDeepSleep = false;

// Function declarations
float measureWaterLevel();
void setupWebServer();
void handleDataLogging();
String getFormattedDateTime();
void goToSleep();

void setup() {
    Serial.begin(115200);
    
    // Initialize pins
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(SWITCH_PIN, INPUT_PULLUP);
    
    // Initialize LittleFS
    if(!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }
    
    // Initialize RTC
    if (!rtc.begin()) {
        Serial.println("RTC failed");
        return;
    }
    
    // Setup Access Point
    WiFi.softAP(ssid, password);
    
    setupWebServer();
}

void loop() {
    if (digitalRead(SWITCH_PIN) == LOW) { // Switch is ON
        if (isDeepSleep) {
            // Wake up from deep sleep
            isDeepSleep = false;
            WiFi.softAP(ssid, password);
            setupWebServer();
        }
        
        unsigned long currentTime = millis();
        if (currentTime - lastMeasurement >= measurementInterval) {
            float waterLevel = measureWaterLevel();
            
            if (waterLevel < 0) {
                failedReadings++;
                if (failedReadings >= 3) {
                    goToSleep();
                }
            } else {
                failedReadings = 0;
                handleDataLogging();
            }
            
            lastMeasurement = currentTime;
        }
    } else {
        goToSleep();
    }
}

float measureWaterLevel() {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    long duration = pulseIn(ECHO_PIN, HIGH);
    float distance = duration * 0.034 / 2;
    
    return distance + calibrationOffset;
}

void handleDataLogging() {
    File file = LittleFS.open("/data.csv", "a");
    if(!file) {
        Serial.println("Failed to open file for writing");
        return;
    }
    
    String dataString = getFormattedDateTime() + "," + String(measureWaterLevel());
    file.println(dataString);
    file.close();
}

String getFormattedDateTime() {
    DateTime now = rtc.now();
    char dateTime[20];
    sprintf(dateTime, "%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    return String(dateTime);
}

void goToSleep() {
    isDeepSleep = true;
    WiFi.disconnect();
    esp_sleep_enable_timer_wakeup(measurementInterval * 1000);
    esp_deep_sleep_start();
}

void setupWebServer() {
    // Serve static files
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    
    // API endpoints
    server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
        File file = LittleFS.open("/data.csv", "r");
        String data = "";
        while(file.available()) {
            data += file.readStringUntil('\n') + "\n";
        }
        file.close();
        request->send(200, "text/plain", data);
    });
    
    server.on("/api/current", HTTP_GET, [](AsyncWebServerRequest *request){
        float level = measureWaterLevel();
        String json = "{\"level\":" + String(level) + ",\"time\":\"" + getFormattedDateTime() + "\"}";
        request->send(200, "application/json", json);
    });
    
    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request){
        if(request->hasParam(PARAM_INTERVAL, true)) {
            measurementInterval = request->getParam(PARAM_INTERVAL, true)->value().toInt() * 1000;
        }
        if(request->hasParam(PARAM_CALIBRATION, true)) {
            calibrationOffset = request->getParam(PARAM_CALIBRATION, true)->value().toFloat();
        }
        request->send(200);
    });
    
    server.on("/api/time", HTTP_POST, [](AsyncWebServerRequest *request){
        if(request->hasParam(PARAM_DATE, true) && request->hasParam(PARAM_TIME, true)) {
            String dateStr = request->getParam(PARAM_DATE, true)->value();
            String timeStr = request->getParam(PARAM_TIME, true)->value();
            // Parse and set RTC time
            // Implementation depends on your date/time format
        }
        request->send(200);
    });
    
    server.on("/api/delete", HTTP_POST, [](AsyncWebServerRequest *request){
        LittleFS.remove("/data.csv");
        request->send(200);
    });
    
    server.begin();
}