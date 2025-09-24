# ESP32 MODBUS RTU Intelligent Proxy - Functional Specification Document

**Version:** 2.0
**Date:** September 2025
**Author:** Andreas Spiess / Claude Code

---

## 1. System Overview

### 1.1 Purpose
The ESP32 MODBUS RTU Intelligent Proxy is a sophisticated power monitoring system that sits between a SUN2000 solar inverter and a DTSU-666 energy meter, providing real-time power correction by integrating wallbox charging data from an EVCC system. The system ensures accurate power flow measurements by compensating for consumption between metering points.

### 1.2 Primary Goals
- **Intelligent Power Correction**: Integrate wallbox power consumption into solar inverter energy management
- **Transparent MODBUS Proxy**: Seamless bidirectional communication between SUN2000 and DTSU-666
- **System Reliability**: Comprehensive auto-restart and health monitoring capabilities
- **Real-time Monitoring**: MQTT-based error reporting and system health status

---

## 2. System Architecture

### 2.1 Hardware Architecture

```
Grid ←→ L&G Meter ←→ Wallbox ←→ DTSU-666 ←→ SUN2000 Inverter
        (Reference)   (4.2kW)    (Proxy)      (Solar)
                         ↑
                      ESP32 Proxy
                    (WiFi/HTTP/MQTT)
                         ↓
                     EVCC System
```

**Hardware Components:**
- **ESP32**: Dual-core microcontroller with WiFi capability
- **Dual RS-485 Interfaces**:
  - UART2 (Pins 16 RX, 17 TX): SUN2000 inverter communication
  - UART1 (Pins 18 RX, 19 TX): DTSU-666 energy meter communication
- **Communication Protocol**: MODBUS RTU at 9600 baud, 8N1

### 2.2 Software Architecture

```
Core 0:                          Core 1:
├── MQTT Task (Priority 1)       ├── Proxy Task (Priority 2)
│   ├── EVCC API polling (10s)   │   ├── MODBUS proxy
│   └── MQTT publishing          │   ├── Power correction
├── Watchdog Task (Priority 3)   │   ├── Serial output (immediate):
│   ├── Health monitoring (5s)   │   │   - DTSU: -5.2W
│   ├── Failure detection        │   │   - API: 1840W (valid)
│   ├── MQTT error reporting     │   │   - SUN2000: 1834.8W (calc)
│   └── Auto-restart triggers    │   └── Heartbeat updates
```

**Task Design:**
- **Multi-core FreeRTOS** with independent task monitoring
- **Thread-safe communication** using mutexes and semaphores
- **Producer-consumer patterns** with FreeRTOS queues
- **Cross-core watchdog** for comprehensive fault detection

---

## 3. Communication Protocols

### 3.1 MODBUS RTU Specification
- **Slave ID**: 11 (DTSU-666 meter)
- **Function Codes**: 0x03 (Read Holding), 0x04 (Read Input), 0x06 (Write Single), 0x10 (Write Multiple)
- **Register Range**: 2102-2181 (80 registers, 160 bytes of IEEE 754 data)
- **Data Format**: Big-endian IEEE 754 32-bit floats
- **CRC Validation**: Automatic validation and recalculation after modifications

### 3.2 EVCC HTTP API
- **URL**: `http://192.168.0.202:7070/api/state`
- **Method**: GET with JSON response
- **Data Path**: `loadpoints[0].chargePower`
- **Polling Interval**: 10 seconds
- **Error Handling**: Automatic retry with failure counting

### 3.3 MQTT Communication
**Published Topics:**
- `MBUS-PROXY/DATA`: Corrected power data with timestamps
- `MBUS-PROXY/ERROR`: Error events with context and error codes
- `MBUS-PROXY/HEALTH`: System health status (every 30 seconds)

---

## 4. Data Structures

### 4.1 DTSU666Data Structure
**40 IEEE 754 Float Values:**
- **Current Measurements**: `current_L1`, `current_L2`, `current_L3`
- **Voltage Measurements**: `voltage_L1N`, `voltage_L2N`, `voltage_L3N`, `voltage_LN_avg`
- **Power Values**: `power_total`, `power_L1`, `power_L2`, `power_L3`
- **Reactive Power**: `reactive_total`, `reactive_L1`, `reactive_L2`, `reactive_L3`
- **Apparent Power**: `apparent_total`, `apparent_L1`, `apparent_L2`, `apparent_L3`
- **Power Factor**: `pf_total`, `pf_L1`, `pf_L2`, `pf_L3`
- **Energy Counters**: Import/Export totals and per-phase values
- **Frequency**: `frequency`

