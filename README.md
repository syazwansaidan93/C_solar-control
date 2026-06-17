```markdown id="solar-relay-readme"
# 🔆 Solar Relay Control System (Orange Pi / Embedded C + Web UI)

A lightweight **solar monitoring and relay control system** built for embedded Linux devices (e.g. Orange Pi).  
It combines a **C-based backend server**, **INA226 power monitoring**, **GPIO relay control**, and a **modern HTML5 dashboard**.

---

## 📌 Features

- ⚡ Real-time voltage, current, power monitoring (INA226 via I2C)
- 🔋 Battery percentage estimation
- 📊 Peak tracking (voltage / current / power)
- 🔁 Dual relay control (automatic + manual override)
- 🌐 Built-in HTTP server (no external backend needed)
- 📱 Responsive HTML5 dashboard UI
- 📈 Energy tracking (Wh accumulation)
- 🧠 Auto control logic based on thresholds
- 🕒 Daily auto reset (6:30 AM)
- 📝 Event logging (last 10 logs)
- 🔧 Configurable thresholds via web UI

---

## 🧠 System Architecture

```

[ INA226 Sensor ] → I2C → [ C Backend Server ] → JSON API → [ Web Dashboard ]
↓
GPIO Relays

```

---

## 🖥️ Web Dashboard

Access via:

```

[http://solar.lan](http://solar.lan)

````

### UI Features:
- Live voltage / current / power display
- Battery percentage bar
- Relay ON/OFF status
- Peak value tracking
- Configuration panel (cutoff thresholds)
- Event history log

---

## 🔌 Hardware Requirements

- Orange Pi (or Linux SBC with GPIO + I2C)
- INA226 current/voltage sensor
- 2x Relay module (GPIO controlled)
- Solar panel + battery system
- Linux GPIO sysfs support

---

## ⚙️ Software Requirements

- GCC compiler
- Linux I2C tools enabled (`/dev/i2c-*`)
- Nginx (optional, for serving UI)
- Root or GPIO permission access

---

## 🛠️ Build Instructions

Compile the backend:

```bash
gcc solar.c -o solar -lm
````

Run:

```bash
sudo ./solar
```

---

## 🌐 API Endpoints

### GET `/api/data`

Returns full system status:

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

Save configuration:

* v_low / v_high thresholds
* current cutoff values
* relay behavior settings

---

## ⚙️ Control Logic

### Relay 1 (Charging Control)

* ON if battery voltage ≤ low threshold
* OFF if voltage ≥ high threshold AND current condition met

### Relay 2 (Load Control)

* ON/OFF based on voltage window

---

## 🔋 Battery Estimation

Battery percentage is estimated using:

* Voltage thresholds
* Load/charge current condition

---

## 🧾 Logging System

Stores last 10 events such as:

* Relay state changes
* System resets
* Configuration updates
* Sensor errors

---

## 🔐 GPIO Mapping

| Relay   | GPIO Pin |
| ------- | -------- |
| Relay 1 | 70       |
| Relay 2 | 69       |

---

## ⏱️ Timing & Safety

* Debounce protection for relay switching
* Adaptive polling interval (1–2s)
* I2C error detection
* Safe GPIO initialization

---

## 📂 Configuration File

Stored at:

```
/var/lib/solar_relay.conf
```

---

## 🚀 Future Improvements

* SQLite logging support
* MQTT integration
* Mobile app dashboard
* Web authentication
* Historical graphs (Chart.js)
* Power forecasting

---

## 📜 License

MIT License — free to use and modify.

---

## 👨‍💻 Author

Built for embedded solar monitoring & automation systems.

```
```
