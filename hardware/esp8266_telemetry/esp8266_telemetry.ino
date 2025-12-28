/*
 * ESP8266 Telemetry Sensor for Edge-Cloud Migration System
 * 
 * Hardware:
 * - DHT22: Data pin -> D4 (GPIO2)
 * - LDR: One leg to 3.3V, other to A0 with 10K resistor to GND
 * - Green LED: D5 (GPIO14) with 220 ohm resistor
 * - Red LED: D6 (GPIO12) with 220 ohm resistor
 * 
 * Sends JSON telemetry over WiFi to Edge device
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <DHT.h>

// ========== CONFIGURATION ==========
const char* WIFI_SSID = "YOUR_WIFI_SSID";      // Change this!
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";   // Change this!
const char* EDGE_IP = "192.168.1.100";          // Edge device IP - Change this!
const int EDGE_PORT = 8080;                      // Edge telemetry port
// ====================================

// Pin definitions
#define DHT_PIN D4        // GPIO2
#define DHT_TYPE DHT22
#define LDR_PIN A0        // Analog pin
#define LED_GREEN D5      // GPIO14 - Task on Edge
#define LED_RED D6        // GPIO12 - Task on Cloud

DHT dht(DHT_PIN, DHT_TYPE);

// Thresholds
const float TEMP_WARNING = 35.0;    // Celsius
const int LIGHT_LOW = 300;          // LDR threshold (0-1024)

void setup() {
    Serial.begin(115200);
    Serial.println("\n[ESP8266] Edge-Cloud Telemetry Sensor Starting...");
    
    // Initialize pins
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    pinMode(LDR_PIN, INPUT);
    
    // Startup LED test
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, HIGH);
    delay(500);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED, LOW);
    
    // Initialize DHT
    dht.begin();
    
    // Connect to WiFi
    Serial.printf("[WIFI] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        digitalWrite(LED_RED, !digitalRead(LED_RED)); // Blink while connecting
    }
    
    Serial.println("\n[WIFI] Connected!");
    Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_GREEN, HIGH); // Solid green = connected
}

void loop() {
    // Read sensors
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int lightLevel = analogRead(LDR_PIN);
    int rssi = WiFi.RSSI();
    
    // Handle DHT read errors
    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("[SENSOR] DHT read failed, using defaults");
        temperature = 25.0;
        humidity = 50.0;
    }
    
    // Calculate stress level (0-100)
    // Higher temp = more stress, lower light = more stress
    int stress = 0;
    if (temperature > TEMP_WARNING) stress += 40;
    if (temperature > 40.0) stress += 30;
    if (lightLevel < LIGHT_LOW) stress += 20;
    if (rssi < -70) stress += 10; // Weak WiFi signal
    if (stress > 100) stress = 100;
    
    // Build JSON payload
    String json = "{";
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"humidity\":" + String(humidity, 1) + ",";
    json += "\"light\":" + String(lightLevel) + ",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"stress\":" + String(stress) + ",";
    json += "\"device\":\"esp8266\"";
    json += "}";
    
    Serial.printf("[SENSOR] Temp:%.1f°C Hum:%.1f%% Light:%d RSSI:%d Stress:%d%%\n",
                  temperature, humidity, lightLevel, rssi, stress);
    
    // Send to Edge device
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;
        
        String url = "http://" + String(EDGE_IP) + ":" + String(EDGE_PORT) + "/telemetry";
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");
        
        int httpCode = http.POST(json);
        
        if (httpCode > 0) {
            String response = http.getString();
            Serial.printf("[HTTP] Response: %s\n", response.c_str());
            
            // Parse response to control LEDs
            if (response.indexOf("\"status\":\"edge\"") > 0) {
                digitalWrite(LED_GREEN, HIGH);
                digitalWrite(LED_RED, LOW);
            } else if (response.indexOf("\"status\":\"cloud\"") > 0) {
                digitalWrite(LED_GREEN, LOW);
                digitalWrite(LED_RED, HIGH);
            } else if (response.indexOf("\"status\":\"idle\"") > 0) {
                digitalWrite(LED_GREEN, LOW);
                digitalWrite(LED_RED, LOW);
            }
        } else {
            Serial.printf("[HTTP] Failed: %s\n", http.errorToString(httpCode).c_str());
            // Blink both LEDs on error
            digitalWrite(LED_GREEN, HIGH);
            digitalWrite(LED_RED, HIGH);
            delay(100);
            digitalWrite(LED_GREEN, LOW);
            digitalWrite(LED_RED, LOW);
        }
        
        http.end();
    }
    
    delay(2000); // Send every 2 seconds
}