### 4.2 Thread-Safe Shared Structures

**SharedEVCCData:**
```cpp
struct SharedEVCCData {
    SemaphoreHandle_t mutex;
    float chargePower;           // Wallbox charging power (W)
    uint32_t timestamp;          // Last update timestamp
    bool valid;                  // Data validity flag
    uint32_t updateCount;        // Successful update counter
    uint32_t errorCount;         // Error counter
};
```

**SystemHealth:**
```cpp
struct SystemHealth {
    uint32_t proxyTaskLastSeen;     // Proxy task heartbeat
    uint32_t mqttTaskLastSeen;      // MQTT task heartbeat
    uint32_t evccConsecutiveFailures; // EVCC API failure counter
    uint32_t mqttConsecutiveFailures; // MQTT failure counter
    uint32_t modbusConsecutiveFailures; // MODBUS failure counter
    uint32_t totalRestarts;         // System restart counter
    uint32_t lastHealthCheck;      // Last health assessment
    bool systemHealthy;            // Overall system status
};
```

---

## 5. Power Correction Algorithm

### 5.1 Correction Process
1. **Data Acquisition**: EVCC API polling every 10 seconds for wallbox power
2. **Threshold Check**: Apply correction only if `|chargePower| > 1000W`
3. **Power Integration**: Add wallbox power to DTSU readings
4. **Phase Distribution**: Distribute correction proportionally across L1/L2/L3
5. **IEEE 754 Encoding**: Direct byte-level modification of MODBUS response
6. **CRC Recalculation**: Update MODBUS frame CRC after data modification

### 5.2 Correction Logic
- **Objective**: Compensate for power consumption between DTSU-666 and grid connection point
- **Method**: Add wallbox charging power to DTSU measurements
- **Distribution**: Proportional across three phases based on existing load ratios
- **Persistence**: Correction applied until next EVCC API update

### 5.3 Example Correction
```
Original DTSU reading: -5076W (significant export, doesn't see wallbox)
Wallbox power: +4200W (charging)
Corrected value: -876W (net household consumption visible to SUN2000)
Result: SUN2000 sees accurate power flow including wallbox load
```

---

## 6. Auto-Restart & Health Monitoring System

### 6.1 Independent Watchdog Architecture
- **Watchdog Task**: Runs on Core 0 with highest priority (3)
- **Monitoring Frequency**: Health checks every 5 seconds
- **Cross-Core Monitoring**: Independent monitoring of all system tasks
- **Graceful Recovery**: 5-second delay for error transmission before restart

### 6.2 Failure Detection Thresholds
- **Task Watchdog**: 60 seconds without heartbeat → System restart
- **EVCC API**: 20 consecutive failures → System restart
- **MQTT**: 50 consecutive failures → System restart
- **MODBUS**: 10 consecutive failures → System restart
- **Memory**: < 20KB free heap → System restart

### 6.3 Health Monitoring Features
- **Real-time Error Reporting**: MQTT notifications with context
- **System Metrics**: Heap usage, uptime, restart counters
- **Failure Analysis**: Consecutive failure tracking per subsystem
- **Recovery Tracking**: Restart frequency monitoring

---

## 7. Error Handling & Recovery

### 7.1 Graceful Degradation
- **Power Correction Fallback**: Raw proxy mode if EVCC API unavailable
- **MQTT Resilience**: Queue overflow protection and connection retry
- **Memory Protection**: Heap monitoring with automatic restart
- **Communication Fallback**: Direct proxy operation if correction fails

### 7.2 Error Reporting System
**Serial Console Output:**
The system provides clean 3-line status output immediately after power correction calculation in the proxy task:
```
DTSU: -5.2W
API: 1840W (valid)
SUN2000: 1834.8W (DTSU -5.2W + correction 1840W)
```
This output is generated synchronously in the proxy loop on Core 1, not as a separate task or operation.

**MQTT Error Format:**
```json
{
    "timestamp": 123456,
    "error_type": "EVCC_API",
    "error_message": "HTTP error",
    "error_code": 404,
    "free_heap": 45000,
    "uptime": 3600,
    "restart_count": 2
}
```

---

## 8. Configuration Parameters

