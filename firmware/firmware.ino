// ============================================================
//  PlantOS - Full Firmware
// ============================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>

// ============================================================
//  PIN DEFINITIONS
// ============================================================
#define LED_PIN         17
#define LED_COUNT       1
#define RELAY1_PIN      21   // PUMP
#define RELAY2_PIN      19   // LIGHT
#define DHT_PIN         4
#define BH1750_SDA      25
#define BH1750_SCL      26
#define MQ135_PIN       14   // ADC
#define DS18B20_PIN     32
#define SOIL_PIN        33   // ADC
#define PT100_PIN       36   // VP (ADC1_CH0)

// ============================================================
//  STORED CONFIG
// ============================================================
#define PREF_NS "plantos"

struct WiFiCreds {
  char ssid[64];
  char pass[64];
};

struct MQTTConfig {
  char server[128];
  int  port;
  char user[64];
  char pass[64];
  char topic[64];
};

WiFiCreds wifiCreds;
MQTTConfig mqttCfg;

// Default values
const char* DEFAULT_SERVER = "f388acb36bf542b69c0c3cb96cb2cb16.s1.eu.hivemq.cloud";
const int   DEFAULT_PORT   = 8883;
const char* DEFAULT_USER   = "plantos";
const char* DEFAULT_PASS   = "Plantos1";
const char* DEFAULT_TOPIC  = "device/RDH001/data";

// ============================================================
//  CALIBRATION OFFSETS (stored in NVS)
// ============================================================
struct CalData {
  float dht_temp_offset;
  float dht_hum_offset;
  float ds18b20_offset;
  float mq135_offset;
  int   soil_dry;
  int   soil_wet;
  float pt100_ref_ohm;
  float pt100_ref_temp;
};

CalData cal;

const CalData CAL_DEFAULTS = {
  .dht_temp_offset  = 0.0f,
  .dht_hum_offset   = 0.0f,
  .ds18b20_offset   = 0.0f,
  .mq135_offset     = 0.0f,
  .soil_dry         = 3500,
  .soil_wet         = 1500,
  .pt100_ref_ohm    = 100.0f,
  .pt100_ref_temp   = 0.0f
};

// ============================================================
//  RELAY THRESHOLDS
// ============================================================
#define SOIL_ON_THRESHOLD    25.0f
#define SOIL_OFF_THRESHOLD   80.0f
#define LUX_ON_THRESHOLD     200.0f
#define LUX_OFF_THRESHOLD    10000.0f

// ============================================================
//  GLOBAL OBJECTS
// ============================================================
WiFiClientSecure net;
PubSubClient     client(net);
Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
DHT              dht(DHT_PIN, DHT22);
BH1750           lightMeter;
OneWire          oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
Preferences      prefs;

// ============================================================
//  STATE
// ============================================================
bool wifiConnected  = false;
bool mqttConnected  = false;
bool bh1750Present  = false;
bool bh1750Checked  = false;
bool relay1State    = false;
bool relay2State    = false;

unsigned long lastMsg   = 0;
unsigned long lastBlink = 0;
bool          ledState  = false;

// ============================================================
//  CALIBRATION WAIT STATE
// ============================================================
enum CalStep { CAL_IDLE, CAL_WAIT_REF };
CalStep   calStep      = CAL_IDLE;
String    calSensor    = "";
float     calRawValue  = 0;

// ============================================================
//  SCAN RESULT STORAGE
// ============================================================
// Stores the last WiFi scan so the user can pick by number
// without re-scanning between the list display and password entry.
#define MAX_SCAN_RESULTS 20
String scannedSSIDs[MAX_SCAN_RESULTS];
int    scannedCount = 0;

// Interactive WiFi state machine
enum WiFiStep {
  WIFI_IDLE,
  WIFI_WAIT_CHOICE,
  WIFI_WAIT_PASSWORD
};
WiFiStep wifiStep     = WIFI_IDLE;
int      wifiChoice   = -1;  // 1-based index chosen by user

