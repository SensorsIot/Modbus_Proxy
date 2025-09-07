//
// File: modbus_sniffer.cpp
// Description: MODBUS RTU Sniffer for ESP32.
//
// This program sniffs all traffic on a RS-485 bus and prints it to the serial console.
// It also tries to decode the messages with a valid CRC.
//
// Hardware setup:
// - One RS-485 board connected to ESP32 pins 16 (RX2),
//   then to the SUN2000 and DTSU-666 bus. 
//

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Pin definitions
#define RS485_SUN2000_RX_PIN 16
#define RS485_SUN2000_TX_PIN 17

// New pin definitions for DTU-666 communication
#define RS485_DTU_RX_PIN 18
#define RS485_DTU_TX_PIN 19

// Baud rate for MODBUS communication
#define MODBUS_BAUDRATE 9600

// Define serial ports for communication
HardwareSerial SerialSUN(2);
HardwareSerial SerialDTU(1); // Using UART1 for DTU-666

// Function to calculate the MODBUS CRC16 (CRC-16/MODBUS)
uint16_t crc16(const uint8_t *buf, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)buf[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Function to get a 32-bit float from a byte array (Modbus big-endian format)
float getFloatFromBytes(const uint8_t* buf) {
    union {
        float f;
        uint8_t b[4];
    } float_union;

    float_union.b[0] = buf[3];
    float_union.b[1] = buf[2];
    float_union.b[2] = buf[1];
    float_union.b[3] = buf[0];
    
    return float_union.f;
}

// =========================================================================
// FreeRTOS Task Prototypes
// =========================================================================
void modbusProxyTask(void *pvParameters);

// =========================================================================
// Main Setup and Loop
// =========================================================================
void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    Serial.println("ESP32 MODBUS Sniffer starting...");

    // Initialize serial port for MODBUS communication
    SerialSUN.begin(MODBUS_BAUDRATE, SERIAL_8N1, RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);

    // Initialize serial port for DTU-666 communication
    SerialDTU.begin(MODBUS_BAUDRATE, SERIAL_8N1, RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);

    // --- FreeRTOS Task Creation ---
    xTaskCreatePinnedToCore(
        modbusProxyTask,
        "ModbusProxyTask",
        4096,
        NULL,
        1,
        NULL,
        1
    );

    Serial.println("FreeRTOS task created.");
}

void loop() {
    vTaskDelay(1000);
}

// =========================================================================
// FreeRTOS Task Implementations
// =========================================================================

