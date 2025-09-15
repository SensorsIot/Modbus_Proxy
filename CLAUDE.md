# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP32-based MODBUS RTU proxy project that acts as a bridge between a SUN2000 solar inverter and a DTSU-666 energy meter. The proxy receives, validates, prints the values in the message, and forwards MODBUS communication between the two devices over RS-485.

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
- Single FreeRTOS task (`modbusProxyTask`) handles all communication
- **Uses ModbusRTU485 library for all MODBUS operations**
- Two `ModbusRTU485` instances: `modbusSUN` and `modbusDTU`
- Library handles CRC validation, frame parsing, and inter-frame timing
- Listens for requests from SUN2000, validates and proxies to DTSU-666
- Receives replies from DTU-666, forwards back to SUN2000
- Only processes requests for Slave ID 11
- Supports MODBUS function codes: 0x03, 0x04, 0x06, 0x10

**Key Functions:**
- `ModbusRTU485::begin()` - Initialize library with serial port and baud rate
- `ModbusRTU485::read()` - Read and parse MODBUS messages with timeout
- `validateMessage()` - Validate message parameters (register counts, etc.)
- `printMessage()` - Pretty-print MODBUS messages for debugging
- `modbusProxyTask()` - Main proxy task using library API

**ModbusRTU485 Library Features:**
- `ModbusMessage` struct contains parsed message details
- `MBType` enum identifies Request/Reply/Exception/Unknown
- Automatic CRC-16 validation
- Proper inter-frame timing (3.5T character gaps)
- Raw frame buffer preserved for forwarding
- Built-in timeout handling

**Message Flow:**
1. `modbusSUN.read()` - Waits for SUN2000 request
2. Message validation and filtering (ID 11, Request type)
3. Raw frame forwarding to DTU-666 via `SerialDTU.write()`
4. `modbusDTU.read()` - Waits for DTU-666 reply
5. Raw frame forwarding back to SUN2000

## Dependencies

- **ModbusRTU485** - Custom MODBUS RTU library (local implementation)
- **PubSubClient** (^2.8) - MQTT client library
- **ArduinoJson** (^6.19.4) - JSON processing
- **Arduino framework** on ESP32 platform

## Development Notes

- **All low-level MODBUS handling is done by ModbusRTU485 library**
- Library provides clean API abstraction over raw serial communication
- Extensive debug logging to Serial (115200 baud) for troubleshooting
- Buffer overflow protection handled by library (512 byte buffers)
- Configurable timeouts for different operations
- Error handling for malformed or oversized MODBUS frames