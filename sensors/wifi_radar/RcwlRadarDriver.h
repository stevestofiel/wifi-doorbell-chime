// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>

using SensorEmitFn = void (*)(const char* source);

class RcwlRadarDriver {
public:
  RcwlRadarDriver(int radarPin,
                  int activeLevel,
                  unsigned long settleMs,
                  SensorEmitFn emit)
    : radarPin(radarPin),
      activeLevel(activeLevel),
      settleMs(settleMs),
      emit(emit) {}

  void begin() {
    pinMode(radarPin, INPUT);

    lastRaw = digitalRead(radarPin);
    stableActive = lastRaw == activeLevel;
    triggeredWhileActive = stableActive;
    Serial.printf("Initial radar input: radar=%d\n", lastRaw);
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
  int activeLevel;
  unsigned long settleMs;
  SensorEmitFn emit;

  unsigned long lastChangeMs = 0;
  bool lastRaw = LOW;
  bool stableActive = false;
  bool triggeredWhileActive = false;
};
