/**
 * FG6485A Temperature & Humidity Reader
 *
 * Reads temperature and humidity from an ASAIR FG6485A sensor over Modbus RTU
 * via the M5Stack Atomic RS485 Base.
 *
 * Hardware:
 *   - M5Stack Atom Lite (ESP32-PICO-D4)
 *   - Atomic RS485 Base
 *     - G22: UART2 RX  ← RS485 RO
 *     - G19: UART2 TX  → RS485 DI (direction auto-controlled by HW transistor)
 *
 * FG6485A Modbus RTU settings:
 *   - Baud rate : 9600, 8N1
 *   - Slave addr: 0x01 (factory default, set by DIP switch on sensor)
 *   - Reg 0x0000: Humidity    (value × 10, e.g. 471 → 47.1 %RH)
 *   - Reg 0x0001: Temperature (value × 10, e.g. 214 → 21.4 °C)
 *
 * Output: Serial (115200 baud) every 5 seconds.
 */

#include <Arduino.h>
#include <FastLED.h>

// === Pin definitions ===
#define LED_PIN     27    // WS2812B on Atom Lite
#define NUM_LEDS    1
#define RS485_RX    22    // UART2 RX (Atomic RS485 Base labelled RX)
#define RS485_TX    19    // UART2 TX (Atomic RS485 Base labelled TX)

// === FG6485A Modbus settings ===
#define FG6485A_SLAVE_ADDR  0x01   // Set by DIP switch on sensor (default = 1)
#define FG6485A_BAUD_RATE   9600
#define READ_INTERVAL_MS    5000UL // 5-second polling interval

CRGB leds[NUM_LEDS];

// ---------------------------------------------------------------------------
// CRC-16/IBM (Modbus RTU)
// ---------------------------------------------------------------------------
static uint16_t modbusCRC16(const uint8_t *buf, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Read holding registers from FG6485A (function code 0x03)
// Reads 2 registers starting at 0x0000: humidity (0x0000), temperature (0x0001)
// Returns true on success.
// ---------------------------------------------------------------------------
static bool readFG6485A(float &outTemperature, float &outHumidity) {
    // Build 8-byte Modbus RTU request frame
    uint8_t request[8];
    request[0] = FG6485A_SLAVE_ADDR;
    request[1] = 0x03;  // Read Holding Registers
    request[2] = 0x00;  // Start address high byte
    request[3] = 0x00;  // Start address low byte (0x0000 = humidity)
    request[4] = 0x00;  // Quantity high byte
    request[5] = 0x02;  // Quantity low byte (2 registers)
    uint16_t crc = modbusCRC16(request, 6);
    request[6] = (uint8_t)(crc & 0xFF);
    request[7] = (uint8_t)(crc >> 8);

    // Flush stale RX data (may include TX echo from RS485 half-duplex)
    while (Serial2.available()) {
        Serial2.read();
    }

    // Transmit — hardware transistor on RS485 Base auto-enables the driver
    Serial2.write(request, sizeof(request));
    Serial2.flush();  // Block until last byte is shifted out

    // Discard echo of our own transmission (RX may mirror TX during send)
    delay(5);
    while (Serial2.available()) {
        Serial2.read();
    }

    // Wait for 9-byte response: [addr, func, byte_count(4), hum_H, hum_L, tmp_H, tmp_L, crc_L, crc_H]
    const uint8_t RESPONSE_LEN = 9;
    uint32_t deadline = millis() + 500;
    while (Serial2.available() < RESPONSE_LEN && millis() < deadline) {
        delay(1);
    }

    if (Serial2.available() < RESPONSE_LEN) {
        Serial.println("[ERROR] Timeout — no response from FG6485A");
        return false;
    }

    uint8_t response[RESPONSE_LEN];
    for (uint8_t i = 0; i < RESPONSE_LEN; i++) {
        response[i] = (uint8_t)Serial2.read();
    }

    // Validate CRC
    uint16_t rxCRC   = response[7] | ((uint16_t)response[8] << 8);
    uint16_t calcCRC = modbusCRC16(response, 7);
    if (rxCRC != calcCRC) {
        Serial.printf("[ERROR] CRC mismatch — received 0x%04X, expected 0x%04X\n",
                      rxCRC, calcCRC);
        return false;
    }

    // Validate slave address and function code
    if (response[0] != FG6485A_SLAVE_ADDR) {
        Serial.printf("[ERROR] Wrong slave address: 0x%02X\n", response[0]);
        return false;
    }
    if (response[1] == 0x83) {
        Serial.printf("[ERROR] Modbus exception code: 0x%02X\n", response[2]);
        return false;
    }
    if (response[1] != 0x03 || response[2] != 0x04) {
        Serial.printf("[ERROR] Unexpected response frame: func=0x%02X count=%u\n",
                      response[1], response[2]);
        return false;
    }

    // Parse data — big-endian, actual value = raw / 10
    int16_t rawHumidity    = (int16_t)((response[3] << 8) | response[4]);
    int16_t rawTemperature = (int16_t)((response[5] << 8) | response[6]);

    outHumidity    = rawHumidity    / 10.0f;
    outTemperature = rawTemperature / 10.0f;

    return true;
}

// ---------------------------------------------------------------------------
// LED helpers
// ---------------------------------------------------------------------------
static void setLED(CRGB color) {
    leds[0] = color;
    FastLED.show();
}

static void flashLED(CRGB color, uint16_t ms = 150) {
    setLED(color);
    delay(ms);
    setLED(CRGB::Black);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(50);
    setLED(CRGB::Blue);  // Blue = initialising

    Serial2.begin(FG6485A_BAUD_RATE, SERIAL_8N1, RS485_RX, RS485_TX);

    delay(500);  // Allow sensor to power up
    setLED(CRGB::Black);

    Serial.println();
    Serial.println("===================================");
    Serial.println("  FG6485A Temperature/Humidity");
    Serial.println("  Modbus RTU via Atomic RS485 Base");
    Serial.println("===================================");
    Serial.printf("  Slave address : 0x%02X\n", FG6485A_SLAVE_ADDR);
    Serial.printf("  Baud rate     : %d\n",      FG6485A_BAUD_RATE);
    Serial.printf("  Poll interval : %lu s\n",   READ_INTERVAL_MS / 1000UL);
    Serial.println("===================================");
    Serial.println();
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
void loop() {
    static uint32_t lastRead = 0;

    if (millis() - lastRead >= READ_INTERVAL_MS) {
        lastRead = millis();

        float temperature = 0.0f;
        float humidity    = 0.0f;

        if (readFG6485A(temperature, humidity)) {
            Serial.println("-------- FG6485A Readings --------");
            Serial.printf("  Temperature : %6.1f °C\n",  temperature);
            Serial.printf("  Humidity    : %6.1f %%RH\n", humidity);
            Serial.println("----------------------------------");
            Serial.println();
            flashLED(CRGB::Green);  // Green flash = success
        } else {
            flashLED(CRGB::Red);    // Red flash = communication error
        }
    }
}
