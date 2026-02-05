# ESP32 MODBUS RTU Intelligent Proxy - Functional Specification Document

**Version:** 5.0
**Date:** February 2026
**Platform:** ESP32-C3 (single-core RISC-V)
**Author:** Andreas Spiess / Claude Code

---

## 1. System Overview

### 1.1 Purpose
The ESP32 MODBUS RTU Intelligent Proxy is a sophisticated power monitoring system that sits between a SUN2000 solar inverter and a DTSU-666 energy meter, providing real-time power correction by integrating wallbox charging data via MQTT.

### 1.2 Primary Goals
- **Intelligent Power Correction**: Integrate wallbox power consumption into solar inverter energy management
- **Transparent MODBUS Proxy**: Seamless bidirectional communication between SUN2000 and DTSU-666
- **System Reliability**: Comprehensive auto-restart and health monitoring capabilities
- **Real-time Monitoring**: MQTT-based power data publishing and system health status
- **OTA Configuration**: Runtime configuration changes via MQTT commands
- **WiFi Provisioning**: Captive portal for initial WiFi setup (triggered by 3 power cycles)
- **Remote Management**: MQTT broker selection and OTA debugging without physical access

---

## 2. Hardware Configuration

### 2.1 ESP32-C3 Configuration (Branch: main)

**Hardware:**
- **MCU**: ESP32-C3 (single-core RISC-V, 160MHz)
- **Board**: ESP32-C3-DevKitM-1
- **UARTs**: 2 available (UART0/UART1 for RS485)
- **Status LED**: GPIO 8 (inverted logic)

**Pin Configuration:**
- **SUN2000 Interface**: UART0 (RX=GPIO7, TX=GPIO10)
- **DTSU-666 Interface**: UART1 (RX=GPIO1, TX=GPIO0)

**Task Distribution:**
- **Single Core**: All tasks (MQTT Priority 1, Proxy Priority 2, Watchdog Priority 3)

**Debug Options:**
- USB Serial debugging (115200 baud, if enabled)

---

## 3. System Architecture

### 3.1 Hardware Flow

```
Grid ←→ L&G Meter ←→ Wallbox ←→ DTSU-666 ←→ SUN2000 Inverter
        (Reference)   (4.2kW)    (Proxy)      (Solar)
                         ↑
                    ESP32 Proxy
                  (WiFi/MQTT/OTA)
                         ↓
                   MQTT Broker
                         ↓
                   Wallbox/EVCC
```

### 3.2 Software Architecture

```
Single Core:
├── Proxy Task (Priority 2)      ← Highest priority for MODBUS timing
│   ├── MODBUS proxy
│   ├── Power correction
│   └── LED activity indication
├── MQTT Task (Priority 1)
│   ├── Wallbox power subscription
│   ├── Config command handling
│   ├── Power data publishing
│   └── Log queue processing
└── Watchdog Task (Priority 3)
    ├── Health monitoring (5s)
    ├── Hardware WDT feed (90s timeout)
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

### 4.2 MQTT Communication

**Broker Configuration:**
- **Default Server**: 192.168.0.203:1883
- **Client ID**: MBUS_PROXY_{MAC_ADDRESS}
- **Buffer Size**: 1024 bytes
- **Keep Alive**: 60 seconds
- **Authentication**: Optional username/password

**Subscribed Topics:**

1. **Wallbox Power** (configurable, default: `wallbox`)
   - Accepts plain float: `1234.5`
   - Accepts JSON: `{"power": 1234.5}` or `{"chargePower": 1234.5}`

2. **Configuration Commands** (`MBUS-PROXY/cmd/config`)
```json
{"cmd": "set_mqtt", "host": "192.168.0.100", "port": 1883, "user": "admin", "pass": "secret"}
{"cmd": "set_wallbox_topic", "topic": "evcc/loadpoints/0/chargePower"}
{"cmd": "set_log_level", "level": 2}
{"cmd": "get_config"}
{"cmd": "factory_reset"}
```

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
  "wallbox_updates": 123,
  "wallbox_errors": 0,
  "proxy_errors": 0,
  "power_correction": 0.0,
  "correction_active": false
}
```

