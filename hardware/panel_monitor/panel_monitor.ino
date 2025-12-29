#include "DHT.h"

// Pin Definitions
#define DHTPIN 2
#define DHTTYPE DHT22
#define TRIG_PIN 4
#define ECHO_PIN 5
#define LDR_PIN A0
#define LED_PIN 9

DHT dht(DHTPIN, DHTTYPE);

// Thresholds
const float TEMP_THRESHOLD = 38.0;
const int DIST_THRESHOLD = 10;
const int LDR_THRESHOLD = 200;

enum State { EDGE, CLOUD };
State systemState = EDGE;

void setup() {
  Serial.begin(9600);
  dht.begin();
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  
  digitalWrite(LED_PIN, LOW);
  Serial.println("[INFO] Electrical Panel Monitor Initialized");
}

long getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  return duration * 0.034 / 2;
}

void loop() {
  float temp = dht.readTemperature();
  long distance = getDistance();
  int light = analogRead(LDR_PIN);
  
  bool hazard = (temp > TEMP_THRESHOLD) || (distance < DIST_THRESHOLD);
  
  // Format data for Python task
  // Format: HAZARD_DATA:Temp,Dist,Light
  if (hazard) {
    if (systemState == EDGE) {
      Serial.print("MIGRATE_REQ,T=");
      Serial.print(temp);
      Serial.print(",D=");
      Serial.print(distance);
      Serial.print(",L=");
      Serial.println(light);
      systemState = CLOUD;
      digitalWrite(LED_PIN, HIGH); 
    }
  } else {
    if (systemState == CLOUD) {
      Serial.println("RETURN_REQ");
      systemState = EDGE;
      digitalWrite(LED_PIN, LOW);
    }
  }

  // Periodic status update for dashboard
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 1000) {
    Serial.print("STATUS,T=");
    Serial.print(temp);
    Serial.print(",D=");
    Serial.print(distance);
    Serial.print(",L=");
    Serial.println(light);
    lastUpdate = millis();
  }

  delay(500);
}
