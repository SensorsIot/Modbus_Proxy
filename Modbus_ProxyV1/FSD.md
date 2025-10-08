# ESP32 MODBUS RTU Intelligent Proxy - Functional Specification Document

**Version:** 3.0
**Date:** January 2025
**Platform:** ESP32-S3 (dual-core) / ESP32-C3 (single-core)
**Author:** Andreas Spiess / Claude Code

---

## 1. System Overview

### 1.1 Purpose
The ESP32 MODBUS RTU Intelligent Proxy is a sophisticated power monitoring system that sits between a SUN2000 solar inverter and a DTSU-666 energy meter, providing real-time power correction by integrating wallbox charging data from an EVCC system.

### 1.2 Primary Goals
- **Intelligent Power Correction**: Integrate wallbox power consumption into solar inverter energy management
- **Transparent MODBUS Proxy**: Seamless bidirectional communication between SUN2000 and DTSU-666
- **System Reliability**: Comprehensive auto-restart and health monitoring capabilities
- **Real-time Monitoring**: MQTT-based power data publishing and system health status

---

## 2. Hardware Variants

### 2.1 ESP32-S3 Configuration (Branch: S3)

**Hardware:**
- **MCU**: ESP32-S3 (dual-core Xtensa, 240MHz)
- **Board**: Lolin S3 Mini
- **UARTs**: 3 available (UART0 for USB CDC, UART1/UART2 for RS485)
- **Status LED**: GPIO 48 (normal logic)

**Pin Configuration:**
- **SUN2000 Interface**: UART1 (RX=GPIO18, TX=GPIO17)
- **DTSU-666 Interface**: UART2 (RX=GPIO16, TX=GPIO15)

**Task Distribution:**
- **Core 0**: MQTT task (Priority 1), Watchdog task (Priority 3)
- **Core 1**: Proxy task (Priority 2, dedicated MODBUS processing)

**Debug Options:**
- USB Serial debugging (115200 baud)
- Telnet wireless debugging (port 23)

### 2.2 ESP32-C3 Configuration (Branch: main)

**Hardware:**
- **MCU**: ESP32-C3 (single-core RISC-V, 160MHz)
- **Board**: ESP32-C3-DevKitM-1
- **UARTs**: 2 available (UART0/UART1 for RS485, no USB CDC)
- **Status LED**: GPIO 8 (inverted logic)

**Pin Configuration:**
- **SUN2000 Interface**: UART0 (RX=GPIO7, TX=GPIO10)
- **DTSU-666 Interface**: UART1 (RX=GPIO1, TX=GPIO0)

**Task Distribution:**
- **Single Core**: All tasks (MQTT Priority 1, Proxy Priority 2, Watchdog Priority 3)

**Debug Options:**
- Telnet wireless debugging only (USB CDC disabled to free UART0)

---

## 3. System Architecture

### 3.1 Hardware Flow

```
Grid ←→ L&G Meter ←→ Wallbox ←→ DTSU-666 ←→ SUN2000 Inverter
        (Reference)   (4.2kW)    (Proxy)      (Solar)
                         ↑
                    ESP32 Proxy
                  (WiFi/HTTP/MQTT)
                         ↓
                   EVCC System
```

### 3.2 Software Architecture (ESP32-S3 Dual-Core)

```
Core 0:                          Core 1:
├── MQTT Task (Priority 1)       ├── Proxy Task (Priority 2)
│   │   Stack: 16KB              │   │   Stack: 4KB
│   ├── EVCC API polling (10s)   │   ├── MODBUS proxy
│   ├── JSON parsing (8KB buf)   │   ├── Power correction
│   └── MQTT publishing          │   ├── LED activity indication
├── Watchdog Task (Priority 3)   │   └── Heartbeat updates
│   ├── Health monitoring (5s)
│   ├── Failure detection
│   └── Auto-restart triggers
```

### 3.3 Software Architecture (ESP32-C3 Single-Core)

```
Single Core:
├── Proxy Task (Priority 2)      ← Highest priority for MODBUS timing
│   ├── MODBUS proxy
│   ├── Power correction
│   └── LED activity indication
├── MQTT Task (Priority 1)
│   ├── EVCC API polling (10s)
│   ├── JSON parsing (8KB buf)
│   └── MQTT publishing
└── Watchdog Task (Priority 3)
    ├── Health monitoring (5s)
    └── Auto-restart triggers
```

---

## 4. Communication Protocols