// ============================================================
//  NVS HELPERS
// ============================================================
void loadPreferences() {
  prefs.begin(PREF_NS, true);

  strncpy(wifiCreds.ssid, prefs.getString("w_ssid", "").c_str(), sizeof(wifiCreds.ssid));
  strncpy(wifiCreds.pass, prefs.getString("w_pass", "").c_str(), sizeof(wifiCreds.pass));

  strncpy(mqttCfg.server, prefs.getString("srv",   DEFAULT_SERVER).c_str(), sizeof(mqttCfg.server));
  mqttCfg.port = prefs.getInt("port", DEFAULT_PORT);
  strncpy(mqttCfg.user,  prefs.getString("user",   DEFAULT_USER).c_str(),   sizeof(mqttCfg.user));
  strncpy(mqttCfg.pass,  prefs.getString("pass",   DEFAULT_PASS).c_str(),   sizeof(mqttCfg.pass));
  strncpy(mqttCfg.topic, prefs.getString("topic",  DEFAULT_TOPIC).c_str(),  sizeof(mqttCfg.topic));

  cal.dht_temp_offset = prefs.getFloat("cal_dt", CAL_DEFAULTS.dht_temp_offset);
  cal.dht_hum_offset  = prefs.getFloat("cal_dh", CAL_DEFAULTS.dht_hum_offset);
  cal.ds18b20_offset  = prefs.getFloat("cal_ds", CAL_DEFAULTS.ds18b20_offset);
  cal.mq135_offset    = prefs.getFloat("cal_mq", CAL_DEFAULTS.mq135_offset);
  cal.soil_dry        = prefs.getInt("cal_sd",    CAL_DEFAULTS.soil_dry);
  cal.soil_wet        = prefs.getInt("cal_sw",    CAL_DEFAULTS.soil_wet);
  cal.pt100_ref_ohm   = prefs.getFloat("cal_pr", CAL_DEFAULTS.pt100_ref_ohm);
  cal.pt100_ref_temp  = prefs.getFloat("cal_pt", CAL_DEFAULTS.pt100_ref_temp);

  prefs.end();
}

void saveWiFiCreds() {
  prefs.begin(PREF_NS, false);
  prefs.putString("w_ssid", wifiCreds.ssid);
  prefs.putString("w_pass", wifiCreds.pass);
  prefs.end();
}

void saveCalibration() {
  prefs.begin(PREF_NS, false);
  prefs.putFloat("cal_dt", cal.dht_temp_offset);
  prefs.putFloat("cal_dh", cal.dht_hum_offset);
  prefs.putFloat("cal_ds", cal.ds18b20_offset);
  prefs.putFloat("cal_mq", cal.mq135_offset);
  prefs.putInt("cal_sd",   cal.soil_dry);
  prefs.putInt("cal_sw",   cal.soil_wet);
  prefs.putFloat("cal_pr", cal.pt100_ref_ohm);
  prefs.putFloat("cal_pt", cal.pt100_ref_temp);
  prefs.end();
}

void saveMQTTConfig() {
  prefs.begin(PREF_NS, false);
  prefs.putString("srv",   mqttCfg.server);
  prefs.putInt("port",     mqttCfg.port);
  prefs.putString("user",  mqttCfg.user);
  prefs.putString("pass",  mqttCfg.pass);
  prefs.putString("topic", mqttCfg.topic);
  prefs.end();
}

// ============================================================
//  LED
// ============================================================
void setLED(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

void handleLED() {
  if (!wifiConnected) {
    setLED(255, 0, 0);
    return;
  }
  if (!mqttConnected) {
    if (millis() - lastBlink > 500) {
      lastBlink = millis();
      ledState = !ledState;
      ledState ? setLED(255, 0, 0) : setLED(0, 0, 0);
    }
    return;
  }
  if (millis() - lastBlink > 500) {
    lastBlink = millis();
    ledState = !ledState;
    ledState ? setLED(0, 0, 255) : setLED(0, 0, 0);
  }
}

// ============================================================
//  RELAY CONTROL
// ============================================================
void setRelay(int pin, bool &state, bool on) {
  state = on;
  digitalWrite(pin, on ? HIGH : LOW);
}

// ============================================================
//  SENSOR READING
// ============================================================
float readDHTTemp()     { return dht.readTemperature() + cal.dht_temp_offset; }
float readDHTHumidity() { return dht.readHumidity()    + cal.dht_hum_offset; }

float readSoilMoisture() {
  int raw = analogRead(SOIL_PIN);
  float pct = map(raw, cal.soil_dry, cal.soil_wet, 0, 100);
  return constrain(pct, 0.0f, 100.0f);
}

float readMQ135() {
  return analogRead(MQ135_PIN) + cal.mq135_offset;
}

float readDS18B20() {
  ds18b20.requestTemperatures();
  float t = ds18b20.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C) return NAN;
  return t + cal.ds18b20_offset;
}

