#include <Arduino.h>
#include <FastLED.h>

#define LED_PIN  27
#define BTN_PIN  39   // Active-low, external pull-up on ATOM Lite
#define NUM_LEDS 1

CRGB leds[NUM_LEDS];

static bool    cycling = true;
static uint8_t hue     = 0;
static bool    lastBtn = HIGH;

void setup() {
    pinMode(BTN_PIN, INPUT);   // GPIO 39 is input-only; board has external pull-up
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(50);
    leds[0] = CRGB::Black;
    FastLED.show();
}

void loop() {
    // Detect falling edge (press) on active-low button
    bool btn = digitalRead(BTN_PIN);
    if (lastBtn == HIGH && btn == LOW) {
        cycling = !cycling;
        if (!cycling) {
            leds[0] = CRGB::Black;
            FastLED.show();
        }
    }
    lastBtn = btn;

    if (cycling) {
        leds[0] = CHSV(hue++, 255, 200);  // Full HSV colour wheel
        FastLED.show();
        delay(20);   // ~50 steps/second
    } else {
        delay(10);
    }
}