3. **MBUS-PROXY/log** (based on log level)
```json
{
  "ts": 123456,
  "level": "WARN",
  "subsys": "MQTT",
  "msg": "Connection lost"
}
```

4. **MBUS-PROXY/cmd/config/response** (command responses)
```json
{
  "cmd": "get_config",
  "status": "ok",
  "mqtt_host": "192.168.0.203",
  "mqtt_port": 1883,
  "mqtt_user": "admin",
  "wallbox_topic": "wallbox",
  "log_level": 2
}
```

---

## 5. Power Correction Algorithm

### 5.1 Correction Logic

**Threshold**: 1000W minimum wallbox power for correction activation
**Data Max Age**: 30 seconds (wallbox data considered stale after this)

**Calculation**:
```
corrected_power = dtsu_power + wallbox_power
```

**Distribution** (when correction applied):
- Total power corrected by full wallbox power
- Phase powers distributed evenly (wallbox_power / 3 per phase)

### 5.2 MODBUS Frame Modification

**Process**:
1. Read MODBUS response from DTSU-666
2. Parse IEEE 754 float values
3. Apply correction if wallbox power > 1000W and data is fresh
4. Write modified values back to frame
5. Recalculate and update CRC16
6. Forward to SUN2000

**Modified Registers**:
- Total power (register 2126)
- Phase L1 power (register 2128)
- Phase L2 power (register 2130)
- Phase L3 power (register 2132)

---

## 6. Configuration System

### 6.1 NVS Persistent Storage

Configuration is stored in ESP32 NVS (Non-Volatile Storage) and survives reboots.

**Stored Parameters:**
- MQTT Host (64 chars max)
- MQTT Port (uint16)
- MQTT Username (32 chars max)
- MQTT Password (32 chars max)
- Wallbox Topic (64 chars max)
- Log Level (0-3)

### 6.2 Default Values

| Parameter | Default Value |
|-----------|---------------|
| MQTT Host | 192.168.0.203 |
| MQTT Port | 1883 |
| MQTT User | admin |
| MQTT Pass | admin |
| Wallbox Topic | wallbox |
| Log Level | 2 (WARN) |

### 6.3 Log Levels

| Level | Name | Description |
|-------|------|-------------|
| 0 | DEBUG | Verbose debugging info |
| 1 | INFO | Informational messages |
| 2 | WARN | Warnings (default) |
| 3 | ERROR | Errors only |

---

## 7. Debug Output Format

### 7.1 Serial Output

**Single-line format per MODBUS transaction**:
```
DTSU: 94.1W | Wallbox: 0.0W | SUN2000: 94.1W (94.1W + 0.0W)
```

**Log format**:
```
[timestamp][LEVEL][SUBSYSTEM] message
```

---

## 8. Build Configuration

### 8.1 PlatformIO Environments

- `esp32-c3-serial`: Serial upload (COM port)
- `esp32-c3-ota`: OTA wireless upload

### 8.2 Build Flags

```ini
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -std=gnu++17
board_build.usb_cdc = true
board_build.arduino.memory_type = qio_qspi
```

### 8.3 Dependencies
- ArduinoJson @ ^6.19.4
- PubSubClient @ ^2.8
- ArduinoOTA
- Preferences (NVS)
- ESPAsyncWebServer (for Web UI)
- AsyncTCP (ESP32 async TCP library)

---

## 9. Memory Management

### 9.1 Task Stack Sizes

| Task | Stack Size |
|------|------------|
| Proxy | 4KB |
| MQTT | 8KB |
| Watchdog | 2KB |

### 9.2 Heap Requirements

- **Minimum Free Heap**: 20KB threshold (warning)
- **Critical Heap**: 10KB (triggers reboot)
- **MQTT Buffer**: 1024 bytes
- **Log Buffer**: 16 entries circular buffer
- **Typical Free Heap**: ~200KB

---

## 10. Error Handling & Recovery

### 10.1 Watchdog Monitoring

**Software Watchdog**:
- Task heartbeat timeout: 60 seconds
- Health check interval: 5 seconds

