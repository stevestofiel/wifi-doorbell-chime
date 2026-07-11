// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>

using SensorEmitFn = void (*)(const char* source);

class ButtonTouchDriver {
public:
  ButtonTouchDriver(int touchPin,
                    int touchActiveLevel,
                    unsigned long touchHoldMs,
                    SensorEmitFn emit)
    : touchPin(touchPin),
      touchActiveLevel(touchActiveLevel),
      touchHoldMs(touchHoldMs),
      emit(emit) {}

  void begin() {
    pinMode(touchPin, INPUT);

    lastTouchRaw = digitalRead(touchPin);
    touchStableActive = lastTouchRaw == touchActiveLevel;
    touchTriggeredWhileActive = touchStableActive;
    Serial.printf("Initial touch input: touch=%d\n", lastTouchRaw);
  }

  void poll() {
    bool touchRaw = digitalRead(touchPin);
    unsigned long now = millis();

    if (touchRaw != lastTouchRaw) {
      lastTouchRaw = touchRaw;
      lastTouchChangeMs = now;
      Serial.printf("Input change: touch=%d\n", touchRaw);
    }

    bool touchActive = lastTouchRaw == touchActiveLevel;
    if ((now - lastTouchChangeMs) >= touchHoldMs && touchActive != touchStableActive) {
      touchStableActive = touchActive;
      if (!touchStableActive) touchTriggeredWhileActive = false;
    }

    if (touchStableActive && !touchTriggeredWhileActive) {
      emit("touch");
      touchTriggeredWhileActive = true;
    }
  }

private:
  int touchPin;
  int touchActiveLevel;
  unsigned long touchHoldMs;
  SensorEmitFn emit;

  unsigned long lastTouchChangeMs = 0;
  bool lastTouchRaw = LOW;
  bool touchStableActive = false;
  bool touchTriggeredWhileActive = false;
};
