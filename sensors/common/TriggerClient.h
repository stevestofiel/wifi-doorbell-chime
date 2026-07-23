// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "SensorConfig.h"

class TriggerClient {
public:
  typedef float (*GainProvider)();

  TriggerClient(const SensorConfigManager& configManager, unsigned long cooldownMs)
    : configManager(configManager), cooldownMs(cooldownMs) {}

  TriggerClient(const SensorConfigManager& configManager, unsigned long cooldownMs, GainProvider gainProvider)
    : configManager(configManager), cooldownMs(cooldownMs), gainProvider(gainProvider) {}

  void send(const char* source) {
    unsigned long now = millis();
    if (now - lastTriggerMs < cooldownMs) return;
    lastTriggerMs = now;
    sendRequest(source, now);
  }

  void sendNow(const char* source) {
    unsigned long now = millis();
    sendRequest(source, now);
  }

private:
  const SensorConfigManager& configManager;
  unsigned long cooldownMs;
  unsigned long lastTriggerMs = 0;
  uint32_t eventCounter = 0;
  GainProvider gainProvider = nullptr;

  static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

  void sendRequest(const char* source, unsigned long now) {
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
      "&input=" + cleanSource(source) +
      "&eventId=" + eventId;

    if (gainProvider) {
      float gain = gainProvider();
      if (gain >= 0.0f && gain <= 3.0f) {
        Serial.printf("Trigger gain: %.2f\n", gain);
        triggerUrl += "&gain=" + String(gain, 2);
      } else {
        Serial.printf("Trigger gain ignored: %.2f\n", gain);
      }
    }

    int status = httpGet(withToken(triggerUrl));
    if (status == 404) {
      Serial.println("Trigger endpoint missing; falling back to /chime");
      httpGet(withToken(config.chimeBaseUrl + "/chime"));
    }
  }

  String makeEventId(unsigned long now) {
    String id = WiFi.macAddress();
    id.replace(":", "");
    id.toLowerCase();
    return id + "-" + String(++eventCounter) + "-" + String(now, HEX);
  }

  static String cleanSource(const char* source) {
    String out;
    if (!source) return out;
    for (size_t i = 0; source[i] != '\0' && out.length() < 24; ++i) {
      char c = source[i];
      if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
      bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
      if (allowed) out += c;
    }
    return out;
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
