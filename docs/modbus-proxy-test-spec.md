# Modbus_Proxy Test Specification

## Document Information

| Field | Value |
|-------|-------|
| Version | 1.8 |
| Status | Draft |
| Created | 2026-02-05 |
| Related | Modbus-Proxy FSD v5.3 |

## 1. Overview

This document specifies test cases for the Modbus_Proxy firmware, which acts as a Modbus RTU proxy between a SUN2000 inverter and a DTSU-666 energy meter, with power correction based on wallbox charging data received via MQTT.

### 1.1 System Context

```
┌─────────────┐     Modbus RTU     ┌─────────────────┐     Modbus RTU     ┌─────────────┐
│  SUN2000    │◄──────────────────►│  Modbus_Proxy   │◄──────────────────►│  DTSU-666   │
│  Inverter   │                    │   (ESP32-C3)    │                    │   Meter     │
└─────────────┘                    └────────┬────────┘                    └─────────────┘
                                            │ WiFi
                                            ▼
                                   ┌─────────────────┐
                                   │  MQTT Broker    │
                                   │                 │
                                   │ - Wallbox power │
                                   │ - Config cmds   │
                                   │ - Log output    │
                                   └─────────────────┘
```

### 1.2 Key Features Under Test

1. **Modbus Proxy**: Transparent forwarding between SUN2000 and DTSU-666
2. **Power Correction**: Adding wallbox power to meter readings
3. **MQTT Subscription**: Receiving wallbox power via MQTT
4. **OTA Configuration**: Runtime config changes via MQTT commands
5. **MQTT Logging**: Log events published to MQTT with offline buffering
6. **NVS Storage**: Persistent configuration across reboots
7. **Watchdog Protection**: Software + hardware watchdog for crash recovery
8. **Web UI**: 3-page dashboard (Dashboard/Status/Setup) with REST API
9. **Captive Portal**: WiFi provisioning via AP mode (3 power cycle trigger)
10. **mDNS**: Hostname advertisement (modbus-proxy.local)

### 1.3 Test Suite at a Glance

The project has **168 automated tests** (run without human intervention) and **48 verification tests** (43 automatable, 5 manual). Together they verify that the proxy reads meter data correctly, adjusts it for wallbox charging, survives network failures, and can be configured remotely.

#### Automated Tests — 168 total

Run on every code change to catch regressions immediately.

| Category | Tests | What it proves in plain English |
|----------|------:|--------------------------------|
| **Data Accuracy** (float conversion + DTSU parsing) | 38 | The device reads and writes meter values correctly — like checking a calculator always gives the right answer. |
| **Transmission Integrity** (CRC validation) | 11 | Data isn't corrupted on the wire — the same checksum principle banks use to detect transfer errors. |
| **Power Correction Math** | 16 | Wallbox charging power is correctly added to meter readings so the inverter sees true household consumption. |
| **Configuration & Defaults** | 12 | The device starts with sensible settings and stores them reliably across reboots. |
| **Web API** (REST endpoints) | 22 | The web interface provides correct status data and accepts configuration changes without errors. |
| **Network Messaging** (MQTT) | 18 | Wallbox power arrives correctly over WiFi, bad messages are rejected, and configuration commands work. |
| **End-to-End Pipeline** (test injection) | 16 | The complete data flow — meter reading through correction to inverter output — produces the right result, verified without needing physical Modbus hardware. |
| **OTA Security** | 4 | Firmware updates require the correct password; unauthorized uploads are rejected. |
| **WiFi Integration** (via WiFi Tester) | 33 | The device connects, reconnects after dropout, handles bad credentials, enters captive portal, and serves all endpoints on an isolated test network. |

**How to run:**
```
pio test -e unit-test                 # 77 unit tests (no hardware, seconds)
pytest test/integration/ -v           # 58 integration tests (needs live device)
pytest test/wifi/ -v                  # 33 WiFi tests (needs WiFi Tester hardware)
pytest test/wifi/ -m "not captive_portal"  # WiFi tests without slow portal tests
```

#### Verification Tests — 48 total

These tests verify the running device via external interfaces. **43 can be fully automated** using pytest with MQTT, HTTP, RFC2217 serial, and the WiFi Tester. Only **5 require manual intervention** (marked below).

| Category | Tests | Auto | Manual | Tools needed |
|----------|------:|-----:|-------:|-------------|
| **Standard Tests** (TC-100 to TC-110) | 11 | 11 | 0 | pytest, paho-mqtt, requests |
| **Edge Cases** (EC-100 to EC-116) | 17 | 14 | 3 | pytest, paho-mqtt, requests, pyserial (RFC2217), WiFi Tester, SSH |
| **Web UI** (WEB-100 to WEB-109) | 10 | 8 | 2 | pytest, requests |
| **Captive Portal** (CP-100 to CP-103) | 4 | 4 | 0 | pytest, pyserial (RFC2217), WiFi Tester |
| **Long Duration** (LD-001 to LD-006) | 6 | 6 | 0 | pytest, paho-mqtt, requests |

**Manual-only tests:** EC-113 / EC-114 / EC-115 (require test firmware with deliberate hang/allocation), WEB-101 (CSS color needs browser), WEB-109 (mDNS is OS-dependent).

## 2. Test Environment

### 2.1 Hardware Setup

