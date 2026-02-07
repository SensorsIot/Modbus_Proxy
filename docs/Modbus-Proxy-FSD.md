# ESP32 MODBUS RTU Intelligent Proxy - Functional Specification Document

**Version:** 5.3
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
├── Watchdog Task (Priority 3)
│   ├── Health monitoring (5s)
│   ├── Hardware WDT feed (90s timeout)
│   └── Auto-restart triggers
├── Web Server (async, runs in main loop context)
│   ├── Dashboard / Status / Setup pages
│   ├── REST API endpoints
│   └── Captive portal mode (AP only)
├── WiFi Manager
│   ├── STA connection with NVS/fallback credentials
│   ├── AP mode for captive portal
│   └── DNS redirect for portal detection
└── Loop Task
    └── ArduinoOTA handler (legacy)
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

| Environment | Purpose | Serial Debug Level |
|-------------|---------|-------------------|
| `esp32-c3-debug` | Development & testing | 2 (DEBUG) — all output |
| `esp32-c3-release` | Field deployment with monitoring | 1 (INFO) — important messages |
| `esp32-c3-production` | Final deployment | 0 (OFF) — silent |
| `unit-test` | Unit tests on host (no hardware) | 2 (DEBUG) |

All ESP32 builds can be flashed via serial (esptool) or HTTP OTA (curl). The upload method is independent of the build environment.

### 8.2 Serial Debug Levels

The compile-time `SERIAL_DEBUG_LEVEL` flag controls all serial output:

| Level | Name | Serial.begin() | What prints to serial |
|-------|------|----------------|----------------------|
| 0 | OFF | No | Nothing |
| 1 | INFO | Yes | MLOG_INFO, MLOG_WARN, MLOG_ERROR |
| 2 | DEBUG | Yes | Everything — all MLOG_* messages + verbose DEBUG_PRINTF boot/diagnostic output |

This is independent of the runtime MQTT log level (Section 6.3), which controls what goes to the MQTT log queue.

### 8.3 Build Flags

```ini
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -DSERIAL_DEBUG_LEVEL=<0|1|2>
    -std=gnu++17
board_build.usb_cdc = true
board_build.arduino.memory_type = qio_qspi
```

### 8.3 Dependencies
- ArduinoJson @ ^6.19.4
- PubSubClient @ ^2.8
- ArduinoOTA
- Preferences (NVS, built-in)
- mathieucarbou/ESP Async WebServer @ ^3.0.6
- mathieucarbou/AsyncTCP @ ^3.3.2
- ESPmDNS (built-in)

---

## 9. Memory Management

### 9.1 Task Stack Sizes

| Task | Stack Size |
|------|------------|
| Proxy | 4KB |
| MQTT | 8KB |
| Watchdog | 2KB |
| Captive Portal | 4KB (only in AP mode) |

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

### 11.1 HTTP OTA (Preferred)

Push-based firmware update via HTTP multipart upload on port 80. Works from Docker containers and through NAT.

- **Endpoint**: `POST /ota`
- **Auth**: `Authorization: Bearer modbus_ota_2023`
- **Health check**: `GET /ota/health` (no auth required)
- **Upload format**: Multipart form (`-F`), NOT `--data-binary`
- **Implementation**: `src/http_ota.cpp` / `src/http_ota.h`

**Update command:**
```bash
curl -X POST http://192.168.0.177/ota \
  -H "Authorization: Bearer modbus_ota_2023" \
  -F "firmware=@.pio/build/esp32-c3-release/firmware.bin"
```

**Response:** `{"status":"ok","message":"Rebooting..."}` on success, device reboots automatically.

### 11.2 ArduinoOTA (Legacy)

- **Port**: 3232
- **Password**: modbus_ota_2023
- **Hostname**: MODBUS-Proxy
- **Limitation**: Does not work from Docker containers (NAT blocks callback)

### 11.3 RFC2217 Serial (Network Flash)