### 4.1 MODBUS RTU Specification
- **Slave ID**: 11 (DTSU-666 meter)
- **Baud Rate**: 9600, 8N1
- **Function Codes**: 0x03 (Read Holding), 0x04 (Read Input)
- **Register Range**: 2102-2181 (80 registers, 160 bytes IEEE 754)
- **Data Format**: Big-endian IEEE 754 32-bit floats
- **CRC Validation**: Automatic validation and recalculation

### 4.2 EVCC HTTP API
- **URL**: `http://192.168.0.202:7070/api/state`
- **Method**: GET (JSON response)
- **Data Path**: `loadpoints[0].chargePower` (watts)
- **Polling Interval**: 10 seconds
- **Buffer Size**: StaticJsonDocument<8192>
- **Hostname**: "MODBUS-Proxy"

### 4.3 MQTT Communication

**Broker Configuration:**
- **Server**: 192.168.0.203:1883
- **Client ID**: MBUS_PROXY_{MAC_ADDRESS}
- **Buffer Size**: 1024 bytes
- **Keep Alive**: 60 seconds

**Published Topics:**

1. **MBUS-PROXY/power** (every MODBUS transaction)
```json
{
  "dtsu": -18.5,
  "wallbox": 0.0,
  "sun2000": -18.5,
  "active": false
}
```

2. **MBUS-PROXY/health** (every 60 seconds)
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
  "power_correction": 0.0,
  "correction_active": false
}
```

---

## 5. Power Correction Algorithm

### 5.1 Correction Logic

**Threshold**: 1000W minimum wallbox power for correction activation

**Calculation**:
```
corrected_power = dtsu_power + wallbox_power
```

**Distribution** (when correction applied):
- Total power corrected by full wallbox power
- Phase powers distributed evenly (wallbox_power / 3 per phase)
- Demand values adjusted proportionally

### 5.2 MODBUS Frame Modification

**Process**:
1. Read MODBUS response from DTSU-666
2. Parse IEEE 754 float values
3. Apply correction if wallbox power > 1000W
4. Write modified values back to frame
5. Recalculate and update CRC16
6. Forward to SUN2000

**Modified Registers**:
- Total power (register 2126)
- Phase L1 power (register 2128)
- Phase L2 power (register 2130)
- Phase L3 power (register 2132)
- Total demand (register 2158)
- Phase demands (registers 2160, 2162, 2164)

---

## 6. Debug Output Format

### 6.1 Serial/Telnet Output

**Single-line format per MODBUS transaction**:
```
DTSU: 94.1W | Wallbox: 0.0W | SUN2000: 94.1W (94.1W + 0.0W)
```

**Success-only logging**:
- No verbose packet dumps
- MQTT publish failures logged only
- Internal SUN2000 messages (ID=5) hidden

---

## 7. Build Configuration

### 7.1 PlatformIO Environments

**ESP32-S3**:
- `esp32-s3-serial`: Serial upload (COM port)
- `esp32-s3-ota`: OTA wireless upload

**ESP32-C3**:
- `esp32-c3-serial`: Serial upload (COM port)
- `esp32-c3-ota`: OTA wireless upload

### 7.2 Build Flags

**ESP32-S3**:
```ini
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -std=gnu++17
board_build.usb_cdc = true
```

**ESP32-C3**:
```ini
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=0
    -std=gnu++17