| Component | Description |
|-----------|-------------|
| ESP32-C3 DevKit | Device Under Test (DUT) |
| Serial Portal Pi | RFC2217 serial server at 192.168.0.87 ([Serial Portal](https://github.com/SensorsIot/Serial-via-Ethernet)) |
| WiFi Tester | Pi Zero W with hostapd/dnsmasq, HTTP-controlled AP at 192.168.0.87:8080 |
| MQTT Broker | Mosquitto on 192.168.0.203:1883 |
| WiFi Network | 2.4GHz network with internet |

### 2.1.1 Infrastructure Rules

The following services are **always-on infrastructure** — tests must NEVER start, stop, or restart them:

| Service | Why |
|---------|-----|
| Serial Portal (`rfc2217-portal`) | Provides RFC2217 serial access to all slots. Stopping it kills serial access for all devices, not just the DUT. |
| MQTT Broker (`mosquitto`) | Shared service; only EC-100/EC-110/LD-006 may stop/start it as part of their specific disconnect tests. |

**Tests are consumers of infrastructure, not managers of it.** If a test needs the portal or broker in a specific state, it is a precondition — not a test step.

### 2.1.2 DUT Initial State

The DUT always starts the test suite in a **well-defined clean state**:

- **NVS is empty** — no saved WiFi credentials, no custom MQTT config, no debug mode, boot count = 0
- **WiFi credentials erased** — no NVS-stored SSID/password; device falls back to `credentials.h` (`private-2G`)
- **Firmware is freshly flashed** — bootloader + partitions + application at correct offsets
- **Device has just booted** — first boot after NVS erase, connected to fallback WiFi, MQTT using compiled defaults

This state is established by the test harness before any test runs (see Section 2.6). Individual tests must not assume any prior test has run. If a test modifies NVS (e.g. changes MQTT config), it must restore defaults in teardown or the harness must re-flash before the next test group.

**How to reach the clean state:**

```bash
# Erase NVS partition (0x9000, 20K) — wipes WiFi creds, MQTT config, boot counter, debug mode
python3 esptool.py --port "rfc2217://192.168.0.87:4001" --chip esp32c3 \
    --baud 921600 erase_region 0x9000 0x5000

# Device resets automatically after erase. On first boot with empty NVS:
#   - WiFi: falls back to credentials.h (private-2G)
#   - MQTT: uses compiled defaults (192.168.0.203:1883)
#   - Boot count: 0
#   - Debug mode: off
```

**Partition layout (for reference):**

| Partition | Offset | Size | Notes |
|-----------|--------|------|-------|
| nvs | 0x9000 | 20K | WiFi creds, MQTT config, boot counter, debug flag |
| otadata | 0xE000 | 8K | OTA boot selection |
| app0 | 0x10000 | 1280K | Primary application |
| app1 | 0x150000 | 1280K | OTA update slot |
| spiffs | 0x290000 | 1408K | Unused |
| coredump | 0x3F0000 | 64K | Crash dump |

### 2.2 Test Tools

| Tool | Purpose |
|------|---------|
| mosquitto_pub/sub | MQTT message injection and monitoring |
| PlatformIO | Firmware build and flash |
| PlatformIO unit-test | Host-side unit test runner (`pio test -e unit-test`) |
| Unity (C) | Unit test framework for unit tests |
| pytest | Python integration test framework |
| paho-mqtt | Python MQTT client for integration tests |
| requests | Python HTTP client for REST API and OTA tests |
| pyserial (RFC2217) | Remote serial control via `rfc2217://192.168.0.87:4003` for device reset/power cycle |
| WiFi Tester driver | HTTP-controlled AP: start/stop AP, scan, HTTP relay, station events |
| SSH (subprocess) | Remote broker restart (`systemctl stop/start mosquitto`) for disconnect tests |

### 2.3 MQTT Topics

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `wallbox` | Subscribe | Wallbox power input (configurable) |
| `MBUS-PROXY/cmd/config` | Subscribe | Configuration commands |
| `MBUS-PROXY/cmd/config/response` | Publish | Config command responses |
| `MBUS-PROXY/power` | Publish | Corrected power data |
| `MBUS-PROXY/health` | Publish | System health status |
| `MBUS-PROXY/log` | Publish | Log events |

### 2.4 Automated Test Coverage

#### Unit Tests (unit-test env, no hardware required)

| Test File | Tests | Source Under Test |
|-----------|-------|-------------------|
| `test_float_conversion` | 23 | `dtsu666.cpp` — parseFloat32, encodeFloat32, parseInt16, parseUInt16 |
| `test_crc_validation` | 11 | `ModbusRTU485.cpp` — crc16, frame validation |
| `test_power_correction` | 16 | `dtsu666.cpp` — applyPowerCorrection, threshold logic |
| `test_dtsu_parsing` | 15 | `dtsu666.cpp` — parseDTSU666Data, encodeDTSU666Response, parseDTSU666Response |
| `test_config_defaults` | 12 | `nvs_config.cpp` — getDefaultConfig, constants |

**Run:** `cd Modbus_Proxy && pio test -e unit-test`

#### Integration Tests (Python, requires live device)

| Test File | Tests | Description |
|-----------|-------|-------------|
| `test_rest_api.py` | ~22 | REST API endpoints: status, config, debug, 404 |
| `test_mqtt.py` | ~18 | MQTT wallbox messages, config commands, edge cases |
| `test_inject.py` | ~16 | Test injection endpoint: pipeline validation, correction, access control |

**Run:** `pip install -r test/integration/requirements.txt && pytest test/integration/ -v`

### 2.5 Test Injection Endpoint

The firmware includes a debug-only REST endpoint `POST /api/test/inject` that simulates DTSU meter data flowing through the full proxy pipeline without requiring physical Modbus hardware.

**Activation:** Debug mode must be enabled first via `POST /api/debug {"enabled": true}`.

**Request body (all fields optional, defaults shown):**
```json
{
  "power_total": 5000.0,
  "voltage": 230.0,
  "frequency": 50.0,
  "current": 10.0
}
```

**Response:**
```json
{
  "status": "ok",
  "dtsu_power": 5000.0,
  "wallbox_power": 0.0,
  "correction_active": false,
  "sun2000_power": 5000.0
}
```

**Pipeline:** The endpoint builds a `DTSU666Data` struct, encodes it to Modbus wire format, parses it back (same as the real proxy), applies power correction if wallbox data is active and above threshold, then updates the shared data that `/api/status` reports.

**Implementation:** `src/test_inject.cpp` / `src/test_inject.h` — encapsulated in a separate file, only the route registration is in `web_server.cpp`.

**Security:** Returns HTTP 403 when debug mode is disabled. Not compiled out — the guard is a runtime check, keeping the binary identical for test and production.

## 3. Test Cases

### 3.0 Setup Test Cases

These run first, in order, to establish and verify the clean DUT state before any functional tests.

#### 3.0.1 TC-000: Flash Firmware and Erase NVS

**Precondition:** Firmware built with `esp32-c3-debug` environment (`SERIAL_DEBUG_LEVEL=2`). This ensures maximum serial output for observing boot behaviour.

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Build firmware: `pio run -e esp32-c3-debug` | Build succeeds |
| 2 | Flash bootloader + partitions + application via esptool | "Hash of data verified" for each |
| 3 | Erase NVS region (0x9000, 0x5000) | "Erase completed successfully" |
| 4 | Hard reset via RTS | DUT reboots into application |
| 5 | Monitor serial output | Boot messages visible: `boot:0xc (SPI_FAST_FLASH_BOOT)`, `ESP32-C3 MODBUS PROXY starting...` |

**Pass Criteria**: All flash/erase operations succeed, DUT resets into normal boot (not download mode), serial output confirms application is running.

**Automation:** esptool via `rfc2217://192.168.0.87:4001`. Flash bootloader.bin, partitions.bin, firmware.bin at correct offsets. Erase NVS. Verify boot via serial.

```bash
ESPTOOL="python3 ~/.platformio/packages/tool-esptoolpy/esptool.py"
PORT="rfc2217://192.168.0.87:4001"
BUILD=".pio/build/esp32-c3-debug"

# Build
cd Modbus_Proxy && pio run -e esp32-c3-debug

# Flash
$ESPTOOL --port $PORT --chip esp32c3 --baud 921600 \
    write_flash --flash_mode dio --flash_size 4MB \
    0x0000 $BUILD/bootloader.bin \
    0x8000 $BUILD/partitions.bin \
    0x10000 $BUILD/firmware.bin

# Erase NVS
$ESPTOOL --port $PORT --chip esp32c3 --baud 921600 \
    erase_region 0x9000 0x5000
```

#### 3.0.2 TC-001: Verify Clean State

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Wait for DUT to boot (max 15s) | DUT connects to fallback WiFi (`private-2G`) |
| 2 | GET /api/status | 200 OK, JSON response |
| 3 | Check `wifi_ssid` | `private-2G` (credentials.h fallback) |
| 4 | Check `mqtt_connected` | `true` (default broker 192.168.0.203:1883) |
| 5 | Check `debug_mode` | `false` |
| 6 | Check `fw_version` | Matches FW_VERSION in config.h |
| 7 | Check `uptime` | < 30 (freshly booted) |
| 8 | Check `free_heap` | > MIN_FREE_HEAP (20000) |
| 9 | Send `get_config` via MQTT | Response shows all compiled defaults |
| 10 | Check wallbox_topic | `wallbox` (default) |
| 11 | Check mqtt_host | `192.168.0.203` (default) |
| 12 | Check log_level | `1` (INFO, default) |

**Pass Criteria**: All fields match compiled defaults. No NVS-stored overrides active.

**Automation:** pytest, requests, paho-mqtt. Poll /api/status until reachable, validate all fields against known defaults. Publish get_config, verify response matches.

### 3.1 Standard Test Cases

#### 3.1.1 TC-100: Basic Startup

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Power on ESP32-C3 | Boot sequence starts |
| 2 | Observe serial log | NVS config loaded, shows MQTT host/port |
| 3 | WiFi connects | IP address assigned |
| 4 | MQTT connects | "CONNECTED" in log |
| 5 | Subscriptions active | Subscribed to wallbox and config topics |

**Pass Criteria**: Boot completes < 15s, MQTT connected.

**Automation:** pytest, requests, paho-mqtt. Verify /api/status uptime > 0, subscribe to MBUS-PROXY/health.

### 3.2 TC-101: Wallbox Power via MQTT (Plain Float)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running, MQTT connected | Normal operation |
| 2 | Publish `3456.7` to `wallbox` topic | Message received |
| 3 | Check serial log | "Wallbox power updated: 3456.7W" |
| 4 | Wait for DTSU response | Power correction applied |
| 5 | Check `MBUS-PROXY/power` | wallbox field shows 3456.7 |

**Pass Criteria**: Plain float parsed correctly, correction applied.

**Automation:** pytest, paho-mqtt. Publish float to wallbox topic, subscribe to MBUS-PROXY/power, verify wallbox field.

### 3.3 TC-102: Wallbox Power via MQTT (JSON power key)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"power": 5000.0}` to `wallbox` | Message received |
| 2 | Check serial log | "Wallbox power updated: 5000.0W" |

**Pass Criteria**: JSON with "power" key parsed correctly.

**Automation:** pytest, paho-mqtt. Publish {"power":5000}, verify via MBUS-PROXY/power or /api/status.

### 3.4 TC-103: Wallbox Power via MQTT (JSON chargePower key)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"chargePower": 7400}` to `wallbox` | Message received |
| 2 | Check serial log | "Wallbox power updated: 7400.0W" |

**Pass Criteria**: EVCC-compatible JSON parsed correctly.

**Automation:** pytest, paho-mqtt. Publish {"chargePower":7400}, verify via /api/status wallbox_power.

### 3.5 TC-104: Config Command - get_config

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"cmd": "get_config"}` to `MBUS-PROXY/cmd/config` | Command received |
| 2 | Subscribe to `MBUS-PROXY/cmd/config/response` | Response published |
| 3 | Check response JSON | Contains mqtt_host, mqtt_port, mqtt_user, wallbox_topic, log_level |

**Pass Criteria**: Current config returned correctly.

**Automation:** pytest, paho-mqtt. Publish get_config, subscribe to response topic, validate JSON fields.

### 3.6 TC-105: Config Command - set_wallbox_topic

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"cmd": "set_wallbox_topic", "topic": "ocpp/wallbox/power"}` | Command received |
| 2 | Check response | status: "ok" |
| 3 | MQTT reconnects | New topic subscribed |
| 4 | Publish power to new topic | Power updated |

**Pass Criteria**: Topic changed, persisted in NVS.

**Automation:** pytest, paho-mqtt. Publish set_wallbox_topic, publish power to new topic, verify update.

### 3.7 TC-106: Config Command - set_log_level

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"cmd": "set_log_level", "level": 0}` | Set to DEBUG |
| 2 | Check response | status: "ok", level: 0 |
| 3 | Observe `MBUS-PROXY/log` topic | DEBUG messages appear |
| 4 | Set level to 3 (ERROR) | Only errors published |

**Pass Criteria**: Log level controls MQTT log output.

**Automation:** pytest, paho-mqtt. Publish set_log_level, subscribe to MBUS-PROXY/log, verify filtering.

### 3.8 TC-107: Config Command - set_mqtt

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"cmd": "set_mqtt", "host": "192.168.0.100", "port": 1883}` | Command received |
| 2 | Check response | status: "ok", reconnecting message |
| 3 | MQTT disconnects from old broker | Connection closed |
| 4 | MQTT connects to new broker | New connection established |

