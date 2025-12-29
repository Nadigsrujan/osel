/*****************************************************
   EDGE – CLOUD TASK MIGRATION (ESP8266 NODEMCU)
   Sensors:
      - DHT22 Temperature → D2 (GPIO4)
      - HC-SR04 Ultrasonic → TRIG=D6 (GPIO12), ECHO=D5 (GPIO14 via divider)
   Output:
      - LED Alert → D1 (GPIO5)

   Logic:
      - If Temp > threshold  OR Distance < threshold
            → Hazard → migrate task to cloud
      - If hazard clears → return to edge
*****************************************************/

#include <DHT.h>
#define DHTPIN  D2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Pin Definitions
#define TRIG_PIN D6
#define ECHO_PIN D5
#define LED_PIN  D1

// Thresholds
float TEMP_THRESHOLD = 38.0;   // °C
int DIST_THRESHOLD = 10;       // cm

// State Machine
enum STATE {EDGE, CLOUD};
STATE systemState = EDGE;


// Function – get ultrasonic distance
long getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 25000); // 25ms timeout
  if (duration == 0) return 999; // no echo, return big value
  return duration * 0.034 / 2;
}

void setup() {
  Serial.begin(9600);
  dht.begin();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  Serial.println("ESP8266 – EDGE NODE READY (State=EDGE)");
  delay(1000);
}

void loop() {

  float temp = dht.readTemperature();
  long dist = getDistanceCM();

  bool hazard = false;
  if (!isnan(temp) && temp > TEMP_THRESHOLD) hazard = true;
  if (dist < DIST_THRESHOLD) hazard = true;

  // NORMAL – Edge running fully locally
  if (!hazard && systemState == EDGE) {
    Serial.print("EDGE_OK,T=");
    Serial.print(temp);
    Serial.print(",D=");
    Serial.println(dist);
    digitalWrite(LED_PIN, LOW);
    delay(700);
    return;
  }

  // FIRST Hazard Trigger → MIGRATE
  if (hazard && systemState == EDGE) {
    Serial.print("MIGRATE,T=");
    Serial.print(temp);
    Serial.print(",D=");
    Serial.println(dist);
    digitalWrite(LED_PIN, HIGH);
    systemState = CLOUD;
    delay(700);
    return;
  }

  // Hazard continues while in CLOUD
  if (hazard && systemState == CLOUD) {
    Serial.print("CLOUD_DATA,T=");
    Serial.print(temp);
    Serial.print(",D=");
    Serial.println(dist);
    digitalWrite(LED_PIN, HIGH);
    delay(700);
    return;
  }

  // Hazard cleared → Switch back to Edge
  if (!hazard && systemState == CLOUD) {
    Serial.println("RETURN");
    digitalWrite(LED_PIN, LOW);
    systemState = EDGE;
    delay(700);
    return;
  }
}
