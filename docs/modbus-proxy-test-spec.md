# Modbus_Proxy Test Specification

## Document Information

| Field | Value |
|-------|-------|
| Version | 1.1 |
| Status | Draft |
| Created | 2026-02-05 |
| Related | OCPP-Server FSD v1.3 |

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

## 2. Test Environment

### 2.1 Hardware Setup

| Component | Description |
|-----------|-------------|
| ESP32-C3 DevKit | Device Under Test (DUT) |
| USB-Serial | For flashing and log monitoring |
| MQTT Broker | Mosquitto on 192.168.0.203:1883 |
| WiFi Network | 2.4GHz network with internet |

### 2.2 Test Tools

| Tool | Purpose |
|------|---------|
| mosquitto_pub/sub | MQTT message injection and monitoring |
| PlatformIO | Firmware build and flash |
| Serial Monitor | Debug log capture |
| Python scripts | Automated test sequences |

### 2.3 MQTT Topics

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `wallbox` | Subscribe | Wallbox power input (configurable) |
| `MBUS-PROXY/cmd/config` | Subscribe | Configuration commands |
| `MBUS-PROXY/cmd/config/response` | Publish | Config command responses |
| `MBUS-PROXY/power` | Publish | Corrected power data |
| `MBUS-PROXY/health` | Publish | System health status |
| `MBUS-PROXY/log` | Publish | Log events |

## 3. Standard Test Cases

### 3.1 TC-100: Basic Startup

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Power on ESP32-C3 | Boot sequence starts |
| 2 | Observe serial log | NVS config loaded, shows MQTT host/port |
| 3 | WiFi connects | IP address assigned |
| 4 | MQTT connects | "CONNECTED" in log |
| 5 | Subscriptions active | Subscribed to wallbox and config topics |

**Pass Criteria**: Boot completes < 15s, MQTT connected.

### 3.2 TC-101: Wallbox Power via MQTT (Plain Float)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running, MQTT connected | Normal operation |
| 2 | Publish `3456.7` to `wallbox` topic | Message received |
| 3 | Check serial log | "Wallbox power updated: 3456.7W" |
| 4 | Wait for DTSU response | Power correction applied |
| 5 | Check `MBUS-PROXY/power` | wallbox field shows 3456.7 |

**Pass Criteria**: Plain float parsed correctly, correction applied.

### 3.3 TC-102: Wallbox Power via MQTT (JSON power key)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"power": 5000.0}` to `wallbox` | Message received |
| 2 | Check serial log | "Wallbox power updated: 5000.0W" |

**Pass Criteria**: JSON with "power" key parsed correctly.

### 3.4 TC-103: Wallbox Power via MQTT (JSON chargePower key)

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"chargePower": 7400}` to `wallbox` | Message received |
| 2 | Check serial log | "Wallbox power updated: 7400.0W" |

**Pass Criteria**: EVCC-compatible JSON parsed correctly.

### 3.5 TC-104: Config Command - get_config

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"cmd": "get_config"}` to `MBUS-PROXY/cmd/config` | Command received |
| 2 | Subscribe to `MBUS-PROXY/cmd/config/response` | Response published |
| 3 | Check response JSON | Contains mqtt_host, mqtt_port, mqtt_user, wallbox_topic, log_level |

**Pass Criteria**: Current config returned correctly.

### 3.6 TC-105: Config Command - set_wallbox_topic

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"cmd": "set_wallbox_topic", "topic": "ocpp/wallbox/power"}` | Command received |
| 2 | Check response | status: "ok" |
| 3 | MQTT reconnects | New topic subscribed |
| 4 | Publish power to new topic | Power updated |

**Pass Criteria**: Topic changed, persisted in NVS.

### 3.7 TC-106: Config Command - set_log_level

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"cmd": "set_log_level", "level": 0}` | Set to DEBUG |
| 2 | Check response | status: "ok", level: 0 |
| 3 | Observe `MBUS-PROXY/log` topic | DEBUG messages appear |
| 4 | Set level to 3 (ERROR) | Only errors published |

**Pass Criteria**: Log level controls MQTT log output.

### 3.8 TC-107: Config Command - set_mqtt

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `{"cmd": "set_mqtt", "host": "192.168.0.100", "port": 1883}` | Command received |
| 2 | Check response | status: "ok", reconnecting message |
| 3 | MQTT disconnects from old broker | Connection closed |
| 4 | MQTT connects to new broker | New connection established |

**Pass Criteria**: MQTT credentials changed, reconnect triggered.

### 3.9 TC-108: Config Command - factory_reset

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Change some config values | Non-default config |
| 2 | Publish `{"cmd": "factory_reset"}` | Command received |
| 3 | Check response | status: "ok" |
| 4 | MQTT reconnects | Using default 192.168.0.203:1883 |
| 5 | get_config | All defaults restored |

**Pass Criteria**: NVS cleared, defaults applied.

### 3.10 TC-109: Power Correction Threshold

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish wallbox power: 500W | Below threshold (1000W) |
| 2 | Check `MBUS-PROXY/power` | active: false, wallbox: 0 |
| 3 | Publish wallbox power: 1500W | Above threshold |
| 4 | Check `MBUS-PROXY/power` | active: true, wallbox: 1500 |

**Pass Criteria**: Correction only applied above threshold.

### 3.11 TC-110: Wallbox Data Staleness

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish wallbox power: 5000W | Data valid |
| 2 | Wait 35 seconds (> 30s max age) | Data expires |
| 3 | Check power correction | active: false (data stale) |
| 4 | Publish new power value | Data valid again |

**Pass Criteria**: Stale data not used for correction.

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