Remote serial flashing via the [Serial Portal](https://github.com/SensorsIot/Serial-via-Ethernet). No buttons required.

```bash
# Check which port the C3 is on
curl -s http://192.168.0.87:8080/api/devices

# Flash (replace port with actual slot port)
python3 -m esptool --chip esp32c3 \
  --port "rfc2217://192.168.0.87:4002" --baud 921600 \
  write-flash -z 0x0 .pio/build/esp32-c3-release/firmware.bin
```

---

## 12. Network Configuration

### 12.1 WiFi Settings

- **Mode**: Station (STA) mode, AP mode for provisioning
- **Hostname**: "MODBUS-Proxy"
- **Power Save**: Disabled for stability
- **Connection Timeout**: 30 seconds

### 12.2 Services

- **mDNS**: Enabled (modbus-proxy.local), advertises HTTP service
- **Web UI**: Port 80 (STA mode - Dashboard/Status/Setup pages)
- **HTTP OTA**: Port 80, `POST /ota` (Bearer auth)
- **ArduinoOTA**: Port 3232 (legacy, doesn't work from Docker)
- **MQTT**: Port 1883 (configurable)
- **Captive Portal**: Port 80 (AP mode - WiFi provisioning page)

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

A lightweight 3-page web interface accessible when device is connected to WiFi (STA mode). Provides real-time power monitoring, system status, and MQTT broker configuration without requiring MQTT connectivity.

### 14.2 Access

- **URL**: http://modbus-proxy.local or http://{device-ip} (e.g. http://192.168.0.177)
- **Port**: 80
- **Authentication**: None (local network only)
- **mDNS**: Hostname `modbus-proxy` advertised via ESPmDNS

### 14.3 Navigation

All three pages share a consistent dark-themed layout with a top navigation bar and status indicator dots (MQTT, DTSU, SUN2000) showing green/amber/red connection health.

### 14.4 Dashboard Page (/)

The primary operational view focused on wallbox power and power correction status.

**Layout**:
- **Status indicators**: MQTT, DTSU, SUN2000 dots (green=ok, amber=warn, red=error)
- **Wallbox Power**: Large center display (3.5em monospace), color-coded:
  - Cyan: 0 W (idle)
  - Green: > 0 W (charging)
  - Amber: > 1000 W (correction active)
- **Power Grid**: 3-column display showing DTSU Meter, Correction, SUN2000 values
- **Auto-refresh**: Every 2 seconds via JavaScript `fetch('/api/status')`

### 14.5 Status Page (/status)

Detailed system information for diagnostics.

**Sections**:
- **System**: Uptime (formatted d/h/m/s), free heap, minimum heap
- **WiFi**: SSID, IP address, RSSI signal strength
- **MQTT**: Connection status, broker host:port, reconnect count
- **MODBUS**: DTSU updates, wallbox updates, wallbox errors, proxy errors
- **Restart Device** button

**Auto-refresh**: Every 5 seconds

### 14.6 Setup Page (/setup)

Configuration interface for runtime settings.

**Debug Mode**:
- Toggle switch (ON/OFF) for verbose MQTT logging
- Persisted in NVS via `POST /api/debug`

**MQTT Broker**:
- Host (text input)
- Port (number input, default 1883)
- Username (text input)
- Password (password input)
- Save button (saves to NVS, triggers MQTT reconnect)

**Wallbox Topic**:
- Topic path (text input)
- Save button

**Log Level**:
- Dropdown: DEBUG (0), INFO (1), WARN (2), ERROR (3)
- Save button

**Factory Reset**:
- Reset button (clears NVS, restarts device)

### 14.7 REST API Endpoints

**GET /api/status** - Full system status:
```json
{
  "fw_version": "1.1.0",
  "uptime": 123456,
  "free_heap": 184000,
  "min_free_heap": 178000,
  "wifi_connected": true,
  "wifi_ssid": "private-2G",
  "wifi_ip": "192.168.0.177",
  "wifi_rssi": -60,
  "mqtt_connected": true,
  "mqtt_host": "192.168.0.203",
  "mqtt_port": 1883,
  "mqtt_reconnects": 0,
  "dtsu_power": -18.5,
  "wallbox_power": 0.0,
  "sun2000_power": -18.5,
  "correction_active": false,
  "dtsu_updates": 1234,
  "wallbox_updates": 123,
  "wallbox_errors": 0,
  "proxy_errors": 0,
  "debug_mode": false
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

**POST /api/config** - Update configuration (type-based dispatch):
```json
{"type": "mqtt", "host": "192.168.0.100", "port": 1883, "user": "admin", "pass": "secret"}
{"type": "wallbox", "topic": "evcc/loadpoints/0/chargePower"}
{"type": "loglevel", "level": 1}
{"type": "reset"}
```

**POST /api/restart** - Restart device

**POST /api/debug** - Toggle debug mode:
```json
{"enabled": true}
```

**GET /api/scan** - WiFi network scan (portal mode only):
```json
{"networks": [{"ssid": "MyNetwork", "rssi": -55, "encrypted": true}]}
```

**POST /api/wifi** - Save WiFi credentials (portal mode only):
```json
{"ssid": "MyNetwork", "password": "secret"}
```

**POST /api/test/inject** - Inject test DTSU data (debug mode only, returns 403 if debug disabled):
```json
{"power_total": 5000.0, "voltage": 230.0, "frequency": 50.0, "current": 10.0}
```
Response:
```json
{"status": "ok", "dtsu_power": -5000.0, "wallbox_power": 0.0, "correction_active": false, "sun2000_power": -5000.0}
```

**GET /ota/health** - OTA health check (no auth required):
```json
{"status": "ok"}
```

**POST /ota** - HTTP OTA firmware upload (multipart form, Bearer auth required):
```bash
curl -X POST http://192.168.0.177/ota \
  -H "Authorization: Bearer modbus_ota_2023" \
  -F "firmware=@.pio/build/esp32-c3-release/firmware.bin"
```

### 14.8 Implementation Notes

- Uses ESPAsyncWebServer (mathieucarbou fork v3.0.6) for non-blocking operation
- AsyncTCP (mathieucarbou fork v3.3.2) as transport layer
- Static HTML/CSS/JS stored in PROGMEM (flash) via `web_assets.h`
- Dark theme (background #1a1a2e) with cyan (#0ff) accent colors
- Responsive design via CSS grid and flexbox
- Each page is a self-contained HTML document with inline CSS/JS
- Portal page served only in AP captive portal mode (separate HTML)

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

### 15.5 Serial Debug Output

Serial output is controlled by the compile-time `SERIAL_DEBUG_LEVEL` (see Section 8.2):
- **Level 0 (OFF)**: No serial output. Use MQTT log for remote debugging.
- **Level 1 (INFO)**: Important messages (WiFi, MQTT, NVS, errors) to serial.
- **Level 2 (DEBUG)**: All messages including verbose MODBUS frames and boot diagnostics.

The MQTT log level (runtime, Section 6.3) and serial debug level (compile-time) are independent. A `release` build (serial level 1) can still send DEBUG-level messages to MQTT when debug mode is enabled.

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

### 17.3 Web UI & Provisioning Tests

- ⬜ Captive portal activation (3 power cycles)
- ⬜ Captive portal WiFi configuration and restart
- ⬜ Boot counter reset on successful WiFi connection
- ✅ Web UI Dashboard - wallbox power display with color coding
- ✅ Web UI Dashboard - status indicators (MQTT/DTSU/SUN2000)
- ✅ Web UI Dashboard - power grid (DTSU/Correction/SUN2000)
- ✅ Web UI Status page - system info, WiFi, MQTT, MODBUS stats
- ✅ Web UI Setup page - debug mode toggle
- ✅ Web UI Setup page - MQTT broker configuration
- ✅ Web UI Setup page - wallbox topic configuration
- ✅ Web UI Setup page - log level selection
- ✅ REST API /api/status endpoint
- ✅ REST API /api/config GET/POST endpoints
- ✅ REST API /api/debug endpoint
- ✅ REST API /api/restart endpoint
- ✅ REST API /api/test/inject endpoint (debug mode only)
- ✅ HTTP OTA firmware update (POST /ota with Bearer auth)
- ✅ OTA health check (GET /ota/health)
- ✅ mDNS hostname advertisement (modbus-proxy.local)
- ✅ Automated unit tests (77 tests via `pio test -e unit-test`)
- ✅ Automated integration tests (58 tests via `pytest`)
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
- v5.3 (February 2026): RFC2217 serial flashing without buttons (plain RFC2217 server for C3 native USB), Serial Portal integration
- v5.2 (February 2026): HTTP OTA with Bearer auth (replaces ArduinoOTA as primary), test injection endpoint, fw_version in /api/status, automated test coverage (77 unit + 58 integration)
- v5.1 (February 2026): Updated Web UI section to match implemented 3-page layout (Dashboard/Status/Setup), documented REST API responses, added mDNS and library versions
- v5.0 (February 2026): Captive portal (3 power cycles), Web UI for status/config, OTA debugging
- v4.0 (February 2025): MQTT-based wallbox power, NVS config, OTA commands
- v3.0 (January 2025): Dual-platform support (ESP32-S3/C3), telnet debugging
- v2.0 (September 2024): MQTT implementation, power correction
- v1.0 (Initial): Basic MODBUS proxy functionality