**Hardware Watchdog (ESP32 WDT)**:
- Timeout: 90 seconds
- Panic on timeout: enabled

### 10.2 Auto-Restart Triggers

- Task heartbeat timeout (proxy or MQTT task)
- Critical memory shortage (< 10KB free heap)
- Hardware watchdog timeout

### 10.3 Graceful Degradation

- **MQTT disconnect**: Continues MODBUS proxy, reconnects automatically
- **Wallbox data stale**: Disables power correction, logs warning
- **Low memory warning**: Logs warning at < 20KB free heap

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

- **Mode**: Station (STA) mode, AP mode for provisioning
- **Hostname**: "MODBUS-Proxy"
- **Power Save**: Disabled for stability
- **Connection Timeout**: 30 seconds

### 12.2 Services

- **mDNS**: Enabled (MODBUS-Proxy.local)
- **OTA**: Port 3232
- **MQTT**: Port 1883 (configurable)
- **Captive Portal**: Port 80 (AP mode only)

---

## 13. WiFi Provisioning (Captive Portal)

### 13.1 Purpose

Allows initial WiFi configuration without reflashing firmware. The captive portal is intentionally difficult to trigger to prevent accidental credential loss during network outages.

### 13.2 Activation Mechanism

**3 Power Cycle Trigger**:
1. On each boot, increment a "boot counter" in NVS
2. Attempt WiFi connection with stored credentials
3. If WiFi connects successfully within 30 seconds, reset boot counter to 0
4. If boot counter reaches 3 (three consecutive failed boots), enter AP/captive portal mode

**Why 3 Power Cycles?**
- Prevents accidental portal activation during temporary network outages
- Device will keep retrying WiFi during outages (counter resets on success)
- Only intentional "I need to reconfigure" scenarios trigger portal
- User must deliberately power cycle 3 times to enter config mode

### 13.3 Captive Portal Behavior

**AP Mode Configuration**:
- **SSID**: "MODBUS-Proxy-Setup"
- **Password**: None (open network)
- **IP Address**: 192.168.4.1
- **DNS**: Captive portal redirect