**Pass Criteria**: MQTT credentials changed, reconnect triggered.

**Automation:** pytest, paho-mqtt, requests. Publish set_mqtt, verify reconnect via /api/status mqtt_reconnects.

### 3.9 TC-108: Config Command - factory_reset

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Change some config values | Non-default config |
| 2 | Publish `{"cmd": "factory_reset"}` | Command received |
| 3 | Check response | status: "ok" |
| 4 | MQTT reconnects | Using default 192.168.0.203:1883 |
| 5 | get_config | All defaults restored |

**Pass Criteria**: NVS cleared, defaults applied.

**Automation:** pytest, paho-mqtt, requests. Set non-default config, publish factory_reset, verify defaults via get_config.

### 3.10 TC-109: Power Correction Threshold

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish wallbox power: 500W | Below threshold (1000W) |
| 2 | Check `MBUS-PROXY/power` | active: false, wallbox: 0 |
| 3 | Publish wallbox power: 1500W | Above threshold |
| 4 | Check `MBUS-PROXY/power` | active: true, wallbox: 1500 |

**Pass Criteria**: Correction only applied above threshold.

**Automation:** pytest, paho-mqtt. Publish below/above threshold values, check MBUS-PROXY/power active flag.

### 3.11 TC-110: Wallbox Data Staleness

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish wallbox power: 5000W | Data valid |
| 2 | Wait 35 seconds (> 30s max age) | Data expires |
| 3 | Check power correction | active: false (data stale) |
| 4 | Publish new power value | Data valid again |

