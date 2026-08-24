# 🔌 ESP32 MODBUS RTU Intelligent Proxy

[![Build](https://img.shields.io/github/actions/workflow/status/SensorsIot/Modbus_Proxy/build.yml?branch=main)](https://github.com/SensorsIot/Modbus_Proxy/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/SensorsIot/Modbus_Proxy)](https://github.com/SensorsIot/Modbus_Proxy/releases)
[![Platform: ESP32-C3](https://img.shields.io/badge/Platform-ESP32--C3-blue.svg)](https://github.com/SensorsIot/Modbus_Proxy/tree/main)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

> **Intelligent power monitoring and correction for solar installations with EV charging**

---

## 🎯 The Problem

A solar inverter decides how much power to send to your house, the battery, or the grid — based on what the energy meter tells it. But the meter cannot tell the difference between your dishwasher and your car charging at 4 kW. So when the car starts charging, the inverter sees a sudden huge load, and reacts in ways you don't want: dumping battery reserve into the car, or throttling export.

## 💡 The Solution

This proxy sits on the wire between the meter and the inverter. It reads every meter response, adds the wallbox charging power it learns over MQTT, and passes the corrected value on. The inverter sees the house load *without* the car, and behaves the way it would if the car weren't there.

## ✨ What It Does

- Relays MODBUS RTU between a SUN2000 inverter and a DTSU-666 meter, transparently
- Adds wallbox charging power to the meter reading before the inverter sees it
- Publishes live power figures and system health over MQTT
- Serves a web dashboard showing power flow, WiFi and MQTT state
- Accepts firmware updates over HTTP, no USB cable needed
- Recovers on its own: retries WiFi, reboots if the link stays down, backs off failed MQTT connects
- Keeps relaying the meter at full rate even with no network at all

---

## 🏗️ How It Works

```
   Grid ──▶ Wallbox ──▶ DTSU-666 Meter ──▶ SUN2000 Inverter
                             │  ▲
                       RS485 │  │ RS485 (corrected)
                             ▼  │
                        ┌───────────┐
                        │  ESP32-C3 │
                        │   Proxy   │
                        └─────┬─────┘
                              │ WiFi
                              ▼
                        MQTT Broker ◀── EVCC (wallbox power)
```

1. The inverter polls the meter; the proxy forwards the request
2. The meter answers; the proxy reads the power value out of the frame
3. Wallbox power, received over MQTT, is added to it
4. The proxy rewrites the frame (fixing the CRC) and passes it to the inverter

Correction only applies above `CORRECTION_THRESHOLD` (1000 W); below that the frame passes through untouched.

---

## 🚀 Quick Start

### Prerequisites

- ESP32-C3 board (ESP32-C3-DevKitM-1)
- Two RS485 transceivers
- An MQTT broker publishing wallbox power
- [PlatformIO](https://platformio.org/) (CLI or VSCode extension)

### Flash from source

```bash
git clone https://github.com/SensorsIot/Modbus_Proxy.git
cd Modbus_Proxy/Modbus_Proxy/src
cp credentials.h.example credentials.h    # add your WiFi SSID and password
cd ..
pio run -e esp32-c3-production --target upload
```

Three build environments are available:

| Environment | Serial output | Use |
|-------------|---------------|-----|
| `esp32-c3-debug` | everything | development, bring-up |
| `esp32-c3-release` | INFO/WARN/ERROR events | field deployment you may need to diagnose |
| `esp32-c3-production` | silent | final deployment |

### Pre-built binaries

Every `v*` tag publishes signed-off binaries to [Releases](https://github.com/SensorsIot/Modbus_Proxy/releases), verifiable against the attached `SHA256SUMS`.

> ⚠️ **Released binaries contain placeholder WiFi credentials.** `credentials.h` is gitignored, so CI builds against `credentials.h.example`. A released binary only joins WiFi if the device has valid credentials stored in NVS (set through the captive portal). On a device without them, flash a locally built image over USB instead — see [Troubleshooting](#-troubleshooting).

### First boot

The device connects with NVS credentials if present, otherwise the ones compiled into `credentials.h`. Once connected it announces itself as `modbus-proxy.local` and serves a dashboard on port 80.

To change WiFi, hold the **GPIO 2** button during boot to start the captive portal (`MODBUS-Proxy-Setup`), then connect and enter the new network.

---

## ⚙️ Configuration

**`credentials.h`** — WiFi only:

```cpp
static const char* ssid = "YOUR_WIFI_SSID";
static const char* password = "YOUR_WIFI_PASSWORD";
```

**MQTT settings live in NVS**, not in the firmware, and can be changed at runtime by publishing to `MBUS-PROXY/cmd/config` (`set_mqtt`, `set_wallbox_topic`, `set_log_level`, `get_config`, `factory_reset`) or through the web UI at `/setup`.

Defaults: broker `192.168.0.203:1883`, user `admin`, wallbox topic `wallbox`.

**Key parameters** in `config.h`:

| Constant | Default | Meaning |
|----------|---------|---------|
| `CORRECTION_THRESHOLD` | 1000 W | Minimum wallbox power before correction applies |
| `WATCHDOG_TIMEOUT_MS` | 60 s | Task heartbeat timeout |
| `WIFI_RETRY_INTERVAL_MS` | 15 s | WiFi reconnect cadence |
| `WIFI_MQTT_RECOVERY_TIMEOUT_MS` | 60 s | WiFi down this long → reboot |
| `MQTT_DOWN_REBOOT_MS` | 10 min | MQTT down this long, WiFi up → reboot |

---

## 📡 MQTT Topics

**`MBUS-PROXY/power`** — every MODBUS transaction (~1/s):

```json
{"timestamp": 123456, "dtsu_power": 94.1, "correction": 1840.0, "sun2000_power": 1934.1}
```

**`MBUS-PROXY/health`** — every 60 seconds:

```json
{"timestamp": 123456, "uptime": 123456, "free_heap": 165368, "min_free_heap": 160968,
 "mqtt_reconnects": 0, "dtsu_updates": 1234, "wallbox_updates": 123,
 "wallbox_errors": 0, "proxy_errors": 0, "power_correction": 1840.0,
 "correction_active": true}
```

Subscribes to the wallbox topic (default `wallbox`) and `MBUS-PROXY/cmd/config`.

---

## 🌐 Web & OTA

| Endpoint | Purpose |
|----------|---------|
| `GET /` | Dashboard |
| `GET /api/status` | Live power, WiFi, MQTT and health values |
| `GET /api/config` | MQTT and logging configuration |
| `POST /api/restart` | Reboot |
| `POST /ota` | Firmware upload |
| `GET /ota/health` | OTA availability check |

Update over the air:

```bash
pio run -e esp32-c3-production
curl -f -H "Authorization: Bearer modbus_ota_2023" \
     -F "firmware=@.pio/build/esp32-c3-production/firmware.bin" \
     http://<device-ip>/ota
```

---

## 🔧 Troubleshooting

**Device never joins WiFi after flashing a release binary.** Released binaries carry placeholder credentials. Either provision the real network through the captive portal (hold GPIO 2 at boot), or flash a locally built image over USB. Since v1.3.0 the device reboots every 60 s while WiFi is down, so this shows as a reboot loop rather than an idle device.

**Boot takes 30 seconds longer than expected.** A stale SSID stored in NVS is tried for the full 30 s connect timeout before the compiled-in fallback. Clear it through the captive portal or `factory_reset`.

**`No MODBUS traffic` in the serial log.** Nothing is arriving on SUN2000 RX (GPIO 7) — check RS485 A/B orientation, common ground, and that the inverter is actually polling. MQTT working while this persists points at wiring, not firmware.

**Nothing on serial at all.** `esp32-c3-production` prints nothing by design, and `esp32-c3-release` prints only event messages. Use `esp32-c3-debug` for continuous output.

**Wrong-chip firmware.** These binaries are ESP32-C3 images and will not boot on an ESP32-S3. Over USB, esptool refuses them; over OTA they are accepted and leave the device boot-looping until reflashed over USB.

---

## 🛠️ Development

```bash
pio run -e esp32-c3-debug --project-dir Modbus_Proxy    # build
pio test -e unit-test --project-dir Modbus_Proxy        # 77 host-side unit tests
pio device monitor -b 115200                            # serial
```

CI builds all three environments and runs the unit tests on every push and pull request. Tagging `vX.Y.Z` publishes a release — the tag must match `FW_VERSION` in `config.h` or the release fails.

An ESP32-S3 variant lives on the [`S3` branch](https://github.com/SensorsIot/Modbus_Proxy/tree/S3); it is not covered by the release pipeline.

---

## 🔬 Technical Details

**MODBUS**: 9600 8N1, slave ID 11, function codes 0x03/0x04, registers 2102–2181 (IEEE 754 floats).

**Pins** (ESP32-C3):

```
SUN2000:  RX=GPIO7,  TX=GPIO10
DTSU-666: RX=GPIO1,  TX=GPIO0
LED:      GPIO8 (LOW=ON)     Portal button: GPIO2 (active LOW)
```

**Tasks**:

| Task | Priority | Stack |
|------|----------|-------|
| Watchdog | 3 (highest) | 2 KB |
| Proxy | 2 | 4 KB |
| MQTT | 1 (lowest) | 8 KB |

**Footprint**: ~981 KB flash, ~165 KB free heap at runtime.

---

## 📚 Documentation

- **[Functional Specification](docs/Modbus-Proxy-FSD.md)** — full system specification
- **[Test Specification](docs/modbus-proxy-test-spec.md)** — automated and manual test coverage
- **[Serial Portal](https://github.com/SensorsIot/Serial-via-Ethernet)** — RFC2217 remote flashing

---

## 📝 License

MIT — see [LICENSE](LICENSE).

## 👨‍💻 Author

**Andreas Spiess** / Claude Code
[YouTube](https://www.youtube.com/AndreasSpiess) · [GitHub](https://github.com/SensorsIot)