float readPT100() {
  int   raw     = analogRead(PT100_PIN);
  float voltage = raw * (3.3f / 4095.0f);
  float r_pt100 = (voltage * 100.0f) / (3.3f - voltage);
  float R0 = cal.pt100_ref_ohm / (1.0f + 3.9083e-3f * cal.pt100_ref_temp);
  float temp = (r_pt100 / R0 - 1.0f) / 3.9083e-3f;
  return temp;
}

float readLux() {
  if (!bh1750Present) return NAN;
  return lightMeter.readLightLevel();
}

// ============================================================
//  RELAY AUTOMATION
// ============================================================
void handleRelays(float moisture, float lux) {
  if (!isnan(moisture)) {
    if (!relay1State && moisture < SOIL_ON_THRESHOLD)
      setRelay(RELAY1_PIN, relay1State, true);
    else if (relay1State && moisture > SOIL_OFF_THRESHOLD)
      setRelay(RELAY1_PIN, relay1State, false);
  }
  if (!isnan(lux)) {
    if (!relay2State && lux < LUX_ON_THRESHOLD)
      setRelay(RELAY2_PIN, relay2State, true);
    else if (relay2State && lux > LUX_OFF_THRESHOLD)
      setRelay(RELAY2_PIN, relay2State, false);
  }
}

// ============================================================
//  BUILD + PUBLISH JSON
// ============================================================
void publishData() {
  if (millis() - lastMsg < 5000) return;
  lastMsg = millis();

  float moisture = readSoilMoisture();
  float lux      = readLux();
  float dhtTemp  = readDHTTemp();
  float dhtHum   = readDHTHumidity();
  float dsTemp   = readDS18B20();
  float mq135    = readMQ135();
  float pt100    = readPT100();

  handleRelays(moisture, lux);

  char payload[512];
  char luxBuf[16], dsTempBuf[16];

  if (isnan(lux))    snprintf(luxBuf,    sizeof(luxBuf),    "null");
  else               snprintf(luxBuf,    sizeof(luxBuf),    "%.2f", lux);

  if (isnan(dsTemp)) snprintf(dsTempBuf, sizeof(dsTempBuf), "null");
  else               snprintf(dsTempBuf, sizeof(dsTempBuf), "%.2f", dsTemp);

  snprintf(payload, sizeof(payload),
    "{"
      "\"ssid\":\"%s\","
      "\"dht_temp\":%.2f,"
      "\"dht_hum\":%.2f,"
      "\"soil_pct\":%.1f,"
      "\"lux\":%s,"
      "\"mq135\":%.0f,"
      "\"ds18b20\":%s,"
      "\"pt100\":%.2f,"
      "\"relay1\":%s,"
      "\"relay2\":%s"
    "}",
    WiFi.SSID().c_str(),
    dhtTemp, dhtHum,
    moisture,
    luxBuf,
    mq135,
    dsTempBuf,
    pt100,
    relay1State ? "true" : "false",
    relay2State ? "true" : "false"
  );

  client.publish(mqttCfg.topic, payload);
  Serial.println(payload);
}

