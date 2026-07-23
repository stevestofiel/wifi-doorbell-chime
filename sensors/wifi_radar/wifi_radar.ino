// SPDX-License-Identifier: MIT
// Wi-Fi RCWL-0516 radar motion sensor prototype for ESP32-S3 Super Mini-style boards.

#include <Arduino.h>
#include "RcwlRadarDriver.h"
#include "../common/SensorButton.h"
#include "../common/SensorConfig.h"
#include "../common/SensorGainTrim.h"
#include "../common/TriggerClient.h"

// ---- Wiring ----------------------------------------------------------------
// RCWL-0516: GND -> GND, 3V3 -> ESP32 3.3V, OUT -> GPIO4.
const int RADAR_PIN = 4;
const int RADAR_ACTIVE_LEVEL = HIGH;

// Service button: one side to GPIO3, other side to GND.
const int SETUP_BUTTON_PIN = 3;

// Optional event-gain trim pot: outer pins to 3.3V/GND, wiper to GPIO2.
const int GAIN_TRIM_PIN = 2;

// ---- Timing ----------------------------------------------------------------
const unsigned long TRIGGER_COOLDOWN_MS = 10000;
const unsigned long BUTTON_DEBOUNCE_MS = 40;
const unsigned long RADAR_SETTLE_MS = 80;
const unsigned long SETUP_HOLD_MS = 2000;
const bool RADAR_INPUT_ENABLED = true;

SensorConfigManager configManager("bench-radar", "motion", "detected");
SensorGainTrim gainTrim(GAIN_TRIM_PIN);

float readEventGain() {
  return gainTrim.readGain();
}

TriggerClient triggerClient(configManager, TRIGGER_COOLDOWN_MS, readEventGain);

void emitSensorEvent(const char* source) {
  if (!RADAR_INPUT_ENABLED && source && strcmp(source, "radar") == 0) {
    return;
  }
  if (source && strcmp(source, "button") == 0) {
    triggerClient.sendNow(source);
  } else {
    triggerClient.send(source);
  }
}

SensorButton serviceButton(
  SETUP_BUTTON_PIN,
  BUTTON_DEBOUNCE_MS,
  emitSensorEvent
);

RcwlRadarDriver driver(
  RADAR_PIN,
  RADAR_ACTIVE_LEVEL,
  RADAR_SETTLE_MS,
  emitSensorEvent
);

void setup() {
  Serial.begin(115200);
  delay(800);

  pinMode(RADAR_PIN, INPUT);
  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
  gainTrim.begin();

  Serial.println();
  Serial.println("Wi-Fi radar sensor prototype starting");
  Serial.printf("Radar GPIO: %d\n", RADAR_PIN);
  Serial.printf("Setup button GPIO: %d\n", SETUP_BUTTON_PIN);
  Serial.printf("Gain trim ADC GPIO: %d\n", GAIN_TRIM_PIN);
  Serial.printf("Trigger cooldown: %lu ms\n", TRIGGER_COOLDOWN_MS);
  Serial.printf("Setup hold: %lu ms\n", SETUP_HOLD_MS);
  Serial.printf("Radar input enabled: %s\n", RADAR_INPUT_ENABLED ? "yes" : "no");

  bool forceSetupPortal = serviceButton.setupHeld(SETUP_HOLD_MS);
  configManager.begin(forceSetupPortal);
  serviceButton.begin();
  if (RADAR_INPUT_ENABLED) driver.begin();
}

void loop() {
  serviceButton.poll();
  if (RADAR_INPUT_ENABLED) driver.poll();
  delay(10);
}
