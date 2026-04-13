# modbusReceiver

A passive Modbus RTU bus sniffer. Listens to RS485 traffic and prints every captured frame — raw bytes, decoded fields, and CRC validation result — to the USB serial port. Nothing is ever transmitted on the bus.

## Purpose

Non-intrusive debugging and monitoring tool. Attach it to a live Modbus network to observe all master/slave exchanges without disturbing the traffic. Useful for verifying that devices are responding correctly, checking register addresses, and diagnosing CRC or framing errors.

## Hardware

| Component | Detail |
|---|---|
| Board | M5Stack Atom Lite (ESP32-PICO-D4) |
| RS485 adapter | Atomic RS485 Base |
| RS485 RX | GPIO 22 (UART2 RX ← RS485 RO) |
| RS485 TX | GPIO 19 (configured but never written) |
| Direction control | Hardware transistor on RS485 Base (automatic) |
| LED | WS2812B on GPIO 27 |

## Behaviour

- Collects incoming bytes into a frame buffer.
- Uses a **5 ms inter-character silence** to detect the end of a Modbus RTU frame (the standard specifies 3.5 character-times; 5 ms is comfortable up to ~19200 baud).
- For each captured frame, prints:
  - Frame number and byte count
  - Raw bytes in hexadecimal
  - Slave address (broadcasts at 0x00 are annotated)
  - Function code with human-readable name
  - Data bytes
  - CRC: received value, calculated value, and pass/fail status

### Recognised function codes

FC 01 Read Coils, FC 02 Read Discrete Inputs, FC 03 Read Holding Registers, FC 04 Read Input Registers, FC 05 Write Single Coil, FC 06 Write Single Register, FC 0F Write Multiple Coils, FC 10 Write Multiple Registers, FC 11 Report Slave ID, FC 16 Mask Write Register, FC 17 Read/Write Multiple Registers. Exception responses (high bit set) are detected automatically.

### LED colours

| Colour | Meaning |
|---|---|
| Blue | Idle, waiting for traffic |
| Green flash | Valid frame received (CRC OK) |
| Red flash | Frame received with CRC error |
| Yellow flash | Frame too short to be valid Modbus (< 4 bytes) |

## Configurable parameters

| Constant | Default | Description |
|---|---|---|
| `MODBUS_BAUD_RATE` | 9600 | Must match the bus being monitored |
| `FRAME_TIMEOUT_MS` | 5 ms | Inter-frame silence threshold |

## Serial output example

```
╔═════════════════════════════════════════════╗
║   Modbus RTU Receiver / Bus Sniffer         ║
║   M5Stack Atom + Atomic RS485 Base          ║
╠═════════════════════════════════════════════╣
║   Baud rate    : 9600                       ║
║   Frame gap    : 5 ms                       ║
║   LED: Blue=idle  Green=OK  Red=CRC err     ║
╚═════════════════════════════════════════════╝

Listening for RS485 traffic...

┌─────────────────────────────────────────────
│ Frame #1  (8 bytes)
├─────────────────────────────────────────────
│ Raw HEX  : 01 03 00 00 00 02 C4 0B
│  Address  : 1  (0x01)
│  Function  : 0x03  Read Holding Registers
│  Data     : 00 00 00 02
│  CRC      : received 0x0BC4  calculated 0x0BC4  →  OK
└─────────────────────────────────────────────
```

## Build & Flash

```
pio run -e modbusReceiver --target upload
pio device monitor -e modbusReceiver
```

## Dependencies

- [FastLED](https://github.com/FastLED/FastLED)