board_build.usb_cdc = false
board_build.arduino.memory_type = qio_qspi
```

### 7.3 Dependencies
- ArduinoJson @ ^6.19.4
- PubSubClient @ ^2.8
- ArduinoOTA

---

## 8. Configuration Files

### 8.1 credentials.h (User-Specific)

**Note**: This file is gitignored. Copy from `credentials.h.example`

```cpp
static const char* ssid = "YOUR_WIFI_SSID";
static const char* password = "YOUR_WIFI_PASSWORD";
static const char* mqttServer = "192.168.0.203";
static const char* evccApiUrl = "http://192.168.0.202:7070/api/state";
```

### 8.2 config.h (Platform-Specific)

**Debug Settings**:
- `ENABLE_SERIAL_DEBUG`: USB serial debugging (ESP32-S3 only)
- `ENABLE_TELNET_DEBUG`: Wireless telnet debugging

**Timing Constants**:
- `CORRECTION_THRESHOLD`: 1000.0f (watts)
- `HTTP_POLL_INTERVAL`: 10000 (ms)
- `WATCHDOG_TIMEOUT_MS`: 60000 (ms)
- `HEALTH_CHECK_INTERVAL`: 5000 (ms)

---

## 9. Memory Management

### 9.1 Task Stack Sizes

| Task | ESP32-S3 | ESP32-C3 |
|------|----------|----------|
| Proxy | 4KB | 4KB |
| MQTT | 16KB | 16KB |
| Watchdog | 2KB | 2KB |

### 9.2 Heap Requirements

- **Minimum Free Heap**: 20KB threshold
- **MQTT Buffer**: 1024 bytes
- **JSON Buffer**: 8192 bytes (StaticJsonDocument)
- **Typical Free Heap**: ~250KB (ESP32-S3), ~200KB (ESP32-C3)

---

## 10. Error Handling & Recovery

### 10.1 Watchdog Monitoring

**Timeouts**:
- Task heartbeat timeout: 60 seconds
- MQTT reconnection: Automatic with exponential backoff
- EVCC API failure: Continue with last valid data

### 10.2 Auto-Restart Triggers

- Initialization failures (MODBUS, MQTT, EVCC)
- Watchdog task timeout
- Critical memory shortage (< 20KB free heap)

### 10.3 Error Reporting

**MQTT Error Messages**:
- Subsystem identifier (MODBUS, MEMORY, WATCHDOG)
- Error description
- Numeric error code

---

## 11. OTA Updates

### 11.1 Configuration

- **Port**: 3232
- **Password**: modbus_ota_2023
- **Hostname**: MODBUS-Proxy

### 11.2 Update Process

1. Device advertises on network
2. PlatformIO connects via espota
3. Firmware uploaded with progress reporting
4. Automatic restart after successful upload

---

## 12. Network Configuration

### 12.1 WiFi Settings

- **Mode**: Station (STA) mode
- **Hostname**: "MODBUS-Proxy"
- **Power Save**: Disabled for stability
- **Connection Timeout**: 30 seconds

### 12.2 Services

- **mDNS**: Enabled (MODBUS-Proxy.local)
- **Telnet**: Port 23 (if enabled)
- **OTA**: Port 3232
- **MQTT**: Port 1883

---

## 13. Performance Characteristics

### 13.1 Timing

- **MODBUS Response Time**: < 100ms typical
- **Power Correction Latency**: < 1ms (in-line processing)
- **MQTT Publish Rate**: Per MODBUS transaction (~1/second)
- **API Polling**: 10 second interval

### 13.2 Resource Usage

**ESP32-S3**:
- **RAM**: 16.5% (54KB / 320KB)
- **Flash**: 72.4% (949KB / 1.3MB)
- **CPU**: Dual-core utilization

**ESP32-C3**:
- **RAM**: 14.8% (48KB / 320KB)
- **Flash**: 74.5% (977KB / 1.3MB)
- **CPU**: Single-core time-sliced

---

## 14. Testing & Validation

### 14.1 Functional Tests

- ✅ MODBUS proxy passthrough
- ✅ Power correction calculation
- ✅ EVCC API polling
- ✅ MQTT publishing
- ✅ Watchdog monitoring
- ✅ Auto-restart recovery

### 14.2 Stress Tests

- ✅ Continuous operation (24+ hours)
- ✅ Network disconnection recovery
- ✅ MQTT broker reconnection
- ✅ EVCC API timeout handling

---

## 15. Future Enhancements

### 15.1 Potential Features

- Historical data logging to SD card
- Web interface for configuration
- Support for multiple wallboxes
- Advanced power flow visualization
- Integration with Home Assistant

### 15.2 Known Limitations

- Single DTSU-666 meter support only
- Fixed EVCC API endpoint
- No authentication on telnet debug port
- Limited to 9600 baud MODBUS communication

---

## Appendix A: Register Map

See DTSU-666 datasheet for complete register definitions (2102-2181)

## Appendix B: Troubleshooting

**Common Issues**:
1. **No MODBUS traffic**: Check GPIO pin assignments, RS485 wiring
2. **MQTT disconnects**: Verify broker address, check network stability
3. **Wrong power values**: Verify EVCC API URL and data structure
4. **OTA fails**: Check WiFi signal strength, verify password

---

**Document Version History**:
- v3.0 (January 2025): Dual-platform support (ESP32-S3/C3), telnet debugging
- v2.0 (September 2024): MQTT implementation, power correction
- v1.0 (Initial): Basic MODBUS proxy functionality