// ============================================================
//  CALIBRATION LOGIC
// ============================================================
void startCalibration(String sensor) {
  sensor.toLowerCase();
  calSensor = sensor;

  if (sensor == "soil") {
    Serial.println("SOIL CAL: Place sensor in AIR (dry), then enter '0'");
    Serial.println("          Or place in WATER and enter '100'");
    Serial.println("Enter reference value (0 = dry / 100 = wet):");
    calRawValue = analogRead(SOIL_PIN);
  } else if (sensor == "dht_temp") {
    calRawValue = dht.readTemperature();
    Serial.printf("  Raw: %.2f C\n", calRawValue);
    Serial.println("Enter REFERENCE temperature (C):");
  } else if (sensor == "dht_hum") {
    calRawValue = dht.readHumidity();
    Serial.printf("  Raw: %.2f %%\n", calRawValue);
    Serial.println("Enter REFERENCE humidity (%%):");
  } else if (sensor == "ds18b20") {
    ds18b20.requestTemperatures();
    calRawValue = ds18b20.getTempCByIndex(0);
    Serial.printf("  Raw: %.2f C\n", calRawValue);
    Serial.println("Enter REFERENCE temperature (C):");
  } else if (sensor == "mq135") {
    calRawValue = analogRead(MQ135_PIN);
    Serial.printf("  Raw ADC: %.0f\n", calRawValue);
    Serial.println("Enter REFERENCE clean-air ADC value:");
  } else if (sensor == "pt100") {
    calRawValue = readPT100();
    Serial.printf("  Current reading: %.2f C\n", calRawValue);
    Serial.println("Enter REFERENCE temperature (C):");
  } else {
    Serial.println("Unknown sensor. Options: dht_temp, dht_hum, ds18b20, mq135, soil, pt100");
    calStep = CAL_IDLE;
    return;
  }
  calStep = CAL_WAIT_REF;
}

void applyCalibration(float refValue) {
  if (calSensor == "dht_temp") {
    cal.dht_temp_offset = refValue - calRawValue;
    Serial.printf("DHT Temp offset: %.2f\n", cal.dht_temp_offset);
  } else if (calSensor == "dht_hum") {
    cal.dht_hum_offset = refValue - calRawValue;
    Serial.printf("DHT Hum offset: %.2f\n", cal.dht_hum_offset);
  } else if (calSensor == "ds18b20") {
    cal.ds18b20_offset = refValue - calRawValue;
    Serial.printf("DS18B20 offset: %.2f\n", cal.ds18b20_offset);
  } else if (calSensor == "mq135") {
    cal.mq135_offset = refValue - calRawValue;
    Serial.printf("MQ135 offset: %.2f\n", cal.mq135_offset);
  } else if (calSensor == "soil") {
    int raw = analogRead(SOIL_PIN);
    if ((int)refValue == 0) {
      cal.soil_dry = raw;
      Serial.printf("Soil DRY point saved: %d\n", raw);
    } else {
      cal.soil_wet = raw;
      Serial.printf("Soil WET point saved: %d\n", raw);
    }
  } else if (calSensor == "pt100") {
    int   raw     = analogRead(PT100_PIN);
    float voltage = raw * (3.3f / 4095.0f);
    float r_pt100 = (voltage * 100.0f) / (3.3f - voltage);
    cal.pt100_ref_ohm  = r_pt100;
    cal.pt100_ref_temp = refValue;
    Serial.printf("PT100 ref: %.2f Ohm @ %.2f C\n", r_pt100, refValue);
  }

  saveCalibration();
  Serial.println("Calibration saved.");
  calStep   = CAL_IDLE;
  calSensor = "";
}

// ============================================================
//  WIFI AUTO-RECONNECT (uses saved credentials)
// ============================================================
bool autoConnectWiFi() {
  if (strlen(wifiCreds.ssid) == 0) return false;

  // Disconnect cleanly before reconnecting to a (possibly new) SSID
  WiFi.disconnect(true);
  delay(200);

  Serial.printf("Auto-connecting to: %s\n", wifiCreds.ssid);
  strlen(wifiCreds.pass) == 0
    ? WiFi.begin(wifiCreds.ssid)
    : WiFi.begin(wifiCreds.ssid, wifiCreds.pass);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500); Serial.print("."); retry++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    wifiConnected = true;
    return true;
  }
  Serial.println("Auto-connect failed.");
  wifiConnected = false;
  return false;
}

