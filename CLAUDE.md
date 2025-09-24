# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-based MODBUS RTU proxy project with **intelligent power correction** that sits between a SUN2000 solar inverter and a DTSU-666 energy meter. The system compensates for power consumption between the meters (such as wallbox charging) by dynamically correcting the power values reported to the inverter.

**Primary Goal:** Integrate wallbox power consumption into the SUN2000 inverter's energy management system by correcting DTSU-666 readings to match the L&G reference meter at the grid connection point.

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

## Architecture

**Hardware Setup:**
- ESP32 with dual RS-485 interfaces
- SerialSUN (UART2): Pins 16 (RX), 17 (TX) - connects to SUN2000 inverter
- SerialDTU (UART1): Pins 18 (RX), 19 (TX) - connects to DTSU-666 meter
- MODBUS RTU at 9600 baud, 8N1

**Software Architecture:**
- **Dual-core FreeRTOS design** with thread-safe inter-task communication
- **Core 1:** `simpleProxyTask()` - High-priority MODBUS proxy with power correction
- **Core 0:** `mqttTask()` - MQTT communications with L&G reference meter
- **Uses ModbusRTU485 library** for all MODBUS operations
- **Thread-safe shared memory** with semaphore protection for MQTT data
- **Producer-consumer pattern** with FreeRTOS queues for MQTT publishing
- Two `ModbusRTU485` instances: `modbusSUN` and `modbusDTU`
- Library handles CRC validation, frame parsing, and inter-frame timing
- Listens for requests from SUN2000, validates and proxies to DTSU-666
- **Dynamically corrects DTSU-666 replies** before forwarding to SUN2000
- Only processes requests for Slave ID 11
- Supports MODBUS function codes: 0x03, 0x04, 0x06, 0x10

**Key Functions:**
- `ModbusRTU485::begin()` - Initialize library with serial port and baud rate
- `ModbusRTU485::read()` - Read and parse MODBUS messages with timeout
- `validateMessage()` - Validate message parameters (register counts, etc.)
- `printMessage()` - Pretty-print MODBUS messages for debugging
- `simpleProxyTask()` - Main proxy task with power correction
- `mqttTask()` - MQTT communication handler for L&G meter data
- `calculatePowerCorrection()` - Compare L&G and DTSU readings, apply corrections
- `applyPowerCorrection()` - Modify DTSU reply frames with corrected power values
- `parseDTSU666Reply()` - Parse DTSU-666 IEEE 754 float data
- `encodeDTSU666Reply()` - Re-encode corrected power data back to MODBUS frame

**ModbusRTU485 Library Features:**
- `ModbusMessage` struct contains parsed message details
- `MBType` enum identifies Request/Reply/Exception/Unknown
- Automatic CRC-16 validation
- Proper inter-frame timing (3.5T character gaps)
- Raw frame buffer preserved for forwarding
- Built-in timeout handling

**Power Correction Flow:**
1. **L&G Reference Data:** MQTT task receives power data from L&G meter at grid connection
2. **DTSU Measurement:** MODBUS proxy reads DTSU-666 power data (behind wallbox)
3. **Difference Calculation:** Compare L&G net power vs DTSU net power
4. **Wallbox Detection:** Power difference represents wallbox consumption between meters
5. **Dynamic Correction:** Add difference to DTSU values (distributed equally across 3 phases)
6. **SUN2000 Integration:** Corrected values ensure inverter sees total household consumption

**MODBUS Message Flow:**
1. `modbusSUN.read()` - Waits for SUN2000 request
2. Message validation and filtering (ID 11, Request type)
3. Raw frame forwarding to DTSU-666 via `SerialDTU.write()`
4. `modbusDTU.read()` - Waits for DTSU-666 reply
5. **Power correction applied to reply data if active**
6. Corrected frame forwarding back to SUN2000

**Example Correction:**
- L&G meter: -881W (slight grid export)
- DTSU-666: -5076W (significant export, doesn't see wallbox)
- Wallbox consumption: ~4.2kW (difference between meters)
- Correction applied: +4196W added to DTSU values
- Result: SUN2000 sees accurate total household power flow including wallbox

## Dependencies

- **ModbusRTU485** - Custom MODBUS RTU library (local implementation)
- **PubSubClient** (^2.8) - MQTT client library for L&G meter communication
- **ArduinoJson** (^6.19.4) - JSON processing for MQTT sensor data
- **Arduino framework** on ESP32 platform
- **FreeRTOS** - Real-time operating system for dual-core task management

## Development Notes

- **All low-level MODBUS handling is done by ModbusRTU485 library**
- Library provides clean API abstraction over raw serial communication
- **Thread-safe architecture** with semaphore-protected shared memory
- **Power correction threshold:** 500W minimum difference to activate correction
- **Correction persistence:** Applied to all DTSU messages until next MQTT update
- **Phase distribution:** Power corrections split equally across L1, L2, L3 phases
- **IEEE 754 float handling** for DTSU-666 register parsing and encoding
- Extensive debug logging to Serial (115200 baud) for troubleshooting
- Buffer overflow protection handled by library (512 byte buffers)
- Configurable timeouts for different operations
- Error handling for malformed or oversized MODBUS frames

## System Integration

**Hardware Setup:**
```
Grid ←→ L&G Meter ←→ Wallbox ←→ DTSU-666 ←→ SUN2000 Inverter
        (Reference)     (4.2kW)    (Proxy)      (Solar)
```

**Data Flow:**
- L&G meter publishes power data via MQTT to ESP32
- DTSU-666 measures power behind wallbox (missing wallbox consumption)
- ESP32 calculates difference and applies correction
- SUN2000 receives corrected values including wallbox power