/**
 * Modbus RTU Address Scanner
 *
 * Sweeps all valid Modbus slave addresses (1–247) and reports every device
 * that replies.  A probe frame (FC 0x03, read 1 holding register at 0x0000)
 * is sent to each address.  Any valid Modbus response — including an exception
 * reply (0x83) — confirms a device is present at that address.
 *
 * Hardware:
 *   - M5Stack Atom Lite (ESP32-PICO-D4)
 *   - Atomic RS485 Base
 *     - G22: UART2 RX  ← RS485 RO
 *     - G19: UART2 TX  → RS485 DI (direction auto-controlled by HW transistor)
 *
 * Serial output: 115200 baud
 * RS485 baud   : SCAN_BAUD_RATE (default 9600, change below)
 *
 * LED colours:
 *   Blue      — scanning in progress
 *   Green     — device found (brief flash)
 *   White     — scan complete, results printed
 *   Red flash — bus error detected during scan
 */

#include <Arduino.h>
#include <FastLED.h>

// === Pin definitions ===
#define LED_PIN     27
#define NUM_LEDS    1
#define RS485_RX    22
#define RS485_TX    19

// === Scanner settings — adjust as needed ===
#define SCAN_BAUD_RATE    9600   // Match your device(s)
#define RESPONSE_TIMEOUT  300    // ms to wait for a reply per address
#define INTER_FRAME_MS    10     // ms gap between probe frames (bus settling)

// Modbus valid address range
#define MODBUS_ADDR_MIN   1
#define MODBUS_ADDR_MAX   247

CRGB leds[NUM_LEDS];

// ---------------------------------------------------------------------------
// CRC-16/IBM (Modbus RTU)
// ---------------------------------------------------------------------------
static uint16_t modbusCRC16(const uint8_t *buf, uint8_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x0001) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// LED helpers
// ---------------------------------------------------------------------------
static void setLED(CRGB color)
{
    leds[0] = color;
    FastLED.show();
}

static void flashLED(CRGB color, uint16_t ms = 120)
{
    setLED(color);
    delay(ms);
    setLED(CRGB::Blue);  // Return to "scanning" colour
}