// ============================================================
//  WIFI CONNECT - NON-BLOCKING SCAN + STATE MACHINE
//
//  Calling connectWiFiScan() starts the process:
//    1. Performs a blocking WiFi scan (takes ~2-3 s).
//    2. Prints numbered list of found networks.
//    3. Sets wifiStep = WIFI_WAIT_CHOICE so the loop() knows
//       the next Serial input is a network number.
//
//  The loop() then feeds further input into
//  handleWiFiInput() which advances the state machine:
//    WIFI_WAIT_CHOICE  -> user types a number -> saved, prompts password
//    WIFI_WAIT_PASSWORD -> user types password -> connect, save, MQTT
// ============================================================
void connectWiFiScan() {
  Serial.println("\nScanning for nearby WiFi networks, please wait...");

  // Disconnect first so the scan works properly even if already connected
  WiFi.disconnect(true);
  delay(100);
  wifiConnected = false;
  mqttConnected = false;

  int n = WiFi.scanNetworks();  // blocking scan

  if (n <= 0) {
    Serial.println("No networks found. Type 'connect' to try again.");
    wifiStep = WIFI_IDLE;
    return;
  }

  scannedCount = (n > MAX_SCAN_RESULTS) ? MAX_SCAN_RESULTS : n;
  Serial.println("\n--- Available Networks ---");
  for (int i = 0; i < scannedCount; i++) {
    scannedSSIDs[i] = WiFi.SSID(i);
    Serial.printf("  %d. %-32s  RSSI: %d dBm  %s\n",
      i + 1,
      scannedSSIDs[i].c_str(),
      WiFi.RSSI(i),
      (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "[Open]" : "[Secured]"
    );
  }
  Serial.println("--------------------------");
  Serial.println("Enter network number to connect:");

  wifiStep   = WIFI_WAIT_CHOICE;
  wifiChoice = -1;
}

// Called from loop() whenever a serial line arrives and wifiStep != WIFI_IDLE
void handleWiFiInput(String input) {
  input.trim();

  if (wifiStep == WIFI_WAIT_CHOICE) {
    int choice = input.toInt();
    if (choice < 1 || choice > scannedCount) {
      Serial.printf("Invalid choice. Please enter a number between 1 and %d:\n", scannedCount);
      return;  // stay in WIFI_WAIT_CHOICE
    }
    wifiChoice = choice;

    // Check if the network is open
    bool isOpen = (WiFi.encryptionType(wifiChoice - 1) == WIFI_AUTH_OPEN);
    Serial.printf("Selected: %s\n", scannedSSIDs[wifiChoice - 1].c_str());
    if (isOpen) {
      Serial.println("Open network detected, connecting without password...");
      // Treat as empty password
      handleWiFiInput("");  // reuse password handler with blank
      return;
    }
    Serial.println("Enter WiFi password (press Enter for open/no password):");
    wifiStep = WIFI_WAIT_PASSWORD;
    return;
  }

  if (wifiStep == WIFI_WAIT_PASSWORD) {
    String ssid = scannedSSIDs[wifiChoice - 1];
    String pass = input;  // already trimmed above

    // Disconnect cleanly before attempting new connection
    WiFi.disconnect(true);
    delay(200);

    Serial.printf("Connecting to: %s ...\n", ssid.c_str());
    if (pass.length() == 0) {
      WiFi.begin(ssid.c_str());
    } else {
      WiFi.begin(ssid.c_str(), pass.c_str());
    }

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) {
      delay(500); Serial.print("."); retry++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi Connected! IP: %s\n", WiFi.localIP().toString().c_str());
      wifiConnected = true;

      // Persist new credentials for future auto-reconnect
      strncpy(wifiCreds.ssid, ssid.c_str(), sizeof(wifiCreds.ssid));
      strncpy(wifiCreds.pass, pass.c_str(), sizeof(wifiCreds.pass));
      saveWiFiCreds();
      Serial.println("WiFi credentials saved.");

      // Reconnect MQTT on the new network
      connectMQTT();
    } else {
      Serial.println("WiFi connection failed. Type 'connect' to try again.");
      wifiConnected = false;
    }

    wifiStep   = WIFI_IDLE;
    wifiChoice = -1;
    return;
  }
}

