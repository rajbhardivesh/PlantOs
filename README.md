# 🌱 PlantOS – Smart IoT Plant Monitoring & Automation System

![Platform](https://img.shields.io/badge/Platform-ESP32-blue)
![Protocol](https://img.shields.io/badge/MQTT-TLS-green)
![Status](https://img.shields.io/badge/Status-Active-success)
![License](https://img.shields.io/badge/License-MIT-orange)

---

## 🚀 Overview

**PlantOS** is a complete **IoT-based plant monitoring and automation system** designed for real-world deployment.

It integrates:
- 📡 Embedded Firmware (ESP32)
- ⚡ Custom PCB Hardware
- 🌐 Web Dashboard
- ☁️ MQTT Cloud Communication

The system continuously monitors environmental conditions and automatically controls irrigation and lighting.

---

## 📸 Hardware Preview

Custom ESP32-based PCB for plant monitoring and automation.

---

## ✨ Key Features

### 🌡️ Multi-Sensor Monitoring
- DHT22 → Temperature & Humidity  
- DS18B20 → High-accuracy temperature  
- PT100 → Industrial temperature sensing  
- BH1750 → Light intensity (Lux)  
- MQ135 → Air quality monitoring  
- Soil Moisture Sensor  

---

### ⚡ Smart Automation
- 💧 Automatic irrigation based on soil moisture  
- 💡 Automatic lighting based on ambient lux  
- ⚙️ Relay-based control system  

---

### 🌐 IoT Connectivity
- WiFi provisioning via Serial interface  
- Secure MQTT communication (TLS supported)  
- Real-time JSON data streaming  

---

### 🧠 Intelligent Calibration System

Supports runtime calibration via commands:

```
cal dht_temp
cal soil
cal pt100
cal mq135
```

✔ Stored in non-volatile memory (NVS)  
✔ No need to reconfigure after reboot  

---

### 💡 LED Status Indicator

| State | Meaning |
|------|--------|
| 🔴 Solid | No WiFi |
| 🔴 Blinking | WiFi connected, MQTT disconnected |
| 🔵 Blinking | Fully connected |

---

## 🧱 Hardware Specifications

### 🔌 Microcontroller
- ESP32

### 🔁 Outputs
| Device | GPIO |
|------|------|
| Pump Relay | GPIO21 |
| Light Relay | GPIO19 |

### 📥 Inputs
| Sensor | GPIO |
|------|------|
| DHT22 | GPIO4 |
| BH1750 | GPIO25, GPIO26 |
| MQ135 | GPIO14 |
| DS18B20 | GPIO32 |
| Soil Sensor | GPIO33 |
| PT100 | GPIO36 |

---

## 📡 MQTT Configuration

### Default Settings

```json
{
  "server": "f388acb36bf542b69c0c3cb96cb2cb16.s1.eu.hivemq.cloud",
  "port": 8883,
  "user": "plantos",
  "topic": "device/RDH001/data"
}
```

---

## 📊 Data Format (Published JSON)

```json
{
  "ssid": "YourWiFi",
  "dht_temp": 28.5,
  "dht_hum": 60.2,
  "soil_pct": 45.0,
  "lux": 320.5,
  "mq135": 210,
  "ds18b20": 27.1,
  "pt100": 26.9,
  "relay1": true,
  "relay2": false
}
```

---

## 🧩 Repository Structure

```
root/
│
├── index.html          # 🌐 Web Dashboard
├── web/src             # Frontend (JS, CSS)
├── pcb/source          # PCB Design Files
├── firmware            # ESP32 Firmware
```

---

## ⚙️ Setup Guide

### 1. Flash Firmware
- Open in Arduino IDE / PlatformIO  
- Select ESP32 board  
- Upload firmware  

### 2. Configure WiFi

Open Serial Monitor (115200 baud):

```
connect
```

### 3. Configure MQTT (Optional)

```
connect mqtt
```

### 4. Launch Dashboard

Open:
```
index.html
```

---

## 📟 Serial Commands

| Command | Description |
|--------|------------|
| connect | Scan WiFi |
| connect mqtt | Configure MQTT |
| status | System status |
| help | Show commands |
| cal <sensor> | Calibrate |

---

## 🔄 Automation Logic

### Pump
- ON → Soil < 25%
- OFF → Soil > 80%

### Light
- ON → Lux < 200
- OFF → Lux > 10000

---

## 🧠 Firmware Highlights

- Non-blocking state machine
- Auto reconnect WiFi
- MQTT keep-alive
- Persistent storage (NVS)
- JSON telemetry system

---

## 🌍 Use Cases

- Smart Agriculture
- Greenhouses
- Indoor Plants
- Research Labs
- Industrial IoT

---

## 📈 Future Roadmap

- Mobile App
- OTA Updates
- Cloud Dashboard
- LoRa Mode
- AI Automation

---

## Speacial  Thanks To 
PCB Design :  Rajbhar Divesh
Firmware & Web Dashboard: Varikar Manan 
 : 
## Author
** Varikar Manan **
## back-end Author 
** himanshu dave **