### 8.1 System Constants
```cpp
// Hardware Configuration
RS485_SUN2000_RX_PIN = 16, RS485_SUN2000_TX_PIN = 17
RS485_DTU_RX_PIN = 18, RS485_DTU_TX_PIN = 19
MODBUS_BAUDRATE = 9600

// Power Correction
CORRECTION_THRESHOLD = 1000.0f  // Minimum wallbox power (W)

// Timing Configuration
HTTP_POLL_INTERVAL = 10000      // EVCC API polling (ms)
WATCHDOG_TIMEOUT_MS = 60000     // Task heartbeat timeout (ms)
HEALTH_CHECK_INTERVAL_MS = 30000 // Health status reporting (ms)

// Failure Thresholds
EVCC_FAILURE_THRESHOLD = 20     // API failures before restart
MQTT_FAILURE_THRESHOLD = 50     // MQTT failures before restart
MODBUS_FAILURE_THRESHOLD = 10   // MODBUS failures before restart
MIN_FREE_HEAP = 20000          // Memory threshold (bytes)
```

### 8.2 Network Configuration
```cpp
// EVCC API
const char* evccApiUrl = "http://192.168.0.202:7070/api/state";

// MQTT Topics
"MBUS-PROXY/DATA"    // Corrected power data
"MBUS-PROXY/ERROR"   // Error notifications
"MBUS-PROXY/HEALTH"  // System health status
```

---

## 9. Performance Specifications

### 9.1 Timing Requirements
- **MODBUS Proxy Response**: < 500ms end-to-end
- **Power Correction Application**: < 50ms processing time
- **EVCC API Response**: 5 second timeout
- **Health Check Frequency**: Every 5 seconds
- **MQTT Publishing**: Real-time with queue buffering

### 9.2 Memory Requirements
- **Total RAM Usage**: ~49KB (15% of 327KB available)
- **Flash Usage**: ~937KB (71.5% of 1310KB available)
- **Stack Allocation**:
  - Proxy Task: 4096 bytes
  - MQTT Task: 3072 bytes
  - Watchdog Task: 2048 bytes

### 9.3 Reliability Metrics
- **System Uptime**: Continuously monitored and reported
- **Error Recovery**: Automatic restart on critical failures
- **Data Integrity**: CRC validation on all MODBUS communications
- **Thread Safety**: Mutex-protected shared data structures

---

## 10. Operational Characteristics

### 10.1 Normal Operation
1. **Startup Sequence**: Hardware initialization → Task creation → Health monitoring active
2. **Proxy Operation**: Continuous SUN2000 ↔ DTSU-666 message forwarding
3. **Power Correction**: Real-time EVCC API data integration
4. **Health Monitoring**: Continuous watchdog supervision and MQTT reporting

### 10.2 Fault Conditions
- **API Unavailable**: Power correction disabled, raw proxy continues
- **MQTT Disconnected**: Local error logging, connection retry
- **Memory Issues**: Automatic system restart with error reporting
- **Task Hang**: Watchdog detection and system restart

### 10.3 Monitoring & Diagnostics
- **Real-time Output**: Clean 3-line status display (immediate in proxy task)
- **MQTT Telemetry**: Comprehensive system health data (every 30s)
- **Error Tracking**: Consecutive failure counters per subsystem
- **Performance Metrics**: Response times, memory usage, uptime tracking
- **Serial Console**: Immediate status updates in proxy processing loop

---

## 11. Dependencies & Libraries

### 11.1 Core Libraries
- **ModbusRTU485**: Custom MODBUS RTU implementation with CRC validation
- **ArduinoJson** (^6.19.4): JSON processing for API and MQTT data
- **PubSubClient** (^2.8): MQTT client library
- **HTTPClient** (2.0.0): EVCC API communication
- **WiFi** (2.0.0): Network connectivity

### 11.2 Development Environment
- **Platform**: ESP32 Arduino Framework
- **Build System**: PlatformIO
- **RTOS**: FreeRTOS (dual-core task management)
- **Compiler**: GCC for ESP32

---

## 12. Future Enhancements

### 12.1 Planned Features
- **Web Interface**: Configuration and monitoring dashboard
- **Historical Data**: Power correction trend analysis
- **Advanced Algorithms**: Predictive power correction based on usage patterns
- **Multiple Wallbox Support**: Support for additional charging points

### 12.2 Scalability Considerations
- **Modular Design**: Easy addition of new data sources
- **Protocol Extensions**: Support for additional MODBUS function codes
- **Multi-Meter Support**: Expansion to handle multiple energy meters
- **Cloud Integration**: Remote monitoring and configuration capabilities

---

*This document reflects the current implementation as of September 2025 and serves as the authoritative specification for the ESP32 MODBUS RTU Intelligent Proxy system.*