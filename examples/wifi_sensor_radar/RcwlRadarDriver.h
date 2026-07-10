// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>

using SensorEmitFn = void (*)(const char* source);

class RcwlRadarDriver {
public:
  RcwlRadarDriver(int radarPin,
                  int resetButtonPin,
                  int activeLevel,
                  unsigned long settleMs,
                  SensorEmitFn emit)
    : radarPin(radarPin),
      resetButtonPin(resetButtonPin),
      activeLevel(activeLevel),
      settleMs(settleMs),
      emit(emit) {}

  void begin() {
    pinMode(radarPin, INPUT);
    pinMode(resetButtonPin, INPUT_PULLUP);

    lastRaw = digitalRead(radarPin);
    stableActive = lastRaw == activeLevel;
    triggeredWhileActive = stableActive;
    Serial.printf("Initial inputs: radar=%d setupButton=%d\n", lastRaw, digitalRead(resetButtonPin));
  }

  bool setupHeld(unsigned long holdMs) {
    if (digitalRead(resetButtonPin) != LOW) return false;

    Serial.println("Setup gesture: reset button held, checking hold duration");
    unsigned long holdStart = millis();
    while (digitalRead(resetButtonPin) == LOW && millis() - holdStart < holdMs) {
      delay(20);
    }
    bool accepted = millis() - holdStart >= holdMs;
    Serial.println(accepted ? "Setup gesture: accepted" : "Setup gesture: ignored");
    return accepted;
  }

  void poll() {
    bool raw = digitalRead(radarPin);
    unsigned long now = millis();

    if (raw != lastRaw) {
      lastRaw = raw;
      lastChangeMs = now;
      Serial.printf("Input change: radar=%d\n", raw);
    }

    bool active = lastRaw == activeLevel;
    if ((now - lastChangeMs) >= settleMs && active != stableActive) {
      stableActive = active;
      if (!stableActive) triggeredWhileActive = false;
    }

    if (stableActive && !triggeredWhileActive) {
      emit("radar");
      triggeredWhileActive = true;
    }
  }

private:
  int radarPin;
  int resetButtonPin;
  int activeLevel;
  unsigned long settleMs;
  SensorEmitFn emit;

  unsigned long lastChangeMs = 0;
  bool lastRaw = LOW;
  bool stableActive = false;
  bool triggeredWhileActive = false;
};
