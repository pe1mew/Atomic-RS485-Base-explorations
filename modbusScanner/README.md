# modbusScanner

Sweeps all valid Modbus RTU slave addresses (1–247) and reports every device that responds. A single scan runs on boot; results are printed to the USB serial port when the sweep completes.

## Purpose

Network discovery tool — identifies which Modbus slave addresses are active on a bus before targeted polling is set up. Useful when the slave address of a device is unknown or needs to be confirmed.

## Hardware

| Component | Detail |
|---|---|
| Board | M5Stack Atom Lite (ESP32-PICO-D4) |
| RS485 adapter | Atomic RS485 Base |
| RS485 RX | GPIO 22 (UART2 RX ← RS485 RO) |
| RS485 TX | GPIO 19 (UART2 TX → RS485 DI) |
| Direction control | Hardware transistor on RS485 Base (automatic) |
| LED | WS2812B on GPIO 27 |

## Behaviour

For each address in the range 1–247 the scanner:
1. Sends a Modbus FC 0x03 probe frame (read 1 holding register at 0x0000).
2. Waits up to 300 ms for a reply.
3. Accepts any valid Modbus frame — including an exception reply (0x83) — as confirmation that a device is present.

After the sweep a summary lists all found addresses.

### LED colours

| Colour | Meaning |
|---|---|
| Blue | Scan in progress |
| Green flash | Device found at current address |
| Red flash | Garbled response (CRC/address mismatch — possible bus contention) |
| White | Scan complete |

## Configurable parameters

| Constant | Default | Description |
|---|---|---|
| `SCAN_BAUD_RATE` | 9600 | RS485 baud rate — must match the bus |
| `RESPONSE_TIMEOUT` | 300 ms | Time to wait for a reply per address |
| `INTER_FRAME_MS` | 10 ms | Settling gap between probe frames |

## Serial output example

```
============================================
  Modbus RTU Address Scanner
  M5Stack Atom + Atomic RS485 Base
============================================
  Baud rate     : 9600
  Address range : 1 – 247
  Timeout/addr  : 300 ms
============================================
  Scanning...

  [FOUND] Device at address   1 (0x01)
  [FOUND] Device at address  16 (0x10)

============================================
  SCAN COMPLETE
============================================
  Found 2 device(s):

    Address   1  (0x01)
    Address  16  (0x10)
============================================
```

## Build & Flash

```
pio run -e modbusScanner --target upload
pio device monitor -e modbusScanner
```

## Dependencies

- [FastLED](https://github.com/FastLED/FastLED)
