// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <math.h>

class SensorGainTrim {
public:
  SensorGainTrim(int pin, float minGain = 0.10f, float maxGain = 1.50f)
    : pin(pin), minGain(minGain), maxGain(maxGain) {}

  void begin() const {
    pinMode(pin, INPUT);
#if defined(ESP32)
    analogReadResolution(12);
#endif
  }

  float readGain() const {
    uint32_t total = 0;
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
      total += analogRead(pin);
      delay(2);
    }

    float raw = float(total) / float(SAMPLE_COUNT);
    float ratio = raw / 4095.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    float gain = minGain + ((maxGain - minGain) * ratio);
    return roundf(gain * 20.0f) / 20.0f;
  }

private:
  static constexpr int SAMPLE_COUNT = 6;

  int pin;
  float minGain;
  float maxGain;
};
