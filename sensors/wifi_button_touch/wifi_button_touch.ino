// SPDX-License-Identifier: MIT
// Wi-Fi remote sensor prototype for ESP32 Super Mini-style boards.

#include <Arduino.h>
#include "ButtonTouchDriver.h"
#include "../common/SensorButton.h"
#include "../common/SensorConfig.h"
#include "../common/TriggerClient.h"

// ---- Wiring ----------------------------------------------------------------
// Button: one side to GPIO3, other side to GND. Uses internal pull-up.
const int BUTTON_PIN = 3;

// Touch module: G -> GND, V -> 3.3V, S -> GPIO4.
const int TOUCH_PIN = 4;
const int TOUCH_ACTIVE_LEVEL = HIGH;

// ---- Timing ----------------------------------------------------------------
const unsigned long TRIGGER_COOLDOWN_MS = 2500;
const unsigned long DEBOUNCE_MS = 40;
const unsigned long TOUCH_HOLD_MS = 180;
const unsigned long SETUP_HOLD_MS = 2000;

SensorConfigManager configManager;
TriggerClient triggerClient(configManager, TRIGGER_COOLDOWN_MS);

void emitSensorEvent(const char* source) {
  triggerClient.send(source);
}

SensorButton serviceButton(
  BUTTON_PIN,
  DEBOUNCE_MS,
  emitSensorEvent
);

ButtonTouchDriver driver(
  TOUCH_PIN,
  TOUCH_ACTIVE_LEVEL,
  TOUCH_HOLD_MS,
  emitSensorEvent
);

void setup() {
  Serial.begin(115200);
  delay(800);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT);

  Serial.println();
  Serial.println("Wi-Fi sensor prototype starting");
  Serial.printf("Service button GPIO: %d\n", BUTTON_PIN);
  Serial.printf("Touch GPIO: %d\n", TOUCH_PIN);
  Serial.printf("Touch hold: %lu ms\n", TOUCH_HOLD_MS);
  Serial.printf("Setup hold: %lu ms\n", SETUP_HOLD_MS);
  Serial.println("Raw input changes will print as serviceButton=<0|1> or touch=<0|1>");

  bool forceSetupPortal = serviceButton.setupHeld(SETUP_HOLD_MS);
  configManager.begin(forceSetupPortal);
  serviceButton.begin();
  driver.begin();
}

void loop() {
  serviceButton.poll();
  driver.poll();
  delay(10);
}
