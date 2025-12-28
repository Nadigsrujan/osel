/*
 * NodeMCU / Arduino - Ultrasonic Sensor Trigger
 * =============================================
 * 
 * PURPOSE: Detect objects within 20cm and send "VEHICLE_DETECTED" over serial
 *          to trigger the Edge OS migration demo.
 * 
 * HARDWARE:
 * ---------
 * HC-SR04 Ultrasonic Sensor
 *   - VCC  -> 5V (or 3.3V with voltage divider on Echo)
 *   - GND  -> GND
 *   - TRIG -> D5 (GPIO14)
 *   - ECHO -> D6 (GPIO12) via voltage divider for NodeMCU
 * 
 * LED (built-in or external)
 *   - LED_BUILTIN or D4 (GPIO2)
 * 
 * VOLTAGE DIVIDER (Required for NodeMCU with 5V sensor):
 * --------------------------------------------------------
 * Echo pin -> 1K resistor -> NodeMCU D6
 *                         -> 2K resistor -> GND
 * This brings 5V down to ~3.3V for NodeMCU safety.
 * 
 * SERIAL OUTPUT:
 * --------------
 * When object < 20cm:  Sends "VEHICLE_DETECTED\n"
 * Every 100ms:         Sends distance for debugging
 */

// Pin Definitions
#ifdef ESP8266
  // NodeMCU pins
  const int TRIG_PIN = D5;  // GPIO14
  const int ECHO_PIN = D6;  // GPIO12
  const int LED_PIN = LED_BUILTIN;  // Usually D4/GPIO2
#else
  // Arduino Uno/Nano pins
  const int TRIG_PIN = 9;
  const int ECHO_PIN = 10;
  const int LED_PIN = 13;
#endif

// Detection threshold in centimeters
const int DETECTION_THRESHOLD_CM = 20;

// Cooldown after detection (ms) to prevent spam
const int DETECTION_COOLDOWN_MS = 5000;

unsigned long lastDetectionTime = 0;

void setup() {
  Serial.begin(9600);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  
  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  
  delay(1000);
  
  Serial.println();
  Serial.println("========================================");
  Serial.println("  EDGE OS - Ultrasonic Trigger Ready");
  Serial.println("========================================");
  Serial.println("Waiting for object within 20cm...");
  Serial.println();
}

long measureDistance() {
  // Send trigger pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Measure echo duration
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);  // 30ms timeout
  
  // Calculate distance in cm (speed of sound = 343m/s)
  // distance = (duration / 2) / 29.1
  if (duration == 0) {
    return -1;  // No echo received
  }
  
  long distance = duration / 58;  // Simplified calculation
  return distance;
}

void loop() {
  long distance = measureDistance();
  
  // Debug output
  if (distance > 0) {
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println(" cm");
  }
  
  // Check for detection
  if (distance > 0 && distance < DETECTION_THRESHOLD_CM) {
    unsigned long now = millis();
    
    // Only trigger if cooldown has passed
    if (now - lastDetectionTime > DETECTION_COOLDOWN_MS) {
      // DETECTED!
      digitalWrite(LED_PIN, HIGH);
      
      Serial.println();
      Serial.println("===============================");
      Serial.println("VEHICLE_DETECTED");
      Serial.println("===============================");
      Serial.println();
      
      lastDetectionTime = now;
      
      // Keep LED on for 500ms
      delay(500);
      digitalWrite(LED_PIN, LOW);
    }
  }
  
  delay(100);  // Measurement interval
}
