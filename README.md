---

# 🔆 Solar Relay Control System (Orange Pi / Embedded C + Web UI)

A lightweight solar monitoring and relay control system designed for embedded Linux devices such as Orange Pi.
It combines a C-based backend server, INA226 power monitoring, GPIO relay control, and a simple web-based dashboard.

---

## 📌 Features

* Real-time voltage, current, and power monitoring (INA226 via I2C)
* Battery percentage estimation
* Peak tracking (voltage, current, power)
* Dual relay control (automatic + manual override)
* Built-in lightweight HTTP server (no external backend required)
* HTML5 web dashboard interface
* Energy tracking (Wh accumulation)
* Automatic control logic based on thresholds
* Daily stats reset (6:30 AM)
* Event logging (last 10 entries)
* Web-based configuration panel

---

## 🧠 System Overview

INA226 sensor reads voltage/current/power →
C backend processes data →
GPIO controls relays →
Web dashboard displays real-time status via JSON API

---

## 🖥️ Web Interface

Access the dashboard at:

```
http://solar.lan
```

### Dashboard includes:

* Live voltage, current, power display
* Battery percentage bar
* Relay status (ON/OFF)
* Peak value tracking
* Configuration panel (threshold settings)
* Event log history

---

## 🔌 Hardware Requirements

* Orange Pi (or any Linux SBC with GPIO + I2C support)
* INA226 current/voltage sensor
* 2x relay module
* Solar panel + battery system

---

## ⚙️ Software Requirements

* GCC compiler
* Linux I2C enabled (`/dev/i2c-*`)
* Root access or GPIO permissions

---

## 🛠️ Build & Run

Compile:

```bash
gcc solar.c -o solar -lm
```

Run:

```bash
sudo ./solar
```

---

## 🌐 API Endpoints

### GET `/api/data`

Returns full system status in JSON format:

```json
{
  "voltage": 12.5,
  "current_ma": 120.0,
  "power_mw": 1500.0,
  "battery_pct": 78.2,
  "relay": 1,
  "relay2": 0,
  "timestamp": "2026-06-17 12:00:00",
  "logs": []
}
```

---

### GET `/toggle?relay=1&state=1`

Manually control relay:

* `relay=1` → Relay 1
* `relay=2` → Relay 2
* `state=1` → ON
* `state=0` → OFF

---

### POST `/save`

Save configuration values:

* Voltage low/high thresholds
* Current cutoff values
* Relay behavior settings

---

## ⚙️ Control Logic

### Relay 1 (Battery/Charging Control)

* ON when voltage ≤ low threshold
* OFF when voltage ≥ high threshold AND current condition satisfied

### Relay 2 (Load Control)

* Based on voltage range window (ON/OFF thresholds)

---

## 🔋 Battery Estimation

Battery percentage is estimated using:

* Voltage thresholds
* Load/charging condition

---

## 🧾 Logging System

Stores last 10 system events:

* Relay state changes
* Configuration updates
* System initialization
* Sensor errors

---

## 🔐 GPIO Mapping

| Relay   | GPIO |
| ------- | ---- |
| Relay 1 | 70   |
| Relay 2 | 69   |

---

## ⏱️ System Behavior

* Adaptive polling (1–2 seconds depending on load condition)
* Debounce protection for relay switching
* I2C error handling
* Safe GPIO initialization on startup

---

## 📂 Configuration File

Stored at:

```
/var/lib/solar_relay.conf
```

---

## 🚀 Future Improvements

* SQLite logging system
* MQTT integration
* Mobile-friendly UI upgrade
* Authentication layer
* Historical graphs (Chart.js)
* Power prediction module

---

## 📜 License

MIT License

---
