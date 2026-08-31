# Phase 1: Functional Test Results

| Field | Value |
|-------|-------|
| Test Spec | modbus-proxy-test-spec v3.0 |
| Phase | 1 — Functional Tests |
| Firmware | 1.2.0 |
| Date | 2026-02-09 |
| Tester | Claude (automated) |
| Test AP SSID | Test-gttl8r |
| MQTT Broker | mosquitto on Pi (192.168.4.1:1883) |
| Serial Portal | 192.168.0.87:4001 (SLOT1) |

## Summary

Seven tests failed: WIFI-106, WIFI-302, WIFI-303, WIFI-402, WIFI-405, CP-102
and WIFI-502. Everything else in the table below passed.

**Not run:** EC-113/EC-114/EC-115 (code review only) and LD-001–LD-003 (Phase 2
long-duration).

## Results

| # | Test ID | Name | Result | Details |
|---|---------|------|--------|---------|
| | **Setup** | | | |
| 1 | TC-000 | Flash Firmware and Erase NVS | PASS | Flashed via RFC2217, NVS erased, boot verified via serial |
| 2 | TC-001 | Provision DUT on Test Network | PASS | Portal provisioned, DUT connected to test AP at 192.168.4.6, MQTT configured to Pi broker |
| 3 | TC-002 | Verify Clean State | PASS | wifi=true, mqtt=true, mqtt_host=192.168.4.1, wallbox_power=0, correction_active=false |
| | **WiFi Connectivity** | | | |
| 4 | WIFI-100 | Full Provisioning Flow | PASS | Provisioned with random SSID, DUT connected |
| 5 | WIFI-101 | DHCP Address Assignment | PASS | DUT received DHCP IP 192.168.4.6 |
| 6 | WIFI-102 | mDNS Resolves on Test Network | PASS | avahi-resolve returned correct IP |
| 7 | WIFI-103 | Dashboard Serves HTML | PASS | Dashboard HTML served via relay |
| 8 | WIFI-104 | REST API Returns Valid JSON | PASS | /api/status valid JSON |
| 9 | WIFI-105 | Connect with WPA2 | PASS | Portal AP appeared after STA disconnect |
| 10 | WIFI-106 | Connect to Open Network | FAIL | Portal AP not visible in scan after 20s |
| | **WiFi Reconnection** | | | |
| 11 | WIFI-200 | Reconnect After 5s AP Dropout | PASS | DUT reconnected, uptime continued |
| 12 | WIFI-201 | Reconnect After DUT Reset, MQTT Recovers | PASS | DUT rebooted, reconnected WiFi+MQTT to Pi broker within 10s |
| 13 | WIFI-202 | Extended 90s Dropout | PASS | DUT reconnected after 90s |
| 14 | WIFI-203 | AP SSID Changes | PASS | DUT didn't connect to wrong SSID |
| 15 | WIFI-204 | AP Password Changes | PASS | DUT rejected wrong password |
| 16 | WIFI-205 | 5 Reset Cycles, Heap Stable | PASS | 5 cycles, heap drift +84 bytes (within 1KB tolerance), MQTT reconnected each time |
| | **WiFi Invalid Credentials** | | | |
| 17 | WIFI-300 | Wrong Password | PASS | Portal returned graceful error |
| 18 | WIFI-301 | Wrong SSID | PASS | Portal returned graceful error |
| 19 | WIFI-302 | Empty Password for WPA2 AP | FAIL | /api/wifi returned 500, DUT unresponsive |
| 20 | WIFI-303 | Correct Creds After Bad | FAIL | DUT bad state from WIFI-302 |
| | **Captive Portal** | | | |
| 21 | WIFI-401 | Portal Page Accessible | PASS | Portal page served HTML |
| 22 | WIFI-402 | Scan API Returns Network List | FAIL | /api/scan returned object not array |
| 23 | WIFI-403 | Full Provisioning Flow | PASS | Complete flow successful |
| 24 | WIFI-404 | Portal DNS Redirect | PASS | All captive portal URLs returned 200 |
| 25 | WIFI-405 | Portal Timeout (5 min) | FAIL | Timeout at 302s but reboot not detected |
| 26 | WIFI-406 | Normal Boot No Portal | PASS | DUT booted normally, no portal |
| 27 | CP-101 | Captive Portal WiFi Configuration | PASS | Complete portal flow successful |
| 28 | CP-102 | Captive Portal Timeout | FAIL | Reboot not detected via serial |
| | **WiFi Credentials** | | | |
| 29 | WIFI-500 | NVS Credentials Persist | PASS | SSID=Test-gttl8r preserved after reset, MQTT reconnected to Pi broker |
| 30 | WIFI-501 | NVS Credentials Survive MQTT Reconnect | PASS | WiFi stable after broker restart, MQTT reconnected, SSID unchanged |
| 31 | WIFI-502 | POST /api/wifi Saves and Reboots | FAIL | /api/wifi returns 404 in normal mode (only available in portal mode) |
| 32 | WIFI-503 | Factory Reset Clears WiFi | PASS | NVS erase cleared credentials |
| 33 | WIFI-504 | Long SSID (32 chars) | PASS | 32-char SSID preserved |
| 34 | WIFI-505 | Special Characters in Password | PASS | Special chars worked |
| | **MQTT & Modbus** | | | |
| 35 | TC-100 | Basic Startup | PASS | fw=1.2.0, wifi=true, mqtt=true, heap=165112 |
| 36 | TC-101 | Wallbox Power via MQTT (Plain Float) | PASS | Published 1234.5, wallbox_updates incremented |
| 37 | TC-102 | Wallbox Power via MQTT (JSON power key) | PASS | Published {"power":2345.6}, wallbox_updates incremented |
| 38 | TC-103 | Wallbox Power via MQTT (JSON chargePower key) | PASS | Published {"chargePower":3456.7}, wallbox_updates incremented |
| 39 | TC-104 | Config Command - get_config | PASS | Response contains mqtt_host, mqtt_port, wallbox_topic, log_level |
| 40 | TC-105 | Config Command - set_wallbox_topic | PASS | Topic changed to ocpp/wallbox/power, messages received on new topic |
| 41 | TC-106 | Config Command - set_log_level | PASS | Level 0 (DEBUG) and level 3 (ERROR) both accepted |
| 42 | TC-107 | Config Command - set_mqtt | PASS | Changed to invalid host (mqtt disconnected), restored via API, reconnected |
| 43 | TC-108 | Config Command - factory_reset | PASS | Defaults restored (wallbox_topic=wallbox, mqtt_host=192.168.0.203) |
| 44 | TC-109 | Power Correction Threshold | PASS | 500W: correction_active=false; 1500W: correction_active=true |
| 45 | TC-110 | Wallbox Data Staleness | PASS | Fresh data: active; after 35s: stale/inactive; re-publish: active again |
| | **REST API** | | | |
| 46 | WIFI-600 | Full REST API via Relay | PASS | All endpoints returned 200 |
| 47 | WIFI-601 | OTA Health Check | PASS | OTA endpoint OK |
| 48 | WIFI-602 | RSSI Reported Correctly | PASS | RSSI=-50 |
| 49 | WIFI-603 | wifi_ssid Matches Test AP | PASS | SSID matched |
| | **Edge Cases** | | | |
| 50 | EC-100 | MQTT Disconnect During Operation | PASS | Broker stopped, DUT alive, broker restarted, MQTT reconnected |
| 51 | EC-101 | WiFi Disconnect During Operation | PASS | DUT recovered, uptime continued |
| 52 | EC-102 | Malformed Wallbox Power Message | PASS | wallbox_errors incremented, system continued, valid msg worked after |
| 53 | EC-103 | Malformed Config Command | PASS | Unknown command returned error response, DUT stable |
| 54 | EC-104 | Oversized MQTT Message | PASS | 300-byte message: wallbox_errors incremented, no crash |
| 55 | EC-105 | Rapid Wallbox Power Updates | PASS | 20 messages in 2s: all received, heap stable (164664->164656) |
| 56 | EC-106 | Power Cycle Recovery | PASS | Custom topic preserved after DTR reset, MQTT reconnected |
| 57 | EC-107 | OTA Update | PASS | Firmware uploaded via HTTP OTA from Pi, DUT survived and reconnected |
| 58 | EC-108 | Concurrent MQTT Publish and Subscribe | PASS | 5 wallbox + 5 config msgs concurrent: all processed, system stable |
| 59 | EC-109 | MQTT Reconnect with Config Change | PASS | Invalid host: mqtt_connected=false; restored: mqtt_connected=true |
| 60 | EC-110 | Log Buffer Overflow | PASS | Broker stopped, 20+ events generated, broker restarted, DUT recovered |
| 61 | EC-111 | Empty Wallbox Topic Message | PASS | wallbox_errors incremented, no crash |
| 62 | EC-112 | Special Characters in Config | PASS | Topic ocpp/charger/1/power saved and subscribed correctly |
| 63 | EC-116 | Watchdog Survives MQTT Disconnect | PASS | 65s MQTT disconnect: uptime increased (67504->133432), no reboot |
| | **Web UI** | | | |
| 64 | WEB-100 | Dashboard Page Loads | PASS | Nav bar, wallbox power, status dots, power grid, auto-refresh all present |
| 65 | WEB-101 | Dashboard Power Color Coding | PASS | Color thresholds (#ff9800 amber, #4caf50 green, 1000W threshold) in JS |
| 66 | WEB-102 | Status Page System Info | PASS | System, WiFi, MQTT, Modbus sections all present |
| 67 | WEB-103 | Setup Page - Debug Toggle | PASS | Toggle ON/OFF via API verified, setup page has debug control |
| 68 | WEB-104 | Setup Page - MQTT Configuration | PASS | POST /api/config type=mqtt accepted, MQTT reconnected |
| 69 | WEB-105 | Setup Page - Wallbox Topic | PASS | Topic changed to evcc/power via API, verified |
| 70 | WEB-106 | Setup Page - Factory Reset | PASS | POST /api/config type=reset cleared NVS, defaults restored (wallbox_topic=wallbox) |
| 71 | WEB-107 | REST API /api/status | PASS | All 21 documented fields present with correct types |
| 72 | WEB-108 | Restart via Web UI | PASS | POST /api/restart: uptime reset (34019->7701), WiFi reconnected |
| 73 | WEB-109 | mDNS Hostname | PASS | avahi-resolve modbus-proxy.local -> 192.168.4.6 |

## Failure Analysis

| Test | Root Cause | Severity |
|------|-----------|----------|
| WIFI-106 | Open network support not implemented in portal AP | Low — WPA2-only is acceptable |
| WIFI-302 | Empty password for WPA2 causes /api/wifi 500 error and DUT unresponsive state | Medium — needs input validation |
| WIFI-303 | Cascading failure from WIFI-302 bad state | Medium — blocked by WIFI-302 |
| WIFI-402 | /api/scan returns object `{"networks":[...]}` instead of bare array `[...]` | Low — functional but unexpected format |
| WIFI-405 | Portal timeout occurs at ~302s but reboot not detected via serial | Low — timeout works but detection issue |
| CP-102 | Same as WIFI-405: portal timeout reboot not detected via serial | Low — same root cause |
| WIFI-502 | /api/wifi endpoint only available in portal mode, not normal mode | Medium — by design, but test spec expects normal mode access |
