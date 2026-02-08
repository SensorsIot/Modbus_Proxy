# Modbus Proxy — Test Specification v2.0

## Document Information

| Field | Value |
|-------|-------|
| Version | 2.0 |
| Status | In Progress |
| Created | 2026-02-08 |
| Based on | modbus-proxy-test-spec v1.8 |
| Related | Modbus-Proxy FSD v5.4 |
| Firmware | v1.1.0 |

## 1. Overview

Executable test specification for the Modbus Proxy firmware. Tests are ordered for sequential execution — each group builds on the previous one. Only tests that have been executed and passed are included.

### 1.1 Test Infrastructure

| Component | Description |
|-----------|-------------|
| DUT | ESP32-C3 Modbus Proxy (192.168.0.177) |
| Serial Portal | Pi Zero W at 192.168.0.87, RFC2217 on port 4001/4002/4003 |
| WiFi Tester | Same Pi, wlan0 radio, HTTP API on :8080 |
| MQTT Broker | Mosquitto at 192.168.0.203:1883 |

### 1.2 DUT Clean State

- NVS erased (no saved WiFi, MQTT config, debug mode, boot count = 0)
- Firmware freshly flashed (esp32-c3-debug, SERIAL_DEBUG_LEVEL=2)
- WiFi falls back to credentials.h (private-2G)
- MQTT uses compiled defaults (192.168.0.203:1883)

---

## 2. Setup Tests

### TC-000: Flash Firmware and Erase NVS

**Result: PASS** (2026-02-08)

| Step | Action | Expected | Actual |
|------|--------|----------|--------|
| 1 | `pio run -e esp32-c3-debug` | Build succeeds | SUCCESS (4.3s, 72.3% flash) |
| 2 | Flash bootloader + partitions + firmware via esptool RFC2217 | "Hash of data verified" for each | All 3 verified |
| 3 | Erase NVS region (0x9000, 0x5000) | "Erase completed successfully" | Completed in 0.4s |
| 4 | Wait for boot | DUT boots into application | /api/status responds |
| 5 | Check fw_version | "1.1.0" | "1.1.0" |

**Commands used:**
```bash
# Build
pio run -e esp32-c3-debug

# Flash (RFC2217 via Serial Portal SLOT2)
esptool.py --chip esp32c3 --port "rfc2217://192.168.0.87:4002" \
    --baud 921600 --before=usb_reset --after=watchdog_reset \
    write_flash --flash_mode dio --flash_size 4MB \
    0x0000 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin

# Erase NVS
esptool.py --chip esp32c3 --port "rfc2217://192.168.0.87:4002" \
    --baud 921600 --before=usb_reset --after=watchdog_reset \
    erase_region 0x9000 0x5000
```

### TC-001: Verify Clean State

**Result: PASS** (2026-02-08)

| Step | Action | Expected | Actual |
|------|--------|----------|--------|
| 1 | GET /api/status | 200 OK | 200 OK |
| 2 | Check wifi_ssid | "private-2G" | "private-2G" |
| 3 | Check mqtt_connected | true | true |
| 4 | Check mqtt_host | "192.168.0.203" | "192.168.0.203" |
| 5 | Check mqtt_port | 1883 | 1883 |
| 6 | Check debug_mode | false | false |
| 7 | Check fw_version | "1.1.0" | "1.1.0" |
| 8 | Check free_heap > 20000 | > 20000 | 164928 |
| 9 | MQTT get_config → wallbox_topic | "wallbox" | "wallbox" |
| 10 | MQTT get_config → log_level | default | 2 (DEBUG, matches build) |

---

## 3. Captive Portal Tests

### CP-100: Captive Portal Activation (3 Power Cycles)

**Result: PASS** (2026-02-08)

| Step | Action | Expected | Actual |
|------|--------|----------|--------|
| 1 | `POST /api/enter-portal {"slot":"SLOT2","resets":3}` | Triggers 3 rapid resets | Clean boot → 3 resets in ~8s |
| 2 | Check boot count progression | Increments 1→2→3 | 1→2→3 (each ~1.5s apart) |
| 3 | After 3rd reset | "CAPTIVE PORTAL MODE TRIGGERED" | Captive portal mode confirmed |
| 4 | Check AP SSID | "MODBUS-Proxy-Setup" | "MODBUS-Proxy-Setup" |
| 5 | Check AP password | "modbus-setup" (WPA2) | "modbus-setup" |
| 6 | Check SLOT2 state after operation | "idle" | "idle" (present=true, running=false) |

