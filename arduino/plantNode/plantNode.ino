#include <Arduino.h>
#include <Arduino_JSON.h>
#include <Arduino_LSM6DSOX.h> // for gyroscope/accelerometer
#include <LittleFS_Mbed_RP2040.h>
#include <RTClib.h>
#include <WiFi.h>
#include <Wire.h>
// FIXME
#include "c:\Workspace\git\micro-code\arduino\ZofCloudConfig.h"
// #include "ZofCloudConfig.h"

bool missedRequest = false;
const int lightPin = A0;
const int tempPin = A1;
const int moisturePin = A2;
const char backlogFile[] = MBED_LITTLEFS_FILE_PREFIX "/offlineStorage.txt";

RTC_DS3231 rtc;
DateTime now;
LittleFS_MBED *myFS;

// TODO: in payload:
// processing speed?
// ph? need a sensor
// memory?
// storage?
// audio information?
// error catching for info that we cannot get for whatever reason

// TODO:
// break out filesystem functionality into its own file
// write code to recieve messages from cloud
// ensure connection security - ssl?
// write code to start new script from cloud
// LCD for local debugging?

void setup() {
  Serial.begin(9600);
  while (!Serial)
  {
    delay(1000);
  }
  pinMode(LED_BUILTIN, OUTPUT);

  // set up real time clock
  // ATTN: clock MUST BE on pins 4 (sda) and 5 (scl) 
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    digitalWrite(LED_BUILTIN, HIGH);
  }

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

  if (readFile(backlogFile) == "ERROR")
  {
    writeFile(backlogFile, "", 1);
  }

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
    while (1);
  }
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
  // Create payload information to send to server
  JSONVar root;
  JSONVar tempObj;
  now = rtc.now();
  tempObj["exterior"] = getExternalTemperature(true);
  tempObj["board"] = getInternalTemperature();
  tempObj["clock"] = rtc.getTemperature();
  root["temperature"] = tempObj;
  root["board"] = BOARD_NAME;
  root["moisture"] = getMoisturePercentage();
  // TODO: regulate light values to make sense?
  root["light"] = analogRead(lightPin);
  root["timestamp"] = now.unixtime();

  if (sendPayload(root)) {
    Serial.println("payload sent successfully.");
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    Serial.println("payload was not sent.");
    digitalWrite(LED_BUILTIN, HIGH);
  }

  sleep_ms(3000);
}

float getExternalTemperature(bool fahrenheit) {
  int reading = analogRead(tempPin);
  float voltage = reading * 3.3;
  voltage /= 1024.0;
  // converting from 10 mV/deg with 500 mV offset
  float temperatureC = (voltage - 0.5) * 100;
  float temperatureF = (temperatureC * 9.0 / 5.0) + 32.0;
  if (fahrenheit) return temperatureF;
  return temperatureC;
}

int getInternalTemperature() {
  int temperature_deg = -1;
  if (IMU.temperatureAvailable()) {
    IMU.readTemperature(temperature_deg);
  }
  return temperature_deg;
}

int getMoisturePercentage() {
  int reading = analogRead(moisturePin);
  int percentage = map(reading, MOISTURE_AIR, MOISTURE_MAX, 0, 100);
  percentage = max(percentage, 0);
  percentage = min(percentage, 100);
  return percentage;
}

bool sendPayload(JSONVar &payload) {
  String payloadStr = JSON.stringify(payload);
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    
    if (client.connect(SERVER_IP, SERVER_PORT)) {
      if (missedRequest)
      {
        String backlog = readFile(backlogFile);
        deleteFile(backlogFile);
        writeFile(backlogFile, "", 1);
        missedRequest = false;
        payload["backlog"] = backlog;
      }
      client.println("POST /submit HTTP/1.1");
      client.println("Host: " + String(SERVER_IP));
      client.println("Content-Type: application/json");
      client.println("Content-Length: " + String(payloadStr.length()));
      client.println();
      client.println(payloadStr);
      client.println();
      client.stop();
      return true;
    } else {
      missedRequest = true;
      Serial.println("cannot connect to server. writing request to file...");
      payload["error"] = "cannot connect to server";
      writeOffline(payload);
      return false;
    }
  }
  missedRequest = true;
  Serial.println("Wifi not connected. Check internet credentials");
  payload["error"] = "not connected to internet";
  writeOffline(payload);
  return false;
}

void writeOffline(JSONVar &obj)
{
  String payloadStr = JSON.stringify(obj);
  const char* out = payloadStr.c_str();
  appendFile(backlogFile, out, payloadStr.length() + 1);
}

String readFile(const char * path)
{
  FILE *file = fopen(path, "r");

  if (!file)
  {
    Serial.println(" => Open Failed");
    digitalWrite(LED_BUILTIN, HIGH);
    return "ERROR";
  }

  char c;
  String out;
  uint32_t numRead = 1;

  while (numRead)
  {
    numRead = fread((uint8_t *) &c, sizeof(c), 1, file);
    if (numRead)
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
  Serial.println(path);

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