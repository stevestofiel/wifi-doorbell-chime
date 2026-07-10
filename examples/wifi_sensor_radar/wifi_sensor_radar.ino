// SPDX-License-Identifier: MIT
// Wi-Fi RCWL-0516 radar motion sensor prototype for ESP32-S3 Super Mini-style boards.

#include <Arduino.h>
#include "RcwlRadarDriver.h"
#include "../wifi_sensor_common/SensorConfig.h"
#include "../wifi_sensor_common/TriggerClient.h"

// ---- Wiring ----------------------------------------------------------------
// RCWL-0516: GND -> GND, 3V3 -> ESP32 3.3V, OUT -> GPIO4.
const int RADAR_PIN = 4;
const int RADAR_ACTIVE_LEVEL = HIGH;

// Setup/reset button: one side to GPIO3, other side to GND.
const int SETUP_BUTTON_PIN = 3;

// ---- Timing ----------------------------------------------------------------
const unsigned long TRIGGER_COOLDOWN_MS = 10000;
const unsigned long RADAR_SETTLE_MS = 80;
const unsigned long SETUP_HOLD_MS = 2000;

SensorConfigManager configManager("bench-radar", "motion", "detected");
TriggerClient triggerClient(configManager, TRIGGER_COOLDOWN_MS);

void emitSensorEvent(const char* source) {
  triggerClient.send(source);
}

RcwlRadarDriver driver(
  RADAR_PIN,
  SETUP_BUTTON_PIN,
  RADAR_ACTIVE_LEVEL,
  RADAR_SETTLE_MS,
  emitSensorEvent
);

void setup() {
  Serial.begin(115200);
  delay(800);

  pinMode(RADAR_PIN, INPUT);
  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);

  Serial.println();
  Serial.println("Wi-Fi radar sensor prototype starting");
  Serial.printf("Radar GPIO: %d\n", RADAR_PIN);
  Serial.printf("Setup button GPIO: %d\n", SETUP_BUTTON_PIN);
  Serial.printf("Trigger cooldown: %lu ms\n", TRIGGER_COOLDOWN_MS);
  Serial.printf("Setup hold: %lu ms\n", SETUP_HOLD_MS);

  bool forceSetupPortal = driver.setupHeld(SETUP_HOLD_MS);
  configManager.begin(forceSetupPortal);
  driver.begin();
}

void loop() {
  driver.poll();
  delay(10);
}
