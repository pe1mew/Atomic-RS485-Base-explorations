# helloWorld

A minimal LED demo for the M5Stack Atom Lite that cycles through the full HSV colour wheel on the built-in WS2812B LED. The front button toggles the cycling on and off.

## Purpose

First-boot sanity check — verifies that the board, LED, and button are all working before moving on to more complex firmware.

## Hardware

| Component | Detail |
|---|---|
| Board | M5Stack Atom Lite (ESP32-PICO-D4) |
| LED | WS2812B on GPIO 27 |
| Button | Active-low on GPIO 39 (input-only pin, board has external pull-up) |

## Behaviour

- On power-up the LED begins cycling through the full HSV colour wheel at roughly 50 steps per second.
- Pressing the front button pauses cycling and turns the LED off.
- Pressing the button again resumes cycling from the current hue.

## Build & Flash

```
pio run -e helloWorld --target upload
```

## Dependencies

- [FastLED](https://github.com/FastLED/FastLED)