### 4.3 EC-102: Malformed Wallbox Power Message

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish `not_a_number` to wallbox topic | Invalid format |
| 2 | Check serial log | "Failed to parse wallbox power" |
| 3 | Check health stats | wallbox_errors incremented |
| 4 | System continues | No crash |
| 5 | Publish valid power | Correctly parsed |

**Pass Criteria**: Graceful handling, error logged, no crash.

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

### 4.5 EC-104: Oversized MQTT Message

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish 300-byte wallbox message | Above 256 limit |
| 2 | Message ignored | wallbox_errors incremented |
| 3 | Publish 600-byte config command | Above 512 limit |
| 4 | Message ignored | No crash |
| 5 | System continues | Normal operation |

**Pass Criteria**: Size limits enforced, no buffer overflow.

### 4.6 EC-105: Rapid Wallbox Power Updates

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish 20 power values in 2 seconds | High message rate |
| 2 | Monitor system | No crash, no watchdog |
| 3 | Check final value | Last value applied |
| 4 | Check heap | No memory leak |

**Pass Criteria**: System stable under rapid updates.

### 4.7 EC-106: Power Cycle Recovery

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Configure custom MQTT settings | Non-default config |
| 2 | Power cycle ESP32 | Immediate restart |
| 3 | System boots | Normal boot sequence |
| 4 | Check NVS config | Custom settings preserved |
| 5 | MQTT connects | Using saved credentials |

**Pass Criteria**: Configuration survives power cycle.

### 4.8 EC-107: OTA Update

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Build new firmware | Different version |
| 2 | Upload via OTA | `pio run -e esp32-c3-ota -t upload` |
| 3 | Monitor progress | OTA progress in serial log |
| 4 | System reboots | New firmware running |
| 5 | Check NVS config | Configuration preserved |

**Pass Criteria**: OTA succeeds, config preserved.

### 4.9 EC-108: Concurrent MQTT Publish and Subscribe

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | DTSU sending meter values | High publish rate |
| 2 | Send config commands | Concurrent subscription |
| 3 | Send wallbox power updates | More subscription traffic |
| 4 | Monitor both interfaces | All messages handled |
| 5 | Check timing | No excessive delays |

**Pass Criteria**: Both directions functional under load.

### 4.10 EC-109: MQTT Reconnect with Config Change

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Change MQTT host to invalid IP | set_mqtt command |
| 2 | MQTT fails to connect | Reconnect attempts |
| 3 | Modbus continues | Proxy still working |
| 4 | Change back to valid host | Another set_mqtt |
| 5 | MQTT connects | Normal operation restored |

**Pass Criteria**: Invalid config doesn't brick device.

### 4.11 EC-110: Log Buffer Overflow

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Disconnect MQTT | Logs buffer locally |
| 2 | Generate > 16 log events | Buffer full |
| 3 | Continue generating logs | Oldest entries overwritten |
| 4 | Reconnect MQTT | Most recent 16 logs sent |
| 5 | No memory issues | Circular buffer works |

**Pass Criteria**: Bounded memory usage, no crash.

### 4.12 EC-111: Empty Wallbox Topic Message

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Publish empty message to wallbox | Zero-length payload |
| 2 | Check handling | Message ignored |
| 3 | wallbox_errors incremented | Error counted |
| 4 | System continues | No crash |

**Pass Criteria**: Empty messages handled gracefully.

### 4.13 EC-112: Special Characters in Config

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | Set wallbox topic with `/` chars | `ocpp/charger/1/power` |
| 2 | Topic saved correctly | NVS stores full path |
| 3 | Subscription works | Messages received |
| 4 | Set MQTT password with special chars | `p@ss!word#123` |
| 5 | Authentication works | MQTT connects |

**Pass Criteria**: Special characters handled in config.

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

**Note**: Requires test firmware with deliberate task hang capability, or can be verified by reading watchdog implementation code paths.

### 4.15 EC-114: Hardware Watchdog - Watchdog Task Recovery

| Step | Action | Expected Result |
|------|--------|-----------------|
| 1 | System running normally | Hardware WDT active |
| 2 | Check serial log at startup | "Hardware WDT initialized (90s timeout)" |
| 3 | Monitor normal operation | `esp_task_wdt_reset()` called every 5s |
| 4 | If watchdog task hangs | Hardware WDT triggers after 90s |
| 5 | System panic and reboot | Automatic hardware recovery |

**Pass Criteria**: Hardware watchdog provides failsafe recovery if software watchdog itself fails.

**Note**: This is a safety-net test. In normal operation, the software watchdog handles recovery before the hardware WDT triggers.

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

## 5. Long-Duration Tests

| Test ID | Duration | Description | Pass Criteria |
|---------|----------|-------------|---------------|
| LD-001 | 24 hours | Continuous Modbus proxy | No memory leaks, stable heap |
| LD-002 | 24 hours | MQTT publish every second | No disconnects, no queue overflow |
| LD-003 | 72 hours | Idle with health reports | No watchdog resets |
| LD-004 | 7 days | Normal usage pattern | < 1 unexpected reset |
| LD-005 | 24 hours | Watchdog stability | No false watchdog triggers, heap stable |
| LD-006 | 48 hours | Repeated WiFi/MQTT disconnects | Watchdog does not trigger during normal reconnects |

## 6. Test Commands Reference

### 6.1 MQTT Test Commands

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

### 6.2 Build and Flash Commands

```bash
# Build for serial upload
pio run -e esp32-c3-serial

# Flash via serial
pio run -e esp32-c3-serial -t upload

# Build for OTA
pio run -e esp32-c3-ota

# Flash via OTA
pio run -e esp32-c3-ota -t upload

# Monitor serial output
pio device monitor -b 115200
```

## 7. Test Report Template

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

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-02-05 | Initial specification |
| 1.1 | 2026-02-05 | Added watchdog test cases (EC-113 to EC-116, LD-005, LD-006) |
