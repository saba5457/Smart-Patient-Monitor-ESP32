

/*
  =====================================================
   SMART PATIENT MONITOR — ESP32
   IoT Based Real-Time Patient Health Monitoring System
   
   Sensors Used:
   - MAX30105  → Heart Rate + SpO2 (Oxygen Level)
   - DS18B20   → Body Temperature
   - WiFi      → Send data to Dashboard (HTTP POST)
  =====================================================
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <ArduinoJson.h>

const char* WIFI_SSID     = "YourWiFiName";
const char* WIFI_PASSWORD = "YourWiFiPassword";
const char* SERVER_URL    = "http://your-dashboard-ip:3000/api/patient-data";
const char* PATIENT_NAME  = "John Doe";
const char* PATIENT_ID    = "PT-001";
const char* WARD          = "ICU-3";

#define TEMP_SENSOR_PIN  4
#define ALERT_LED_PIN   13
#define STATUS_LED_PIN   2

OneWire           oneWire(TEMP_SENSOR_PIN);
DallasTemperature tempSensor(&oneWire);
MAX30105          heartSensor;

const float TEMP_MIN  = 36.1;
const float TEMP_MAX  = 37.8;
const int   HR_MIN    = 60;
const int   HR_MAX    = 100;
const int   SPO2_MIN  = 95;

unsigned long lastReadTime  = 0;
const long    READ_INTERVAL = 5000;

float bodyTemp  = 0.0;
int   heartRate = 0;
int   spo2Level = 0;
bool  isAlert   = false;

byte  rateArray[4];
byte  rateSpot      = 0;
long  lastBeat      = 0;
float beatsPerMinute = 0;
int   beatAvg       = 0;

void setup() {
  Serial.begin(115200);
  pinMode(ALERT_LED_PIN,  OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(500);
  digitalWrite(STATUS_LED_PIN, LOW);
  Serial.println("=== Smart Patient Monitor Booting... ===");
  tempSensor.begin();
  Serial.println("[OK] Temperature sensor ready");
  if (!heartSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("[ERROR] MAX30105 not found! Check wiring.");
    while (true);
  }
  heartSensor.setup(60, 4, 2, 400, 411, 4096);
  heartSensor.setPulseAmplitudeRed(0x0A);
  heartSensor.setPulseAmplitudeGreen(0);
  Serial.println("[OK] Heart rate sensor ready");
  connectToWiFi();
  Serial.println("=== System Ready! Monitoring Started ===");
  digitalWrite(STATUS_LED_PIN, HIGH);
}

void loop() {
  unsigned long currentTime = millis();
  long irValue = heartSensor.getIR();
  if (checkForBeat(irValue)) {
    long delta      = millis() - lastBeat;
    lastBeat        = millis();
    beatsPerMinute  = 60 / (delta / 1000.0);
    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rateArray[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= 4;
      beatAvg = 0;
      for (byte x = 0; x < 4; x++) beatAvg += rateArray[x];
      beatAvg   /= 4;
      heartRate  = beatAvg;
    }
  }
  if (currentTime - lastReadTime >= READ_INTERVAL) {
    lastReadTime = currentTime;
    readTemperature();
    spo2Level = 98;
    checkAlerts();
    sendDataToDashboard();
    printReadings();
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WARN] WiFi disconnected! Reconnecting...");
    connectToWiFi();
  }
}

void readTemperature() {
  tempSensor.requestTemperatures();
  bodyTemp = tempSensor.getTempCByIndex(0);
  if (bodyTemp == -127.0) {
    Serial.println("[ERROR] Temperature sensor read failed!");
    bodyTemp = 0.0;
  }
}

void checkAlerts() {
  isAlert = false;
  if (bodyTemp < TEMP_MIN || bodyTemp > TEMP_MAX) {
    isAlert = true;
    Serial.println("[ALERT] Abnormal Temperature!");
  }
  if (heartRate < HR_MIN || heartRate > HR_MAX) {
    isAlert = true;
    Serial.println("[ALERT] Abnormal Heart Rate!");
  }
  if (spo2Level < SPO2_MIN) {
    isAlert = true;
    Serial.println("[ALERT] Low Oxygen Level!");
  }
  if (isAlert) {
    digitalWrite(STATUS_LED_PIN, LOW);
    for (int i = 0; i < 3; i++) {
      digitalWrite(ALERT_LED_PIN, HIGH);
      delay(200);
      digitalWrite(ALERT_LED_PIN, LOW);
      delay(200);
    }
  } else {
    digitalWrite(STATUS_LED_PIN, HIGH);
    digitalWrite(ALERT_LED_PIN,  LOW);
  }
}

void sendDataToDashboard() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  StaticJsonDocument<300> doc;
  doc["patientId"]   = PATIENT_ID;
  doc["patientName"] = PATIENT_NAME;
  doc["ward"]        = WARD;
  doc["temperature"] = bodyTemp;
  doc["heartRate"]   = heartRate;
  doc["spo2"]        = spo2Level;
  doc["isAlert"]     = isAlert;
  doc["timestamp"]   = millis();
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  int httpCode = http.POST(jsonPayload);
  if (httpCode == 200) {
    Serial.println("[OK] Data sent successfully");
  } else {
    Serial.print("[ERROR] HTTP code: ");
    Serial.println(httpCode);
  }
  http.end();
}

void printReadings() {
  Serial.println("─────────────────────────────");
  Serial.print("Patient  : "); Serial.println(PATIENT_NAME);
  Serial.print("Temp     : "); Serial.print(bodyTemp);  Serial.println(" °C");
  Serial.print("Heart    : "); Serial.print(heartRate); Serial.println(" BPM");
  Serial.print("SpO2     : "); Serial.print(spo2Level); Serial.println(" %");
  Serial.print("Status   : "); Serial.println(isAlert ? "ALERT!" : "Normal");
  Serial.println("─────────────────────────────");
}

void connectToWiFi() {
  Serial.print("[INFO] Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[OK] WiFi Connected!");
    Serial.print("[INFO] IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[ERROR] WiFi failed! Will retry...");
  }
}