**Timing (from activity log):**
```
23:00:45  serial.reset(SLOT2) — clean boot...
23:00:47  SLOT2 in NORMAL mode — WiFi (fallback) IP: 192.168.0.177
23:00:48  Sending 3 rapid resets to trigger captive portal...
23:00:50  Reset 1/3 — Boot count: 1 (threshold: 3)
23:00:51  Reset 2/3 — Boot count: 2 (threshold: 3)
23:00:53  Reset 3/3 — Boot count: 3 (threshold: 3)
23:00:56  CAPTIVE PORTAL mode — SSID: MODBUS-Proxy-Setup  Password: modbus-setup
```

**Note:** Resets use direct serial `/dev/ttyACM0` on the Pi (not RFC2217) — the port stays open across DTR/RTS resets, allowing ~1.5s between resets. This is fast enough to prevent WiFi from connecting and clearing the boot counter.

### CP-101: WiFi Provisioning via Portal

**Result: PASS** (2026-02-08)

**Precondition:** DUT is in captive portal mode (from CP-100). Test AP credentials defined: SSID=`TestAP-Modbus`, password=`test12345`.

| Step | Action | Expected | Actual |
|------|--------|----------|--------|
| 1 | WiFi Tester joins DUT AP: `POST /api/wifi/sta_join {"ssid":"MODBUS-Proxy-Setup","pass":"modbus-setup"}` | Connected, IP in 192.168.4.x | Connected, IP 192.168.4.2 |
| 2 | HTTP relay: `POST http://192.168.4.1/api/wifi {"ssid":"TestAP-Modbus","password":"test12345"}` | `{"status":"ok"}` | `{"status":"ok"}` |
| 3 | WiFi Tester switches to AP mode: `POST /api/wifi/ap_start {"ssid":"TestAP-Modbus","pass":"test12345"}` | AP active (WPA2) | AP active at 192.168.4.1 |
| 4 | Monitor serial: DUT reboots and connects to TestAP-Modbus | "WiFi connected" with IP 192.168.4.x | "WiFi connected via NVS! IP: 192.168.4.6" |
| 5 | Check AP station list | DUT MAC in stations | MAC 94:a9:90:47:5b:48 at 192.168.4.6 |

**Serial log (DUT boot after credential submission):**
```
WiFi credentials loaded: SSID=TestAP-Modbus
Trying NVS WiFi credentials: SSID=TestAP-Modbus
[6][0][0][0][0][0][3]
WiFi connected via NVS! IP: 192.168.4.6
Boot count reset to 0
```

**Note:** WiFi Tester `sta_join` and `ap_start` API uses `"pass"` (not `"password"`) for the credential field. The DUT `/api/wifi` endpoint uses `"password"`.

### CP-103: Boot Counter Reset on Success

**Result: PASS** (2026-02-08)

**Precondition:** DUT is connected to WiFi Tester AP (from CP-101).

| Step | Action | Expected | Actual |
|------|--------|----------|--------|
| 1 | Verify via serial (from CP-101 boot) | "Boot count reset to 0" | Confirmed (DUT connected to TestAP-Modbus) |
| 2 | Reset DUT once: `POST /api/enter-portal {"slot":"SLOT2","resets":0}` | DUT boots in NORMAL mode | "portal mode not detected" — DUT stayed in NORMAL mode |
| 3 | Check AP station list | DUT MAC in stations | MAC 94:a9:90:47:5b:48 at 192.168.4.6 |
| 4 | Confirm boot count did not accumulate | Count was 1 (not 2+) | Normal mode confirms count < threshold |

**Note:** Reset uses `enter-portal` with `resets=0` (clean boot only, no rapid resets). The portal stops the proxy, opens direct serial, sends reset pulse, monitors for portal mode (which times out because the DUT boots normally), then restores proxy.

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 2.0 | 2026-02-08 | New document — tests selected from v1.8 and executed in order |
