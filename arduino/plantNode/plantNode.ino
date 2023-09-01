#include <Arduino.h>
#include <ArduinoJson.h>
#include <Arduino_LSM6DSOX.h>
#include <WiFi.h>
#include <Wire.h>
#include "c:\Workspace\git\micro-code\arduino\ZofCloudConfig.h"

#include <LittleFS_Mbed_RP2040.h>
// #include "../ZofCloudConfig.h"

const int lightPin = A0;
const int tempPin = A1;
const int moisturePin = A2;
const size_t capacity = JSON_ARRAY_SIZE(5) + JSON_OBJECT_SIZE(10) + 60;
DynamicJsonDocument jsonDoc(capacity);
LittleFS_MBED *myFS;

// TODO: in payload:
// board temp
// processing speed
// memory?
// storage?
// audio information?
// stream audio option?? <- probably not
// String boardInfo = "RP2040 Board: " + String(ARDUINO);

// TODO:
// error reporting
// ensure connection security - ssl?
// write code to recieve messages from cloud
// write code to start new script from cloud
char filename[] = MBED_LITTLEFS_FILE_PREFIX "/offlineStorage.txt";
bool missedRequest = false;

void setup() {
  Serial.begin(9600);
    while (!Serial)

    delay(1000);
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.println(BOARD_NAME);
  Serial.println(ARDUINO);

#if defined(LFS_MBED_RP2040_VERSION_MIN)

  if (LFS_MBED_RP2040_VERSION_INT < LFS_MBED_RP2040_VERSION_MIN)
  {
    Serial.print("Warning. Must use this example on Version equal or later than : ");
    Serial.println(LFS_MBED_RP2040_VERSION_MIN_TARGET);
  }

#endif

  myFS = new LittleFS_MBED();

  if (!myFS->init())
  {
    Serial.println("LITTLEFS Mount Failed");
    digitalWrite(LED_BUILTIN, HIGH);
    return;
  }

  // readFile(fileName1);
  // writeFile(fileName1, message, sizeof(message));

  // Init wifi connection
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setTimeout(CLIENT_TIMEOUT);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Serial.println("Connected to WiFi");
  // Connect to IMU
  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  // Create payload information to send to server
  int light = analogRead(lightPin);
  float temp = get_external_temperature(true);
  int moisture = get_moisture_percentage();

  // Serial.println(light);
  // Serial.println(temp);
  // Serial.println(moisture);
  JsonObject root = jsonDoc.to<JsonObject>();
  root["board"] = BOARD_NAME;
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
      if (missedRequest)
      {
        String backlog = readFile(filename);
        deleteFile(filename);
        missedRequest = false;
        // TODO: add backlog to error secton of json and send it with current payload
      }
      
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
      // append information into file
      missedRequest = true;
      writeOffline(payload);
      return false;
    }
  }
  missedRequest = true;
  writeOffline(payload);
  return false;
}

void writeOffline(JsonObject &obj)
{
  String payloadStr;
  serializeJson(obj, payloadStr);
  payloadStr += "\n";
  const char* out = payloadStr.c_str();
  writeFile(filename, out, sizeof(out));
}

String readFile(const char * path)
{
  FILE *file = fopen(path, "r");

  if (!file)
  {
    Serial.println(" => Open Failed");
    digitalWrite(LED_BUILTIN, HIGH);
    return "";
  }

  char c;
  String out;
  uint32_t numRead = 1;

  while (numRead)
  {
    numRead = fread((uint8_t *) &c, sizeof(c), 1, file);

    if (numRead)
      Serial.print(c);
      out += c;
  }

  fclose(file);
  return out;
}

bool writeFile(const char * path, const char * message, size_t messageSize)
{
  FILE *file = fopen(path, "w");

  if (!file)
  {
    Serial.println(" => Open Failed");
    digitalWrite(LED_BUILTIN, HIGH);
    return false;
  }

  if (!fwrite((uint8_t *) message, 1, messageSize, file))
  {
    Serial.println("* Writing failed");
    digitalWrite(LED_BUILTIN, HIGH);
    fclose(file);
    return false;
  }

  fclose(file);
  return true;
}

bool appendFile(const char * path, const char * message, size_t messageSize)
{
  Serial.print("Appending file: ");
  Serial.print(path);

  FILE *file = fopen(path, "a");

  if (!file)
  {
    Serial.println(" => Open Failed");
    digitalWrite(LED_BUILTIN, HIGH);
    return false;
  }

  if (!fwrite((uint8_t *) message, 1, messageSize, file))
  {
    Serial.println("* Appending failed");
    digitalWrite(LED_BUILTIN, HIGH);
    fclose(file);
    return false;
  }

  fclose(file);
  return true;
}

void deleteFile(const char * path)
{
  Serial.print("Deleting file: ");
  Serial.print(path);

  if (remove(path) == 0)
  {
    Serial.println(" => OK");
  }
  else
  {
    Serial.println(" => Failed");
    digitalWrite(LED_BUILTIN, HIGH);
    return;
  }
}