**Pass Criteria**: Stale data not used for correction.

**Automation:** pytest, paho-mqtt. Publish wallbox power, sleep 35s, verify correction deactivates via /api/status.

## 4. Edge Case Tests

### 4.1 EC-100: MQTT Disconnect During Operation

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running, wallbox power active | Normal operation |
| 2 | Stop MQTT broker | Connection lost |
| 3 | Check serial log | "MQTT reconnecting..." messages |
| 4 | Modbus proxy continues | SUN2000 ↔ DTSU still working |
| 5 | Log messages buffered | Up to 16 entries queued |
| 6 | Restart MQTT broker | Auto-reconnect |
| 7 | Buffered logs published | Logs appear on MBUS-PROXY/log |

**Pass Criteria**: Modbus unaffected, logs buffered and recovered.

**Automation:** pytest, paho-mqtt, subprocess (SSH). Stop/start mosquitto on broker via `ssh`, verify log buffer recovery on reconnect.

### 4.2 EC-101: WiFi Disconnect During Operation

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running normally | WiFi + MQTT connected |
| 2 | Disable WiFi AP | WiFi connection lost |
| 3 | Modbus proxy continues | SUN2000 ↔ DTSU still working |
| 4 | Wallbox data goes stale | Correction deactivates after 30s |
| 5 | Re-enable WiFi AP | WiFi reconnects |
| 6 | MQTT reconnects | Subscriptions restored |

**Pass Criteria**: Modbus unaffected, graceful WiFi recovery.

**Automation:** pytest, WiFi Tester (ap_stop/ap_start). Drop AP, wait for staleness timeout, restore, verify recovery via HTTP relay.

### 4.3 EC-102: Malformed Wallbox Power Message

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `not_a_number` to wallbox topic | Invalid format |
| 2 | Check serial log | "Failed to parse wallbox power" |
| 3 | Check health stats | wallbox_errors incremented |
| 4 | System continues | No crash |
| 5 | Publish valid power | Correctly parsed |

**Pass Criteria**: Graceful handling, error logged, no crash.

**Automation:** pytest, paho-mqtt, requests. Publish "not_a_number", verify wallbox_errors incremented via /api/status.

