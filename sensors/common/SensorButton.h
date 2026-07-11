// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>

using SensorButtonPressFn = void (*)(const char* source);

class SensorButton {
public:
  SensorButton(int pin,
               unsigned long debounceMs,
               SensorButtonPressFn onShortPress)
    : pin(pin),
      debounceMs(debounceMs),
      onShortPress(onShortPress) {}

  void begin() {
    pinMode(pin, INPUT_PULLUP);
    lastRaw = digitalRead(pin);
    stable = lastRaw;
    Serial.printf("Initial service button: %d\n", lastRaw);
  }

  bool setupHeld(unsigned long holdMs) {
    pinMode(pin, INPUT_PULLUP);
    if (digitalRead(pin) != LOW) return false;

    Serial.println("Setup gesture: service button held, checking hold duration");
    unsigned long holdStart = millis();
    while (digitalRead(pin) == LOW && millis() - holdStart < holdMs) {
      delay(20);
    }
    bool accepted = millis() - holdStart >= holdMs;
    Serial.println(accepted ? "Setup gesture: accepted" : "Setup gesture: ignored");
    return accepted;
  }

  void poll() {
    bool raw = digitalRead(pin);
    unsigned long now = millis();

    if (raw != lastRaw) {
      lastRaw = raw;
      lastChangeMs = now;
      Serial.printf("Input change: serviceButton=%d\n", raw);
    }

    if ((now - lastChangeMs) > debounceMs && raw != stable) {
      stable = raw;
      if (stable == LOW && onShortPress) onShortPress("button");
    }
  }

private:
  int pin;
  unsigned long debounceMs;
  SensorButtonPressFn onShortPress;

  unsigned long lastChangeMs = 0;
  bool lastRaw = HIGH;
  bool stable = HIGH;
};