// ============================================================
//  MQTT CONFIG EDITOR
// ============================================================

// Non-blocking MQTT config editor state machine
enum MQTTStep {
  MQTT_IDLE,
  MQTT_WAIT_SERVER,
  MQTT_WAIT_PORT,
  MQTT_WAIT_USER,
  MQTT_WAIT_PASS,
  MQTT_WAIT_TOPIC
};
MQTTStep mqttStep = MQTT_IDLE;

void startMQTTConfig() {
  Serial.println("\n--- MQTT Configuration ---");
  Serial.println("Press Enter to keep the current value shown in [ ].\n");
  Serial.printf("Server [%s]: ", mqttCfg.server);
  mqttStep = MQTT_WAIT_SERVER;
}

void handleMQTTInput(String input) {
  input.trim();

  switch (mqttStep) {
    case MQTT_WAIT_SERVER:
      if (input.length()) strncpy(mqttCfg.server, input.c_str(), sizeof(mqttCfg.server));
      Serial.printf("Port [%d]: ", mqttCfg.port);
      mqttStep = MQTT_WAIT_PORT;
      break;

    case MQTT_WAIT_PORT:
      if (input.length()) mqttCfg.port = input.toInt();
      Serial.printf("User [%s]: ", mqttCfg.user);
      mqttStep = MQTT_WAIT_USER;
      break;

    case MQTT_WAIT_USER:
      if (input.length()) strncpy(mqttCfg.user, input.c_str(), sizeof(mqttCfg.user));
      Serial.print("Pass (hidden): ");
      mqttStep = MQTT_WAIT_PASS;
      break;

    case MQTT_WAIT_PASS:
      if (input.length()) strncpy(mqttCfg.pass, input.c_str(), sizeof(mqttCfg.pass));
      Serial.printf("Topic [%s]: ", mqttCfg.topic);
      mqttStep = MQTT_WAIT_TOPIC;
      break;

    case MQTT_WAIT_TOPIC:
      if (input.length()) strncpy(mqttCfg.topic, input.c_str(), sizeof(mqttCfg.topic));
      saveMQTTConfig();
      Serial.println("MQTT config saved. Reconnecting...");
      client.disconnect();
      mqttConnected = false;
      if (wifiConnected) connectMQTT();
      mqttStep = MQTT_IDLE;
      break;

    default:
      mqttStep = MQTT_IDLE;
      break;
  }
}

// ============================================================
//  MQTT CONNECT
// ============================================================
void connectMQTT() {
  net.setInsecure();
  client.setServer(mqttCfg.server, mqttCfg.port);
  Serial.println("Connecting to MQTT broker...");
  if (client.connect("ESP32Client", mqttCfg.user, mqttCfg.pass)) {
    Serial.println("MQTT Connected!");
    mqttConnected = true;
  } else {
    Serial.printf("MQTT Failed! rc=%d\n", client.state());
    mqttConnected = false;
  }
}

// ============================================================
//  STATUS COMMAND
// ============================================================
void printStatus() {
  Serial.println("\n--- PlantOS Status ---");
  Serial.printf("WiFi SSID   : %s\n", wifiConnected ? WiFi.SSID().c_str() : "Not connected");
  Serial.printf("IP Address  : %s\n", wifiConnected ? WiFi.localIP().toString().c_str() : "N/A");
  Serial.printf("MQTT Broker : %s:%d\n", mqttCfg.server, mqttCfg.port);
  Serial.printf("MQTT Topic  : %s\n", mqttCfg.topic);
  Serial.printf("MQTT Status : %s\n", mqttConnected ? "Connected" : "Disconnected");
  Serial.printf("Relay1 PUMP : %s\n", relay1State ? "ON" : "OFF");
  Serial.printf("Relay2 LIGHT: %s\n", relay2State ? "ON" : "OFF");
  Serial.printf("Saved SSID  : %s\n", strlen(wifiCreds.ssid) ? wifiCreds.ssid : "(none)");
  Serial.println("----------------------");
}

