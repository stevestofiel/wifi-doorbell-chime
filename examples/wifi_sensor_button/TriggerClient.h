// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "SensorConfig.h"

class TriggerClient {
public:
  TriggerClient(const SensorConfigManager& configManager, unsigned long cooldownMs)
    : configManager(configManager), cooldownMs(cooldownMs) {}

  void send(const char* source) {
    unsigned long now = millis();
    if (now - lastTriggerMs < cooldownMs) return;
    lastTriggerMs = now;

    Serial.printf("Trigger: %s\n", source);
    if (!ensureWiFi()) return;
    if (!configManager.hasRequiredConfig()) {
      Serial.println("Trigger: missing chime URL");
      return;
    }

    const SensorConfig& config = configManager.config;
    String eventId = makeEventId(now);
    String triggerUrl = config.chimeBaseUrl +
      "/trigger?sensor=" + config.sensorId +
      "&type=" + config.sensorType +
      "&event=" + config.sensorEvent +
      "&eventId=" + eventId;

    int status = httpGet(withToken(triggerUrl));
    if (status == 404) {
      Serial.println("Trigger endpoint missing; falling back to /chime");
      httpGet(withToken(config.chimeBaseUrl + "/chime"));
    }
  }

private:
  const SensorConfigManager& configManager;
  unsigned long cooldownMs;
  unsigned long lastTriggerMs = 0;
  uint32_t eventCounter = 0;

  static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

  String makeEventId(unsigned long now) {
    String id = WiFi.macAddress();
    id.replace(":", "");
    id.toLowerCase();
    return id + "-" + String(++eventCounter) + "-" + String(now, HEX);
  }

  bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;

    Serial.println("WiFi: reconnecting");
    WiFi.reconnect();

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("WiFi: connected, IP ");
      Serial.println(WiFi.localIP());
      return true;
    }

    Serial.println("WiFi: connect failed");
    return false;
  }

  String withToken(const String& url) const {
    const String& token = configManager.config.chimeToken;
    if (token.length() == 0) return url;
    return url + (url.indexOf('?') >= 0 ? "&token=" : "?token=") + token;
  }

  int httpGet(const String& url) {
    HTTPClient http;
    Serial.println("GET " + url);
    if (!http.begin(url)) {
      Serial.println("HTTP: begin failed");
      return -1;
    }

    int status = http.GET();
    String body = http.getString();
    http.end();

    Serial.printf("HTTP: %d\n", status);
    if (body.length() > 0) Serial.println(body);
    return status;
  }
};