// ---------------------------------------------------------------------------
// Probe a single Modbus address.
// Sends FC 0x03 (read 1 holding register at 0x0000).
// Returns:
//   1  — device responded with a valid Modbus frame (even an exception)
//   0  — timeout / no response
//  -1  — framing or CRC error on received data
// ---------------------------------------------------------------------------
static int8_t probeAddress(uint8_t addr)
{
    // Build request
    uint8_t req[8];
    req[0] = addr;
    req[1] = 0x03;  // Read Holding Registers
    req[2] = 0x00;  // Register address high
    req[3] = 0x00;  // Register address low
    req[4] = 0x00;  // Quantity high
    req[5] = 0x01;  // Quantity low (1 register)
    uint16_t crc = modbusCRC16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    // Flush any stale / echo bytes
    while (Serial2.available()) Serial2.read();

    // Transmit
    Serial2.write(req, sizeof(req));
    Serial2.flush();

    // Discard TX echo — wait long enough for all 8 echo bytes to arrive at
    // 9600 baud (~8.3 ms each way) then drain exactly that many bytes.
    // Using a fixed count rather than "drain everything" avoids accidentally
    // eating a fast sensor response.
    delay(12);  // > 8.3 ms bit-time + line turnaround margin
    uint8_t echoBytesToDiscard = Serial2.available();
    if (echoBytesToDiscard > sizeof(req)) echoBytesToDiscard = sizeof(req);
    for (uint8_t i = 0; i < echoBytesToDiscard; i++) Serial2.read();

    // Wait for at least 4 bytes (minimum Modbus exception frame: addr+func+code+crc_L+crc_H = 5)
    const uint8_t MIN_RESPONSE = 5;
    uint32_t deadline = millis() + RESPONSE_TIMEOUT;
    while (Serial2.available() < MIN_RESPONSE && millis() < deadline) {
        delay(1);
    }

    if (Serial2.available() < MIN_RESPONSE) {
        return 0;  // Timeout
    }

    // Drain all available bytes (up to 256 — safety cap)
    uint8_t buf[256];
    uint8_t received = 0;
    uint32_t silenceStart = millis();
    while (millis() - silenceStart < 20 && received < sizeof(buf)) {
        if (Serial2.available()) {
            buf[received++] = (uint8_t)Serial2.read();
            silenceStart = millis();
        }
    }

    if (received < MIN_RESPONSE) return 0;

    // Debug: print raw bytes for every response received (remove once working)
    Serial.printf("  [DBG] addr %3u got %u byte(s):", addr, received);
    for (uint8_t i = 0; i < received; i++) Serial.printf(" %02X", buf[i]);
    Serial.println();

    // Validate: first byte must echo the address we sent
    if (buf[0] != addr) return -1;

    // Validate CRC of the received frame
    uint16_t rxCRC   = buf[received - 2] | ((uint16_t)buf[received - 1] << 8);
    uint16_t calcCRC = modbusCRC16(buf, received - 2);
    if (rxCRC != calcCRC) return -1;

    // Any valid frame (normal reply or Modbus exception) counts as "found"
    return 1;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(50);
    setLED(CRGB::Blue);

    Serial2.begin(SCAN_BAUD_RATE, SERIAL_8N1, RS485_RX, RS485_TX);
    delay(500);

    Serial.println();
    Serial.println("============================================");
    Serial.println("  Modbus RTU Address Scanner");
    Serial.println("  M5Stack Atom + Atomic RS485 Base");
    Serial.println("============================================");
    Serial.printf("  Baud rate     : %d\n",     SCAN_BAUD_RATE);
    Serial.printf("  Address range : %d – %d\n", MODBUS_ADDR_MIN, MODBUS_ADDR_MAX);
    Serial.printf("  Timeout/addr  : %d ms\n",  RESPONSE_TIMEOUT);
    Serial.println("============================================");
    Serial.println("  Scanning...");
    Serial.println();
}

// ---------------------------------------------------------------------------
// Loop — runs the scan once, then prints results and halts
// ---------------------------------------------------------------------------
void loop()
{
    static bool scanDone = false;
    if (scanDone) {
        delay(10000);  // Idle — results already printed
        return;
    }
    scanDone = true;

    uint8_t found[MODBUS_ADDR_MAX - MODBUS_ADDR_MIN + 1];
    uint8_t foundCount = 0;

    for (uint16_t addr = MODBUS_ADDR_MIN; addr <= MODBUS_ADDR_MAX; addr++) {

        // Progress tick every 10 addresses
        if (addr % 10 == 0) {
            Serial.printf("  Probing address %3u / %u ...\r", addr, MODBUS_ADDR_MAX);
        }

        int8_t result = probeAddress((uint8_t)addr);

        if (result == 1) {
            found[foundCount++] = (uint8_t)addr;
            Serial.printf("\n  [FOUND] Device at address %3u (0x%02X)\n", addr, addr);
            flashLED(CRGB::Green, 200);
        } else if (result == -1) {
            // Got bytes but CRC/address mismatch — note it but keep scanning
            Serial.printf("\n  [NOISE] Garbled response at address %3u (0x%02X) — possible bus contention\n",
                          addr, addr);
            flashLED(CRGB::Red, 80);
        }

        delay(INTER_FRAME_MS);
    }

    // ---- Print summary ----
    Serial.println();
    Serial.println("============================================");
    Serial.println("  SCAN COMPLETE");
    Serial.println("============================================");
    if (foundCount == 0) {
        Serial.println("  No devices found.");
        Serial.println("  Check wiring, baud rate, and RS485 termination.");
    } else {
        Serial.printf("  Found %u device(s):\n\n", foundCount);
        for (uint8_t i = 0; i < foundCount; i++) {
            Serial.printf("    Address %3u  (0x%02X)\n", found[i], found[i]);
        }
    }
    Serial.println("============================================");
    Serial.println();

    setLED(CRGB::White);
}
