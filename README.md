# ESP32-S3 MODBUS RTU Intelligent Proxy

[![PlatformIO CI](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![ESP32-S3](https://img.shields.io/badge/ESP32--S3-Supported-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A sophisticated power monitoring system that sits between a SUN2000 solar inverter and a DTSU-666 energy meter, providing real-time power correction by integrating wallbox charging data from an EVCC system. This was needed because the Wallbox is outside the Solar loop, but connected to our home. With this method, the Solar loop contains also the wallbox. So the inverter can optimize the entire consumption to 0 for the entire home.

## 🎯 Overview

The ESP32-S3 MODBUS RTU Intelligent Proxy ensures accurate power flow measurements by compensating for wallbox consumption between metering points. This allows the solar inverter to see household consumption **excluding** wallbox charging, enabling separate control of solar generation and EV charging.

### Key Features

- 🔄 **Transparent MODBUS Proxy**: Seamless bidirectional communication between SUN2000 and DTSU-666
- ⚡ **Intelligent Power Correction**: Real-time wallbox power integration via EVCC API
- 🌐 **Remote Management**: Arduino OTA updates via `Modbus-Proxy.local` hostname
- 🏥 **Health Monitoring**: Comprehensive auto-restart and MQTT error reporting
- 🧵 **Thread-Safe Architecture**: Dual-core FreeRTOS with mutex-protected data structures
- 📊 **Clean Output**: Real-time 3-line status display with timing information

## 🏗️ System Architecture

```
Grid ←→ L&G Meter ←→ Wallbox ←→ DTSU-666 ←→ SUN2000 Inverter
        (Reference)   (4.2kW)    (Proxy)      (Solar)
                         ↑
                      ESP32-S3
                    (WiFi/HTTP/MQTT)
                         ↓
                     EVCC System
```

### Hardware Requirements

- **ESP32-S3** (I   use a Supermini board)
- **Dual RS-485 Interfaces**:
  - UART2 (RS485_SUN2000_TX_PIN, RS485_SUN2000_RX_PIN): SUN2000 inverter
  - UART1 (RS485_DTU_TX_PIN, RS485_DTU_RX_PIN): DTSU-666 energy meter
- **Status LED**: GPIO 48 (onboard LED)
- **Power Supply**: 5V via USB-C or external supply

## 🚀 Quick Start

### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- ESP32-S3 development board
- RS-485 transceivers (e.g., MAX485)

### Installation

1. **Clone the repository**
   ```bash
   git clone https://github.com/SensorsIot/Modbus_Proxy.git
   cd Modbus_Proxy
   git checkout S3
   ```

2. **Configure credentials**
   ```cpp
   // Edit Modbus_ProxyV1/src/credentials.h
   inline const char* ssid = "your_wifi_network";
   inline const char* password = "your_wifi_password";
   inline const char* mqttServer = "192.168.0.203";
   inline const char* evccApiUrl = "http://192.168.0.202:7070/api/state";
   ```

3. **Build and upload (first time)**
   ```bash
   cd Modbus_ProxyV1
   # For serial upload (first time only)
   pio run -e esp32-s3-serial -t upload
   ```

4. **Monitor output**
   ```bash
   pio device monitor
   ```

### OTA Updates (After Initial Flash)

Once running, you can update remotely:

```bash
# For OTA updates (after initial flash)
pio run -e esp32-s3-ota -t upload
```

## 📊 Real-Time Output

The system provides clean, immediate status output:

```
DTSU: -1250.3W
API:  4200W (valid)
SUN2000: 2949.7W (DTSU -1250.3W + correction 4200W)
```

This shows:
- **DTSU**: Raw meter reading (negative = importing power)
- **API**: Wallbox charging power from EVCC API
- **SUN2000**: Corrected value sent to inverter

## ⚙️ Configuration

### Power Correction Settings

```cpp
// Power correction only applies if |wallboxPower| > 1000W
const float CORRECTION_THRESHOLD = 1000.0f;

// EVCC API polling interval (10 seconds)
const uint32_t HTTP_POLL_INTERVAL = 10000;
```

### Network Configuration

The device advertises as `Modbus-Proxy` on your network:
- **Hostname**: `Modbus-Proxy.local`
- **OTA Password**: `modbus_ota_2023`
- **MQTT Topics**: `MBUS-PROXY/power`, `MBUS-PROXY/health`

### Hardware Pin Assignments

```cpp
// ESP32-S3 GPIO Configuration
#define RS485_SUN2000_RX_PIN 4     // SUN2000 inverter (UART2)
#define RS485_SUN2000_TX_PIN 3
#define RS485_DTU_RX_PIN 13        // DTSU-666 meter (UART1)
#define RS485_DTU_TX_PIN 12
#define STATUS_LED_PIN 48          // Onboard LED activity indicator
```

## 🔧 Development

### Project Structure

```
Modbus_ProxyV1/
├── src/
│   ├── main.cpp              # Main program entry point
│   ├── modbus_proxy.cpp      # Core MODBUS proxy logic
│   ├── evcc_api.cpp          # EVCC HTTP API integration
│   ├── mqtt_handler.cpp      # MQTT communication
│   ├── dtsu666.cpp           # DTSU-666 data parsing
│   ├── config.h              # Hardware configuration
│   └── credentials.h         # Network credentials
├── platformio.ini            # PlatformIO configuration
└── Modbus-Proxy-FSD.md       # Functional specification
```

### Task Architecture

- **Core 0**: MQTT Task (Priority 1) + Watchdog Task (Priority 3)
- **Core 1**: Proxy Task (Priority 2)
- **Memory**: 16KB MQTT stack, 8KB JSON buffer, mutex-protected shared data

### Building from Source

```bash
# Build only (both environments)
pio run

# Build specific environment
pio run -e esp32-s3-serial
pio run -e esp32-s3-ota

# Clean build
pio run -t clean

# Upload via serial (first time)
pio run -e esp32-s3-serial -t upload

# Upload via OTA (after initial flash)
pio run -e esp32-s3-ota -t upload
```

## 📈 Monitoring

### MQTT Topics

- **`MBUS-PROXY/power`**: Essential power data (DTSU, wallbox, SUN2000, active status)
  ```json
  {"dtsu":-18.5,"wallbox":4140.0,"sun2000":4121.5,"active":true}
  ```
- **`MBUS-PROXY/health`**: System health status (every 60 seconds)
  ```json
  {"uptime":123456,"heap":54000,"mqtt_reconnects":2,"errors":0}
  ```

### Health Monitoring

The system includes comprehensive health monitoring:
- **Task Watchdog**: 60s timeout → auto-restart
- **API Failures**: 20 consecutive failures → auto-restart
- **Memory Protection**: <20KB free heap → auto-restart
- **MQTT Publishing**: Real-time power data (~1-2 seconds), health data (60 seconds)
- **Optimized Payloads**: ~60-70 bytes for broker compatibility

### Serial Console Output

Connect to see real-time system status:
```bash
pio device monitor -b 115200
```

## 🛠️ Troubleshooting

### Common Issues

1. **Compilation Errors**
   - Ensure PlatformIO is updated: `pio upgrade`
   - Check ESP32-S3 platform: `pio platform install espressif32`

2. **OTA Upload Fails**
   - Device must be on same network
   - Check hostname resolution: `ping Modbus-Proxy.local`
   - Use IP address directly in `platformio.ini`: change `upload_port = Modbus-Proxy.local` to `upload_port = 192.168.0.XXX`

3. **MODBUS Communication Issues**
   - Verify RS-485 wiring and termination
   - Check baud rate (9600 8N1)
   - Ensure proper ground connections

4. **Stack Overflow Crashes**
   - Already resolved with 16KB MQTT task stack
   - Monitor heap usage via serial output

### Debug Mode

Enable detailed logging by modifying debug flags in `platformio.ini`:
```ini
build_flags =
    -DDEBUG_MODBUS=1
    -DDEBUG_HTTP=1
```

## 📚 Documentation

- **[Functional Specification](Modbus_ProxyV1/Modbus-Proxy-FSD.md)**: Complete technical specification
- **[PlatformIO Docs](https://docs.platformio.org/)**: Build system documentation
- **[ESP32-S3 Reference](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)**: Hardware documentation

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/amazing-feature`
3. Commit changes: `git commit -m 'Add amazing feature'`
4. Push to branch: `git push origin feature/amazing-feature`
5. Open a Pull Request

### Development Guidelines

- Follow existing code style and conventions
- Add comprehensive error handling
- Include mutex protection for shared data
- Test thoroughly on actual hardware
- Update documentation as needed

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- **Andreas Spiess** - Original concept and hardware integration
- **Claude Code** - Code architecture and ESP32-S3 migration
- **PlatformIO** - Excellent build system for embedded development
- **Espressif** - ESP32-S3 microcontroller platform

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/SensorsIot/Modbus_Proxy/issues)
- **Discussions**: [GitHub Discussions](https://github.com/SensorsIot/Modbus_Proxy/discussions)
- **Documentation**: [Functional Specification](Modbus_ProxyV1/Modbus-Proxy-FSD.md)

---

**Made with ❤️ for the solar energy community**

*This project enables smarter solar energy management by providing accurate power measurements that exclude EV charging, allowing for optimal solar generation control and independent wallbox charging management.*