### 4.4 EC-103: Malformed Config Command

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{invalid json` | Parse error |
| 2 | Check serial log | "Config command parse error" |
| 3 | Publish `{"foo": "bar"}` | Missing cmd field |
| 4 | Check serial log | "Config command missing 'cmd' field" |
| 5 | Publish `{"cmd": "unknown_cmd"}` | Unknown command |
| 6 | Check response | status: "error", message: "Unknown command" |

**Pass Criteria**: All malformed commands handled gracefully.

**Automation:** pytest, paho-mqtt. Publish invalid JSON, missing cmd, unknown cmd — check error responses on response topic.

### 4.5 EC-104: Oversized MQTT Message

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish 300-byte wallbox message | Above 256 limit |
| 2 | Message ignored | wallbox_errors incremented |
| 3 | Publish 600-byte config command | Above 512 limit |
| 4 | Message ignored | No crash |
| 5 | System continues | Normal operation |

**Pass Criteria**: Size limits enforced, no buffer overflow.

**Automation:** pytest, paho-mqtt, requests. Publish 300-byte and 600-byte messages, verify device alive via /api/status.

### 4.6 EC-105: Rapid Wallbox Power Updates

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish 20 power values in 2 seconds | High message rate |
| 2 | Monitor system | No crash, no watchdog |
| 3 | Check final value | Last value applied |
| 4 | Check heap | No memory leak |

**Pass Criteria**: System stable under rapid updates.

**Automation:** pytest, paho-mqtt, requests. Burst 20 messages in 2s, verify last value and heap stability via /api/status.

### 4.7 EC-106: Power Cycle Recovery

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Configure custom MQTT settings | Non-default config |
| 2 | Power cycle ESP32 | Immediate restart |
| 3 | System boots | Normal boot sequence |
| 4 | Check NVS config | Custom settings preserved |
| 5 | MQTT connects | Using saved credentials |

**Pass Criteria**: Configuration survives power cycle.

**Automation:** pytest, pyserial (RFC2217). Set custom config via MQTT, toggle DTR on rfc2217://192.168.0.87:4003 to reset, verify NVS preserved via get_config.

### 4.8 EC-107: OTA Update

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Build new firmware | Different version |
| 2 | Upload via OTA | HTTP OTA: `curl -X POST http://{ip}/ota -H "Authorization: Bearer modbus_ota_2023" -F "firmware=@.pio/build/esp32-c3-release/firmware.bin"` |
| 3 | Monitor progress | OTA progress in serial log |
| 4 | System reboots | New firmware running |
| 5 | Check NVS config | Configuration preserved |

**Pass Criteria**: OTA succeeds, config preserved.

**Automation:** pytest, requests. POST firmware.bin to /ota with auth header, verify new fw_version via /api/status.

### 4.9 EC-108: Concurrent MQTT Publish and Subscribe

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | DTSU sending meter values | High publish rate |
| 2 | Send config commands | Concurrent subscription |
| 3 | Send wallbox power updates | More subscription traffic |
| 4 | Monitor both interfaces | All messages handled |
| 5 | Check timing | No excessive delays |

**Pass Criteria**: Both directions functional under load.

**Automation:** pytest, paho-mqtt (threaded). Concurrent publish/subscribe threads, verify all messages processed without timeouts.

### 4.10 EC-109: MQTT Reconnect with Config Change

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Change MQTT host to invalid IP | set_mqtt command |
| 2 | MQTT fails to connect | Reconnect attempts |
| 3 | Modbus continues | Proxy still working |
| 4 | Change back to valid host | Another set_mqtt |
| 5 | MQTT connects | Normal operation restored |

**Pass Criteria**: Invalid config doesn't brick device.

**Automation:** pytest, paho-mqtt, requests. Send set_mqtt with invalid host, verify via /api/status, send valid host, verify recovery.

### 4.11 EC-110: Log Buffer Overflow

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Disconnect MQTT | Logs buffer locally |
| 2 | Generate > 16 log events | Buffer full |
| 3 | Continue generating logs | Oldest entries overwritten |
| 4 | Reconnect MQTT | Most recent 16 logs sent |
| 5 | No memory issues | Circular buffer works |

**Pass Criteria**: Bounded memory usage, no crash.

**Automation:** pytest, paho-mqtt, subprocess (SSH). Stop broker, trigger >16 log events, restart broker, verify most recent 16 logs published.

### 4.12 EC-111: Empty Wallbox Topic Message

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish empty message to wallbox | Zero-length payload |
| 2 | Check handling | Message ignored |
| 3 | wallbox_errors incremented | Error counted |
| 4 | System continues | No crash |

**Pass Criteria**: Empty messages handled gracefully.

**Automation:** pytest, paho-mqtt, requests. Publish empty message, verify wallbox_errors incremented via /api/status.

### 4.13 EC-112: Special Characters in Config

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Set wallbox topic with `/` chars | `ocpp/charger/1/power` |
| 2 | Topic saved correctly | NVS stores full path |
| 3 | Subscription works | Messages received |
| 4 | Set MQTT password with special chars | `p@ss!word#123` |
| 5 | Authentication works | MQTT connects |

**Pass Criteria**: Special characters handled in config.

**Automation:** pytest, paho-mqtt. Set topic with "/" chars via set_wallbox_topic, publish to new topic, verify received.

### 4.14 EC-113: Software Watchdog - Task Timeout Detection

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running normally | All tasks healthy |
| 2 | Monitor `MBUS-PROXY/health` | Regular health reports |
| 3 | Simulate task hang (test firmware) | Stop heartbeat updates |
| 4 | Wait 60+ seconds | Software watchdog triggers |
| 5 | Check `MBUS-PROXY/log` | "task timeout - triggering reboot" logged |
| 6 | System reboots automatically | ESP.restart() called |
| 7 | After reboot | Normal operation resumes |

**Pass Criteria**: Hung task detected within 65s, automatic recovery via reboot.

**Automation:** Manual — requires test firmware with deliberate task hang capability. Cannot be triggered via external interfaces.

**Note**: Can alternatively be verified by reading watchdog implementation code paths.

### 4.15 EC-114: Hardware Watchdog - Watchdog Task Recovery

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running normally | Hardware WDT active |
| 2 | Check serial log at startup | "Hardware WDT initialized (90s timeout)" |
| 3 | Monitor normal operation | `esp_task_wdt_reset()` called every 5s |
| 4 | If watchdog task hangs | Hardware WDT triggers after 90s |
| 5 | System panic and reboot | Automatic hardware recovery |

**Pass Criteria**: Hardware watchdog provides failsafe recovery if software watchdog itself fails.

**Automation:** Manual — safety-net test; the watchdog task itself must hang, which cannot be triggered externally. Verified by code review.

**Note**: In normal operation, the software watchdog handles recovery before the hardware WDT triggers.

### 4.16 EC-115: Critical Memory Watchdog

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running normally | Heap > MIN_FREE_HEAP |
| 2 | Monitor heap via `MBUS-PROXY/health` | free_heap and min_free_heap values |
| 3 | Simulate memory pressure | Allocate memory (test firmware) |
| 4 | Heap drops below MIN_FREE_HEAP | "Low heap" warning logged |
| 5 | Heap drops below MIN_FREE_HEAP/2 | Critical memory threshold |
| 6 | Check `MBUS-PROXY/log` | "Critical heap - triggering reboot" |
| 7 | System reboots automatically | Memory recovered |

**Pass Criteria**: Critical memory exhaustion triggers preventive reboot before crash.

**Automation:** Manual — requires test firmware with deliberate memory allocation to exhaust heap. Cannot be triggered via external interfaces.

### 4.17 EC-116: Watchdog Survives MQTT Disconnect

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running, MQTT connected | Watchdog task active |
| 2 | Stop MQTT broker | Connection lost |
| 3 | Wait 5 minutes | Extended disconnect period |
| 4 | Monitor serial log | Health checks continue (every 5s) |
| 5 | No watchdog resets | System remains stable |
| 6 | Restart MQTT broker | Connection restored |
| 7 | Health reports resume on MQTT | Watchdog unaffected by MQTT state |

**Pass Criteria**: Watchdog operates independently of MQTT connectivity.

**Automation:** pytest, paho-mqtt, subprocess (SSH), requests. Stop broker 5 min, verify no reboot (uptime increases) via /api/status, restart broker.

## 5. Web UI & Captive Portal Tests

### 5.1 WEB-100: Dashboard Page Loads

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Open http://{device-ip}/ in browser | Dashboard page loads |
| 2 | Verify navigation bar | Dashboard/Status/Setup links present |
| 3 | Verify status indicators | MQTT, DTSU, SUN2000 dots visible |
| 4 | Verify wallbox power display | Large centered value shown |
| 5 | Verify power grid | DTSU, Correction, SUN2000 values displayed |
| 6 | Wait 4 seconds | Values auto-refresh (2s interval) |

**Pass Criteria**: Page loads, all elements present, auto-refresh working.

**Automation:** pytest, requests. GET /, verify 200 OK, check HTML for nav bar, status dots, power grid elements.

### 5.2 WEB-101: Dashboard Power Color Coding

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Set wallbox power to 0W | Wallbox value displays cyan |
| 2 | Publish wallbox power 500W | Value displays green |
| 3 | Publish wallbox power 1500W | Value displays amber (correction active) |

**Pass Criteria**: Color changes match thresholds (0=cyan, >0=green, >1000=amber).

**Automation:** Manual — CSS color verification requires visual browser inspection. Can partially verify CSS class names via requests.

### 5.3 WEB-102: Status Page System Info

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Navigate to /status | Status page loads |
| 2 | Check System section | Uptime, heap, min heap displayed |
| 3 | Check WiFi section | SSID, IP, RSSI displayed |
| 4 | Check MQTT section | Connection status, host:port, reconnects |
| 5 | Check MODBUS section | DTSU/wallbox/proxy update counts |
| 6 | Wait 10 seconds | Values auto-refresh (5s interval) |

**Pass Criteria**: All sections display correct live data.

**Automation:** pytest, requests. GET /status, verify HTML contains System, WiFi, MQTT, MODBUS sections.

### 5.4 WEB-103: Setup Page - Debug Toggle

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Navigate to /setup | Setup page loads |
| 2 | Toggle debug mode ON | POST /api/debug {"enabled":true} |
| 3 | Refresh /api/status | debug_mode: true |
| 4 | Toggle debug mode OFF | POST /api/debug {"enabled":false} |
| 5 | Restart device | Debug mode persisted in NVS |

**Pass Criteria**: Debug toggle saves to NVS, survives reboot.

**Automation:** pytest, requests. POST /api/debug, verify via GET /api/status debug_mode field, toggle back.

### 5.5 WEB-104: Setup Page - MQTT Configuration

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Navigate to /setup | Current MQTT config loaded |
| 2 | Change MQTT host | Enter new host value |
| 3 | Click Save | POST /api/config {"type":"mqtt",...} |
| 4 | Check response | {"status":"ok"} |
| 5 | MQTT reconnects | New broker connection |
| 6 | Verify /api/config | Updated host returned |

**Pass Criteria**: MQTT config saved, reconnect triggered.

**Automation:** pytest, requests, paho-mqtt. POST /api/config MQTT settings, verify response, check MQTT reconnect.

### 5.6 WEB-105: Setup Page - Wallbox Topic

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Navigate to /setup | Current wallbox topic shown |
| 2 | Change topic to "evcc/power" | Enter new topic |
| 3 | Click Save | POST /api/config {"type":"wallbox","topic":"evcc/power"} |
| 4 | Verify subscription | MQTT re-subscribes to new topic |

**Pass Criteria**: Topic changed, MQTT subscription updated.

**Automation:** pytest, requests, paho-mqtt. POST /api/config wallbox topic, verify MQTT subscription updated.

### 5.7 WEB-106: Setup Page - Factory Reset

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Change MQTT config to non-default | Custom settings saved |
| 2 | Navigate to /setup | Setup page |
| 3 | Click Factory Reset | POST /api/config {"type":"reset"} |
| 4 | Device restarts | Boot sequence |
| 5 | Check /api/config | All defaults restored |

**Pass Criteria**: NVS cleared, all settings return to defaults.

**Automation:** pytest, requests. POST /api/config {"type":"reset"}, wait for reboot, verify defaults via GET /api/config.

### 5.8 WEB-107: REST API /api/status

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | GET /api/status | 200 OK, JSON response |
| 2 | Verify fields | uptime, free_heap, wifi_*, mqtt_*, dtsu_power, wallbox_power, etc. |
| 3 | Compare with serial output | Values consistent |

**Pass Criteria**: All documented fields present with correct types.

**Automation:** pytest, requests. GET /api/status, validate all documented fields present with correct JSON types.

### 5.9 WEB-108: Restart via Web UI

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | POST /api/restart | {"status":"ok"} |
| 2 | Device reboots | Normal boot sequence |
| 3 | Web UI accessible | Dashboard loads at same IP |

**Pass Criteria**: Clean restart, config preserved.

**Automation:** pytest, requests. POST /api/restart, wait ~15s, verify GET / responds with 200 OK.

### 5.10 WEB-109: mDNS Hostname

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Device connected to WiFi | mDNS started |
| 2 | Browse to http://modbus-proxy.local | Dashboard loads |
| 3 | Verify mDNS service | HTTP service advertised on port 80 |

**Pass Criteria**: Device reachable via hostname (requires mDNS client on network).

**Automation:** Manual — mDNS resolution is OS/network dependent; not reliably automatable across platforms.

### 5.11 CP-100: Captive Portal Activation (3 Power Cycles)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Power cycle device 3 times rapidly | Boot count increments each time |
| 2 | On 3rd boot | "CAPTIVE PORTAL MODE TRIGGERED" in serial |
| 3 | Check WiFi | AP mode: SSID "MODBUS-Proxy-Setup" |
| 4 | Connect phone/laptop to AP | Auto-redirect to portal page |
| 5 | Portal page displays | WiFi scan and password entry |

**Pass Criteria**: 3 rapid reboots triggers AP mode with captive portal.

**Automation:** pytest, pyserial (RFC2217), WiFi Tester. Three serial DTR resets via rfc2217://192.168.0.87:4003, WiFi Tester scans for "MODBUS-Proxy-Setup" AP, joins it, verifies portal page at 192.168.4.1.

### 5.12 CP-101: Captive Portal WiFi Configuration

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | In portal mode | Connected to "MODBUS-Proxy-Setup" AP |
| 2 | Scan for networks | GET /api/scan returns nearby networks |
| 3 | Select network, enter password | Fill form |
| 4 | Click Save | POST /api/wifi saves credentials |
| 5 | Device restarts | Connects to configured network |
| 6 | Boot count resets | Normal operation |

**Pass Criteria**: WiFi credentials saved via portal, device connects on restart.

**Automation:** pytest, WiFi Tester (HTTP relay). In portal mode: GET /api/scan, POST /api/wifi with test AP credentials via relay, verify DUT joins new AP.

### 5.13 CP-102: Captive Portal Timeout

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Trigger captive portal mode | AP active |
| 2 | Do nothing for 5 minutes | Timeout reached |
| 3 | Device restarts | Attempts normal WiFi connection |

**Pass Criteria**: Portal doesn't run indefinitely; 5-minute timeout triggers restart.

**Automation:** pytest, pyserial (RFC2217), WiFi Tester. Trigger portal via 3 resets, wait 5+ minutes, verify DUT restarts (AP disappears, STA reconnects).

### 5.14 CP-103: Boot Counter Reset on Success

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Power cycle once | Boot count = 1 |
| 2 | WiFi connects successfully | Boot count reset to 0 |
| 3 | Power cycle once more | Boot count = 1 (not 2) |

**Pass Criteria**: Successful WiFi connection resets boot counter, preventing accidental portal activation.

**Automation:** pytest, pyserial (RFC2217), requests. Single DTR reset, verify WiFi connects, reset again, verify boot count = 1 (not 2) via /api/status.

---

## 6. Long-Duration Tests

| Test ID | Duration | Description | Pass Criteria |
|---------|----------|-------------|---------------|
| LD-001 | 24 hours | Continuous Modbus proxy | No memory leaks, stable heap |
| LD-002 | 24 hours | MQTT publish every second | No disconnects, no queue overflow |
| LD-003 | 72 hours | Idle with health reports | No watchdog resets |
| LD-004 | 7 days | Normal usage pattern | < 1 unexpected reset |
| LD-005 | 24 hours | Watchdog stability | No false watchdog triggers, heap stable |
| LD-006 | 48 hours | Repeated WiFi/MQTT disconnects | Watchdog does not trigger during normal reconnects |

**Automation (all LD tests):** pytest, paho-mqtt, requests. Long-running scripts with periodic /api/status polling (free_heap, uptime, mqtt_reconnects). LD-001/LD-002: subscribe to MBUS-PROXY/power or /health, verify steady heap. LD-003/LD-005: monitor /api/status uptime (no unexpected resets). LD-004: periodic health checks over 7 days. LD-006: subprocess (SSH) to cycle mosquitto stop/start, WiFi Tester for AP dropout cycles.

## 7. Test Commands Reference

### 7.1 MQTT Test Commands

```bash
# Subscribe to all MBUS-PROXY topics
mosquitto_sub -h 192.168.0.203 -t "MBUS-PROXY/#" -v

# Publish wallbox power (plain float)
mosquitto_pub -h 192.168.0.203 -t "wallbox" -m "3500.5"

# Publish wallbox power (JSON)
mosquitto_pub -h 192.168.0.203 -t "wallbox" -m '{"power": 5000}'

# Get current config
mosquitto_pub -h 192.168.0.203 -t "MBUS-PROXY/cmd/config" -m '{"cmd": "get_config"}'

# Set wallbox topic
mosquitto_pub -h 192.168.0.203 -t "MBUS-PROXY/cmd/config" -m '{"cmd": "set_wallbox_topic", "topic": "ocpp/power"}'

# Set log level (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)
mosquitto_pub -h 192.168.0.203 -t "MBUS-PROXY/cmd/config" -m '{"cmd": "set_log_level", "level": 1}'

# Set MQTT credentials
mosquitto_pub -h 192.168.0.203 -t "MBUS-PROXY/cmd/config" -m '{"cmd": "set_mqtt", "host": "192.168.0.100", "port": 1883, "user": "admin", "pass": "secret"}'

# Factory reset
mosquitto_pub -h 192.168.0.203 -t "MBUS-PROXY/cmd/config" -m '{"cmd": "factory_reset"}'
```

### 7.2 Build and Flash Commands

```bash
# Build environments
pio run -e esp32-c3-debug        # Max serial debug (level 2)
pio run -e esp32-c3-release      # Info serial (level 1)
pio run -e esp32-c3-production   # No serial (level 0)
pio test -e unit-test            # Run unit tests on host

# Flash via serial (esptool, any build)
python3 esptool.py --chip esp32c3 --port "rfc2217://192.168.0.87:4001" --baud 921600 \
    write_flash 0x0 .pio/build/esp32-c3-debug/bootloader.bin \
    0x8000 .pio/build/esp32-c3-debug/partitions.bin \
    0x10000 .pio/build/esp32-c3-debug/firmware.bin

# Flash via HTTP OTA (any build)
curl -X POST http://192.168.0.177/ota \
    -H "Authorization: Bearer modbus_ota_2023" \
    -F "firmware=@.pio/build/esp32-c3-release/firmware.bin"

# Monitor serial output
pio device monitor -b 115200
```

## 8. Test Report Template

```
═══════════════════════════════════════════════════════════════
                 MODBUS_PROXY TEST REPORT
═══════════════════════════════════════════════════════════════
Date:           ___________
Firmware:       v___
Tester:         ___________
Test Suite:     Standard / Edge Cases / Long Duration

───────────────────────────────────────────────────────────────
SUMMARY
───────────────────────────────────────────────────────────────
Total Tests:    ___
Passed:         ___
Failed:         ___
Skipped:        ___
Pass Rate:      ___%

───────────────────────────────────────────────────────────────
FAILED TESTS
───────────────────────────────────────────────────────────────
Test ID    | Description              | Failure Reason
-----------|--------------------------|---------------------------
           |                          |

───────────────────────────────────────────────────────────────
NOTES
───────────────────────────────────────────────────────────────


═══════════════════════════════════════════════════════════════
```

## 9. Automated Test Edge Cases

### 9.1 Unit Test Edge Cases

These edge cases are covered by the unit tests (no hardware required):

#### Float Conversion (`test_float_conversion`)
- IEEE754 special values: NaN, +infinity, -infinity, subnormal (smallest positive)
- Round-trip encode→parse for positive, negative, small, and large values
- Offset parameter correctness (data not at byte 0)

#### CRC Validation (`test_crc_validation`)
- Empty/zero-length buffer (returns initial CRC 0xFFFF)
- Single-byte buffer, all-zeros, all-0xFF buffers
- Corrupted data byte vs corrupted CRC byte detection
- Null pointer handling, minimum valid frame (4 bytes)

#### Power Correction (`test_power_correction`)
- Threshold boundary: exactly 1000W does NOT trigger (strict `>` comparison)
- Negative power above threshold (e.g., -1500W → `fabs()` > 1000)
- Null buffer, short buffer (<165 bytes) → returns false
- Phase distribution: correction/3 applied to L1, L2, L3 individually
- Demand fields corrected identically to power fields
- Large correction (22kW), negative correction, zero correction
- Non-power fields (voltage, frequency) preserved unchanged
- CRC recalculated correctly after modification

#### DTSU Parsing (`test_dtsu_parsing`)
- Invalid message (valid=false), wrong type (Request vs Reply), null raw pointer
- Wrong payload size (80 instead of 160 bytes) → returns false
- Power sign inversion: wire value 3000.0 → parsed as -3000.0 (power_scale=-1)
- Voltage passthrough: no inversion (volt_scale=1.0)
- Encode-parse round-trip for all 40 fields (power fields double-negated)
- All-zero data round-trip

#### Config Defaults (`test_config_defaults`)
- Each default value matches #define (host, port, user, pass, topic, logLevel)
- Null termination of char[] fields at last byte position
- Constants: CORRECTION_THRESHOLD=1000, WALLBOX_DATA_MAX_AGE_MS=30000, WATCHDOG_TIMEOUT_MS=60000, MIN_FREE_HEAP=20000

### 9.2 Integration Test Edge Cases

These edge cases are covered by Python integration tests (requires live device + MQTT broker):

#### REST API (`test_rest_api.py`)
- Invalid JSON body → 400 response
- Unknown config type → error status in 200 response
- Non-existent path → 404
- Debug toggle ON/OFF verified via /api/status
- All documented /api/status fields present with correct types

#### Test Injection (`test_inject.py`)
- Access control: returns 403 when debug mode disabled, 200 when enabled
- Default values: no parameters → 5000W/230V/50Hz/10A injected
- Custom power: 0W, negative (-3000W), large (22kW)
- Status integration: increments `dtsu_updates`, updates `dtsu_power` in `/api/status`
- Multiple injections: last value reflected in status
- Correction pipeline: without wallbox → no correction; with wallbox MQTT → correction applied
- Invalid JSON → 400, GET method → 404/405

#### MQTT (`test_mqtt.py`)
- Empty wallbox message → ignored, error count incremented
- Non-numeric wallbox message ("not_a_number") → error count incremented
- Oversized message (300 bytes, >256 limit) → handled gracefully, device alive
- Malformed JSON config command → device continues operating
- Missing "cmd" field in config JSON → error response
- Rapid burst: 10 messages in 1 second → last value applied, no crash
- Special characters in payload (`<>&"'`) → no crash
- Negative wallbox power values → correctly stored and reported

---

## 10. WiFi Integration Tests (Automated via WiFi Tester)

These tests use the [WiFi Tester](https://github.com/SensorsIot/Wifi-Tester) — a serial-controlled ESP32-C3 that acts as a programmable WiFi access point. All DUT HTTP communication goes through the WiFi Tester's serial relay. No manual intervention required.

**Hardware required:** WiFi Tester (ESP32-C3 SuperMini) connected via USB serial.

**Run:**
```bash
pip install -r test/wifi/requirements.txt
pip install -e <path-to-Wifi-Tester>/pytest  # WiFi Tester driver

# All WiFi tests
WIFI_TESTER_PORT=/dev/ttyACM0 DUT_IP=192.168.0.177 pytest test/wifi/ -v

# Skip slow captive portal tests
pytest test/wifi/ -v -m "not captive_portal"

# Only captive portal tests
pytest test/wifi/ -v -m captive_portal
```

### 10.1 WiFi Connection Tests (WIFI-1xx)

| ID | Test | What it proves |
|----|------|---------------|
| WIFI-100 | Connect to test AP | DUT connects and reports correct SSID |
| WIFI-101 | DHCP address assigned | DUT gets IP in 192.168.4.x range |
| WIFI-102 | mDNS resolves on test network | modbus-proxy.local works on isolated network |
| WIFI-103 | Web dashboard accessible | HTML page loads via relay |
| WIFI-104 | REST API accessible | /api/status returns valid JSON |
| WIFI-105 | Connect with WPA2 | DUT handles WPA2 authentication |
| WIFI-106 | Connect to open network | DUT connects without password |
| WIFI-107 | Boot counter resets on success | Single reboot doesn't trigger captive portal |

### 10.2 AP Dropout and Reconnection (WIFI-2xx)

| ID | Test | What it proves |
|----|------|---------------|
| WIFI-200 | Reconnect after 5s AP dropout | DUT auto-reconnects after AP returns |
| WIFI-201 | Brief 2s dropout, no reboot | DUT reconnects without rebooting (uptime continues) |
| WIFI-202 | Extended 90s dropout | DUT recovers even after long outage |
| WIFI-203 | AP SSID changes | DUT cannot connect to wrong SSID |
| WIFI-204 | AP password changes | DUT cannot connect with old password |
| WIFI-205 | 5 dropout cycles, heap stable | No memory leak across repeated reconnections |

### 10.3 Invalid Credentials (WIFI-3xx)

| ID | Test | What it proves |
|----|------|---------------|
| WIFI-300 | Wrong password | DUT fails gracefully, doesn't crash |
| WIFI-301 | Wrong SSID | DUT fails and falls back to credentials.h |
| WIFI-302 | Empty password for WPA2 AP | Auth failure handled |
| WIFI-303 | Correct creds after bad | DUT recovers after credential correction |

### 10.4 Captive Portal (WIFI-4xx)

| ID | Test | What it proves |
|----|------|---------------|
| WIFI-400 | Portal activates after 3 failed boots | Boot counter triggers AP mode correctly |
| WIFI-401 | Portal page accessible | Portal HTML served on 192.168.4.1 |
| WIFI-402 | WiFi scan endpoint in portal | /api/scan returns network list |
| WIFI-403 | Full provisioning flow | Portal → submit creds → DUT connects to new AP |
| WIFI-404 | Portal DNS redirect | Captive portal detection URLs redirect |
| WIFI-405 | Portal timeout (5 min) | Portal auto-restarts after timeout |
| WIFI-406 | Normal boot no portal | Single reboot doesn't trigger portal |

### 10.5 Credential Management (WIFI-5xx)

| ID | Test | What it proves |
|----|------|---------------|
| WIFI-500 | NVS credentials persist | DUT reconnects to same AP after reboot |
| WIFI-501 | NVS priority over fallback | NVS creds used before credentials.h |
| WIFI-502 | POST /api/wifi saves and reboots | Credential save endpoint works |
| WIFI-503 | Factory reset clears WiFi | Reset removes NVS creds, falls back |
| WIFI-504 | Long SSID (32 chars) | Max SSID length works |
| WIFI-505 | Special characters in password | Handles !@#$% in credentials |

### 10.6 Network Services on Test AP (WIFI-6xx)

| ID | Test | What it proves |
|----|------|---------------|
| WIFI-600 | Full REST API via relay | All key endpoints work through serial proxy |
| WIFI-601 | OTA health check | /ota/health responds via relay |
| WIFI-602 | RSSI reported correctly | RSSI is plausible negative value |
| WIFI-603 | wifi_ssid matches test AP | Reported SSID matches configured AP |

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-02-05 | Initial specification |
| 1.1 | 2026-02-05 | Added watchdog test cases (EC-113 to EC-116, LD-005, LD-006) |
| 1.2 | 2026-02-06 | Added Web UI test cases (WEB-100 to WEB-109), captive portal tests (CP-100 to CP-103), mDNS |
| 1.3 | 2026-02-06 | Added automated test coverage (Section 2.4), edge cases chapter (Section 9), PlatformIO unit-test + pytest tools |
| 1.4 | 2026-02-06 | Added test injection endpoint (Section 2.5), `test_inject.py` integration tests, removed manual test cross-references |
| 1.5 | 2026-02-06 | Added test suite overview (Section 1.3) with classification and layman summaries |
| 1.6 | 2026-02-06 | Added WiFi integration tests (Section 10, WIFI-1xx to WIFI-6xx) using WiFi Tester instrument |
| 1.7 | 2026-02-07 | Added automation tools to all verification tests (Sections 3–6), updated hardware/tools for Serial Portal and WiFi Tester, corrected test count (48 not 47) |
| 1.8 | 2026-02-07 | Simplified serial debug to 3 levels (OFF/INFO/DEBUG), renamed build envs to esp32-c3-debug/release/production/unit-test, updated TC-000 to use debug build with serial verification |
