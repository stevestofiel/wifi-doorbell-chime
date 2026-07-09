// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>

using SensorEmitFn = void (*)(const char* source);

class ButtonTouchDriver {
public:
  ButtonTouchDriver(int buttonPin,
                    int touchPin,
                    int touchActiveLevel,
                    unsigned long debounceMs,
                    unsigned long touchHoldMs,
                    SensorEmitFn emit)
    : buttonPin(buttonPin),
      touchPin(touchPin),
      touchActiveLevel(touchActiveLevel),
      debounceMs(debounceMs),
      touchHoldMs(touchHoldMs),
      emit(emit) {}

  void begin() {
    pinMode(buttonPin, INPUT_PULLUP);
    pinMode(touchPin, INPUT);

    lastButtonRaw = digitalRead(buttonPin);
    buttonStable = lastButtonRaw;
    lastTouchRaw = digitalRead(touchPin);
    touchStableActive = lastTouchRaw == touchActiveLevel;
    touchTriggeredWhileActive = touchStableActive;
    Serial.printf("Initial inputs: button=%d touch=%d\n", lastButtonRaw, lastTouchRaw);
  }

  bool setupHeld(unsigned long holdMs) {
    if (digitalRead(buttonPin) != LOW) return false;

    Serial.println("Setup gesture: button held, checking hold duration");
    unsigned long holdStart = millis();
    while (digitalRead(buttonPin) == LOW && millis() - holdStart < holdMs) {
      delay(20);
    }
    bool accepted = millis() - holdStart >= holdMs;
    Serial.println(accepted ? "Setup gesture: accepted" : "Setup gesture: ignored");
    return accepted;
  }

  void poll() {
    bool buttonRaw = digitalRead(buttonPin);
    bool touchRaw = digitalRead(touchPin);
    unsigned long now = millis();

    if (buttonRaw != lastButtonRaw) {
      lastButtonRaw = buttonRaw;
      lastButtonChangeMs = now;
      Serial.printf("Input change: button=%d touch=%d\n", buttonRaw, touchRaw);
    }

    if ((now - lastButtonChangeMs) > debounceMs && buttonRaw != buttonStable) {
      buttonStable = buttonRaw;
      if (buttonStable == LOW) emit("button");
    }

    if (touchRaw != lastTouchRaw) {
      lastTouchRaw = touchRaw;
      lastTouchChangeMs = now;
      Serial.printf("Input change: button=%d touch=%d\n", buttonRaw, touchRaw);
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
  int buttonPin;
  int touchPin;
  int touchActiveLevel;
  unsigned long debounceMs;
  unsigned long touchHoldMs;
  SensorEmitFn emit;

  unsigned long lastButtonChangeMs = 0;
  unsigned long lastTouchChangeMs = 0;
  bool lastButtonRaw = HIGH;
  bool buttonStable = HIGH;
  bool lastTouchRaw = LOW;
  bool touchStableActive = false;
  bool touchTriggeredWhileActive = false;
};
