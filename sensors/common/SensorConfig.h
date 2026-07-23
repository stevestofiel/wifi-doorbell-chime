// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>

struct SensorConfig {
  String chimeBaseUrl = "";
  String chimeToken = "";
  String sensorId = "bench-button";
  String sensorType = "doorbell";
  String sensorEvent = "press";
};

class SensorConfigManager {
public:
  SensorConfig config;

  SensorConfigManager(const char* defaultSensorId = "bench-button",
                      const char* defaultSensorType = "doorbell",
                      const char* defaultSensorEvent = "press")
    : defaultSensorId(defaultSensorId),
      defaultSensorType(defaultSensorType),
      defaultSensorEvent(defaultSensorEvent) {
    config.sensorId = defaultSensorId;
    config.sensorType = defaultSensorType;
    config.sensorEvent = defaultSensorEvent;
  }

  void begin(bool forceSetupPortal) {
    load();
    if (forceSetupPortal) {
      Serial.println("Config: preparing Wi-Fi for setup reset");
      WiFi.mode(WIFI_STA);
      delay(100);
      bool credentialsCleared = WiFi.disconnect(false, true);
      WiFi.mode(WIFI_AP_STA);
      delay(250);
      Serial.printf("Config: Wi-Fi credentials cleared=%s mode=%d\n",
                    credentialsCleared ? "yes" : "no",
                    static_cast<int>(WiFi.getMode()));
      clear();
    }

    char chimeBuf[96];
    char tokenBuf[65];
    char idBuf[32];
    char typeBuf[24];
    char eventBuf[24];
    copyParam(config.chimeBaseUrl, chimeBuf, sizeof(chimeBuf));
    copyParam(config.chimeToken, tokenBuf, sizeof(tokenBuf));
    copyParam(config.sensorId, idBuf, sizeof(idBuf));
    copyParam(config.sensorType, typeBuf, sizeof(typeBuf));
    copyParam(config.sensorEvent, eventBuf, sizeof(eventBuf));

    WiFiManager wm;
    wm.setSaveParamsCallback([]() { paramsSaved = true; });
    wm.setConnectTimeout(20);
    wm.setConfigPortalTimeout(0);
    wm.setWiFiAPChannel(6);
    wm.setAPCallback([](WiFiManager*) {
      Serial.printf("Setup AP: ssid=%s ip=%s mac=%s channel=%d\n",
                    SETUP_AP_SSID,
                    WiFi.softAPIP().toString().c_str(),
                    WiFi.softAPmacAddress().c_str(),
                    WiFi.channel());
    });

    WiFiManagerParameter chimeParam("chime", "Chime URL", chimeBuf, sizeof(chimeBuf));
    WiFiManagerParameter tokenParam("token", "Playback token", tokenBuf, sizeof(tokenBuf), "type='password'");
    WiFiManagerParameter idParam("sensor", "Sensor ID", idBuf, sizeof(idBuf));
    WiFiManagerParameter typeParam("type", "Sensor type", typeBuf, sizeof(typeBuf));
    WiFiManagerParameter eventParam("event", "Event", eventBuf, sizeof(eventBuf));
    wm.addParameter(&chimeParam);
    wm.addParameter(&tokenParam);
    wm.addParameter(&idParam);
    wm.addParameter(&typeParam);
    wm.addParameter(&eventParam);

    bool ok = false;
    if (hasRequiredConfig() && !forceSetupPortal) {
      Serial.println("WiFiManager: autoConnect");
      ok = wm.autoConnect(SETUP_AP_SSID, SETUP_AP_PASSWORD);
    } else {
      Serial.println("WiFiManager: missing chime URL, starting setup portal");
      ok = wm.startConfigPortal(SETUP_AP_SSID, SETUP_AP_PASSWORD);
    }
    Serial.println(ok ? "WiFiManager: connected" : "WiFiManager: portal timeout or connect failed");

    SensorConfig next;
    next.chimeBaseUrl = cleanUrl(String(chimeParam.getValue()));
    next.chimeToken = String(tokenParam.getValue());
    next.sensorId = cleanId(String(idParam.getValue()), defaultSensorId, 31);
    next.sensorType = cleanId(String(typeParam.getValue()), defaultSensorType, 23);
    next.sensorEvent = cleanId(String(eventParam.getValue()), defaultSensorEvent, 23);

    bool changed = next.chimeBaseUrl != config.chimeBaseUrl ||
                   next.chimeToken != config.chimeToken ||
                   next.sensorId != config.sensorId ||
                   next.sensorType != config.sensorType ||
                   next.sensorEvent != config.sensorEvent;
    config = next;
    if (changed || paramsSaved) save();

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    Serial.printf("Config: chime=%s sensor=%s type=%s event=%s\n",
                  config.chimeBaseUrl.c_str(),
                  config.sensorId.c_str(),
                  config.sensorType.c_str(),
                  config.sensorEvent.c_str());
  }

