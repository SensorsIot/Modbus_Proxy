# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-based MODBUS RTU proxy project with **intelligent power correction** that sits between a SUN2000 solar inverter and a DTSU-666 energy meter. The system compensates for wallbox power consumption by dynamically correcting power values reported to the inverter.

**For complete technical specifications, see:** `Modbus_ProxyV1/Modbus-Proxy-FSD.md`

## Build System

This project uses PlatformIO for ESP32 development:

**Build commands:**
- `pio run` - Build the project
- `pio run --target upload` - Build and upload to ESP32
- `pio device monitor` - Open serial monitor (115200 baud)
- `pio run --target clean` - Clean build files

**Development workflow:**
- Main source code is in `Modbus_ProxyV1/src/main.cpp`
- Configuration is in `Modbus_ProxyV1/platformio.ini`
- Libraries are auto-managed by PlatformIO based on `lib_deps` in platformio.ini

## Development Rules & Guidelines

### Code Standards
- **Follow existing conventions**: Mimic code style, use existing libraries and utilities
- **Thread Safety**: Always use mutex protection for shared data structures
- **Error Handling**: Implement comprehensive error handling with MQTT error reporting
- **Memory Management**: Monitor heap usage, avoid memory leaks
- **No Comments**: DO NOT ADD comments unless explicitly requested

### System Architecture Rules
- **Dual-Core Design**: Maintain Core 0 (MQTT/Watchdog) and Core 1 (Proxy) separation
- **Task Priorities**: Watchdog (3) > Proxy (2) > MQTT (1)
- **Independent Watchdog**: Never remove or modify the independent watchdog task
- **Thread-Safe Data Access**: Always use provided getter/setter functions for shared data

### Power Correction Rules
- **EVCC API Integration**: Only use EVCC HTTP API, no MQTT reception
- **Threshold Enforcement**: Only apply corrections above 1000W threshold
- **IEEE 754 Handling**: Preserve exact byte-level float encoding/decoding
- **CRC Integrity**: Always recalculate MODBUS CRC after data modifications

### MODBUS Protocol Rules
- **Slave ID 11 Only**: Only process messages for DTSU-666 (ID 11)
- **Function Code Support**: 0x03, 0x04, 0x06, 0x10 only
- **Register Range**: 2102-2181 (80 registers, 160 bytes IEEE 754 data)
- **Raw Frame Preservation**: Maintain exact MODBUS frame structure

### Health Monitoring Rules
- **Failure Thresholds**: Never disable or increase safety thresholds
- **MQTT Error Reporting**: Always report errors to MQTT topics before restart
- **Graceful Degradation**: System must continue proxy operation if power correction fails
- **Restart Mechanisms**: Use ESP.restart() only, never halt or infinite loops

### Output & Logging Rules
- **Clean Output**: Maintain 3-line format (DTSU, API, SUN2000)
- **No Verbose Debug**: Avoid excessive logging, focus on essential data
- **Error Visibility**: Always log critical errors to serial and MQTT
- **Performance Data**: Include timing and memory usage in health reports

### Configuration Management
- **Network Settings**: EVCC API URL, MQTT broker settings
- **Timing Constants**: API polling (10s), health checks (5s), watchdog (60s)
- **Safety Limits**: Memory threshold (20KB), failure counts
- **Hardware Pins**: RS485 interfaces (UART1: 18/19, UART2: 16/17)

### Testing & Validation
- **Compile First**: Always run `pio run` to verify compilation
- **No Assumptions**: Verify library availability before using new dependencies
- **Integration Testing**: Test MODBUS, EVCC API, and MQTT functionality
- **Memory Monitoring**: Check heap usage and stack watermarks

### Forbidden Actions
- ❌ **No WiFi Config Changes**: Never modify WiFi setup or credentials
- ❌ **No Task Priority Changes**: Never alter task priorities without system redesign
- ❌ **No Watchdog Removal**: Never disable or remove watchdog functionality
- ❌ **No CRC Shortcuts**: Never skip CRC validation or recalculation
- ❌ **No Blocking Operations**: Avoid operations that could hang tasks
- ❌ **No Direct Serial Access**: Use provided MODBUS library abstractions

## Quick Reference

**Key Files:**
- `src/main.cpp` - Main implementation
- `src/ModbusRTU485.h/.cpp` - MODBUS RTU library
- `platformio.ini` - Build configuration
- `Modbus-Proxy-FSD.md` - Complete technical specification

**Important Functions:**
- `proxyTask()` - Main MODBUS proxy (Core 1)
- `mqttTask()` - MQTT/API handler (Core 0)
- `watchdogTask()` - System health monitor (Core 0)
- `pollEvccApi()` - EVCC API polling
- `calculatePowerCorrection()` - Power correction logic

**Configuration Constants:**
- `CORRECTION_THRESHOLD = 1000.0f` - Power correction minimum
- `HTTP_POLL_INTERVAL = 10000` - EVCC API polling interval
- `WATCHDOG_TIMEOUT_MS = 60000` - Task heartbeat timeout
- `evccApiUrl = "http://192.168.0.202:7070/api/state"` - EVCC API endpoint