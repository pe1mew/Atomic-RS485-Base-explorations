/**
 * Modbus RTU Receiver / Bus Sniffer
 *
 * Passively listens on the RS485 bus and prints every frame it observes to
 * the USB serial port.  Nothing is transmitted on the bus — the sketch only
 * reads.  Useful for debugging Modbus networks without disturbing traffic.
 *
 * Hardware:
 *   - M5Stack Atom Lite (ESP32-PICO-D4)
 *   - Atomic RS485 Base
 *     - G22: UART2 RX  ← RS485 RO
 *     - G19: UART2 TX  → RS485 DI (unused — never written)
 *
 * Serial output : 115200 baud
 * RS485 baud    : MODBUS_BAUD_RATE (default 9600, change below)
 *
 * Frame detection: Modbus RTU uses 3.5 character-times of bus silence to mark
 * the end of a frame.  At 9600 baud one character = ~1.04 ms, so the timeout
 * is set to 5 ms (generous, works up to ~19200 baud without change).
 *
 * LED colours:
 *   Blue        — idle, waiting for traffic
 *   Green flash — valid frame received (CRC OK)
 *   Red flash   — frame received with CRC error
 *   Yellow flash— frame too short to be a valid Modbus frame (< 4 bytes)
 */

#include <Arduino.h>
#include <FastLED.h>

// === Pin definitions ===
#define LED_PIN     27
#define NUM_LEDS    1
#define RS485_RX    22
#define RS485_TX    19

// === Receiver settings ===
#define MODBUS_BAUD_RATE   9600   // Must match the bus being monitored
// Inter-frame silence threshold in microseconds.
// Set to 1.75 character-times — above the 1.5-char intra-frame maximum and
// below the 3.5-char inter-frame minimum required by Modbus RTU.
// Formula: 1.75 * 10 bits * 1,000,000 us/s / baud
#define FRAME_TIMEOUT_US   (17500000UL / MODBUS_BAUD_RATE)
#define MAX_FRAME_BYTES    256    // Maximum frame buffer size

CRGB leds[NUM_LEDS];

// ---------------------------------------------------------------------------
// CRC-16/IBM (Modbus RTU)
// ---------------------------------------------------------------------------
static uint16_t modbusCRC16(const uint8_t *buf, uint16_t len)
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

// Non-blocking LED flash — call scheduleLED() to start a flash, then
// serviceLED() every loop iteration to turn it off after the duration expires.
static uint32_t ledOffAt = 0;

static void scheduleLED(CRGB color, uint16_t ms = 80)
{
    setLED(color);
    ledOffAt = millis() + ms;
}

static void serviceLED()
{
    if (ledOffAt && millis() >= ledOffAt) {
        setLED(CRGB::Blue);
        ledOffAt = 0;
    }
}

// ---------------------------------------------------------------------------
// Decode and print a function-code description
// ---------------------------------------------------------------------------
static void printFunctionCode(uint8_t fc)
{
    // Strip exception bit (0x80) to get base function code
    uint8_t base = fc & 0x7F;
    bool isException = (fc & 0x80) != 0;

    const char *name;
    switch (base) {
        case 0x01: name = "Read Coils";                break;
        case 0x02: name = "Read Discrete Inputs";      break;
        case 0x03: name = "Read Holding Registers";    break;
        case 0x04: name = "Read Input Registers";      break;
        case 0x05: name = "Write Single Coil";         break;
        case 0x06: name = "Write Single Register";     break;
        case 0x0F: name = "Write Multiple Coils";      break;
        case 0x10: name = "Write Multiple Registers";  break;
        case 0x11: name = "Report Slave ID";           break;
        case 0x16: name = "Mask Write Register";       break;
        case 0x17: name = "Read/Write Multiple Regs";  break;
        default:   name = "Unknown / Vendor-specific"; break;
    }

    if (isException) {
        Serial.printf("Function : 0x%02X  EXCEPTION for FC 0x%02X (%s)\n", fc, base, name);
    } else {
        Serial.printf("Function : 0x%02X  %s\n", fc, name);
    }
}

