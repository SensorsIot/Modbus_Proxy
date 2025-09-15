# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-based intelligent MODBUS RTU proxy that acts as a bridge between a SUN2000 solar inverter and a DTSU-666 energy meter. The system provides real-time power correction by comparing measurements from two independent meters to ensure optimal solar generation control.

**Core Features:**
- **Bidirectional MODBUS Proxy**: Forwards requests/replies between SUN2000 ↔ DTSU-666
- **Power Measurement Correction**: Compensates for loads between meters using MQTT reference data
- **MQTT Integration**: Receives reference power data from Landis & Gyr meter
- **IEEE 754 Float Processing**: Parses and modifies DTSU-666 measurement data
- **Production-Ready Logging**: Clean, focused output for monitoring

**Power Correction Algorithm:**
When MQTT data arrives, calculates difference between L&G meter and DTSU-666. If difference > 500W, adds this correction to DTSU power values before forwarding to SUN2000. This ensures SUN2000 sees complete power consumption for optimal solar control.

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
- **Simple Direct Proxy**: Single `simpleProxyTask()` handles all communication
- **ModbusRTU485 Library**: Custom library for all MODBUS operations
- **MQTT Task**: Separate task handles WiFi/MQTT communication for power reference data
- **Power Correction Engine**: Real-time modification of DTSU-666 responses
- **IEEE 754 Float Processing**: Parses and modifies 40 float values from DTSU-666
- **Thread-Safe Operation**: Mutex-protected shared data structures
- Only processes requests for Slave ID 11
- Supports MODBUS function codes: 0x03, 0x04, 0x06, 0x10

**Key Functions:**
- `simpleProxyTask()` - Main proxy loop with power correction
- `parseDTSU666Reply()` - Parse 160-byte IEEE 754 float payload from DTSU-666
- `applyPowerCorrections()` - Apply MQTT-based power corrections
- `encodeDTSU666Reply()` - Re-encode corrected data for SUN2000
- `onMqttMessage()` - Process incoming Landis & Gyr meter data
- `calculatePowerCorrection()` - Determine power adjustment based on meter difference

**ModbusRTU485 Library Features:**
- `ModbusMessage` struct contains parsed message details
- `MBType` enum identifies Request/Reply/Exception/Unknown
- Automatic CRC-16 validation and recalculation
- Proper inter-frame timing (3.5T character gaps)
- Raw frame buffer preserved for modification
- Built-in timeout handling

**Enhanced Message Flow:**
1. `modbusSUN.read()` - Waits for SUN2000 request
2. Forward request to DTSU-666 via `modbusDTU.write()`
3. `modbusDTU.read()` - Receives DTSU-666 reply
4. `parseDTSU666Reply()` - Parse IEEE 754 float measurements
5. `applyPowerCorrections()` - Apply MQTT-based power corrections
6. `encodeDTSU666Reply()` - Re-encode with CRC recalculation
7. Forward corrected response to SUN2000

## Dependencies

- **ModbusRTU485** - Custom MODBUS RTU library (local implementation)
- **PubSubClient** (^2.8) - MQTT client library
- **ArduinoJson** (^6.19.4) - JSON processing for MQTT sensor data
- **WiFi** - ESP32 WiFi connectivity for MQTT
- **Arduino framework** on ESP32 platform
- **credentials.h** - WiFi and MQTT configuration (create from example)

## Data Structures

**DTSU666Data**: Parsed 40 IEEE 754 float values from DTSU-666 (registers 2102-2181)
- Current, voltage, frequency measurements
- Active/reactive/apparent power (per phase and total)
- Power factor, demand, energy import/export

**MQTTSensorData**: Landis & Gyr meter data from MQTT
- Power import/export (kW), current (A), voltage (V)
- Used as reference for power correction calculations

**Power Correction Logic**:
- Threshold: 500W difference between meters
- Proportional distribution across L1/L2/L3 phases
- Persistent correction until next MQTT update

## Development Notes

- **Production-ready logging**: Streamlined output for monitoring
- **IEEE 754 float processing**: Big-endian 32-bit float parsing/encoding
- **Power sign convention**: DTSU-666 controls sign (negative = importing)
- **DTSU-666 register map**: 2102-2181 (40 float values, 80 registers)
- **CRC recalculation**: Automatic after power corrections applied
- **Thread-safe MQTT**: Separate task with mutex protection
- **Error handling**: Graceful degradation when MQTT unavailable