  bool hasRequiredConfig() const {
    return config.chimeBaseUrl.startsWith("http://") ||
           config.chimeBaseUrl.startsWith("https://");
  }

private:
  Preferences prefs;
  const char* defaultSensorId;
  const char* defaultSensorType;
  const char* defaultSensorEvent;

  static constexpr const char* SETUP_AP_SSID = "ChimeSensor";
  static constexpr const char* SETUP_AP_PASSWORD = "config123";
  static inline bool paramsSaved = false;

  static String cleanId(const String& input, const String& fallback, size_t maxLen) {
    String out;
    out.reserve(maxLen);
    for (size_t i = 0; i < input.length(); ++i) {
      char c = input[i];
      if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
      bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
      if (allowed) out += c;
      if (out.length() >= maxLen) break;
    }
    return out.length() ? out : fallback;
  }

  static String cleanUrl(const String& input) {
    String out = input;
    out.trim();
    String lower = out;
    lower.toLowerCase();
    while (lower.startsWith("http://http://") || lower.startsWith("http://https://")) {
      out = out.substring(7);
      lower = out;
      lower.toLowerCase();
    }
    while (lower.startsWith("https://http://") || lower.startsWith("https://https://")) {
      out = out.substring(8);
      lower = out;
      lower.toLowerCase();
    }
    if (lower.startsWith("http://")) {
      out = "http://" + out.substring(7);
    } else if (lower.startsWith("https://")) {
      out = "https://" + out.substring(8);
    } else if (out.length() > 0) {
      out = "http://" + out;
    }
    int triggerIdx = out.indexOf("/trigger");
    int chimeIdx = out.indexOf("/chime");
    int playIdx = out.indexOf("/play");
    int cutIdx = -1;
    if (triggerIdx >= 0) cutIdx = triggerIdx;
    if (chimeIdx >= 0 && (cutIdx < 0 || chimeIdx < cutIdx)) cutIdx = chimeIdx;
    if (playIdx >= 0 && (cutIdx < 0 || playIdx < cutIdx)) cutIdx = playIdx;
    if (cutIdx > 0) out = out.substring(0, cutIdx);
    while (out.endsWith("/")) out.remove(out.length() - 1);
    return out;
  }

  void load() {
    prefs.begin("sensor", true);
    config.chimeBaseUrl = prefs.getString("chime", "");
    config.chimeToken = prefs.getString("token", "");
    config.sensorId = prefs.getString("id", config.sensorId);
    config.sensorType = prefs.getString("type", config.sensorType);
    config.sensorEvent = prefs.getString("event", config.sensorEvent);
    prefs.end();

    config.chimeBaseUrl = cleanUrl(config.chimeBaseUrl);
    config.sensorId = cleanId(config.sensorId, defaultSensorId, 31);
    config.sensorType = cleanId(config.sensorType, defaultSensorType, 23);
    config.sensorEvent = cleanId(config.sensorEvent, defaultSensorEvent, 23);
  }

  void save() {
    prefs.begin("sensor", false);
    prefs.putString("chime", config.chimeBaseUrl);
    prefs.putString("token", config.chimeToken);
    prefs.putString("id", config.sensorId);
    prefs.putString("type", config.sensorType);
    prefs.putString("event", config.sensorEvent);
    prefs.end();
    Serial.println("Config: saved sensor settings");
  }

  void clear() {
    prefs.begin("sensor", false);
    prefs.clear();
    prefs.end();
    config = SensorConfig();
    config.sensorId = defaultSensorId;
    config.sensorType = defaultSensorType;
    config.sensorEvent = defaultSensorEvent;
    Serial.println("Config: cleared sensor settings");
  }

  static void copyParam(const String& value, char* buffer, size_t size) {
    value.toCharArray(buffer, size);
  }
};