void modbusProxyTask(void *pvParameters) {
    (void)pvParameters;
    uint8_t sunBuffer[256];
    uint8_t dtuBuffer[256];
    const unsigned long MODBUS_TIMEOUT_MS = 5; // Modbus RTU inter-frame delay is 3.5 character times.
    const unsigned long DTU_REPLY_TIMEOUT_MS = 100; // Increased timeout for DTU-666 reply

    while (1) {
        // --- Listen for requests from SUN2000 (SerialSUN) ---
        uint8_t sunBufferIndex = 0;
        unsigned long lastByteTime = millis();
        bool messageReceived = false;

        // Serial.println("DEBUG: Waiting for SUN2000 request..."); // REMOVED
        while (SerialSUN.available() > 0 || (millis() - lastByteTime) < MODBUS_TIMEOUT_MS) {
            if (SerialSUN.available()) {
                sunBuffer[sunBufferIndex++] = SerialSUN.read();
                lastByteTime = millis();
                if (sunBufferIndex >= 256) { // Overflow protection
                    Serial.println("SUN2000 buffer overflow!");
                    sunBufferIndex = 0;
                    break;
                }
                messageReceived = true;
            }
        }

        if (messageReceived && sunBufferIndex > 0) {
            Serial.printf("DEBUG: SUN2000 request received, length: %d\n", sunBufferIndex);
            if (sunBufferIndex < 4) { // Minimum Modbus frame size (SlaveID, FunctionCode, CRC_L, CRC_H)
                Serial.println("Short frame from SUN2000, ignoring.");
                continue;
            }

            Serial.println("DEBUG: Calculating SUN2000 CRC...");
            uint16_t receivedCrc = (sunBuffer[sunBufferIndex - 1] << 8) | sunBuffer[sunBufferIndex - 2];
            uint16_t calculatedCrc = crc16(sunBuffer, sunBufferIndex - 2);

            if (receivedCrc == calculatedCrc) {
                Serial.println("DEBUG: SUN2000 CRC valid.");
                uint8_t slaveId = sunBuffer[0];
                uint8_t functionCode = sunBuffer[1];

                Serial.printf("DEBUG: SUN2000 Request - Slave ID: %d, Function Code: 0x%02X\n", slaveId, functionCode); 

                if (slaveId == 11) { // Only process messages for slave ID 11
                    uint16_t startAddress = 0; // Initialize to avoid uninitialized warning
                    uint16_t numRegisters = 0; // Initialize to avoid uninitialized warning

                    // Log raw request bytes before proxying
                    Serial.print("DEBUG: Raw SUN2000 Request: ");
                    for (int i = 0; i < sunBufferIndex; i++) {
                        Serial.printf("%02X ", sunBuffer[i]);
                    }
                    Serial.println();

                    Serial.printf("\n--- MODBUS Request from SUN2000 (Slave ID: %d, Function Code: 0x%02X) ---\n", slaveId, functionCode);

                    bool proxyThisRequest = true; // Flag to control proxying

                    switch (functionCode) {
                        case 0x03: // Read Holding Registers
                        case 0x04: // Read Input Registers
                            if (sunBufferIndex >= 8) { // SlaveID + FunctionCode + StartAddress (2) + NumRegisters (2) + CRC (2)
                                startAddress = (sunBuffer[2] << 8) | sunBuffer[3];
                                numRegisters = (sunBuffer[4] << 8) | sunBuffer[5];
                                Serial.printf("  Type: Read Request\n");
                                Serial.printf("  Starting Address: %d\n", startAddress);
                                Serial.printf("  Number of Registers: %d\n", numRegisters);

                                // Check for excessively large number of registers for a request
                                // Max registers for FC 0x03/0x04 is 125 (250 bytes data + 5 bytes overhead = 255 bytes total)
                                if (numRegisters > 125) {
                                    Serial.printf("  Error: Number of Registers (%d) too large for Modbus read request. Not proxying.\n", numRegisters);
                                    proxyThisRequest = false;
                                }

                            } else {
                                Serial.println("  Error: Malformed Read Request (too short)");
                                proxyThisRequest = false;
                            }
                            break;
                        case 0x06: // Write Single Register
                            if (sunBufferIndex >= 8) { // SlaveID + FunctionCode + RegisterAddress (2) + RegisterValue (2) + CRC (2)
                                startAddress = (sunBuffer[2] << 8) | sunBuffer[3];
                                uint16_t registerValue = (sunBuffer[4] << 8) | sunBuffer[5];
                                Serial.printf("  Type: Write Single Register Request\n");
                                Serial.printf("  Register Address: %d\n", startAddress);
                                Serial.printf("  Register Value: %d\n", registerValue);
                            } else {
                                Serial.println("  Error: Malformed Write Single Register Request (too short)");
                                proxyThisRequest = false;
                            }
                            break;
                        case 0x10: // Write Multiple Registers
                            if (sunBufferIndex >= 9) { // SlaveID + FunctionCode + StartAddress (2) + NumRegisters (2) + ByteCount (1) + Data (N) + CRC (2)
                                startAddress = (sunBuffer[2] << 8) | sunBuffer[3];
                                numRegisters = (sunBuffer[4] << 8) | sunBuffer[5];
                                uint8_t byteCount = sunBuffer[6];
                                Serial.printf("  Type: Write Multiple Registers Request\n");
                                Serial.printf("  Starting Address: %d\n", startAddress);
                                Serial.printf("  Number of Registers: %d\n", numRegisters);
                                Serial.printf("  Byte Count: %d\n", byteCount);
                                Serial.print("  Data: ");
                                for (int i = 0; i < byteCount; i++) {
                                    Serial.printf("%02X ", sunBuffer[7 + i]);
                                }
                                Serial.println();

                                // Check for excessively large number of registers/byte count
                                // Max data bytes for FC 0x10 is 246 (256 - 9 bytes overhead)
                                if (byteCount > 246) {
                                    Serial.printf("  Error: Byte Count (%d) too large for Modbus write request. Not proxying.\n", byteCount);
                                    proxyThisRequest = false;
                                }

                            } else {
                                Serial.println("  Error: Malformed Write Multiple Registers Request (too short)");
                                proxyThisRequest = false;
                            }
                            break;
                        default:
                            Serial.println("  Unknown or Unsupported Function Code for Request");
                            proxyThisRequest = false;
                            break;
                    }
                    Serial.println("--------------------------------------------------");

                    if (proxyThisRequest) {
                        Serial.println("DEBUG: Proxying request to DTU-666...");
                        // --- Proxy the request to DTU-666 (SerialDTU) ---
                        SerialDTU.write(sunBuffer, sunBufferIndex);
                        Serial.println("  Request proxied to DTU-666.");

                        // --- Listen for reply from DTU-666 (SerialDTU) ---
                        uint8_t dtuBufferIndex = 0;
                        lastByteTime = millis();
                        bool dtuMessageReceived = false;

                        Serial.println("DEBUG: Waiting for DTU-666 reply...");
                        while (SerialDTU.available() > 0 || (millis() - lastByteTime) < DTU_REPLY_TIMEOUT_MS) {
                            if (SerialDTU.available()) {
                                dtuBuffer[dtuBufferIndex++] = SerialDTU.read();
                                lastByteTime = millis();
                                if (dtuBufferIndex >= 256) { // Overflow protection
                                    Serial.println("DTU-666 buffer overflow!");
                                    dtuBufferIndex = 0;
                                    break;
                                }
                                dtuMessageReceived = true;
                            }
                        }

                        if (dtuMessageReceived && dtuBufferIndex > 0) {
                            Serial.printf("DEBUG: DTU-666 reply received, length: %d\n", dtuBufferIndex);
                            // Log raw reply bytes even if short or invalid CRC
                            Serial.print("DEBUG: Raw DTU-666 Reply: ");
                            for (int i = 0; i < dtuBufferIndex; i++) {
                                Serial.printf("%02X ", dtuBuffer[i]);
                            }
                            Serial.println();

                            if (dtuBufferIndex < 4) { // Minimum Modbus frame size
                                Serial.println("Short frame from DTU-666, ignoring.");
                                // Do not continue here, as we want to proxy even short replies if they are valid error responses
                            }

                            receivedCrc = (dtuBuffer[dtuBufferIndex - 1] << 8) | dtuBuffer[dtuBufferIndex - 2];
                            calculatedCrc = crc16(dtuBuffer, dtuBufferIndex - 2);

                            if (receivedCrc == calculatedCrc) {
                                Serial.println("DEBUG: DTU-666 CRC valid.");
                                Serial.printf("\n--- MODBUS Reply from DTU-666 (Slave ID: %d, Function Code: 0x%02X) ---\n", dtuBuffer[0], dtuBuffer[1]);
                                // Log the raw reply bytes (already done above as DEBUG)

                                // Basic decoding for read responses (0x03, 0x04)
                                if ((dtuBuffer[1] == 0x03 || dtuBuffer[1] == 0x04) && dtuBufferIndex >= 5) { // SlaveID + FunctionCode + ByteCount + Data + CRC
                                    uint8_t byteCount = dtuBuffer[2];
                                    Serial.printf("  Byte Count: %d\n", byteCount);
                                    Serial.print("  Data: ");
                                    for (int i = 0; i < byteCount; i++) {
                                        Serial.printf("%02X ", dtuBuffer[3 + i]);
                                    }
                                    Serial.println();

                                    // --- Enhanced Decoding based on Register Map ---
                                    // To correctly decode, we need the 'startAddress' and 'numRegisters' from the *original request*.
                                    // This requires storing the request details or passing them. For simplicity here, we'll use the last known values.
                                    // A more robust solution would involve a queue or state machine to link requests to replies.
                                    // For now, we'll assume the reply corresponds to the last proxied request.
                                    
                                    // Note: 'startAddress' and 'numRegisters' are local to the 'if (slaveId == 11)' block.
                                    // To use them here, they would need to be passed or stored globally/in a task context.
                                    // For this example, we'll just show the raw data for now if the original request's details aren't directly accessible.
                                    Serial.println("  (No specific decoding implemented for this register/data type yet)");

                                } else if (dtuBuffer[1] == (functionCode | 0x80)) { // Exception response
                                    Serial.printf("  Exception Code: 0x%02X\n", dtuBuffer[2]);
                                } else {
                                    Serial.println("  (No specific decoding implemented for this function code yet)");
                                }
                                Serial.println("--------------------------------------------------");

                                Serial.println("DEBUG: Proxying reply back to SUN2000...");
                                // --- Proxy the reply back to SUN2000 (SerialSUN) ---
                                SerialSUN.write(dtuBuffer, dtuBufferIndex);
                                Serial.println("  Reply from DTU-666 proxied back to SUN2000.");
                            } else {
                                Serial.printf("  Invalid CRC for DTU-666 reply. Received: 0x%04X, Calculated: 0x%04X. Not forwarding.\n", receivedCrc, calculatedCrc);
                            }
                        } else {
                            Serial.println("  No reply from DTU-666 within timeout.");
                        }
                    }
                } 
            } else {
                Serial.printf("  Invalid CRC for SUN2000 request. Received: 0x%04X, Calculated: 0x%04X. Discarding.\n", receivedCrc, calculatedCrc);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent watchdog timer issues
    }
}