// ============================================================
//  HELP
// ============================================================
void printHelp() {
  Serial.println("\n--- PlantOS Commands ---");
  Serial.println("connect             : Scan nearby WiFi and connect to selected hotspot");
  Serial.println("connect mqtt        : Edit MQTT broker settings");
  Serial.println("status              : Show current WiFi, MQTT and relay state");
  Serial.println("cal dht_temp        : Calibrate DHT22 temperature");
  Serial.println("cal dht_hum         : Calibrate DHT22 humidity");
  Serial.println("cal ds18b20         : Calibrate DS18B20 temperature");
  Serial.println("cal mq135           : Calibrate MQ135 gas sensor");
  Serial.println("cal soil            : Calibrate soil moisture (two-point: dry=0, wet=100)");
  Serial.println("cal pt100           : Calibrate PT100 temperature");
  Serial.println("help                : Show this command list");
  Serial.println("------------------------");
}

// ============================================================
//  COMMAND HANDLER (top-level, only when no sub-state is active)
// ============================================================
void handleCommand(String cmd) {
  cmd.trim();

  if (cmd == "connect") {
    connectWiFiScan();
    return;
  }

  if (cmd == "connect mqtt") {
    startMQTTConfig();
    return;
  }

  if (cmd == "status") {
    printStatus();
    return;
  }

  if (cmd == "help") {
    printHelp();
    return;
  }

  if (cmd.startsWith("cal ")) {
    String sensor = cmd.substring(4);
    sensor.trim();
    startCalibration(sensor);
    return;
  }

  Serial.println("Unknown command. Type 'help' for a list of commands.");
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(RELAY1_PIN, OUTPUT); digitalWrite(RELAY1_PIN, LOW);
  pinMode(RELAY2_PIN, OUTPUT); digitalWrite(RELAY2_PIN, LOW);

  led.begin(); led.show();

  loadPreferences();

  dht.begin();

  Wire.begin(BH1750_SDA, BH1750_SCL);
  bh1750Present = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire);
  bh1750Checked = true;
  if (!bh1750Present) Serial.println("[WARN] BH1750 Not Detected");

  ds18b20.begin();

  Serial.println("\nPlantOS Ready.");
  printHelp();

  // Auto-reconnect using saved credentials
  if (autoConnectWiFi()) {
    connectMQTT();
  } else {
    Serial.println("No saved WiFi credentials. Type 'connect' to configure WiFi.");
  }
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Serial command input - read one full line when available
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    // Priority order: calibration > MQTT config > WiFi scan > normal command
    if (calStep == CAL_WAIT_REF) {
      float ref = input.toFloat();
      applyCalibration(ref);

    } else if (mqttStep != MQTT_IDLE) {
      handleMQTTInput(input);

    } else if (wifiStep != WIFI_IDLE) {
      handleWiFiInput(input);

    } else {
      handleCommand(input);
    }
  }

  // WiFi watchdog - only reconnect if not in the middle of interactive setup
  if (wifiStep == WIFI_IDLE && wifiConnected && WiFi.status() != WL_CONNECTED) {
    Serial.println("[WARN] WiFi dropped. Attempting auto-reconnect...");
    wifiConnected = false;
    mqttConnected = false;
    autoConnectWiFi();
  }

  // If WiFi came back but was never connected (first boot, no saved creds), skip watchdog noise
  if (wifiStep == WIFI_IDLE && !wifiConnected && WiFi.status() != WL_CONNECTED
      && strlen(wifiCreds.ssid) > 0) {
    // Only retry auto-connect if we have saved creds and are not in interactive mode
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 30000) {  // retry every 30 s
      lastRetry = millis();
      autoConnectWiFi();
    }
  }

  // MQTT keep-alive
  if (wifiConnected && !client.connected()) {
    mqttConnected = false;
    connectMQTT();
  }
  if (client.connected()) {
    mqttConnected = true;
    client.loop();
    publishData();
  }

  handleLED();
}