// ---------------------------------------------------------------------------
// Print a complete captured frame
//   direction: true  = master request (→)
//              false = slave response  (←)
// ---------------------------------------------------------------------------
static void printFrame(const uint8_t *buf, uint16_t len, uint32_t frameNo, bool isMaster)
{
    const char *dir = isMaster ? "-> Master Request" : "<- Slave Response";
    Serial.printf("== Frame #%lu  %s  (%u byte%s) ", frameNo, dir, len, len == 1 ? "" : "s");
    // Pad header line to fixed width
    int headerLen = 18 + strlen(dir) + 2
                       + (frameNo >= 1000 ? 4 : frameNo >= 100 ? 3 : frameNo >= 10 ? 2 : 1)
                       + (len  >= 100     ? 3 : len  >= 10      ? 2 : 1);
    for (int i = headerLen; i < 65; i++) Serial.print('=');
    Serial.println();

    // --- Raw bytes ---
    Serial.print("Raw HEX  : ");
    for (uint16_t i = 0; i < len; i++) {
        Serial.printf("%02X ", buf[i]);
    }
    Serial.println();

    if (len < 4) {
        Serial.println("[WARN] Frame too short for Modbus RTU (need >= 4 bytes)");
        Serial.println("-------------------------------------------------");
        Serial.println();
        scheduleLED(CRGB::Yellow, 120);
        return;
    }

    // --- Decode address and function code ---
    Serial.printf("Address  : %u  (0x%02X)", buf[0], buf[0]);
    if (buf[0] == 0x00) Serial.print("  [Broadcast]");
    Serial.println();
    printFunctionCode(buf[1]);

    // --- Data bytes (between function code and CRC) ---
    if (len > 4) {
        Serial.print("Data     : ");
        for (uint16_t i = 2; i < len - 2; i++) {
            Serial.printf("%02X ", buf[i]);
        }
        Serial.println();
    }

    // --- CRC check ---
    uint16_t rxCRC   = (uint16_t)buf[len - 2] | ((uint16_t)buf[len - 1] << 8);
    uint16_t calcCRC = modbusCRC16(buf, len - 2);
    bool crcOk = (rxCRC == calcCRC);

    if (crcOk) {
        Serial.printf("CRC      : OK (RX: 0x%04X, Calculated: 0x%04X)\n", rxCRC, calcCRC);
    } else {
        Serial.printf("CRC      : ERROR (RX: 0x%04X, Calculated: 0x%04X)\n", rxCRC, calcCRC);
    }

    Serial.println("-------------------------------------------------");
    Serial.println();

    if (crcOk) {
        scheduleLED(CRGB::Green, 80);
    } else {
        scheduleLED(CRGB::Red, 150);
    }
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

    // RX only — TX pin configured but never used
    Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, RS485_RX, RS485_TX);

    delay(500);

    Serial.println();
    Serial.println("=================================================");
    Serial.println("  Modbus RTU Receiver / Bus Sniffer");
    Serial.println("  M5Stack Atom + Atomic RS485 Base");
    Serial.println("-------------------------------------------------");
    Serial.printf( "  Baud rate : %d\n", MODBUS_BAUD_RATE);
    Serial.printf( "  Frame gap : %lu us (1.75 char-times)\n", FRAME_TIMEOUT_US);
    Serial.println("  LED: Blue=idle  Green=OK  Red=CRC err");
    Serial.println("=================================================");
    Serial.println();
    Serial.println("Listening for RS485 traffic...");
    Serial.println();
}

// ---------------------------------------------------------------------------
// Frame queue — decouple collection from printing so the read loop is never
// stalled by USB-Serial output while bytes accumulate in the UART FIFO.
// ---------------------------------------------------------------------------
struct QueuedFrame {
    uint8_t  buf[MAX_FRAME_BYTES];
    uint16_t len;
    uint32_t frameNo;
    bool     isMaster;
};

#define QUEUE_DEPTH 8
static QueuedFrame fq[QUEUE_DEPTH];
static uint8_t     fqHead = 0;
static uint8_t     fqTail = 0;

