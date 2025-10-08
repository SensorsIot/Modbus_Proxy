# 🔌 ESP32 MODBUS RTU Intelligent Proxy

[![Platform: ESP32-C3](https://img.shields.io/badge/Platform-ESP32--C3-blue.svg)](https://github.com/SensorsIot/Modbus_Proxy/tree/main)
[![Platform: ESP32-S3](https://img.shields.io/badge/Platform-ESP32--S3-green.svg)](https://github.com/SensorsIot/Modbus_Proxy/tree/S3)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Ready-orange.svg)](https://platformio.org)

> **Intelligent power monitoring and correction system for solar installations with EV charging**

A sophisticated MODBUS RTU proxy that sits between a SUN2000 solar inverter and DTSU-666 energy meter, providing real-time power correction by integrating wallbox charging data from an EVCC system.

---

## 📋 Table of Contents

- [Features](#-features)
- [Hardware Platforms](#-hardware-platforms)
- [Quick Start](#-quick-start)
- [System Architecture](#-system-architecture)
- [Configuration](#-configuration)
- [MQTT Topics](#-mqtt-topics)
- [Development](#-development)
- [Documentation](#-documentation)
- [License](#-license)

---

## ✨ Features

- 🔄 **Transparent MODBUS Proxy**: Seamless communication between SUN2000 inverter and DTSU-666 meter
- ⚡ **Intelligent Power Correction**: Real-time correction for wallbox charging (EVCC integration)
- 📊 **MQTT Publishing**: Live power data and system health monitoring
- 🛡️ **Watchdog Monitoring**: Automatic fault detection and recovery
- 🔧 **Dual Platform Support**: ESP32-C3 (single-core) and ESP32-S3 (dual-core)
- 📡 **OTA Updates**: Wireless firmware updates
- 🐛 **Debug Options**: USB Serial and Telnet wireless debugging

---

## 🔧 Hardware Platforms

### 📱 ESP32-C3 (Branch: `main`)
![ESP32-C3](https://img.shields.io/badge/Core-Single--Core%20RISC--V-blue)
![Speed](https://img.shields.io/badge/Speed-160MHz-blue)
![RAM](https://img.shields.io/badge/RAM-320KB-blue)

- **Board**: ESP32-C3-DevKitM-1
- **Architecture**: Single-core RISC-V @ 160MHz
- **UARTs**: 2 available (UART0/UART1 for RS485)
- **LED**: GPIO 8 (inverted logic)
- **Debug**: Telnet only (USB CDC disabled)

**Pin Configuration**:
```
SUN2000:  UART0 (RX=GPIO7,  TX=GPIO10)
DTSU-666: UART1 (RX=GPIO1,  TX=GPIO0)
LED:      GPIO8 (LOW=ON, HIGH=OFF)
```

### 🚀 ESP32-S3 (Branch: `S3`)
![ESP32-S3](https://img.shields.io/badge/Core-Dual--Core%20Xtensa-green)
![Speed](https://img.shields.io/badge/Speed-240MHz-green)
![RAM](https://img.shields.io/badge/RAM-320KB-green)

- **Board**: Lolin S3 Mini
- **Architecture**: Dual-core Xtensa @ 240MHz
- **UARTs**: 3 available (UART0 for USB, UART1/UART2 for RS485)
- **LED**: GPIO 48 (normal logic)
- **Debug**: USB Serial or Telnet

**Pin Configuration**:
```
SUN2000:  UART1 (RX=GPIO18, TX=GPIO17)
DTSU-666: UART2 (RX=GPIO16, TX=GPIO15)
LED:      GPIO48 (HIGH=ON, LOW=OFF)
```

**Core Distribution**:
- **Core 0**: MQTT publishing, EVCC API polling, Watchdog
- **Core 1**: MODBUS proxy with power correction (dedicated)

---

## 🚀 Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/) installed (VSCode extension or CLI)
- ESP32-C3 or ESP32-S3 development board
- Two RS485 adapters
- WiFi network access

### Installation

1. **Clone the repository**:
```bash
git clone https://github.com/SensorsIot/Modbus_Proxy.git
cd Modbus_Proxy
```

2. **Choose your platform**:
```bash
# For ESP32-C3 (main branch)
git checkout main

# For ESP32-S3 (S3 branch)
git checkout S3
```

3. **Configure credentials**:
```bash
cd Modbus_ProxyV1/src
cp credentials.h.example credentials.h
# Edit credentials.h with your WiFi and MQTT settings
```

4. **Build and upload**:
```bash
cd Modbus_ProxyV1

# Serial upload (first time)
pio run -e esp32-c3-serial --target upload    # For ESP32-C3
pio run -e esp32-s3-serial --target upload    # For ESP32-S3

# OTA upload (after initial flash)
pio run -e esp32-c3-ota --target upload       # For ESP32-C3
pio run -e esp32-s3-ota --target upload       # For ESP32-S3
```

5. **Monitor output**:
```bash
pio device monitor    # USB Serial
telnet <IP_ADDRESS> 23  # Telnet (if enabled)
```

---

## 🏗️ System Architecture

```
┌─────────────┐     ┌──────────┐     ┌──────────┐     ┌─────────────┐
│   Grid      │────▶│ Wallbox  │────▶│ DTSU-666 │────▶│  SUN2000    │
│  (L&G)      │     │  (4.2kW) │     │  (Meter) │     │  Inverter   │
└─────────────┘     └─────┬────┘     └────┬─────┘     └─────────────┘
                          │                │
                          │            ┌───▼────┐
                          │            │ ESP32  │
                          │            │ Proxy  │
                          │            └───┬────┘
                          │                │
                          │          WiFi/MQTT
                          │                │
                    ┌─────▼────────────────▼─────┐
                    │     EVCC System            │
                    │  (EV Charging Controller)  │
                    └────────────────────────────┘
```

### Power Flow

1. **DTSU-666** measures grid connection power
2. **ESP32 Proxy** reads MODBUS data from DTSU-666
3. **EVCC API** provides wallbox charging power
4. **Power Correction** adds wallbox power to DTSU reading
5. **SUN2000** receives corrected power values

---

## ⚙️ Configuration

### credentials.h

```cpp
static const char* ssid = "YOUR_WIFI_SSID";
static const char* password = "YOUR_WIFI_PASSWORD";
static const char* mqttServer = "192.168.0.203";
static const char* evccApiUrl = "http://192.168.0.202:7070/api/state";
```

### config.h (Platform-Specific)

**Debug Settings**:
```cpp
#define ENABLE_SERIAL_DEBUG true   // USB serial (ESP32-S3)
#define ENABLE_TELNET_DEBUG false  // Wireless telnet
```

**Key Parameters**:
- `CORRECTION_THRESHOLD`: 1000W (minimum wallbox power for correction)
- `HTTP_POLL_INTERVAL`: 10000ms (EVCC API polling)
- `WATCHDOG_TIMEOUT_MS`: 60000ms (task heartbeat timeout)

---

## 📡 MQTT Topics

### MBUS-PROXY/power
Published every MODBUS transaction (~1/second)

```json
{
  "dtsu": 94.1,        // DTSU-666 reading (W)
  "wallbox": 1840.0,   // Wallbox power (W)
  "sun2000": 1934.1,   // Corrected value to SUN2000 (W)
  "active": true       // Correction applied
}
```

### MBUS-PROXY/health
Published every 60 seconds

```json
{
  "timestamp": 123456,
  "uptime": 123456,
  "free_heap": 250000,
  "min_free_heap": 200000,
  "mqtt_reconnects": 0,
  "dtsu_updates": 1234,
  "evcc_updates": 123,
  "evcc_errors": 0,
  "proxy_errors": 0,
  "power_correction": 1840.0,
  "correction_active": true
}
```

---

## 🛠️ Development

### Building

```bash
cd Modbus_ProxyV1

# Build only (no upload)
pio run -e esp32-c3-serial   # ESP32-C3
pio run -e esp32-s3-serial   # ESP32-S3

# Clean build
pio run --target clean
```

### Debugging

**USB Serial** (ESP32-S3 only):
```bash
pio device monitor -b 115200
```

**Telnet** (both platforms):
```bash
telnet <DEVICE_IP> 23
```

### OTA Password

Default OTA password: `modbus_ota_2023`

---

## 📚 Documentation

- **[Modbus-Proxy-FSD.md](Modbus_ProxyV1/Modbus-Proxy-FSD.md)**: Complete Functional Specification Document (v3.0)

---

## 🔬 Technical Details

### MODBUS Configuration
- **Baud Rate**: 9600, 8N1
- **Slave ID**: 11 (DTSU-666)
- **Function Codes**: 0x03, 0x04
- **Register Range**: 2102-2181 (80 registers, IEEE 754 floats)

### Memory Usage

| Platform | RAM | Flash |
|----------|-----|-------|
| ESP32-C3 | 14.8% (48KB) | 74.5% (977KB) |
| ESP32-S3 | 16.5% (54KB) | 72.3% (947KB) |

### Task Priorities

| Task | Priority | Stack |
|------|----------|-------|
| Watchdog | 3 (highest) | 2KB |
| Proxy | 2 | 4KB |
| MQTT | 1 (lowest) | 16KB |

---

## 🤝 Contributing

Contributions welcome! Please read the FSD document first to understand the system architecture.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## 📝 License

This project is licensed under the MIT License - see the LICENSE file for details.

---

## 👨‍💻 Author

**Andreas Spiess** / Claude Code

- YouTube: [@AndreasSpiess](https://www.youtube.com/AndreasSpiess)
- GitHub: [@SensorsIot](https://github.com/SensorsIot)

---

## 🙏 Acknowledgments

- ESP32 Arduino Core team
- PlatformIO team
- EVCC project
- MODBUS community

---

## 📞 Support

- 📺 YouTube videos on Andreas Spiess channel
- 🐛 [GitHub Issues](https://github.com/SensorsIot/Modbus_Proxy/issues)
- 💬 Community discussions

---

<div align="center">

**Made with ❤️ for the solar and EV charging community**

[![YouTube](https://img.shields.io/badge/YouTube-Andreas%20Spiess-red?style=for-the-badge&logo=youtube)](https://www.youtube.com/AndreasSpiess)
[![GitHub](https://img.shields.io/badge/GitHub-SensorsIot-black?style=for-the-badge&logo=github)](https://github.com/SensorsIot)

</div>
