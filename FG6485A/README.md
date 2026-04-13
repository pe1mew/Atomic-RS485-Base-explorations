# FG6485A

Reads temperature and humidity from an ASAIR FG6485A sensor over Modbus RTU via the M5Stack Atomic RS485 Base, and prints the values to the USB serial port every 5 seconds.

## Purpose

Demonstrates how to poll a specific Modbus RTU slave device, parse a multi-register response, and report scaled physical values over serial.

## Hardware

| Component | Detail |
|---|---|
| Board | M5Stack Atom Lite (ESP32-PICO-D4) |
| RS485 adapter | Atomic RS485 Base |
| RS485 RX | GPIO 22 (UART2 RX ← RS485 RO) |
| RS485 TX | GPIO 19 (UART2 TX → RS485 DI) |
| Direction control | Hardware transistor on RS485 Base (automatic) |
| LED | WS2812B on GPIO 27 |

## Sensor: ASAIR FG6485A

| Setting | Value |
|---|---|
| Protocol | Modbus RTU, 8N1 |
| Baud rate | 9600 |
| Slave address | 0x01 (factory default, set by DIP switch) |
| Register 0x0000 | Humidity × 10 (e.g. 471 → 47.1 %RH) |
| Register 0x0001 | Temperature × 10 (e.g. 214 → 21.4 °C) |

## Behaviour

- Sends a Modbus FC 0x03 request for 2 holding registers (humidity + temperature) every 5 seconds.
- Validates the CRC of every response before parsing.
- Prints temperature and humidity to Serial at 115200 baud.
- **Green LED flash** — successful read.
- **Red LED flash** — communication error (timeout, CRC failure, or Modbus exception).

## Serial output example

```
===================================
  FG6485A Temperature/Humidity
  Modbus RTU via Atomic RS485 Base
===================================
  Slave address : 0x01
  Baud rate     : 9600
  Poll interval : 5 s
===================================

-------- FG6485A Readings --------
  Temperature :   21.4 °C
  Humidity    :   47.1 %RH
----------------------------------
```

## Build & Flash

```
pio run -e FG6485A --target upload
pio device monitor -e FG6485A
```

## Dependencies

- [FastLED](https://github.com/FastLED/FastLED)