**Web Interface** (http://192.168.4.1):
- WiFi SSID selection (scan available networks)
- WiFi password entry
- Save and restart button

**Portal Timeout**:
- 5 minutes of inactivity triggers restart
- Restart attempts normal WiFi connection

### 13.4 Configuration Scope

The captive portal configures **WiFi credentials only**:
- WiFi SSID
- WiFi Password

**NOT configured via captive portal** (configured via Web UI in STA mode):
- MQTT broker address/port
- MQTT credentials
- Wallbox topic
- Log level

### 13.5 NVS Storage for Provisioning

| Key | Type | Description |
|-----|------|-------------|
| boot_count | uint8 | Failed boot counter (0-3) |
| wifi_ssid | string | WiFi network name (64 chars) |
| wifi_pass | string | WiFi password (64 chars) |
| provisioned | bool | WiFi credentials have been set |

---

## 14. Web UI (Status & Configuration)

### 14.1 Purpose

A lightweight web interface accessible when device is connected to WiFi (STA mode). Provides status monitoring and MQTT broker configuration without requiring MQTT connectivity.

### 14.2 Access

- **URL**: http://MODBUS-Proxy.local or http://{device-ip}
- **Port**: 80
- **Authentication**: None (local network only)

### 14.3 Status Page (/)

**Real-time Status Display**:
```
┌─────────────────────────────────────────────┐
│  MODBUS Proxy Status                        │
├─────────────────────────────────────────────┤
│  WiFi:     Connected (RSSI: -65 dBm)        │
│  MQTT:     Connected to 192.168.0.203       │
│  Uptime:   2d 14h 32m                       │
│  Heap:     184 KB free                      │
├─────────────────────────────────────────────┤
│  Power Readings                             │
│  ├─ DTSU-666:    -18.5 W                    │
│  ├─ Wallbox:       0.0 W                    │
│  ├─ SUN2000:     -18.5 W                    │
│  └─ Correction:  Inactive                   │
├─────────────────────────────────────────────┤
│  MODBUS Traffic                             │
│  ├─ Last RX:     2 seconds ago              │
│  ├─ Transactions: 12,345                    │
│  └─ Errors:      0                          │
├─────────────────────────────────────────────┤
│  Debug Mode:   [OFF]                        │
├─────────────────────────────────────────────┤
│  [Configure]  [Restart]                     │
└─────────────────────────────────────────────┘
```

**Auto-refresh**: Every 5 seconds via JavaScript

### 14.4 Configuration Page (/config)

**MQTT Settings**:
- Broker Host (text input)
- Broker Port (number input, default 1883)
- Username (text input)
- Password (password input)
- Wallbox Topic (text input)
- Test Connection button
- Save button

**Debug Settings**:
- Debug Mode (toggle: ON/OFF)
  - When ON: Verbose logging enabled via MQTT topic `MBUS-PROXY/debug`
  - When OFF: Normal operation (WARN level only)

**Actions**:
- Save Configuration (saves to NVS, reconnects MQTT)
- Factory Reset (clears all NVS, restarts)
- Restart Device

### 14.5 REST API Endpoints

**GET /api/status** - JSON status:
```json
{
  "wifi": {"connected": true, "rssi": -65, "ssid": "private-2G"},
  "mqtt": {"connected": true, "broker": "192.168.0.203:1883"},
  "uptime": 123456,
  "heap": {"free": 184000, "min": 178000},
  "power": {"dtsu": -18.5, "wallbox": 0.0, "sun2000": -18.5, "active": false},
  "modbus": {"last_rx": 2, "transactions": 12345, "errors": 0}
}
```

**GET /api/config** - Current configuration:
```json
{
  "mqtt_host": "192.168.0.203",
  "mqtt_port": 1883,
  "mqtt_user": "admin",
  "wallbox_topic": "wallbox",
  "log_level": 2
}
```

**POST /api/config** - Update configuration:
```json
{
  "mqtt_host": "192.168.0.100",
  "mqtt_port": 1883,
  "mqtt_user": "newuser",
  "mqtt_pass": "newpass",
  "wallbox_topic": "evcc/loadpoints/0/chargePower",
  "log_level": 1
}
```

**POST /api/restart** - Restart device

**POST /api/factory-reset** - Factory reset

**POST /api/debug** - Toggle debug mode:
```json
{"enabled": true}
```

### 14.6 Implementation Notes

- Uses ESPAsyncWebServer for non-blocking operation
- Static HTML/CSS/JS stored in PROGMEM (flash)
- Minimal footprint (~15KB flash for web assets)
- Status page auto-refreshes via JavaScript fetch

---

## 15. OTA Debugging

### 15.1 Purpose

Remote debugging capabilities without physical serial connection. All debug output is available via MQTT when enabled.

### 15.2 Debug Levels via MQTT

**Log Level Control** (MQTT command):
```json
{"cmd": "set_log_level", "level": 0}
```

| Level | Name | Output |
|-------|------|--------|
| 0 | DEBUG | All messages including MODBUS frames, timing |
| 1 | INFO | Connection events, config changes |
| 2 | WARN | Warnings, timeouts (default) |
| 3 | ERROR | Errors only |

### 15.3 Debug Topics

**MBUS-PROXY/log** - Structured log messages:
```json
{
  "ts": 123456,
  "level": "DEBUG",
  "subsys": "MODBUS",
  "msg": "Frame RX: 0B 03 00 20 00 50 ..."
}
```

**MBUS-PROXY/debug** - Verbose debug stream (level 0 only):
```json
{
  "ts": 123456,
  "type": "modbus_frame",
  "direction": "rx",
  "source": "dtsu",
  "hex": "0B03002000504483",
  "len": 8
}
```

### 15.4 Remote Debug Commands

**Enable verbose frame logging**:
```json
{"cmd": "set_log_level", "level": 0}
```

**Request system state dump**:
```json
{"cmd": "debug_dump"}
```

Response on `MBUS-PROXY/debug/dump`:
```json
{
  "uptime": 123456,
  "free_heap": 184000,
  "min_heap": 178000,
  "wifi_rssi": -65,
  "mqtt_state": "connected",
  "boot_count": 0,
  "last_modbus_rx": 1234,
  "last_wallbox_rx": 5678,
  "correction_active": false,
  "nvs_config": {
    "mqtt_host": "192.168.0.203",
    "mqtt_port": 1883,
    "wallbox_topic": "wallbox"
  }
}
```

**Request config reload**:
```json
{"cmd": "reload_config"}
```

### 15.5 Serial Debug Mirror

When USB serial is connected AND debug level is 0:
- All MQTT debug output is also sent to serial
- Allows simultaneous local and remote debugging

---

## 16. Performance Characteristics

### 16.1 Timing

- **MODBUS Response Time**: < 100ms typical
- **Power Correction Latency**: < 1ms (in-line processing)
- **MQTT Publish Rate**: Per MODBUS transaction (~1/second)
- **Wallbox Data Max Age**: 30 seconds

### 16.2 Resource Usage

- **RAM**: ~18% (60KB / 320KB) - increased for web server
- **Flash**: ~70% (920KB / 1.3MB) - increased for web assets
- **CPU**: Single-core time-sliced

---

## 17. Testing & Validation

### 17.1 Functional Tests

- ✅ MODBUS proxy passthrough
- ✅ Power correction calculation
- ✅ MQTT wallbox subscription
- ✅ MQTT publishing
- ✅ Watchdog monitoring
- ✅ Auto-restart recovery
- ✅ NVS configuration persistence
- ✅ OTA configuration commands

### 17.2 Stress Tests

- ✅ Continuous operation (24+ hours)
- ✅ Network disconnection recovery
- ✅ MQTT broker reconnection
- ✅ Wallbox data timeout handling

### 17.3 New Feature Tests

- ⬜ Captive portal activation (3 power cycles)
- ⬜ Captive portal WiFi configuration
- ⬜ Boot counter reset on successful WiFi
- ⬜ Web UI status page display
- ⬜ Web UI MQTT broker configuration
- ⬜ Web UI debug mode toggle
- ⬜ REST API endpoints
- ⬜ OTA debug output via MQTT
- ⬜ Debug dump command via MQTT

---

## 18. MQTT Configuration Examples

### 18.1 Change MQTT Broker

```bash
mosquitto_pub -h 192.168.0.203 -t "MBUS-PROXY/cmd/config" -m '{"cmd":"set_mqtt","host":"192.168.0.100","port":1883,"user":"myuser","pass":"mypass"}'
```

### 18.2 Set Wallbox Topic for EVCC

```bash
mosquitto_pub -h 192.168.0.203 -t "MBUS-PROXY/cmd/config" -m '{"cmd":"set_wallbox_topic","topic":"evcc/loadpoints/0/chargePower"}'
```

### 18.3 Get Current Configuration

```bash
mosquitto_pub -h 192.168.0.203 -t "MBUS-PROXY/cmd/config" -m '{"cmd":"get_config"}'
# Response on: MBUS-PROXY/cmd/config/response
```

### 18.4 Monitor Power Data

```bash
mosquitto_sub -h 192.168.0.203 -t "MBUS-PROXY/#" -v
```

---

## Appendix A: Register Map

See DTSU-666 datasheet for complete register definitions (2102-2181)

## Appendix B: Troubleshooting

**Common Issues**:
1. **No MODBUS traffic**: Check GPIO pin assignments, RS485 wiring
2. **MQTT disconnects**: Verify broker address, check network stability
3. **Power correction not working**: Check wallbox topic, verify data format
4. **OTA fails**: Check WiFi signal strength, verify password
5. **Config not saving**: NVS may be full, try factory_reset command

---

**Document Version History**:
- v5.0 (February 2026): Captive portal (3 power cycles), Web UI for status/config, OTA debugging
- v4.0 (February 2025): MQTT-based wallbox power, NVS config, OTA commands
- v3.0 (January 2025): Dual-platform support (ESP32-S3/C3), telnet debugging
- v2.0 (September 2024): MQTT implementation, power correction
- v1.0 (Initial): Basic MODBUS proxy functionality