static bool enqueueFrame(const uint8_t *buf, uint16_t len, uint32_t frameNo, bool isMaster)
{
    uint8_t next = (fqHead + 1) % QUEUE_DEPTH;
    if (next == fqTail) return false;  // queue full
    memcpy(fq[fqHead].buf, buf, len);
    fq[fqHead].len      = len;
    fq[fqHead].frameNo  = frameNo;
    fq[fqHead].isMaster = isMaster;
    fqHead = next;
    return true;
}

// ---------------------------------------------------------------------------
// CRC-assisted frame splitter
// When a timing gap is missed (master violates 3.5-char rule), the collected
// buffer contains multiple concatenated frames.  Scan for a valid CRC at
// every possible boundary; when found, enqueue the first valid frame and
// recurse on the remainder.  Direction alternates with each split segment.
// Probability of a false-positive CRC split: ~1.5e-5 per split point.
// ---------------------------------------------------------------------------
static void splitAndEnqueue(const uint8_t *buf, uint16_t len,
                            uint32_t &frameCount, bool firstIsMaster)
{
    if (len < 4) {
        enqueueFrame(buf, len, ++frameCount, firstIsMaster);
        return;
    }

    // Check if the whole buffer is already a valid frame.
    uint16_t rxCRC   = (uint16_t)buf[len-2] | ((uint16_t)buf[len-1] << 8);
    uint16_t calcCRC = modbusCRC16(buf, len - 2);
    if (rxCRC == calcCRC) {
        enqueueFrame(buf, len, ++frameCount, firstIsMaster);
        return;
    }

    // CRC error — scan for a valid split point.
    // Require at least 4 bytes remaining after the split (minimum Modbus frame).
    for (uint16_t split = 4; split <= len - 4; split++) {
        uint16_t rx1   = (uint16_t)buf[split-2] | ((uint16_t)buf[split-1] << 8);
        uint16_t calc1 = modbusCRC16(buf, split - 2);
        if (rx1 == calc1) {
            // First segment has a valid CRC — enqueue it.
            enqueueFrame(buf, split, ++frameCount, firstIsMaster);
            // Process the remainder, direction alternates.
            splitAndEnqueue(buf + split, len - split, frameCount, !firstIsMaster);
            return;
        }
    }

    // No valid split found — enqueue the whole buffer (CRC error will show).
    enqueueFrame(buf, len, ++frameCount, firstIsMaster);
}

void loop()
{
    static uint8_t  frameBuf[MAX_FRAME_BYTES];
    static uint16_t frameLen      = 0;   // uint16_t: MAX_FRAME_BYTES=256 overflows uint8_t
    static uint32_t lastByteTime  = 0;   // micros() timestamp of last received byte
    static uint32_t frameCount    = 0;
    static uint32_t lastFrameEnd  = 0;   // micros() timestamp when previous frame was flushed

    // Drain all available bytes in a tight loop — never block here.
    while (Serial2.available()) {
        frameBuf[frameLen++] = (uint8_t)Serial2.read();
        lastByteTime = micros();

        if (frameLen >= MAX_FRAME_BYTES) {
            uint32_t gap = micros() - lastFrameEnd;
            bool isMaster = (gap > 50000UL) || (frameCount % 2 == 0);
            splitAndEnqueue(frameBuf, frameLen, frameCount, isMaster);
            lastFrameEnd = micros();
            frameLen = 0;
        }
    }

    // Check for end-of-frame silence.
    if (frameLen > 0 && (micros() - lastByteTime) >= FRAME_TIMEOUT_US) {
        uint32_t gap = lastByteTime - lastFrameEnd;
        bool isMaster = (gap > 50000UL) || (frameCount % 2 == 0);
        splitAndEnqueue(frameBuf, frameLen, frameCount, isMaster);
        lastFrameEnd = micros();
        frameLen = 0;
    }

    // Print one queued frame per iteration, but only when the UART is idle
    // so printing never races with incoming bytes.
    if (!Serial2.available() && fqHead != fqTail) {
        QueuedFrame &f = fq[fqTail];
        printFrame(f.buf, f.len, f.frameNo, f.isMaster);
        fqTail = (fqTail + 1) % QUEUE_DEPTH;
    }

    serviceLED();
}
