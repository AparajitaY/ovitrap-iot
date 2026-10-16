#include "DHT.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>                          

/* ----------- WIFI & UBIDOTS CONFIG ----------- */
#define WIFI_SSID       "<wifi SSID>"
#define WIFI_PASSWORD   "<wifi password>"
#define UBIDOTS_TOKEN   "<ubidots token>"
#define MQTT_BROKER     "stem.ubidots.com"
#define MQTT_PORT       1883
#define NODE_ID         "<trap name>"
#define NODE_LAT        12.9682
#define NODE_LON        79.1564

/* ----------- NTP CONFIG ----------- */
#define NTP_SERVER     "pool.ntp.org"
#define UTC_OFFSET_IST  19800             // IST = UTC+5:30 = 19800 seconds

/* ----------- PIN DEFINITIONS ----------- */
#define IR1_PIN   26
#define IR2_PIN   27
#define LED_LOW   18
#define LED_MED   19
#define LED_HIGH  21
#define DHTPIN    4
#define DHTTYPE   DHT22

/* ----------- GEOMETRY & FLIGHT MODEL ----------- */
#define BEAM_SPACING_MM           20.0
#define MOSQ_MIN_SPEED_MM_PER_MS  0.15
#define MOSQ_MAX_SPEED_MM_PER_MS  0.50
#define MOSQ_MIN_DT_MS  (BEAM_SPACING_MM / MOSQ_MAX_SPEED_MM_PER_MS)  // 40ms
#define MOSQ_MAX_DT_MS  (BEAM_SPACING_MM / MOSQ_MIN_SPEED_MM_PER_MS)  // 133ms
#define WINDOW_SIZE     30

/* ----------- OBJECTS ----------- */
DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

/* ----------- VARIABLES ----------- */
volatile unsigned long lastIR1Time = 0;
volatile unsigned long lastValidEventTime = 0;
volatile bool newMosquitoEvent = false;
volatile unsigned long lastDT = 0;

int minuteCounts[WINDOW_SIZE];
int minuteIndex = 0;
int mosquitoCount = 0;
unsigned long totalMosquitoCount = 0;

/* ----------- NTP: GET REAL IST HOUR ----------- */
int getRealHour() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("[NTP] Time not synced yet, using millis fallback");
    return (millis() / 3600000) % 24;    // fallback if NTP fails
  }
  return timeinfo.tm_hour;               // real IST hour 0-23
}

/* ----------- INTERRUPTS ----------- */
void IRAM_ATTR IR1_trigger() {
  lastIR1Time = millis();
}

void IRAM_ATTR IR2_trigger() {
  unsigned long now = millis();
  unsigned long dt = now - lastIR1Time;
  if (dt >= MOSQ_MIN_DT_MS && dt <= MOSQ_MAX_DT_MS &&
      (now - lastValidEventTime > 150)) {
    mosquitoCount++;
    totalMosquitoCount++;
    lastValidEventTime = now;
    lastDT = dt;
    newMosquitoEvent = true;
  }
}

/* ----------- HELPERS ----------- */
int recent30minTotal() {
  int sum = 0;
  for (int i = 0; i < WINDOW_SIZE; i++) sum += minuteCounts[i];
  return sum;
}

float computeMinuteIndex(float temp, float hum) {
  float mpm = recent30minTotal() / (float)WINDOW_SIZE;

  int hr = getRealHour();               // ← real IST hour now
  if ((hr >= 5 && hr <= 8) || (hr >= 17 && hr <= 20)) mpm *= 1.8;
  if (temp > 26 && temp < 32 && hum > 65) mpm *= 1.4;
  return mpm;
}

int classifyMinuteRisk(float index) {
  if (index < 0.1)  return 0;
  if (index < 0.67) return 1;
  return 2;
}

void updateLEDs(int risk) {
  digitalWrite(LED_LOW,  LOW);
  digitalWrite(LED_MED,  LOW);
  digitalWrite(LED_HIGH, LOW);
  if (risk == 0)      digitalWrite(LED_LOW,  HIGH);
  else if (risk == 1) digitalWrite(LED_MED,  HIGH);
  else                digitalWrite(LED_HIGH, HIGH);
}

