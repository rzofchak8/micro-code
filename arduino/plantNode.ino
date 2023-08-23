#include <Arduino.h>
#include <ArduinoJson.h>
#include <Arduino_LSM6DSOX.h>
#include <WiFi.h>
#include <Wire.h>
#include "ZofCloudConfig.h"

const int lightPin = A0;
const int tempPin = A1;
const int moisturePin = A2;
const size_t capacity = JSON_ARRAY_SIZE(5) + JSON_OBJECT_SIZE(10) + 60;
DynamicJsonDocument jsonDoc(capacity);

// TODO: in payload:
// board name
// board temp
// processing speed
// memory?
// storage?
// timestamp
// audio information?
// stream audio option?? <- probably not
// String boardInfo = "RP2040 Board: " + String(ARDUINO);

// TODO:
// error reporting
// ensure connection security
// write code to recieve messages from cloud
// write code to start new script from cloud

void setup() {
  Serial.begin(9600);

  // connect built-in LED for error showing
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // Init wifi connection
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("Connected to WiFi");

  // Connect to IMU
  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1)
      ;
  }
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  // Create payload information to send to server
  int light = analogRead(lightPin);
  float temp = get_external_temperature(true);
  int moisture = get_moisture_percentage();

  Serial.println(light);
  Serial.println(temp);
  Serial.println(moisture);
  JsonObject root = jsonDoc.to<JsonObject>();
  root["temperature"] = temp;
  root["moisture"] = moisture;
  root["light"] = light;

  if (send_payload(root)) {
    Serial.println("payload sent successfully.");
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    Serial.println("payload was not sent.");
    digitalWrite(LED_BUILTIN, HIGH);
  }

  sleep_ms(3000);
}

float get_external_temperature(bool fahrenheit) {

  int reading = analogRead(tempPin);
  float voltage = reading * 3.3;
  voltage /= 1024.0;

  /* temperature in Celsius */
  float temperatureC = (voltage - 0.5) * 100; /*converting from 10 mv per degree wit 500 mV offset */
  /* Convert to Fahrenheit */
  float temperatureF = (temperatureC * 9.0 / 5.0) + 32.0;

  if (fahrenheit) return temperatureF;
  return temperatureC;
}

float get_internal_temperature() {
  int temperature_deg = 0;
  if (IMU.temperatureAvailable()) {
    IMU.readTemperature(temperature_deg);

    Serial.print("LSM6DSOX Temperature = ");
    Serial.print(temperature_deg);
    Serial.println(" °C");
  }

  return temperature_deg;
}

int get_moisture_percentage() {
  int reading = analogRead(moisturePin);
  int percentage = map(reading, MOISTURE_AIR, MOISTURE_MAX, 0, 100);
  percentage = max(percentage, 0);
  percentage = min(percentage, 100);
  return percentage;
}

JsonObject create_payload() {
  JsonObject root = jsonDoc.to<JsonObject>();
  return root;
}

bool send_payload(JsonObject &payload) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;

    if (client.connect(SERVER_IP, SERVER_PORT)) {
      Serial.println("sending request...");
      client.println("POST /submit HTTP/1.1");
      client.println("Host: " + String(SERVER_IP));
      client.println("Content-Type: application/json");
      client.println("Content-Length: " + String(capacity));
      client.println();
      serializeJson(payload, client);
      client.println();
      client.stop();
      return true;
    } else {
      Serial.println("not looking ideal for request");
      return false;
    }
  }
}

void loop_error_led() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}