/* ----------- WIFI ----------- */
void connectWiFi() {
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
}

void ensureWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nReconnected: " + WiFi.localIP().toString());
  }
}

/* ----------- NTP SYNC ----------- */
void syncNTP() {
  configTime(UTC_OFFSET_IST, 0, NTP_SERVER);
  Serial.print("Syncing NTP time");
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (getLocalTime(&timeinfo)) {
    Serial.printf("\nTime synced: %02d:%02d:%02d IST\n",
      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    Serial.println("\nNTP sync failed, using millis fallback");
  }
}

/* ----------- MQTT ----------- */
void connectMQTT() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setSocketTimeout(10);
  mqttClient.setBufferSize(768);
  while (!mqttClient.connected()) {
    Serial.print("Connecting to Ubidots...");
    String clientId = "ESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqttClient.connect(clientId.c_str(), UBIDOTS_TOKEN, "")) {
      Serial.println("connected!");
    } else {
      Serial.print("failed rc=");
      Serial.print(mqttClient.state());
      Serial.println(", retrying in 3s...");
      delay(3000);
    }
  }
}

/* ----------- PUBLISH TO UBIDOTS ----------- */
void publishData(float temp, float hum, float activityIndex, int riskLevel) {
  ensureWiFiConnected();
  if (!mqttClient.connected()) connectMQTT();

  String topic = "/v1.6/devices/" + String(NODE_ID);

  StaticJsonDocument<768> doc;
  doc["temp"]           = temp;
  doc["humidity"]       = hum;
  doc["mosquito_count"] = recent30minTotal();
  doc["activity_index"] = activityIndex;
  doc["risk_level"]     = riskLevel;
  doc["total_count"]    = (int)totalMosquitoCount;

  JsonObject position = doc.createNestedObject("position");
  position["value"] = 1;
  JsonObject context = position.createNestedObject("context");
  context["lat"] = NODE_LAT;
  context["lng"] = NODE_LON;

  char payload[768];
  serializeJson(doc, payload);

  if (mqttClient.publish(topic.c_str(), payload)) {
    Serial.println("Published: " + String(payload));
  } else {
    Serial.println("Publish failed, will retry next cycle.");
  }
}

/* ----------- SETUP ----------- */
void setup() {
  Serial.begin(115200);
  pinMode(IR1_PIN,  INPUT_PULLUP);
  pinMode(IR2_PIN,  INPUT_PULLUP);
  pinMode(LED_LOW,  OUTPUT);
  pinMode(LED_MED,  OUTPUT);
  pinMode(LED_HIGH, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(IR1_PIN), IR1_trigger, FALLING);
  attachInterrupt(digitalPinToInterrupt(IR2_PIN), IR2_trigger, FALLING);
  dht.begin();
  memset(minuteCounts, 0, sizeof(minuteCounts));
  Serial.println("Smart Ovitrap Node v2.2 — Ubidots Edition");
  connectWiFi();
  syncNTP();                             // ← NTP sync after WiFi
  connectMQTT();
}

/* ----------- LOOP ----------- */
void loop() {
  ensureWiFiConnected();
  mqttClient.loop();

  if (newMosquitoEvent) {
    newMosquitoEvent = false;
    Serial.printf("[EVENT] Mosquito | dt:%lu ms | Min:%d | Total:%lu\n",
      lastDT, mosquitoCount, totalMosquitoCount);
  }

  static unsigned long lastMinute = 0;
  if (millis() - lastMinute >= 60000) {
    lastMinute = millis();
    minuteCounts[minuteIndex] = mosquitoCount;
    minuteIndex = (minuteIndex + 1) % WINDOW_SIZE;
    mosquitoCount = 0;
  }

  static unsigned long lastDHT = 0;
  if (millis() - lastDHT > 30000) {
    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();
    if (!isnan(temp) && !isnan(hum)) {
      float ai = computeMinuteIndex(temp, hum);
      int   rl = classifyMinuteRisk(ai);
      updateLEDs(rl);
      publishData(temp, hum, ai, rl);
      Serial.printf("Temp:%.1f Hum:%.1f AI:%.2f Risk:%d\n", temp, hum, ai, rl);
    }
    lastDHT = millis();
  }

  delay(50);
}