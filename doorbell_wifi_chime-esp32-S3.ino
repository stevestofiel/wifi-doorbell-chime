// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Steve Stofiel

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <esp_partition.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include "AudioFileSourceSPIFFS.h"
#include "AudioFileSourceID3.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioGeneratorWAV.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "boot_mp3.h"

#ifndef CLEAR_AUTH_ON_BOOT
#define CLEAR_AUTH_ON_BOOT 0
#endif

// ── Globals ────────────────────────────────────────────────────────────────
AudioGeneratorWAV *wav = nullptr;
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSPIFFS *fileSource = nullptr;
AudioFileSourceID3 *id3 = nullptr;
AudioFileSourcePROGMEM *bootSource = nullptr;
AudioOutputI2S *out = nullptr;
AsyncWebServer server(80);

const int BUTTON_PIN = 13;
const int LED_PIN = 48; // onboard blue LED
bool wasPressed = false;
bool ledPulseActive = false;
unsigned long ledPulseUntilMs = 0;

float currentGain = 1.0f;

String activeFilePath        = "/chime.wav";
const char* SOUNDS_FILE      = "/sounds.json";
const char* DEVICE_FILE      = "/device.json";
const char* RULES_FILE       = "/rules.json";
String      displayFilename  = "No chime loaded";
bool        uploadSucceeded  = false;
String      uploadError      = "";
String      uploadTargetPath = "";
String      uploadDisplayName = "";
String      deviceLabel      = "";
String      lanDnsSuffix     = "";
String      authToken        = "";
bool        playbackAuth     = false;
String      mdnsName         = "doorbell";
bool        mdnsOk           = false;
unsigned long lastMdnsTryMs  = 0;
bool        shouldReboot     = false;
bool        pendingRestart   = false;
unsigned long restartAtMs    = 0;
bool        wifiWasConnected = false;
unsigned long lastWiFiReconnectMs = 0;

const unsigned long WIFI_RECONNECT_INTERVAL_MS = 10000;
const unsigned long LED_PULSE_MS = 160;
const size_t EVENT_LOG_SIZE = 20;

struct ChimeEvent {
  uint32_t seq = 0;
  String eventId;
  String sensor;
  String type;
  String event;
  String source;
  String soundKey;
  String soundPath;
  unsigned long timeMs = 0;
};

struct SoundResolution {
  String path;
  String key;
  String source;
};

ChimeEvent eventLog[EVENT_LOG_SIZE];
size_t eventLogCount = 0;
size_t eventLogNext = 0;
uint32_t eventSeq = 0;

void onConfigSaved() {
  shouldReboot = true;
}

String buildMdnsName(const String &label) {
  String name = "doorbell";
  if (label.length() > 0) name += "-" + label;
  return name;
}

String buildLanDnsHost(const String &host, const String &suffix) {
  if (suffix.length() == 0) return "";
  return host + "." + suffix;
}

bool startMdnsNow(const String &name) {
  Serial.printf("mDNS: begin %s\n", name.c_str());
  bool ok = MDNS.begin(name.c_str());
  if (ok) {
    Serial.println("mDNS: started");
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("doorbell-chime", "tcp", 80);
  } else {
    Serial.println("mDNS: begin failed, will retry");
  }
  mdnsOk = ok;
  lastMdnsTryMs = millis();
  return ok;
}

void scheduleRestart(unsigned long delayMs) {
  pendingRestart = true;
  restartAtMs = millis() + delayMs;
}

String sanitizeToken(const String &input) {
  String out;
  out.reserve(64);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
      out += c;
    }
    if (out.length() >= 64) break;
  }
  return out;
}

String sanitizeLanDnsSuffix(const String &input) {
  String out;
  out.reserve(48);
  bool lastWasDot = true;
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (allowed) {
      out += c;
      lastWasDot = false;
    } else if (c == '.' && !lastWasDot && out.length() > 0) {
      out += c;
      lastWasDot = true;
    }
    if (out.length() >= 48) break;
  }
  while (out.endsWith(".")) out.remove(out.length() - 1);
  return out;
}

String requestToken(AsyncWebServerRequest *request) {
  if (request->hasParam("token", true)) return request->getParam("token", true)->value();
  if (request->hasParam("token")) return request->getParam("token")->value();
  if (request->hasHeader("X-Auth-Token")) {
    const AsyncWebHeader* h = request->getHeader("X-Auth-Token");
    if (h) return h->value();
  }
  return "";
}

bool tokenMatches(AsyncWebServerRequest *request) {
  return authToken.length() == 0 || requestToken(request) == authToken;
}

bool requireAdminAuth(AsyncWebServerRequest *request) {
  if (tokenMatches(request)) return true;
  request->send(403, "application/json", "{\"ok\":false,\"error\":\"Forbidden\"}");
  return false;
}

bool requirePlaybackAuth(AsyncWebServerRequest *request) {
  if (!playbackAuth || tokenMatches(request)) return true;
  sendTriggerResponse(request, 403, "Forbidden");
  return false;
}

void maintainWiFiConnection() {
  wl_status_t status = WiFi.status();
  bool connected = (status == WL_CONNECTED);
  unsigned long now = millis();

  if (connected) {
    if (!wifiWasConnected) {
      Serial.print("WiFi: reconnected, IP: ");
      Serial.println(WiFi.localIP());
      wifiWasConnected = true;
      if (mdnsOk) MDNS.end();
      startMdnsNow(mdnsName);
    }
    return;
  }

  if (wifiWasConnected) {
    Serial.printf("WiFi: disconnected, status=%d\n", status);
    wifiWasConnected = false;
    mdnsOk = false;
    MDNS.end();
  }

  if (now - lastWiFiReconnectMs >= WIFI_RECONNECT_INTERVAL_MS) {
    Serial.printf("WiFi: reconnect attempt, status=%d\n", status);
    WiFi.reconnect();
    lastWiFiReconnectMs = now;
  }
}

bool applyGainParam(AsyncWebServerRequest *request) {
  if (!request->hasParam("gain")) return true;
  float newGain = request->getParam("gain")->value().toFloat();
  if (newGain < 0.0f || newGain > 3.0f) return false;
  currentGain = newGain;
  if (out) out->SetGain(currentGain);
  return true;
}

bool prefersHtml(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Accept")) return false;
  const AsyncWebHeader* h = request->getHeader("Accept");
  return h && h->value().indexOf("text/html") >= 0;
}

void sendTriggerResponse(AsyncWebServerRequest *request, int statusCode, const char* text) {
  if (prefersHtml(request)) {
    request->redirect("/");
    return;
  }
  request->send(statusCode, "text/plain", text);
}

void pulseEventLed() {
  digitalWrite(LED_PIN, HIGH);
  ledPulseActive = true;
  ledPulseUntilMs = millis() + LED_PULSE_MS;
}

void maintainEventLed() {
  if (ledPulseActive && (long)(millis() - ledPulseUntilMs) >= 0) {
    digitalWrite(LED_PIN, LOW);
    ledPulseActive = false;
  }
}

size_t eventLogIndexNewest(size_t offset) {
  size_t newest = eventLogNext == 0 ? EVENT_LOG_SIZE - 1 : eventLogNext - 1;
  return (newest + EVENT_LOG_SIZE - offset) % EVENT_LOG_SIZE;
}

const ChimeEvent* latestEvent() {
  if (eventLogCount == 0) return nullptr;
  return &eventLog[eventLogIndexNewest(0)];
}

String normalizeEventId(const String &input) {
  String id = input;
  id.trim();
  if (id.length() > 64) id = id.substring(0, 64);
  return id;
}

bool hasSeenEventId(const String &eventId) {
  String id = normalizeEventId(eventId);
  if (id.length() == 0) return false;

  for (size_t i = 0; i < eventLogCount; ++i) {
    const ChimeEvent &event = eventLog[eventLogIndexNewest(i)];
    if (event.eventId == id) return true;
  }
  return false;
}

void appendEventJson(JsonObject obj, const ChimeEvent &event) {
  obj["seq"] = event.seq;
  obj["eventId"] = event.eventId;
  obj["sensor"] = event.sensor;
  obj["type"] = event.type;
  obj["event"] = event.event;
  obj["source"] = event.source;
  obj["soundKey"] = event.soundKey;
  obj["soundPath"] = event.soundPath;
  obj["timeMs"] = event.timeMs;
  obj["ageMs"] = millis() - event.timeMs;
}

void recordChimeEvent(const String &eventId,
                      const String &sensor,
                      const String &type,
                      const String &eventType,
                      const String &source,
                      const String &soundKey,
                      const String &soundPath) {
  ChimeEvent &entry = eventLog[eventLogNext];
  entry.seq = ++eventSeq;
  entry.eventId = normalizeEventId(eventId);
  if (entry.eventId.length() == 0) {
    entry.eventId = "local-" + String(entry.seq);
  }
  entry.sensor = sensor;
  entry.type = type;
  entry.event = eventType.length() ? eventType : "trigger";
  entry.source = source;
  entry.soundKey = soundKey;
  entry.soundPath = soundPath;
  entry.timeMs = millis();

  eventLogNext = (eventLogNext + 1) % EVENT_LOG_SIZE;
  if (eventLogCount < EVENT_LOG_SIZE) eventLogCount++;

  pulseEventLed();
}

// ── Status JSON ────────────────────────────────────────────────────────────
void handleStatus(AsyncWebServerRequest *request) {
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  int signalPct = rssi <= -100 ? 0 : (rssi >= -50 ? 100 : (rssi + 100) * 2);

  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  size_t freeBytes = totalBytes >= usedBytes ? (totalBytes - usedBytes) : 0;
  int usedPct = totalBytes ? int((usedBytes * 100) / totalBytes) : 0;

  StaticJsonDocument<1280> doc;
  doc["rssi"] = rssi;
  doc["signalPct"] = signalPct;
  doc["fsTotalKB"] = totalBytes / 1024.0;
  doc["fsFreeKB"] = freeBytes / 1024.0;
  doc["fsUsedPct"] = usedPct;
  doc["wifi"] = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
  doc["ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : "";
  doc["mdns"] = buildMdnsName(deviceLabel) + ".local";
  doc["lanDns"] = buildLanDnsHost(buildMdnsName(deviceLabel), lanDnsSuffix);
  doc["lanDnsSuffix"] = lanDnsSuffix;
  doc["deviceLabel"] = deviceLabel;
  doc["gain"] = currentGain;
  doc["activeName"] = displayFilename;
  doc["activePath"] = activeFilePath;
  doc["authEnabled"] = authToken.length() > 0;
  doc["playbackAuth"] = playbackAuth;
  doc["eventCount"] = eventLogCount;

  const ChimeEvent* last = latestEvent();
  if (last) {
    doc["lastEventId"] = last->eventId;
    doc["lastEventSensor"] = last->sensor;
    doc["lastEventType"] = last->type;
    doc["lastEvent"] = last->event;
    doc["lastEventSource"] = last->source;
    doc["lastEventAgeMs"] = millis() - last->timeMs;
  }

  String payload;
  serializeJson(doc, payload);
  request->send(200, "application/json", payload);
}

void handleEvents(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(6144);
  doc["count"] = eventLogCount;
  doc["capacity"] = EVENT_LOG_SIZE;
  JsonArray items = doc.createNestedArray("items");

  for (size_t i = 0; i < eventLogCount; ++i) {
    JsonObject item = items.createNestedObject();
    appendEventJson(item, eventLog[eventLogIndexNewest(i)]);
  }

  String payload;
  serializeJson(doc, payload);
  request->send(200, "application/json", payload);
}

void handleList(AsyncWebServerRequest *request) {
  StaticJsonDocument<1024> doc;
  doc["active"] = activeFilePath;
  JsonArray items = doc.createNestedArray("items");
  size_t idx = 0;

  if (SPIFFS.exists(SOUNDS_FILE)) {
    File f = SPIFFS.open(SOUNDS_FILE, "r");
    if (f) {
      StaticJsonDocument<1024> src;
      DeserializationError error = deserializeJson(src, f);
      f.close();
      if (!error && src.containsKey("items")) {
        for (JsonObject item : src["items"].as<JsonArray>()) {
          String path = item["path"] | "";
          String name = item["name"] | "";
          if (path.length() == 0) continue;
          if (!SPIFFS.exists(path)) continue;
          JsonObject out = items.createNestedObject();
          idx++;
          out["index"] = idx;
          out["path"] = path;
          out["name"] = name.length() ? name : path.substring(1);
          String key = item["key"] | "";
          if (key.length() == 0) key = keyForPath(path);
          out["key"] = key;
          out["endpoint"] = "/play?key=" + key;
        }
      }
    }
  } else {
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
      String path = file.name();
      if (!path.startsWith("/")) path = "/" + path;
      String lower = path;
      lower.toLowerCase();
      if (lower.endsWith(".wav") || lower.endsWith(".mp3")) {
        JsonObject out = items.createNestedObject();
        idx++;
        out["index"] = idx;
        out["path"] = path;
        out["name"] = path.substring(1);
        String key = keyForPath(path);
        out["key"] = key;
        out["endpoint"] = "/play?key=" + key;
      }
      file = root.openNextFile();
    }
  }

  String payload;
  serializeJson(doc, payload);
  request->send(200, "application/json", payload);
}

String requestValue(AsyncWebServerRequest *request, const char* name) {
  if (request->hasParam(name, true)) return request->getParam(name, true)->value();
  if (request->hasParam(name)) return request->getParam(name)->value();
  return "";
}

bool loadRulesDocument(DynamicJsonDocument &doc) {
  if (SPIFFS.exists(RULES_FILE)) {
    File f = SPIFFS.open(RULES_FILE, "r");
    if (f) {
      DeserializationError error = deserializeJson(doc, f);
      f.close();
      if (!error && doc["rules"].is<JsonArray>()) return true;
    }
  }
  doc.clear();
  doc.createNestedArray("rules");
  return true;
}

bool saveRulesDocument(DynamicJsonDocument &doc) {
  File f = SPIFFS.open(RULES_FILE, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

void addDefaultRuleJson(JsonArray defaults, const char* sensor, const char* type, const char* eventName, const char* key) {
  JsonObject item = defaults.createNestedObject();
  item["sensor"] = sensor;
  item["type"] = type;
  item["event"] = eventName;
  item["key"] = key;
}

void appendDefaultSoundRules(JsonArray defaults) {
  addDefaultRuleJson(defaults, "", "doorbell", "press", "doorbell");
  addDefaultRuleJson(defaults, "mailbox", "mailbox", "flag-raised", "mailbox");
  addDefaultRuleJson(defaults, "", "motion", "detected", "motion");
  addDefaultRuleJson(defaults, "", "package", "detected", "package");
}

void handleRulesGet(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(4096);
  loadRulesDocument(doc);
  JsonArray defaults = doc.createNestedArray("defaults");
  appendDefaultSoundRules(defaults);

  String payload;
  serializeJson(doc, payload);
  request->send(200, "application/json", payload);
}

void handleRulesPost(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;

  String sensor = normalizeRuleField(requestValue(request, "sensor"));
  String type = normalizeRuleField(requestValue(request, "type"));
  String eventName = normalizeRuleField(requestValue(request, "event"));
  String key = normalizeRuleField(requestValue(request, "key"));
  String path = requestValue(request, "path");
  String removeValue = requestValue(request, "delete");
  bool shouldDelete = (removeValue == "1" || removeValue == "true" || removeValue == "on");

  if (sensor.length() == 0 && type.length() == 0 && eventName.length() == 0) {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing rule selector\"}");
    return;
  }

  DynamicJsonDocument doc(4096);
  loadRulesDocument(doc);
  JsonArray rules = doc["rules"].as<JsonArray>();

  for (int i = (int)rules.size() - 1; i >= 0; --i) {
    JsonObject rule = rules[i];
    if (normalizeRuleField(rule["sensor"] | "") == sensor &&
        normalizeRuleField(rule["type"] | "") == type &&
        normalizeRuleField(rule["event"] | "") == eventName) {
      rules.remove(i);
    }
  }

  if (!shouldDelete) {
    SoundResolution test;
    if (!resolvePathOrKey(path, key, test, "rule")) {
      request->send(404, "application/json", "{\"ok\":false,\"error\":\"Sound not found\"}");
      return;
    }

    JsonObject rule = rules.createNestedObject();
    rule["sensor"] = sensor;
    rule["type"] = type;
    rule["event"] = eventName;
    rule["key"] = test.key;
    rule["path"] = test.path;
  }

  if (!saveRulesDocument(doc)) {
    request->send(500, "application/json", "{\"ok\":false,\"error\":\"Save failed\"}");
    return;
  }

  DynamicJsonDocument response(4096);
  loadRulesDocument(response);
  response["ok"] = true;
  JsonArray defaults = response.createNestedArray("defaults");
  appendDefaultSoundRules(defaults);

  String payload;
  serializeJson(response, payload);
  request->send(200, "application/json", payload);
}

void handleSetActive(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  if (!request->hasParam("path")) {
    request->send(400, "application/json", "{\"ok\":false}");
    return;
  }
  String path = request->getParam("path")->value();
  if (!path.startsWith("/")) path = "/" + path;
  String lower = path;
  lower.toLowerCase();
  if (!(lower.endsWith(".wav") || lower.endsWith(".mp3")) || !SPIFFS.exists(path)) {
    request->send(404, "application/json", "{\"ok\":false}");
    return;
  }
  activeFilePath = path;
  saveSoundsConfig(activeFilePath, "");
  loadSoundsConfig();

  StaticJsonDocument<128> doc;
  doc["ok"] = true;
  doc["displayName"] = displayFilename;
  String payload;
  serializeJson(doc, payload);
  request->send(200, "application/json", payload);
}

void handleDelete(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  if (!request->hasParam("path")) {
    request->send(400, "application/json", "{\"ok\":false}");
    return;
  }
  String path = request->getParam("path")->value();
  if (!path.startsWith("/")) path = "/" + path;
  String lower = path;
  lower.toLowerCase();
  if (!(lower.endsWith(".wav") || lower.endsWith(".mp3"))) {
    request->send(400, "application/json", "{\"ok\":false}");
    return;
  }
  if (SPIFFS.exists(path)) {
    SPIFFS.remove(path);
  }
  if (path == activeFilePath) {
    activeFilePath = "";
  }
  saveSoundsConfig(activeFilePath, "");
  loadSoundsConfig();
  request->send(200, "application/json", "{\"ok\":true}");
}

void handlePlayByKey(AsyncWebServerRequest *request) {
  if (!requirePlaybackAuth(request)) return;
  if (!applyGainParam(request)) {
    sendTriggerResponse(request, 400, "Invalid gain");
    return;
  }
  if (!request->hasParam("key")) {
    sendTriggerResponse(request, 400, "Missing key");
    return;
  }
  String key = request->getParam("key")->value();
  key.toLowerCase();
  String path;
  String unusedName;
  if (!resolveSoundByKey(key, path, unusedName)) {
    sendTriggerResponse(request, 404, "Sound key not found");
    return;
  }
  playChimePath(path);
  recordChimeEvent("", "", "chime", "play-key", "http", key, path);
  sendTriggerResponse(request, 200, "Chime triggered OK");
}

bool resolveSoundByIndex(size_t oneBasedIndex, String &pathOut, String &nameOut) {
  if (oneBasedIndex == 0) return false;
  size_t current = 0;

  if (SPIFFS.exists(SOUNDS_FILE)) {
    File f = SPIFFS.open(SOUNDS_FILE, "r");
    if (f) {
      StaticJsonDocument<1024> src;
      DeserializationError error = deserializeJson(src, f);
      f.close();
      if (!error && src.containsKey("items")) {
        for (JsonObject item : src["items"].as<JsonArray>()) {
          String path = item["path"] | "";
          String name = item["name"] | "";
          if (path.length() == 0) continue;
          if (!SPIFFS.exists(path)) continue;
          current++;
          if (current == oneBasedIndex) {
            pathOut = path;
            nameOut = name.length() ? name : path.substring(1);
            return true;
          }
        }
      }
    }
  }

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String path = file.name();
    if (!path.startsWith("/")) path = "/" + path;
    String lower = path;
    lower.toLowerCase();
    if (lower.endsWith(".wav") || lower.endsWith(".mp3")) {
      current++;
      if (current == oneBasedIndex) {
        pathOut = path;
        nameOut = path.substring(1);
        return true;
      }
    }
    file = root.openNextFile();
  }
  return false;
}

String keyForPath(const String &path) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < path.length(); ++i) {
    h ^= (uint8_t)path[i];
    h *= 16777619u;
  }
  String key = "k" + String(h, HEX);
  key.toLowerCase();
  return key;
}

bool resolveSoundByKey(const String &key, String &pathOut, String &nameOut) {
  if (key.length() == 0) return false;

  if (SPIFFS.exists(SOUNDS_FILE)) {
    File f = SPIFFS.open(SOUNDS_FILE, "r");
    if (f) {
      StaticJsonDocument<1024> src;
      DeserializationError error = deserializeJson(src, f);
      f.close();
      if (!error && src.containsKey("items")) {
        for (JsonObject item : src["items"].as<JsonArray>()) {
          String path = item["path"] | "";
          String name = item["name"] | "";
          String itemKey = item["key"] | "";
          if (path.length() == 0) continue;
          if (!SPIFFS.exists(path)) continue;
          if (itemKey.length() == 0) itemKey = keyForPath(path);
          if (itemKey == key) {
            pathOut = path;
            nameOut = name.length() ? name : path.substring(1);
            return true;
          }
        }
      }
    }
  }

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String path = file.name();
    if (!path.startsWith("/")) path = "/" + path;
    String lower = path;
    lower.toLowerCase();
    if (lower.endsWith(".wav") || lower.endsWith(".mp3")) {
      if (keyForPath(path) == key) {
        pathOut = path;
        nameOut = path.substring(1);
        return true;
      }
    }
    file = root.openNextFile();
  }
  return false;
}

String normalizeRuleField(const String &input) {
  String out = sanitizeToken(input);
  out.toLowerCase();
  return out;
}

bool ruleFieldMatches(const String &ruleValue, const String &eventValue) {
  return ruleValue.length() == 0 || ruleValue == eventValue;
}

String defaultSoundKeyForEvent(const String &sensor, const String &type, const String &eventType) {
  String s = normalizeRuleField(sensor);
  String t = normalizeRuleField(type);
  String e = normalizeRuleField(eventType);

  if (t == "mailbox" || s.indexOf("mailbox") >= 0 || e == "flag-raised" || e == "mail") return "mailbox";
  if (t == "motion" || t == "pir" || e == "motion" || e == "detected") return "motion";
  if (t == "package" || e == "package" || e == "package-detected") return "package";
  if (t == "doorbell" || e == "press" || e == "pressed") return "doorbell";
  return "";
}

bool resolvePathOrKey(const String &path, const String &key, SoundResolution &result, const String &source) {
  String cleanPath = path;
  if (cleanPath.length() > 0 && !cleanPath.startsWith("/")) cleanPath = "/" + cleanPath;
  if (cleanPath.length() > 0 && SPIFFS.exists(cleanPath)) {
    result.path = cleanPath;
    result.key = key;
    result.source = source;
    return true;
  }

  String cleanKey = normalizeRuleField(key);
  if (cleanKey.length() > 0) {
    String resolvedPath;
    String unusedName;
    if (resolveSoundByKey(cleanKey, resolvedPath, unusedName)) {
      result.path = resolvedPath;
      result.key = cleanKey;
      result.source = source;
      return true;
    }
  }
  return false;
}

bool resolveRuleSound(const String &sensor, const String &type, const String &eventType, SoundResolution &result) {
  if (!SPIFFS.exists(RULES_FILE)) return false;
  File f = SPIFFS.open(RULES_FILE, "r");
  if (!f) return false;

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error || !doc.containsKey("rules")) return false;

  String sensorKey = normalizeRuleField(sensor);
  String typeKey = normalizeRuleField(type);
  String eventKey = normalizeRuleField(eventType);

  for (JsonObject rule : doc["rules"].as<JsonArray>()) {
    String ruleSensor = normalizeRuleField(rule["sensor"] | "");
    String ruleType = normalizeRuleField(rule["type"] | "");
    String ruleEvent = normalizeRuleField(rule["event"] | "");
    if (!ruleFieldMatches(ruleSensor, sensorKey)) continue;
    if (!ruleFieldMatches(ruleType, typeKey)) continue;
    if (!ruleFieldMatches(ruleEvent, eventKey)) continue;

    String path = rule["path"] | "";
    String key = rule["key"] | "";
    if (resolvePathOrKey(path, key, result, "rule")) return true;
  }
  return false;
}

bool resolveSensorSound(const String &explicitKey,
                        const String &sensor,
                        const String &type,
                        const String &eventType,
                        SoundResolution &result) {
  String cleanExplicitKey = normalizeRuleField(explicitKey);
  if (cleanExplicitKey.length() > 0) {
    if (resolvePathOrKey("", cleanExplicitKey, result, "explicit")) return true;
    return false;
  }

  if (resolveRuleSound(sensor, type, eventType, result)) return true;

  String defaultKey = defaultSoundKeyForEvent(sensor, type, eventType);
  if (defaultKey.length() > 0 && resolvePathOrKey("", defaultKey, result, "default")) return true;

  result.path = activeFilePath;
  result.key = "";
  result.source = "active";
  return true;
}

// ── Load sounds config ─────────────────────────────────────────────────────
void loadSoundsConfig() {
  displayFilename = "No chime loaded";
  activeFilePath = "";

  if (SPIFFS.exists(SOUNDS_FILE)) {
    File f = SPIFFS.open(SOUNDS_FILE, "r");
    if (f) {
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, f);
      f.close();
      if (!error) {
        if (doc.containsKey("active")) {
          activeFilePath = doc["active"].as<String>();
        }
        if (doc.containsKey("items")) {
          for (JsonObject item : doc["items"].as<JsonArray>()) {
            String path = item["path"] | "";
            String name = item["name"] | "";
            if (path == activeFilePath && name.length() > 0) {
              displayFilename = name;
            }
          }
        }
      }
    }
  }

  // Migration path if no config found
  if (activeFilePath.length() == 0) {
    if (SPIFFS.exists("/chime.wav")) {
      activeFilePath = "/chime.wav";
      displayFilename = "chime.wav";
    } else if (SPIFFS.exists("/chime.mp3")) {
      activeFilePath = "/chime.mp3";
      displayFilename = "chime.mp3";
    } else {
      File root = SPIFFS.open("/");
      File file = root.openNextFile();
      while (file) {
        String path = file.name();
        if (!path.startsWith("/")) path = "/" + path;
        String lower = path;
        lower.toLowerCase();
        if (lower.endsWith(".wav") || lower.endsWith(".mp3")) {
          activeFilePath = path;
          displayFilename = path.substring(1);
          break;
        }
        file = root.openNextFile();
      }
    }
  }
}

String sanitizeLabel(const String &input) {
  String out;
  out.reserve(32);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out += c;
    } else if (c == ' ' || c == '_' || c == '-' || c == '.') {
      if (out.length() == 0 || out.endsWith("-")) continue;
      out += '-';
    }
    if (out.length() >= 24) break;
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out;
}

String sanitizeBase(const String &input) {
  String out;
  out.reserve(24);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out += c;
    } else if (c == ' ' || c == '_' || c == '-' || c == '.') {
      if (out.length() == 0 || out.endsWith("-")) continue;
      out += '-';
    }
    if (out.length() >= 20) break;
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  if (out.length() == 0) out = "chime";
  return out;
}

String makeUniquePath(const String &base, const String &ext) {
  String path = "/sound-" + base + ext;
  if (!SPIFFS.exists(path)) return path;
  for (int i = 2; i < 100; ++i) {
    String candidate = "/sound-" + base + "-" + String(i) + ext;
    if (!SPIFFS.exists(candidate)) return candidate;
  }
  return "/sound-" + base + "-" + String(millis() % 1000) + ext;
}

void saveSoundsConfig(const String &activePath, const String &activeName) {
  StaticJsonDocument<1536> existing;
  if (SPIFFS.exists(SOUNDS_FILE)) {
    File e = SPIFFS.open(SOUNDS_FILE, "r");
    if (e) {
      deserializeJson(existing, e);
      e.close();
    }
  }

  StaticJsonDocument<1536> doc;
  doc["active"] = activePath;
  JsonArray items = doc.createNestedArray("items");

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String path = file.name();
    if (!path.startsWith("/")) path = "/" + path;
    String lower = path;
    lower.toLowerCase();
    if (lower.endsWith(".wav") || lower.endsWith(".mp3")) {
      JsonObject item = items.createNestedObject();
      item["path"] = path;
      String name = "";
      String key = "";
      if (existing.containsKey("items")) {
        for (JsonObject prev : existing["items"].as<JsonArray>()) {
          const char* prevPath = prev["path"] | "";
          if (String(prevPath) == path) {
            name = prev["name"] | "";
            key = prev["key"] | "";
          }
        }
      }
      if (path == activePath && activeName.length() > 0) name = activeName;
      if (name.length() == 0) name = path.substring(1);
      if (key.length() == 0) key = keyForPath(path);
      item["name"] = name;
      item["key"] = key;
    }
    file = root.openNextFile();
  }

  File f = SPIFFS.open(SOUNDS_FILE, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

void loadDeviceConfig() {
  deviceLabel = "";
  lanDnsSuffix = "";
  authToken = "";
  playbackAuth = false;
  if (!SPIFFS.exists(DEVICE_FILE)) return;
  File f = SPIFFS.open(DEVICE_FILE, "r");
  if (!f) return;

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();

  if (!error && doc.containsKey("label")) {
    deviceLabel = doc["label"].as<String>();
  }
  if (!error && doc.containsKey("token")) {
    authToken = sanitizeToken(doc["token"].as<String>());
  }
  if (!error && doc.containsKey("lanDnsSuffix")) {
    lanDnsSuffix = sanitizeLanDnsSuffix(doc["lanDnsSuffix"].as<String>());
  }
  if (!error && doc.containsKey("playbackAuth")) {
    playbackAuth = doc["playbackAuth"].as<bool>();
  }
}

void saveDeviceConfig(const String &label) {
  StaticJsonDocument<512> doc;
  doc["label"] = label;
  doc["lanDnsSuffix"] = lanDnsSuffix;
  if (authToken.length() > 0) doc["token"] = authToken;
  doc["playbackAuth"] = playbackAuth;
  File f = SPIFFS.open(DEVICE_FILE, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// ── Playback ───────────────────────────────────────────────────────────────
void stopPlayback() {
  if (wav && wav->isRunning()) wav->stop();
  if (mp3 && mp3->isRunning()) mp3->stop();
  if (id3) { delete id3; id3 = nullptr; }
  if (fileSource) { delete fileSource; fileSource = nullptr; }
  if (bootSource) { delete bootSource; bootSource = nullptr; }
}

void playBootSound() {
  if (Startup_Signal_mp3_len == 0 || !out) return;
  stopPlayback();
  Serial.printf("Boot sound: %u bytes\n", Startup_Signal_mp3_len);
  bootSource = new AudioFileSourcePROGMEM(Startup_Signal_mp3, Startup_Signal_mp3_len);
  if (!bootSource) {
    Serial.println("Boot sound: source allocation failed");
    return;
  }
  if (!mp3) mp3 = new AudioGeneratorMP3();
  out->SetGain(currentGain);
  mp3->begin(bootSource, out);

  // Keep setup responsive: cap boot-chime playback window.
  unsigned long startMs = millis();
  while (mp3->isRunning() && (millis() - startMs) < 5000) {
    if (!mp3->loop()) break;
    delay(1);
  }
  Serial.println("Boot sound: done");
  stopPlayback();
}

void playChime() {
  Serial.println("playChime() called");
  if (activeFilePath.length() == 0 || !SPIFFS.exists(activeFilePath)) {
    Serial.println("  No file: " + activeFilePath);
    return;
  }
  File test = SPIFFS.open(activeFilePath, "r");
  if (!test || test.size() == 0) {
    Serial.println("  File empty or cannot open");
    if (test) test.close();
    return;
  }
  test.close();

  stopPlayback();

  Serial.println("  Opening " + activeFilePath);
  fileSource = new AudioFileSourceSPIFFS(activeFilePath.c_str());
  if (!fileSource->isOpen()) {
    Serial.println("  Failed to open file");
    delete fileSource;
    fileSource = nullptr;
    return;
  }
  Serial.printf("  File size: %u bytes\n", fileSource->getSize());
  fileSource->seek(0, SEEK_SET);
  out->SetGain(currentGain);

  String lower = activeFilePath;
  lower.toLowerCase();
  if (lower.endsWith(".mp3")) {
    if (!mp3) mp3 = new AudioGeneratorMP3();
    id3 = new AudioFileSourceID3(fileSource);
    mp3->begin(id3, out);
    Serial.println("  MP3 playback started");
  } else {
    if (!wav) wav = new AudioGeneratorWAV();
    wav->begin(fileSource, out);
    Serial.println("  WAV playback started");
  }
}

bool playChimePath(const String &path) {
  if (path.length() == 0 || !SPIFFS.exists(path)) return false;
  String previous = activeFilePath;
  activeFilePath = path;
  playChime();
  activeFilePath = previous;
  return true;
}

// ── Webhook trigger ────────────────────────────────────────────────────────
void handleChime(AsyncWebServerRequest *request) {
  if (!requirePlaybackAuth(request)) return;
  if (!applyGainParam(request)) {
    sendTriggerResponse(request, 400, "Invalid gain");
    return;
  }

  if (request->hasParam("idx")) {
    int idx = request->getParam("idx")->value().toInt();
    if (idx <= 0) {
      sendTriggerResponse(request, 400, "Invalid index");
      return;
    }
    String path;
    String unusedName;
    if (!resolveSoundByIndex((size_t)idx, path, unusedName)) {
      sendTriggerResponse(request, 404, "Sound index not found");
      return;
    }
    playChimePath(path);
    recordChimeEvent("", "", "chime", "play-index", "http", String(idx), path);
    sendTriggerResponse(request, 200, "Chime triggered OK");
    return;
  }

  playChime();
  recordChimeEvent("", "", "chime", "play-active", "http", "", activeFilePath);
  sendTriggerResponse(request, 200, "Chime triggered OK");
}

void handleSensorTrigger(AsyncWebServerRequest *request) {
  if (!requirePlaybackAuth(request)) return;
  if (!applyGainParam(request)) {
    sendTriggerResponse(request, 400, "Invalid gain");
    return;
  }

  String sensorId = request->hasParam("sensor") ? request->getParam("sensor")->value() : "";
  String sensorType = request->hasParam("type") ? request->getParam("type")->value() : "";
  String eventType = request->hasParam("event") ? request->getParam("event")->value() : "";
  String eventId = request->hasParam("eventId") ? request->getParam("eventId")->value() : "";
  String soundKey = request->hasParam("sound") ? request->getParam("sound")->value() : "";
  if (soundKey.length() == 0 && request->hasParam("key")) {
    soundKey = request->getParam("key")->value();
  }
  soundKey.toLowerCase();

  if (hasSeenEventId(eventId)) {
    sendTriggerResponse(request, 200, "Duplicate trigger ignored");
    return;
  }

  SoundResolution sound;
  if (!resolveSensorSound(soundKey, sensorId, sensorType, eventType, sound)) {
    if (soundKey.length() > 0) {
      sendTriggerResponse(request, 404, "Sound key not found");
      return;
    }
  }

  if (sound.path == activeFilePath) {
    playChime();
  } else {
    playChimePath(sound.path);
  }
  recordChimeEvent(eventId, sensorId, sensorType, eventType, "http", sound.key, sound.path);
  sendTriggerResponse(request, 200, "Sensor trigger OK");
}

// ── Set gain ───────────────────────────────────────────────────────────────
void handleSetGain(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  if (request->hasParam("value")) {
    float newGain = request->getParam("value")->value().toFloat();
    if (newGain >= 0.0 && newGain <= 3.0) {
      currentGain = newGain;
      out->SetGain(currentGain);
      Serial.printf("Gain set to %.2f\n", currentGain);
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Invalid gain");
    }
  } else {
    request->send(400, "text/plain", "Missing value");
  }
}

void handleSetLabel(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  String raw = "";
  if (request->hasParam("label", true)) {
    raw = request->getParam("label", true)->value();
  } else if (request->hasParam("label")) {
    raw = request->getParam("label")->value();
  } else {
    request->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing label\"}");
    return;
  }

  String newLabel = sanitizeLabel(raw);
  if (newLabel != deviceLabel) {
    deviceLabel = newLabel;
  }

  String suffixRaw = lanDnsSuffix;
  if (request->hasParam("lanDnsSuffix", true)) {
    suffixRaw = request->getParam("lanDnsSuffix", true)->value();
  } else if (request->hasParam("lanDnsSuffix")) {
    suffixRaw = request->getParam("lanDnsSuffix")->value();
  }
  lanDnsSuffix = sanitizeLanDnsSuffix(suffixRaw);
  saveDeviceConfig(deviceLabel);

  mdnsName = buildMdnsName(deviceLabel);
  WiFi.setHostname(mdnsName.c_str());
  if (mdnsOk) MDNS.end();
  startMdnsNow(mdnsName);

  StaticJsonDocument<512> doc;
  doc["ok"] = true;
  doc["label"] = deviceLabel;
  doc["mdns"] = mdnsName + ".local";
  doc["lanDns"] = buildLanDnsHost(mdnsName, lanDnsSuffix);
  doc["lanDnsSuffix"] = lanDnsSuffix;
  String payload;
  serializeJson(doc, payload);
  request->send(200, "application/json", payload);
}

void handleSetSecurity(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;

  if (request->hasParam("newToken", true)) {
    authToken = sanitizeToken(request->getParam("newToken", true)->value());
  } else if (request->hasParam("newToken")) {
    authToken = sanitizeToken(request->getParam("newToken")->value());
  }

  if (request->hasParam("playbackAuth", true)) {
    String val = request->getParam("playbackAuth", true)->value();
    playbackAuth = (val == "1" || val == "true" || val == "on");
  } else if (request->hasParam("playbackAuth")) {
    String val = request->getParam("playbackAuth")->value();
    playbackAuth = (val == "1" || val == "true" || val == "on");
  }

  saveDeviceConfig(deviceLabel);

  StaticJsonDocument<128> doc;
  doc["ok"] = true;
  doc["authEnabled"] = authToken.length() > 0;
  doc["playbackAuth"] = playbackAuth;
  String payload;
  serializeJson(doc, payload);
  request->send(200, "application/json", payload);
}

void handleResetWiFi(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  WiFi.disconnect(true, true); // clear credentials from NVS
  scheduleRestart(600);
  request->send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
}

// ── Clean all user files ───────────────────────────────────────────────────
void handleClean(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  int count = 0;
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    String path = file.name();
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.startsWith("/System Volume") && !path.startsWith("/.Trash") && path != DEVICE_FILE) {  // skip macOS junk + device label
      SPIFFS.remove(path);
      count++;
    }
    file = root.openNextFile();
  }

  Serial.printf("Cleaned %d files\n", count);
  loadSoundsConfig();
  request->send(200, "text/plain", String("Cleaned ") + count + " files. Refresh page.");
}

// ── Root page ──────────────────────────────────────────────────────────────
static const char ROOT_PAGE_TEMPLATE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Doorbell Chime</title>
  <style>
    :root {
      --bg:#090d14;
      --panel:#121923;
      --panel-2:#182230;
      --edge:#2b3a4c;
      --text:#f4f7fb;
      --muted:#8fa0b3;
      --blue:#2f7df6;
      --blue-hi:#5aa2ff;
      --cyan:#4fd1ff;
      --amber:#f59e0b;
      --red:#ef4444;
    }
    * {box-sizing:border-box;}
    body {
      font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
      margin:0;
      min-height:100vh;
      padding:1.25rem;
      background:
        radial-gradient(circle at 50% -10%, rgba(47,125,246,0.22), transparent 42%),
        linear-gradient(180deg, #101827 0%, var(--bg) 100%);
      color:var(--text);
      display:flex;
      align-items:center;
      justify-content:center;
    }
    .card {
      width:100%;
      max-width:520px;
      background:linear-gradient(180deg, rgba(24,34,48,0.98), rgba(13,19,29,0.98));
      border:1px solid rgba(143,160,179,0.22);
      border-radius:22px;
      padding:1.45rem;
      box-shadow:0 24px 70px rgba(0,0,0,0.45), inset 0 1px 0 rgba(255,255,255,0.06);
    }
    .topline {display:flex; align-items:flex-start; justify-content:space-between; gap:1rem; margin-bottom:1.1rem;}
    h1 {font-size:1.45rem; line-height:1.1; margin:0; letter-spacing:0; color:var(--text);}
    .device-id {margin-top:0.35rem; color:var(--muted); font-size:0.86rem; overflow-wrap:anywhere;}
    .status-pill {
      color:#c7d2fe;
      border:1px solid rgba(99,102,241,0.35);
      background:rgba(37,99,235,0.14);
      border-radius:999px;
      padding:0.35rem 0.65rem;
      font-size:0.78rem;
      white-space:nowrap;
    }
    .active-panel {
      background:linear-gradient(180deg, rgba(255,255,255,0.055), rgba(255,255,255,0.025));
      border:1px solid rgba(143,160,179,0.18);
      border-radius:16px;
      padding:0.9rem;
      margin-bottom:1.15rem;
    }
    .label {font-size:0.75rem; color:var(--muted); text-transform:uppercase; letter-spacing:0.08em; margin-bottom:0.35rem;}
    .file-info {font-size:1rem; color:var(--text); line-height:1.35; overflow-wrap:anywhere;}
    .play-wrap {display:flex; justify-content:center; margin:0.85rem 0 1.05rem;}
    button {font:inherit;}
    .play-btn {
      position:relative;
      isolation:isolate;
      min-width:172px;
      height:56px;
      border:none;
      border-radius:999px;
      color:white;
      cursor:pointer;
      background:
        linear-gradient(105deg, transparent 0 34%, rgba(255,255,255,0.13) 42%, transparent 52%),
        linear-gradient(180deg, rgba(255,255,255,0.24), rgba(255,255,255,0.06) 45%, rgba(255,255,255,0.02)),
        rgba(164,182,205,0.16);
      border:1px solid rgba(236,244,255,0.28);
      box-shadow:
        0 12px 24px rgba(0,0,0,0.32),
        0 0 0 1px rgba(255,255,255,0.08),
        inset 0 1px 2px rgba(255,255,255,0.72),
        inset 0 -7px 14px rgba(0,0,0,0.24);
      backdrop-filter:blur(10px);
      -webkit-backdrop-filter:blur(10px);
      overflow:hidden;
      transition:transform 0.08s ease, box-shadow 0.08s ease, filter 0.15s ease, border-color 0.15s ease;
      display:flex;
      flex-direction:row;
      align-items:center;
      justify-content:center;
      gap:0.6rem;
      padding:0 1.15rem;
      text-shadow:0 1px 7px rgba(255,255,255,0.22);
    }
    .play-btn::before {
      content:"";
      position:absolute;
      inset:3px 5px auto 5px;
      height:40%;
      border-radius:999px;
      background:linear-gradient(180deg, rgba(255,255,255,0.34), rgba(255,255,255,0.04));
      opacity:0.62;
      z-index:-1;
    }
    .play-btn::after {
      content:"";
      position:absolute;
      inset:-30% -18%;
      background:
        linear-gradient(118deg, transparent 0 45%, rgba(255,255,255,0.18) 49%, transparent 54%),
        radial-gradient(circle at 82% 20%, rgba(79,209,255,0.18), transparent 26%);
      opacity:0.72;
      z-index:-2;
    }
    .play-btn:hover:not(:disabled) {
      filter:brightness(1.08);
      border-color:rgba(236,244,255,0.42);
    }
    .play-btn:active:not(:disabled) {
      transform:translateY(3px) scale(0.995);
      box-shadow:
        0 7px 16px rgba(0,0,0,0.32),
        0 0 0 1px rgba(255,255,255,0.06),
        inset 0 1px 2px rgba(255,255,255,0.58),
        inset 0 -4px 10px rgba(0,0,0,0.24);
    }
    .play-btn:disabled {
      cursor:not-allowed;
      color:#94a3b8;
      background:rgba(100,116,139,0.16);
      border-color:rgba(148,163,184,0.18);
      box-shadow:inset 0 1px 0 rgba(255,255,255,0.08);
    }
    .play-icon {
      width:0;
      height:0;
      border-top:8px solid transparent;
      border-bottom:8px solid transparent;
      border-left:13px solid currentColor;
      margin-left:0.15rem;
      filter:drop-shadow(0 2px 3px rgba(0,0,0,0.28));
    }
    .play-label {font-size:0.98rem; font-weight:700;}
    .metrics {display:grid; grid-template-columns:1fr 1fr; gap:0.8rem; margin-bottom:1rem;}
    .info-box {
      background:rgba(4,9,16,0.36);
      border:1px solid rgba(143,160,179,0.16);
      border-radius:14px;
      padding:0.75rem;
    }
    .info-row {display:flex; justify-content:space-between; align-items:center; gap:0.75rem; font-size:0.84rem; color:var(--muted); margin-bottom:0.55rem;}
    .info-row span:last-child {color:#d8e2ee; text-align:right;}
    .bar {width:100%; height:8px; background:#273244; border-radius:999px; overflow:hidden; box-shadow:inset 0 1px 2px rgba(0,0,0,0.35);}
    .fill {height:100%; background:linear-gradient(90deg, var(--blue), var(--cyan));}
    .fill.warn {background:linear-gradient(90deg, var(--amber), #fbbf24);}
    .fill.bad {background:linear-gradient(90deg, var(--red), #fb7185);}
    .actions {display:flex; justify-content:center; margin-top:0.25rem;}
    .manage-btn {
      background:linear-gradient(180deg, #243246, #182230);
      color:#d8e2ee;
      border:1px solid rgba(143,160,179,0.25);
      padding:0.7rem 1rem;
      border-radius:10px;
      font-size:0.95rem;
      text-decoration:none;
      box-shadow:0 5px 0 #0d131d, inset 0 1px 0 rgba(255,255,255,0.08);
      cursor:pointer;
      min-width:160px;
    }
    .manage-btn:hover {filter:brightness(1.08);}
    .manage-btn:active {transform:translateY(3px); box-shadow:0 2px 0 #0d131d, inset 0 1px 0 rgba(255,255,255,0.08);}
    @media (max-width:520px) {
      body {padding:0.75rem; align-items:flex-start;}
      .card {padding:1rem; border-radius:18px;}
      .topline {display:block;}
      .status-pill {display:inline-flex; margin-top:0.85rem;}
      .play-btn {min-width:168px; height:54px;}
      .metrics {grid-template-columns:1fr;}
    }
  </style>
</head>
<body>
  <div class="card">
    <div class="topline">
      <div>
        <h1>Doorbell Chime</h1>
        <div class="device-id">__MDNS_HOST__ · __IPADDR__</div>
      </div>
      <div class="status-pill">__STATUS__</div>
    </div>

    <div class="active-panel">
      <div class="label">Active Sound</div>
      <div class="file-info" id="fileInfo">__FILEINFO__</div>
    </div>

    <div class="play-wrap">
      __PLAYBTN__
    </div>

    <div class="metrics">
      <div class="info-box" id="wifiBox">
        <div class="info-row">
          <span>Wi‑Fi</span>
          <span id="wifiText">__WIFITEXT__</span>
        </div>
        <div class="bar"><div id="wifiBar" class="fill__WIFICLASS__" style="width:__WIFIPCT__%"></div></div>
      </div>

      <div class="info-box" id="fsBox">
        <div class="info-row">
          <span>Storage</span>
          <span id="fsText">__FSTEXT__</span>
        </div>
        <div class="bar"><div id="fsBar" class="fill__FSCLASS__" style="width:__FSPCT__%"></div></div>
      </div>
    </div>

    <div class="actions">
      <a href="/manage"><button class="manage-btn">Manage Chimes</button></a>
    </div>
  </div>

  <script>
    const wifiText = document.getElementById('wifiText');
    const wifiBar  = document.getElementById('wifiBar');
    const fsText   = document.getElementById('fsText');
    const fsBar    = document.getElementById('fsBar');
    const playBtn  = document.getElementById('playBtn');

    function withToken(url) {
      const token = localStorage.getItem('doorbellAuthToken') || '';
      if (!token) return url;
      return `${url}${url.includes('?') ? '&' : '?'}token=${encodeURIComponent(token)}`;
    }

    function barClass(pct, warnAt, badAt) {
      if (pct < badAt) return 'fill bad';
      if (pct < warnAt) return 'fill warn';
      return 'fill';
    }

    function refreshStatus() {
      fetch('/status')
        .then(r => r.json())
        .then(s => {
          const signalPct = s.signalPct ?? 0;
          const rssi = s.rssi ?? -100;
          const wifi = s.wifi || 'disconnected';
          wifiText.textContent = (wifi === 'connected')
            ? `${rssi} dBm (${signalPct}%)`
            : 'Disconnected';
          wifiBar.style.width = `${signalPct}%`;
          wifiBar.className = barClass(signalPct, 60, 30);

          const usedPct = s.fsUsedPct ?? 0;
          const freeKB = (s.fsFreeKB ?? 0).toFixed(1);
          const totalKB = (s.fsTotalKB ?? 0).toFixed(1);
          fsText.textContent = `${freeKB} kB free of ${totalKB} kB`;
          fsBar.style.width = `${usedPct}%`;
          fsBar.className = barClass(100 - usedPct, 30, 15);
        })
        .catch(() => {});
    }

    setInterval(refreshStatus, 10000);
    if (playBtn) playBtn.addEventListener('click', () => fetch(withToken('/chime')).catch(() => {}));
  </script>
</body>
</html>
)rawliteral";

void handleRoot(AsyncWebServerRequest *request) {
  bool hasChime = SPIFFS.exists(activeFilePath);
  String statusLine = hasChime ? "Chime Loaded" : "Chime Missing";
  String fileInfo = "No chime loaded";
  if (hasChime) {
    File f = SPIFFS.open(activeFilePath, "r");
    if (f) {
      size_t sz = f.size();
      f.close();
      fileInfo = displayFilename + " (" + String(sz / 1024.0, 1) + " kB)";
    }
  }

  String playButtonHTML = hasChime ?
    "<button id=\"playBtn\" class=\"play-btn\" type=\"button\" title=\"Play the active chime now\"><span class=\"play-icon\" aria-hidden=\"true\"></span><span class=\"play-label\">Play</span></button>" :
    "<button class=\"play-btn\" disabled title=\"Upload or select a chime before playing\"><span class=\"play-icon\" aria-hidden=\"true\"></span><span class=\"play-label\">No Chime</span></button>";

  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  int signalPct = rssi <= -100 ? 0 : (rssi >= -50 ? 100 : (rssi + 100) * 2);
  String signalText = (WiFi.status() == WL_CONNECTED)
    ? String(rssi) + " dBm (" + String(signalPct) + "%)"
    : "Disconnected";

  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  size_t freeBytes = totalBytes >= usedBytes ? (totalBytes - usedBytes) : 0;
  int usedPct = totalBytes ? int((usedBytes * 100) / totalBytes) : 0;
  String fsText = String(freeBytes / 1024.0, 1) + " kB free of " + String(totalBytes / 1024.0, 1) + " kB";
  String ipText = WiFi.isConnected() ? WiFi.localIP().toString() : "not connected";
  String mdnsHost = buildMdnsName(deviceLabel) + ".local";

  String html = FPSTR(ROOT_PAGE_TEMPLATE);
  html.replace("__STATUS__", statusLine);
  html.replace("__FILEINFO__", fileInfo);
  html.replace("__IPADDR__", ipText);
  html.replace("__PLAYBTN__", playButtonHTML);
  html.replace("__WIFITEXT__", signalText);
  html.replace("__WIFIPCT__", String(signalPct));
  html.replace("__WIFICLASS__", String(signalPct < 30 ? " bad" : (signalPct < 60 ? " warn" : "")));
  html.replace("__FSTEXT__", fsText);
  html.replace("__FSPCT__", String(usedPct));
  html.replace("__FSCLASS__", String(usedPct > 85 ? " bad" : (usedPct > 70 ? " warn" : "")));
  html.replace("__GAIN100__", String(int(currentGain * 100)));
  html.replace("__GAIN__", String(currentGain, 2));
  html.replace("__DEVICE_LABEL__", deviceLabel);
  html.replace("__MDNS_HOST__", mdnsHost);

  AsyncWebServerResponse *response = request->beginResponse(200, "text/html", html);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

// ── Upload form ────────────────────────────────────────────────────────────
static const char UPLOAD_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Manage Chimes</title>
  <style>
    :root {
      --primary: #5aa2ff;
      --primary-dark: #2f7df6;
      --bg: #090d14;
      --card: rgba(18,25,35,0.96);
      --panel: rgba(255,255,255,0.055);
      --panel-strong: rgba(255,255,255,0.085);
      --text: #f4f7fb;
      --text-light: #8fa0b3;
      --danger: #fb7185;
      --border-rest: rgba(143,160,179,0.28);
      --border-hover: rgba(90,162,255,0.55);
      --drop-bg: rgba(4,9,16,0.34);
      --drop-hover: rgba(90,162,255,0.1);
      --drop-active: rgba(90,162,255,0.16);
    }
    * { box-sizing: border-box; }
    body {
      font-family: system-ui, -apple-system, sans-serif;
      background:
        radial-gradient(circle at 50% -10%, rgba(47,125,246,0.22), transparent 42%),
        linear-gradient(180deg, #101827 0%, var(--bg) 100%);
      color: var(--text);
      margin: 0;
      padding: 1.5rem;
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: flex-start;
    }
    .card {
      background: var(--card);
      border-radius: 12px;
      padding: 1.6rem 1.75rem;
      border:1px solid rgba(143,160,179,0.22);
      box-shadow:0 24px 70px rgba(0,0,0,0.45), inset 0 1px 0 rgba(255,255,255,0.06);
      width: 100%;
      max-width: 1120px;
    }
    h1 {
      color: var(--text);
      font-size: 1.8rem;
      margin: 0;
      text-align: left;
      font-weight: 600;
    }
    .topbar {
      display:flex;
      justify-content:space-between;
      align-items:flex-start;
      gap:1rem;
      margin-bottom:1rem;
    }
    .home-link {
      color:#d8e2ee;
      text-decoration:none;
      font-size:0.95rem;
      font-weight:500;
      white-space:nowrap;
      margin-top:0.25rem;
    }
    .home-link:hover {color:#ffffff; text-decoration:underline;}
    .status {
      display:grid;
      grid-template-columns:minmax(0, 1fr) auto;
      gap:0.4rem 1rem;
      align-items:start;
      margin-bottom: 1rem;
      padding-bottom:1rem;
      border-bottom:1px solid rgba(143,160,179,0.18);
    }
    .status .big {
      font-size: 1rem;
      font-weight: 700;
      color: var(--text);
      grid-column:1 / -1;
    }
    .status .sub {
      color: var(--text-light);
      font-size: 0.95rem;
      word-break: break-word;
    }
    .status .tiny {
      color: var(--text-light);
      font-size: 0.8rem;
      text-align:right;
    }
    .metrics-grid {display:grid; grid-template-columns:1fr 1fr; gap:1rem; margin-bottom:1rem;}
    .info-box {margin:0; text-align:left;}
    .info-row {display:flex; justify-content:space-between; align-items:center; font-size:0.95rem; color:var(--text-light); margin-bottom:0.4rem;}
    .info-row span:last-child {color:#d8e2ee;}
    .bar {width:100%; height:10px; background:#273244; border-radius:999px; overflow:hidden; box-shadow:inset 0 1px 2px rgba(0,0,0,0.35);}
    .fill {height:100%; background:linear-gradient(90deg, var(--primary-dark), #4fd1ff);}
    .fill.warn {background:#f59e0b;}
    .fill.bad {background:#ef4444;}
    .tabbar {display:none;}
    .manage-grid {
      display:grid;
      grid-template-columns:minmax(0, 1fr) 360px;
      gap:1.25rem;
      align-items:start;
    }
    .side-stack {display:grid; gap:1rem;}
    .section {
      margin: 0;
      padding: 1rem;
      border: 1px solid rgba(143,160,179,0.18);
      border-radius: 8px;
      background:linear-gradient(180deg, rgba(255,255,255,0.055), rgba(255,255,255,0.025));
      box-shadow:inset 0 1px 0 rgba(255,255,255,0.04);
    }
    .section h2 {
      margin: 0 0 0.8rem;
      font-size: 1rem;
      color: #d8e2ee;
      text-align: left;
    }
    .list-item {
      display:grid;
      grid-template-columns:22px minmax(0, 1fr) 36px 36px;
      align-items:center;
      column-gap:0.2rem;
      min-height:44px;
      padding:0;
      line-height:1.2;
      text-align:left;
    }
    .list-item input[type=radio] {margin:0;}
    .list-item label {cursor:pointer; margin:0; min-width:0; overflow-wrap:anywhere;}
    .trash-btn {
      background: transparent;
      border: none;
      color: var(--text-light);
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      padding: 0;
      margin: 0;
      height: 44px;
      width: 36px;
      line-height: 1;
      touch-action: manipulation;
      -webkit-tap-highlight-color: transparent;
    }
    .trash-btn:hover { color:#fb7185; }
    .trash-btn svg { width: 18px; height: 18px; display:block; pointer-events:none; }
    .play-btn {
      background: transparent;
      border: none;
      color: #70b7ff;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      padding: 0;
      margin: 0;
      height: 44px;
      width: 36px;
      line-height: 1;
      touch-action: manipulation;
      -webkit-tap-highlight-color: transparent;
    }
    .play-btn:hover { color:#ffffff; }
    .play-btn svg { width: 18px; height: 18px; display:block; pointer-events:none; }
    .network-box {text-align:left; background:rgba(4,9,16,0.28); border:1px solid rgba(143,160,179,0.16); border-radius:10px; padding:0.8rem;}
    .network-title {font-size:0.9rem; font-weight:600; color:#d8e2ee; margin-bottom:0.5rem;}
    .network-row {display:grid; grid-template-columns:minmax(0, 1fr) auto auto; gap:0.4rem; align-items:center;}
    .network-row input {flex:1; color:var(--text); background:rgba(4,9,16,0.42); border:1px solid var(--border-rest); border-radius:8px; padding:0.45rem 0.55rem; font-size:0.9rem;}
    .network-row button {width:auto; min-width:auto; margin:0; padding:0.5rem 0.75rem; font-size:0.85rem; line-height:1.1; border-radius:8px; white-space:nowrap;}
    .network-help {margin-top:0.45rem; font-size:0.78rem; color:var(--text-light); text-align:left;}
    .save-state {margin-top:0.45rem; min-height:1rem; font-size:0.78rem; color:var(--text-light);}
    .save-state.error {color:#fb7185;}
    .save-state.ok {color:#86efac;}
    .dns-options {display:grid; gap:0.35rem; margin-top:0.75rem; font-size:0.85rem; color:#d8e2ee;}
    .dns-options label {display:flex; gap:0.45rem; align-items:center;}
    .dns-options input[type=radio] {width:auto;}
    .dns-custom {display:grid; grid-template-columns:auto minmax(0, 1fr); gap:0.45rem; align-items:center;}
    .dns-custom input[type=text] {color:var(--text); background:rgba(4,9,16,0.42); border:1px solid var(--border-rest); border-radius:8px; padding:0.42rem 0.55rem; font-size:0.85rem;}
    .advanced-box {margin-top:0.75rem; border-top:1px solid rgba(143,160,179,0.18); padding-top:0.65rem;}
    .advanced-box summary {cursor:pointer; color:#d8e2ee; font-size:0.88rem; font-weight:600;}
    .advanced-body {margin-top:0.65rem;}
    .security-row {display:grid; grid-template-columns:minmax(0, 1fr) auto; gap:0.4rem; align-items:center; margin-top:0.5rem;}
    .security-row input[type=password] {flex:1; color:var(--text); background:rgba(4,9,16,0.42); border:1px solid var(--border-rest); border-radius:8px; padding:0.45rem 0.55rem; font-size:0.9rem;}
    .security-row button {width:auto; min-width:auto; margin:0; padding:0.5rem 0.75rem; font-size:0.85rem; line-height:1.1; border-radius:8px; white-space:nowrap;}
    .security-notice {display:none; margin-top:0.75rem; padding:0.75rem; border:1px solid rgba(245,158,11,0.58); background:rgba(245,158,11,0.1); border-radius:8px; color:#fed7aa; font-size:0.86rem;}
    .security-notice .notice-actions {display:flex; gap:0.5rem; margin-top:0.65rem;}
    .security-notice button {width:auto; min-width:auto; margin:0; padding:0.45rem 0.75rem; font-size:0.85rem; border-radius:8px;}
    .security-notice .notice-secondary {background:transparent; color:#fed7aa; border:1px solid rgba(245,158,11,0.38);}
    .security-notice .notice-secondary:hover:not(:disabled) {background:rgba(245,158,11,0.14);}
    .security-explain {margin-top:0.55rem; font-size:0.78rem; line-height:1.35; color:var(--text-light);}
    .check-row {display:flex; gap:0.45rem; align-items:center; margin-top:0.55rem; font-size:0.85rem; color:#d8e2ee;}
    .check-row input {width:auto;}
    .event-list {display:grid; gap:0.45rem;}
    .event-empty {color:var(--text-light); font-size:0.9rem;}
    .event-item {
      display:grid;
      grid-template-columns:minmax(0, 1fr) auto;
      gap:0.2rem 0.75rem;
      padding:0.65rem 0;
      border-bottom:1px solid rgba(143,160,179,0.14);
    }
    .event-item:last-child {border-bottom:none;}
    .event-main {min-width:0; color:#d8e2ee; font-size:0.92rem; overflow-wrap:anywhere;}
    .event-age {color:var(--text-light); font-size:0.78rem; white-space:nowrap;}
    .event-meta {grid-column:1 / -1; color:var(--text-light); font-size:0.78rem; overflow-wrap:anywhere;}
    .event-actions {display:flex; justify-content:flex-end; margin-top:0.75rem;}
    .event-actions button {width:auto; min-width:auto; margin:0; padding:0.5rem 0.75rem; font-size:0.85rem; line-height:1.1; border-radius:8px;}
    .rules-form {display:grid; gap:0.55rem; margin-bottom:0.85rem;}
    .rules-row {display:grid; grid-template-columns:repeat(3, minmax(0, 1fr)); gap:0.45rem;}
    .rules-field {display:grid; gap:0.25rem;}
    .rules-field label {color:var(--text-light); font-size:0.76rem; font-weight:600;}
    .rules-form input,
    .rules-form select {
      width:100%;
      color:var(--text);
      background:rgba(4,9,16,0.42);
      border:1px solid var(--border-rest);
      border-radius:8px;
      padding:0.48rem 0.55rem;
      font-size:0.86rem;
    }
    .rules-actions {display:grid; grid-template-columns:1fr auto; gap:0.45rem; align-items:center;}
    .rules-actions button {width:auto; min-width:auto; margin:0; padding:0.55rem 0.8rem; font-size:0.85rem; line-height:1.1; border-radius:8px;}
    .rule-list {display:grid; gap:0.45rem;}
    .rule-empty {color:var(--text-light); font-size:0.9rem;}
    .rule-item {
      display:grid;
      grid-template-columns:minmax(0, 1fr) auto auto;
      gap:0.2rem 0.65rem;
      padding:0.65rem 0;
      border-bottom:1px solid rgba(143,160,179,0.14);
    }
    .rule-item:last-child {border-bottom:none;}
    .rule-main {min-width:0; color:#d8e2ee; font-size:0.92rem; overflow-wrap:anywhere;}
    .rule-meta {grid-column:1 / -1; color:var(--text-light); font-size:0.78rem; overflow-wrap:anywhere;}
    .rule-delete {
      width:auto;
      height:30px;
      min-width:30px;
      margin:0;
      padding:0 0.55rem;
      border-radius:8px;
      background:rgba(255,255,255,0.055);
      border:1px solid rgba(143,160,179,0.16);
      box-shadow:none;
      color:var(--danger);
      font-size:0.8rem;
    }
    .rule-edit {
      width:auto;
      height:30px;
      min-width:30px;
      margin:0;
      padding:0 0.55rem;
      border-radius:8px;
      background:rgba(255,255,255,0.055);
      border:1px solid rgba(143,160,179,0.16);
      box-shadow:none;
      color:#d8e2ee;
      font-size:0.8rem;
    }
    .rule-save-state {min-height:1rem; font-size:0.78rem; color:var(--text-light);}
    .rule-save-state.error {color:#fb7185;}
    .rule-save-state.ok {color:#86efac;}
    .btn-secondary {background:linear-gradient(180deg, rgba(148,163,184,0.22), rgba(71,85,105,0.32));}
    .btn-secondary:hover:not(:disabled) {background:linear-gradient(180deg, rgba(148,163,184,0.3), rgba(71,85,105,0.42));}
    .volume-box {margin-top: 0.5rem; padding:0.8rem; background:rgba(4,9,16,0.28); border:1px solid rgba(143,160,179,0.16); border-radius:8px; text-align:center;}
    .slider-container {margin:0.4rem 0;}
    input[type=range] {width:90%; max-width:400px; height:12px;}
    #gainDisplay {font-size:1.4em; font-weight:bold; margin:0.5rem 0;}
    #fileName {
      font-weight: 600;
      color: #d8e2ee;
      margin: 0.6rem 0 0.3rem;
      font-size: 1.05rem;
      word-break: break-all;
    }
    #fileSize {
      color: var(--text-light);
      font-size: 0.9rem;
    }
    input[type="file"] {
      width: 100%;
      padding: 0.7rem;
      border: 2px solid var(--border-rest);
      border-radius: 10px;
      background: var(--drop-bg);
      color: var(--text);
    }
    button {
      background:
        linear-gradient(180deg, rgba(255,255,255,0.2), rgba(255,255,255,0.05) 46%, rgba(255,255,255,0.02)),
        rgba(90,162,255,0.22);
      color: white;
      border: 1px solid rgba(236,244,255,0.24);
      padding: 0.8rem 1rem;
      border-radius: 10px;
      font-size: 1rem;
      font-weight: 500;
      cursor: pointer;
      width: 100%;
      margin-top: 1.5rem;
      box-shadow:0 5px 0 rgba(4,9,16,0.9), inset 0 1px 0 rgba(255,255,255,0.18);
      transition: all 0.2s;
    }
    button:hover:not(:disabled) {
      filter:brightness(1.08);
      border-color:rgba(236,244,255,0.36);
    }
    button:disabled {
      background: rgba(100,116,139,0.2);
      color: #94a3b8;
      cursor: not-allowed;
      box-shadow:none;
    }
    .list-item .play-btn,
    .list-item .trash-btn {
      width:30px;
      height:30px;
      min-width:30px;
      margin:0;
      padding:0;
      border-radius:8px;
      background:rgba(255,255,255,0.055);
      border:1px solid rgba(143,160,179,0.16);
      box-shadow:none;
      font-size:1rem;
    }
    .list-item .play-btn:hover,
    .list-item .trash-btn:hover {
      background:rgba(255,255,255,0.09);
      border-color:rgba(143,160,179,0.28);
      filter:none;
    }
    .spin {
      display: inline-block;
      width: 24px;
      height: 24px;
      border-radius: 50%;
      border: 3px solid rgba(37,99,235,0.2);
      border-top-color: var(--primary);
      animation: spin 0.9s linear infinite;
      vertical-align: middle;
      margin-right: 0.6rem;
    }
    @keyframes spin {
      to { transform: rotate(360deg); }
    }
    .btn-spin {
      display: none;
      width: 20px;
      height: 20px;
      border-width: 2px;
      border-color: rgba(255,255,255,0.45);
      border-top-color: #ffffff;
      margin-right: 0.5rem;
    }
    button.is-loading .btn-spin {
      display: inline-block;
    }
    .progress {
      margin-top: 1rem;
      font-size: 0.95rem;
      color: var(--text-light);
    }
    .progress-bar {
      width: 100%;
      height: 10px;
      background: #273244;
      border-radius: 999px;
      overflow: hidden;
      margin-top: 0.4rem;
    }
    .progress-fill {
      height: 100%;
      width: 0%;
      background: linear-gradient(90deg, var(--primary-dark), #4fd1ff);
      transition: width 0.15s linear;
    }
    .danger {
      margin-top: 1rem;
      border-top: 1px solid rgba(143,160,179,0.18);
      padding-top: 1rem;
      text-align: center;
    }
    .danger button {
      background: transparent;
      color: var(--danger);
      border: 1px solid rgba(251,113,133,0.5);
      box-shadow:none;
    }
    .danger button:hover:not(:disabled) {
      background: rgba(251,113,133,0.12);
    }
    .back {
      text-align: center;
      margin-top: 2rem;
      color: var(--text-light);
      font-size: 0.95rem;
    }
    .back a {
      color: #d8e2ee;
      text-decoration: none;
      font-weight: 500;
    }
    .back a:hover { text-decoration: underline; }
    @media (max-width: 720px) {
      body {padding:0.75rem;}
      .card {padding:1rem; border-radius:10px;}
      .topbar {align-items:center;}
      h1 {font-size:1.45rem;}
      .status {grid-template-columns:1fr;}
      .status .tiny {text-align:left;}
      .metrics-grid {grid-template-columns:1fr; gap:0.75rem;}
      .tabbar {
        display:grid;
        grid-template-columns:repeat(6, minmax(0, 1fr));
        gap:0.35rem;
        margin:0.5rem 0 1rem;
      }
      .tabbar button {
        width:100%;
        margin:0;
        padding:0.55rem 0.15rem;
        border-radius:8px;
        background:rgba(255,255,255,0.075);
        color:#d8e2ee;
        font-size:0.72rem;
        border:1px solid rgba(143,160,179,0.16);
        box-shadow:none;
      }
      .tabbar button.active {
        background:rgba(90,162,255,0.25);
        color:#ffffff;
        border-color:rgba(90,162,255,0.5);
      }
      .manage-grid,
      .side-stack {display:block;}
      .tab-panel {display:none;}
      .tab-panel.active {display:block;}
      .section {padding:0.9rem;}
      .section + .section {margin-top:1rem;}
      .network-row,
      .security-row,
      .rules-row,
      .rules-actions {display:grid; grid-template-columns:1fr; gap:0.5rem;}
      .network-row button,
      .security-row button {width:100%;}
      .dns-custom {grid-template-columns:1fr;}
      .back {display:none;}
    }
  </style>
</head>
<body>
  <div class="card">
    <div class="topbar">
      <h1>Manage Chimes</h1>
      <a class="home-link" href="/" title="Return to the simple chime status and play page">Home</a>
    </div>
    <div class="status">
      <div class="big" id="deviceStatus">Loading…</div>
      <div class="sub" id="deviceActive">No chime loaded</div>
      <div>
        <div class="tiny">IP: <span id="deviceIp">—</span></div>
        <div class="tiny">mDNS: <span id="deviceMdns">doorbell.local</span></div>
        <div class="tiny" id="deviceLanDnsRow" style="display:none;">LAN DNS: <span id="deviceLanDns">off</span></div>
      </div>
    </div>

    <div class="metrics-grid">
      <div class="info-box" id="wifiBox">
        <div class="info-row">
          <span>Wi‑Fi Signal</span>
          <span id="wifiText">Loading…</span>
        </div>
        <div class="bar"><div id="wifiBar" class="fill" style="width:0%"></div></div>
      </div>
      <div class="info-box" id="fsBox">
        <div class="info-row">
          <span>Available Storage</span>
          <span id="fsText">Loading…</span>
        </div>
        <div class="bar"><div id="fsBar" class="fill" style="width:0%"></div></div>
      </div>
    </div>

    <div class="tabbar" role="tablist" aria-label="Manage sections">
      <button class="active" type="button" data-tab="chimes" title="Select, preview, or delete uploaded chime sounds">Chimes</button>
      <button type="button" data-tab="rules" title="Map sensor events to specific sounds">Rules</button>
      <button type="button" data-tab="events" title="View recent sensor and chime events">Events</button>
      <button type="button" data-tab="upload" title="Upload a new WAV or MP3 chime sound">Upload</button>
      <button type="button" data-tab="device" title="Adjust volume, device name, Wi-Fi, and advanced network options">Device</button>
      <button type="button" data-tab="security" title="Set the LAN admin password and manage protected actions">Security</button>
    </div>

    <div class="manage-grid">
      <div class="side-stack">
        <div class="section tab-panel active" data-panel="chimes">
          <h2>Chimes</h2>
          <div id="soundList" style="text-align:left;">Loading…</div>
        </div>

        <div class="section tab-panel" data-panel="rules">
          <h2>Rules</h2>
          <div class="rules-form">
            <div class="rules-row">
              <div class="rules-field">
                <label for="ruleSensorInput">Sensor</label>
                <input id="ruleSensorInput" type="text" maxlength="31" placeholder="bench-button" title="Sensor ID, such as bench-button or mailbox">
              </div>
              <div class="rules-field">
                <label for="ruleTypeSelect">Type</label>
                <select id="ruleTypeSelect" title="Sensor type">
                  <option value="doorbell">doorbell</option>
                  <option value="mailbox">mailbox</option>
                  <option value="motion">motion</option>
                  <option value="package">package</option>
                </select>
              </div>
              <div class="rules-field">
                <label for="ruleEventSelect">Event</label>
                <select id="ruleEventSelect" title="Sensor event"></select>
              </div>
            </div>
            <div class="rules-actions">
              <select id="ruleSoundSelect" title="Sound to play when this rule matches"></select>
              <button id="saveRuleBtn" type="button" title="Save or replace this sensor sound rule">Save Rule</button>
            </div>
            <div id="ruleSaveState" class="rule-save-state"></div>
          </div>
          <div class="network-help">Sensor is optional. Leave it blank to match all sensors of the selected type and event.</div>
          <div id="ruleList" class="rule-list">
            <div class="rule-empty">Loading...</div>
          </div>
        </div>

        <div class="section tab-panel" data-panel="events">
          <h2>Events</h2>
          <div id="eventList" class="event-list">
            <div class="event-empty">Loading...</div>
          </div>
          <div class="event-actions">
            <button id="refreshEventsBtn" type="button" title="Refresh recent events">Refresh</button>
          </div>
        </div>

        <div class="section tab-panel" data-panel="upload">
          <h2>Upload Sound</h2>
          <form id="uploadForm" action="/upload" method="POST" enctype="multipart/form-data">
            <label for="fileInput" class="prompt">Choose a WAV or MP3 file</label>
            <input type="file" id="fileInput" name="file" accept=".wav,.mp3" title="Choose a WAV or MP3 file to upload to the chime" required>
            <div id="fileName"></div>
            <div id="fileSize"></div>
            <button type="submit" id="uploadBtn" title="Upload the selected sound and make it available as a chime" disabled>
              <span class="spin btn-spin" aria-hidden="true"></span>
              <span id="uploadText">Upload</span>
            </button>
            <div class="progress" id="progressBox" style="display:none;">
              <div id="progressText">Starting…</div>
              <div class="progress-bar"><div id="progressFill" class="progress-fill"></div></div>
            </div>
          </form>
        </div>
      </div>

      <div class="side-stack">
        <div class="section tab-panel" data-panel="device">
          <h2>Device</h2>
          <div class="volume-box">
            <div class="slider-container">
              <label>Volume Gain (0.0 – 3.0):</label><br>
              <input type="range" min="0" max="300" value="100" step="1" id="gainSlider" title="Adjust playback gain from muted to louder output">
              <div id="gainDisplay">1.00</div>
            </div>
          </div>

          <div class="network-box" style="margin-top:1rem;">
            <div class="network-title">Device Name</div>
            <div class="network-row">
              <input id="labelInput" type="text" value="" maxlength="24" placeholder="front-door" title="Short room or location label used in the mDNS name">
              <button id="saveLabelBtn" type="button" title="Save the device name and advanced LAN DNS setting">Save</button>
              <button id="resetWifiBtn" class="btn-secondary" type="button" title="Shows a warning, then clears saved Wi-Fi credentials and reboots into setup mode">Reset Wi‑Fi</button>
            </div>
            <div class="network-help">mDNS: <span id="mdnsHost">doorbell.local</span></div>
            <div class="save-state" id="deviceSaveState"></div>
            <details class="advanced-box">
              <summary title="Show optional network settings for users with local DNS configured">Advanced</summary>
              <div class="advanced-body">
                <div class="network-title">LAN DNS Name</div>
                <div class="network-help">LAN DNS: <span id="lanDnsHost">off</span></div>
                <div class="dns-options">
                  <label title="Do not show a LAN DNS name; use mDNS or a reserved IP instead"><input type="radio" name="lanDnsSuffix" value="" checked> Off</label>
                  <label title="Use this only if your router or DNS server resolves .lan names"><input type="radio" name="lanDnsSuffix" value="lan"> .lan</label>
                  <label title="Standards-friendly home-network DNS suffix; requires router or DNS support"><input type="radio" name="lanDnsSuffix" value="home.arpa"> .home.arpa</label>
                  <label class="dns-custom">
                    <span><input id="lanDnsCustomRadio" type="radio" name="lanDnsSuffix" value="custom" title="Use a custom local DNS suffix"> Custom</span>
                    <input id="lanDnsCustomInput" type="text" maxlength="48" placeholder="iot.lan" title="Custom suffix your router or DNS server resolves, such as iot.lan">
                  </label>
                </div>
                <div class="network-help">For UniFi Protect, use this only if UniFi Network or local DNS resolves the name. Otherwise use a reserved IP address.</div>
              </div>
            </details>
          </div>
        </div>

        <div class="section tab-panel" data-panel="security">
          <h2>Security</h2>
          <div class="network-box">
            <div class="network-title">LAN Admin Password</div>
            <div class="security-row">
              <input id="tokenInput" type="password" value="" maxlength="64" placeholder="optional LAN password" title="Optional LAN admin password for management actions">
              <button id="saveSecurityBtn" type="button" title="Save or clear the LAN admin password and playback protection setting">Save Password</button>
            </div>
            <div class="security-explain">Protects management actions on your local network. This page uses HTTP, so do not reuse an important password.</div>
            <label class="check-row">
              <input id="playbackAuthInput" type="checkbox" title="Require the LAN admin password when triggering playback URLs">
              Require admin password for playback URLs
            </label>
            <div class="network-help" id="securityState">No LAN admin password set</div>
            <div class="security-notice" id="securityNotice">
              Anyone on this Wi-Fi network can manage sounds and settings.
              <div class="notice-actions">
                <button id="addPasswordBtn" type="button" title="Jump to the LAN admin password field">Add Password</button>
                <button id="dismissSecurityBtn" class="notice-secondary" type="button" title="Hide this warning for this browser session">Not Now</button>
              </div>
            </div>
          </div>
          <div class="danger">
            <button id="cleanBtn" type="button" title="Shows a warning, then permanently deletes all uploaded sounds">Delete All Files</button>
          </div>
        </div>
      </div>
    </div>

    <div class="back">
      <a href="/">← Back to Home</a>
    </div>
  </div>

  <script>
    const fileInput   = document.getElementById('fileInput');
    const fileNameEl  = document.getElementById('fileName');
    const fileSizeEl  = document.getElementById('fileSize');
    const uploadBtn   = document.getElementById('uploadBtn');
    const uploadText  = document.getElementById('uploadText');
    const progressBox = document.getElementById('progressBox');
    const progressText = document.getElementById('progressText');
    const progressFill = document.getElementById('progressFill');
    const cleanBtn = document.getElementById('cleanBtn');
    const wifiText = document.getElementById('wifiText');
    const wifiBar = document.getElementById('wifiBar');
    const fsText = document.getElementById('fsText');
    const fsBar = document.getElementById('fsBar');
    const eventList = document.getElementById('eventList');
    const refreshEventsBtn = document.getElementById('refreshEventsBtn');
    const ruleSensorInput = document.getElementById('ruleSensorInput');
    const ruleTypeSelect = document.getElementById('ruleTypeSelect');
    const ruleEventSelect = document.getElementById('ruleEventSelect');
    const ruleSoundSelect = document.getElementById('ruleSoundSelect');
    const saveRuleBtn = document.getElementById('saveRuleBtn');
    const ruleSaveState = document.getElementById('ruleSaveState');
    const ruleList = document.getElementById('ruleList');
    const deviceStatus = document.getElementById('deviceStatus');
    const deviceActive = document.getElementById('deviceActive');
    const deviceIp = document.getElementById('deviceIp');
    const deviceMdns = document.getElementById('deviceMdns');
    const deviceLanDnsRow = document.getElementById('deviceLanDnsRow');
    const deviceLanDns = document.getElementById('deviceLanDns');
    const soundList = document.getElementById('soundList');
    const gainSlider = document.getElementById('gainSlider');
    const gainDisplay = document.getElementById('gainDisplay');
    const labelInput = document.getElementById('labelInput');
    const saveLabelBtn = document.getElementById('saveLabelBtn');
    const resetWifiBtn = document.getElementById('resetWifiBtn');
    const mdnsHost = document.getElementById('mdnsHost');
    const lanDnsHost = document.getElementById('lanDnsHost');
    const deviceSaveState = document.getElementById('deviceSaveState');
    const lanDnsRadios = Array.from(document.querySelectorAll('input[name="lanDnsSuffix"]'));
    const lanDnsCustomRadio = document.getElementById('lanDnsCustomRadio');
    const lanDnsCustomInput = document.getElementById('lanDnsCustomInput');
    const tokenInput = document.getElementById('tokenInput');
    const saveSecurityBtn = document.getElementById('saveSecurityBtn');
    const playbackAuthInput = document.getElementById('playbackAuthInput');
    const securityState = document.getElementById('securityState');
    const securityNotice = document.getElementById('securityNotice');
    const addPasswordBtn = document.getElementById('addPasswordBtn');
    const dismissSecurityBtn = document.getElementById('dismissSecurityBtn');
    const tabButtons = Array.from(document.querySelectorAll('[data-tab]'));
    const tabPanels = Array.from(document.querySelectorAll('[data-panel]'));
    let maxBytes = 3000 * 1024;
    let authToken = localStorage.getItem('doorbellAuthToken') || '';
    let securityNoticeDismissed = sessionStorage.getItem('doorbellSecurityNoticeDismissed') === '1';
    let availableSounds = [];
    const eventOptionsByType = {
      doorbell: ['press'],
      mailbox: ['flag-raised'],
      motion: ['detected'],
      package: ['detected']
    };

    function withToken(url) {
      if (!authToken) return url;
      return `${url}${url.includes('?') ? '&' : '?'}token=${encodeURIComponent(authToken)}`;
    }

    function rememberToken(token) {
      authToken = (token || '').trim();
      if (authToken) localStorage.setItem('doorbellAuthToken', authToken);
      else localStorage.removeItem('doorbellAuthToken');
    }

    function promptForToken() {
      const token = prompt('Enter LAN admin password for this chime');
      if (token === null) return false;
      rememberToken(token);
      if (tokenInput) tokenInput.value = '';
      return !!authToken;
    }

    function updateSecurityUi(authEnabled) {
      securityState.textContent = authEnabled ? 'LAN admin password enabled' : 'No LAN admin password set';
      if (securityNotice) {
        securityNotice.style.display = (!authEnabled && !securityNoticeDismissed) ? 'block' : 'none';
      }
    }

    function fetchAuth(url, options = {}, retry = true) {
      return fetch(withToken(url), options).then(r => {
        if (r.status === 403 && retry && promptForToken()) {
          return fetchAuth(url, options, false);
        }
        return r;
      });
    }

    function tokenBody(params = {}) {
      const body = new URLSearchParams(params);
      if (authToken) body.set('token', authToken);
      return body.toString();
    }

    function barClass(pct, warnAt, badAt) {
      if (pct < badAt) return 'fill bad';
      if (pct < warnAt) return 'fill warn';
      return 'fill';
    }

    function setActiveTab(name) {
      tabButtons.forEach(btn => btn.classList.toggle('active', btn.dataset.tab === name));
      tabPanels.forEach(panel => panel.classList.toggle('active', panel.dataset.panel === name));
      if (name === 'events') refreshEvents();
      if (name === 'rules') refreshRules();
    }

    function selectedLanDnsSuffix() {
      const selected = lanDnsRadios.find(r => r.checked);
      if (!selected) return '';
      if (selected.value === 'custom') return lanDnsCustomInput.value.trim();
      return selected.value;
    }

    function setLanDnsSuffix(suffix) {
      const value = (suffix ?? '').trim();
      const known = lanDnsRadios.find(r => r.value === value);
      if (known) {
        known.checked = true;
        if (value !== 'custom') lanDnsCustomInput.value = '';
      } else {
        lanDnsCustomRadio.checked = true;
        lanDnsCustomInput.value = value;
      }
    }

    function setDeviceSaveState(text, kind = '') {
      deviceSaveState.textContent = text;
      deviceSaveState.className = kind ? `save-state ${kind}` : 'save-state';
    }

    function setRuleSaveState(text, kind = '') {
      ruleSaveState.textContent = text;
      ruleSaveState.className = kind ? `rule-save-state ${kind}` : 'rule-save-state';
    }

    function ruleLabel(rule) {
      const selector = [
        rule.sensor || '*',
        rule.type || '*',
        rule.event || '*'
      ].join(' / ');
      return `${selector} -> ${rule.key || rule.path || 'sound'}`;
    }

    function populateRuleSoundSelect() {
      ruleSoundSelect.innerHTML = '';
      if (!availableSounds.length) {
        const option = document.createElement('option');
        option.value = '';
        option.textContent = 'No uploaded sounds';
        ruleSoundSelect.appendChild(option);
        ruleSoundSelect.disabled = true;
        saveRuleBtn.disabled = true;
        return;
      }

      ruleSoundSelect.disabled = false;
      saveRuleBtn.disabled = false;
      availableSounds.forEach(sound => {
        const option = document.createElement('option');
        option.value = sound.key || '';
        option.textContent = sound.name || sound.path || sound.key;
        option.dataset.path = sound.path || '';
        ruleSoundSelect.appendChild(option);
      });
    }

    function populateRuleEventSelect() {
      const type = ruleTypeSelect.value || 'doorbell';
      const events = eventOptionsByType[type] || ['detected'];
      const previous = ruleEventSelect.value;
      ruleEventSelect.innerHTML = '';
      events.forEach(eventName => {
        const option = document.createElement('option');
        option.value = eventName;
        option.textContent = eventName;
        ruleEventSelect.appendChild(option);
      });
      if (events.includes(previous)) ruleEventSelect.value = previous;
    }

    function updateFileInfo() {
      const file = fileInput.files[0];
      if (file) {
        if (file.size > maxBytes) {
          const mb = (maxBytes / (1024*1024)).toFixed(2);
          alert(`File too large (max ~${mb} MB available)`);
          fileInput.value = '';
          updateFileInfo();
          return;
        }
        fileNameEl.textContent   = file.name;
        fileSizeEl.textContent   = (file.size / 1024).toFixed(1) + ' kB';
        uploadBtn.disabled       = false;
      } else {
        fileNameEl.textContent   = '';
        fileSizeEl.textContent   = '';
        uploadBtn.disabled       = true;
      }
    }

    fileInput.addEventListener('change', updateFileInfo);

    gainSlider.addEventListener('input', () => {
      const val = gainSlider.value / 100;
      gainDisplay.textContent = val.toFixed(2);
      fetchAuth(`/setgain?value=${val}`).catch(() => {});
    });

    document.getElementById('uploadForm').addEventListener('submit', e => {
      const file = fileInput.files[0];
      if (!file) {
        e.preventDefault();
        return;
      }
      e.preventDefault();
      uploadBtn.disabled = true;
      uploadBtn.classList.add('is-loading');
      uploadText.textContent = 'Uploading…';
      progressBox.style.display = 'block';
      progressFill.style.width = '0%';
      progressText.textContent = '0%';

      const formData = new FormData();
      formData.append('file', file);
      if (authToken) formData.append('token', authToken);

      const xhr = new XMLHttpRequest();
      const startTime = performance.now();
      xhr.upload.onprogress = (evt) => {
        if (!evt.lengthComputable) return;
        const pct = Math.round((evt.loaded / evt.total) * 100);
        const elapsed = (performance.now() - startTime) / 1000;
        const rate = elapsed > 0 ? (evt.loaded / 1024 / elapsed) : 0;
        progressFill.style.width = `${pct}%`;
        progressText.textContent = `${pct}% – ${rate.toFixed(1)} kB/s`;
      };
      xhr.onload = () => {
        if (xhr.status >= 200 && xhr.status < 300) {
          document.open();
          document.write(xhr.responseText);
          document.close();
        } else if (xhr.status === 403 && promptForToken()) {
          progressText.textContent = 'Password saved. Tap Upload again.';
        } else {
          progressText.textContent = `Upload failed (${xhr.status})`;
        }
        uploadBtn.disabled = false;
        uploadBtn.classList.remove('is-loading');
        uploadText.textContent = 'Upload';
      };
      xhr.onerror = () => {
        progressText.textContent = 'Upload failed (network)';
        uploadBtn.disabled = false;
        uploadBtn.classList.remove('is-loading');
        uploadText.textContent = 'Upload';
      };
      xhr.open('POST', withToken('/upload'), true);
      xhr.send(formData);
    });

    cleanBtn.addEventListener('click', () => {
      if (!confirm('Delete ALL uploaded files? Cannot be undone!')) return;
      fetchAuth('/clean', {method:'POST'})
        .then(r => {
          if (!r.ok) throw new Error(`Clean failed (${r.status})`);
          location.href = '/';
        })
        .catch(err => alert(err.message || 'Clean failed'));
    });

    function refreshStatus() {
      fetch('/status')
        .then(r => r.json())
        .then(s => {
          const signalPct = s.signalPct ?? 0;
          const rssi = s.rssi ?? -100;
          const wifi = s.wifi || 'disconnected';
          wifiText.textContent = (wifi === 'connected')
            ? `${rssi} dBm (${signalPct}%)`
            : 'Disconnected';
          wifiBar.style.width = `${signalPct}%`;
          wifiBar.className = barClass(signalPct, 60, 30);

          const usedPct = s.fsUsedPct ?? 0;
          const freeKB = (s.fsFreeKB ?? 0).toFixed(1);
          const totalKB = (s.fsTotalKB ?? 0).toFixed(1);
          fsText.textContent = `${freeKB} kB free of ${totalKB} kB`;
          fsBar.style.width = `${usedPct}%`;
          fsBar.className = barClass(100 - usedPct, 30, 15);

          maxBytes = Math.max(0, ((s.fsFreeKB ?? 0) * 1024) - (4 * 1024));
          const activeName = s.activeName || 'No chime loaded';
          const hasActive = (s.activePath || '').length > 0;
          deviceStatus.textContent = hasActive ? 'Active Chime' : 'No Chime Loaded';
          deviceActive.textContent = activeName;
          deviceIp.textContent = s.ip || 'not connected';
          deviceMdns.textContent = s.mdns || 'doorbell.local';
          deviceLanDns.textContent = s.lanDns || 'off';
          deviceLanDnsRow.style.display = s.lanDns ? 'block' : 'none';
          mdnsHost.textContent = s.mdns || 'doorbell.local';
          lanDnsHost.textContent = s.lanDns || 'off';
          labelInput.value = (s.deviceLabel ?? labelInput.value ?? '');
          if (document.activeElement !== lanDnsCustomInput) {
            setLanDnsSuffix(s.lanDnsSuffix ?? '');
          }
          playbackAuthInput.checked = !!s.playbackAuth;
          updateSecurityUi(!!s.authEnabled);
          const gain = Number(s.gain ?? 1);
          if (document.activeElement !== gainSlider) {
            gainSlider.value = Math.round(gain * 100);
            gainDisplay.textContent = gain.toFixed(2);
          }
        })
        .catch(() => {});
    }

    function eventAge(ms) {
      if (ms < 1000) return 'now';
      const sec = Math.floor(ms / 1000);
      if (sec < 60) return `${sec}s`;
      const min = Math.floor(sec / 60);
      if (min < 60) return `${min}m`;
      const hr = Math.floor(min / 60);
      if (hr < 24) return `${hr}h`;
      return `${Math.floor(hr / 24)}d`;
    }

    function eventTitle(item) {
      const sensor = item.sensor || item.source || 'chime';
      const event = item.event || 'trigger';
      const type = item.type ? ` ${item.type}` : '';
      return `${sensor}${type}: ${event}`;
    }

    function refreshEvents() {
      fetch('/events')
        .then(r => r.json())
        .then(data => {
          const items = data.items || [];
          if (!items.length) {
            eventList.innerHTML = '<div class="event-empty">No events yet.</div>';
            return;
          }
          eventList.innerHTML = '';
          items.slice(0, 20).forEach(item => {
            const row = document.createElement('div');
            row.className = 'event-item';
            const main = document.createElement('div');
            main.className = 'event-main';
            main.textContent = eventTitle(item);
            const age = document.createElement('div');
            age.className = 'event-age';
            age.textContent = eventAge(Number(item.ageMs || 0));
            const meta = document.createElement('div');
            meta.className = 'event-meta';
            const bits = [
              item.soundPath || '',
              item.eventId ? `id ${item.eventId}` : '',
              item.source ? `via ${item.source}` : ''
            ].filter(Boolean);
            meta.textContent = bits.join(' · ');
            row.appendChild(main);
            row.appendChild(age);
            row.appendChild(meta);
            eventList.appendChild(row);
          });
        })
        .catch(() => {
          eventList.innerHTML = '<div class="event-empty">Unable to load events.</div>';
        });
    }

    function renderRules(data) {
      const rules = data.rules || [];
      if (!rules.length) {
        ruleList.innerHTML = '<div class="rule-empty">No custom rules yet.</div>';
        return;
      }

      ruleList.innerHTML = '';
      rules.forEach(rule => {
        const row = document.createElement('div');
        row.className = 'rule-item';
        const main = document.createElement('div');
        main.className = 'rule-main';
        main.textContent = ruleLabel(rule);
        const edit = document.createElement('button');
        edit.className = 'rule-edit';
        edit.type = 'button';
        edit.title = 'Edit this rule';
        edit.textContent = 'Edit';
        edit.addEventListener('click', () => editRule(rule));
        const del = document.createElement('button');
        del.className = 'rule-delete';
        del.type = 'button';
        del.title = 'Delete this rule';
        del.textContent = 'Delete';
        del.addEventListener('click', () => deleteRule(rule));
        const meta = document.createElement('div');
        meta.className = 'rule-meta';
        meta.textContent = rule.path || '';
        row.appendChild(main);
        row.appendChild(edit);
        row.appendChild(del);
        row.appendChild(meta);
        ruleList.appendChild(row);
      });
    }

    function editRule(rule) {
      ruleSensorInput.value = rule.sensor || '';
      ruleTypeSelect.value = rule.type || 'doorbell';
      populateRuleEventSelect();
      ruleEventSelect.value = rule.event || ruleEventSelect.value;
      const key = rule.key || '';
      if (key) ruleSoundSelect.value = key;
      setRuleSaveState('Editing existing rule');
      ruleSensorInput.focus();
    }

    function refreshRules() {
      return fetch('/rules')
        .then(r => r.json())
        .then(data => {
          renderRules(data);
        })
        .catch(() => {
          ruleList.innerHTML = '<div class="rule-empty">Unable to load rules.</div>';
        });
    }

    function saveRule() {
      const selected = ruleSoundSelect.selectedOptions[0];
      const key = selected ? selected.value : '';
      if (!key) {
        setRuleSaveState('Choose a sound first', 'error');
        return;
      }

      const body = tokenBody({
        sensor: ruleSensorInput.value,
        type: ruleTypeSelect.value,
        event: ruleEventSelect.value,
        key
      });

      setRuleSaveState('Saving...');
      fetchAuth('/rules', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      })
      .then(r => {
        if (!r.ok) throw new Error(`Save failed (${r.status})`);
        return r.json();
      })
      .then(data => {
        setRuleSaveState('Saved', 'ok');
        renderRules(data);
      })
      .catch(err => setRuleSaveState(err.message || 'Save failed', 'error'));
    }

    function deleteRule(rule) {
      const body = tokenBody({
        sensor: rule.sensor || '',
        type: rule.type || '',
        event: rule.event || '',
        delete: '1'
      });

      fetchAuth('/rules', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      })
      .then(r => {
        if (!r.ok) throw new Error(`Delete failed (${r.status})`);
        return r.json();
      })
      .then(data => {
        setRuleSaveState('Deleted', 'ok');
        renderRules(data);
      })
      .catch(err => setRuleSaveState(err.message || 'Delete failed', 'error'));
    }

    function refreshSounds() {
      fetch('/list')
        .then(r => r.json())
        .then(s => {
          availableSounds = s.items || [];
          populateRuleSoundSelect();
          if (!s.items || s.items.length === 0) {
            soundList.textContent = 'No chimes uploaded yet.';
            return;
          }
          soundList.innerHTML = '';
          s.items.forEach(item => {
            const row = document.createElement('div');
            row.className = 'list-item';
            const radio = document.createElement('input');
            radio.type = 'radio';
            radio.name = 'activeSound';
            radio.value = item.path;
            radio.title = 'Set this sound as the active chime';
            radio.checked = (item.path === s.active);
            radio.addEventListener('change', () => {
              fetchAuth(`/setactive?path=${encodeURIComponent(item.path)}`)
                .then(r => {
                  if (!r.ok) throw new Error(`Select failed (${r.status})`);
                  refreshSounds();
                  refreshStatus();
                })
                .catch(err => alert(err.message || 'Select failed'));
            });
            const label = document.createElement('label');
            label.textContent = item.name || item.path;
            label.title = item.path || item.name || 'Uploaded chime sound';
            const play = document.createElement('button');
            play.className = 'play-btn';
            play.title = 'Preview this sound';
            play.type = 'button';
            play.innerHTML = `<svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
              <path d="M8 5v14l11-7z"/>
            </svg>`;
            play.addEventListener('click', e => {
              e.preventDefault();
              e.stopPropagation();
              const endpoint = item.endpoint || `/play?key=${encodeURIComponent(item.key || '')}`;
              fetchAuth(endpoint)
                .then(r => {
                  if (!r.ok) throw new Error(`Play failed (${r.status})`);
                  setTimeout(refreshEvents, 250);
                })
                .catch(err => alert(err.message || 'Play failed'));
            });
            const del = document.createElement('button');
            del.className = 'trash-btn';
            del.title = 'Shows a warning, then deletes this sound';
            del.type = 'button';
            del.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">
              <path d="M3 6h18"/><path d="M8 6V4h8v2"/><path d="M19 6l-1 14H6L5 6"/><path d="M10 11v6M14 11v6"/>
            </svg>`;
            del.addEventListener('click', e => {
              e.preventDefault();
              e.stopPropagation();
              if (!confirm(`Delete "${item.name || item.path}"?`)) return;
              fetchAuth(`/delete?path=${encodeURIComponent(item.path)}`)
                .then(r => {
                  if (!r.ok) throw new Error(`Delete failed (${r.status})`);
                  refreshSounds();
                  refreshStatus();
                })
                .catch(err => alert(err.message || 'Delete failed'));
            });
            row.appendChild(radio);
            row.appendChild(label);
            row.appendChild(play);
            row.appendChild(del);
            soundList.appendChild(row);
          });
        })
        .catch(() => { soundList.textContent = 'Unable to load list.'; });
    }

    function saveDeviceSettings(message = 'Saved') {
      setDeviceSaveState('Saving...');
      const body = tokenBody({
        label: labelInput.value,
        lanDnsSuffix: selectedLanDnsSuffix()
      });
      fetchAuth('/setlabel', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      })
      .then(r => {
        if (!r.ok) throw new Error(`Save failed (${r.status})`);
        return r.json();
      })
      .then(resp => {
        if (resp && resp.ok) {
          labelInput.value = resp.label || '';
          mdnsHost.textContent = resp.mdns || 'doorbell.local';
          deviceMdns.textContent = resp.mdns || 'doorbell.local';
          lanDnsHost.textContent = resp.lanDns || 'off';
          deviceLanDns.textContent = resp.lanDns || 'off';
          deviceLanDnsRow.style.display = resp.lanDns ? 'block' : 'none';
          setLanDnsSuffix(resp.lanDnsSuffix ?? '');
          setDeviceSaveState(message, 'ok');
        }
      })
        .catch(err => {
          setDeviceSaveState(err.message || 'Save failed', 'error');
        });
    }

    function saveSecurity() {
      const newToken = tokenInput.value.trim();
      const previousToken = authToken;
      const body = tokenBody({
        newToken,
        playbackAuth: playbackAuthInput.checked ? '1' : '0'
      });
      fetchAuth('/setsecurity', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      })
      .then(r => {
        if (!r.ok) throw new Error('save failed');
        return r.json();
      })
      .then(resp => {
        rememberToken(newToken);
        tokenInput.value = '';
        securityNoticeDismissed = false;
        sessionStorage.removeItem('doorbellSecurityNoticeDismissed');
        updateSecurityUi(!!resp.authEnabled);
      })
      .catch(() => {
        rememberToken(previousToken);
        securityState.textContent = 'Password save failed';
      });
    }

    function resetWiFi() {
      if (!confirm('Reset Wi-Fi credentials and reboot to captive portal?')) return;
      fetchAuth('/resetwifi', {method:'POST'})
        .then(r => {
          if (!r.ok) throw new Error(`Reset failed (${r.status})`);
          deviceStatus.textContent = 'Rebooting…';
        })
        .catch(err => alert(err.message || 'Reset failed'));
    }

    saveLabelBtn.addEventListener('click', () => saveDeviceSettings());
    labelInput.addEventListener('keydown', e => {
      if (e.key === 'Enter') {
        e.preventDefault();
        saveDeviceSettings();
      }
    });
    resetWifiBtn.addEventListener('click', resetWiFi);
    lanDnsCustomInput.addEventListener('focus', () => {
      lanDnsCustomRadio.checked = true;
    });
    lanDnsRadios.forEach(radio => {
      radio.addEventListener('change', () => {
        if (radio.value === 'custom') {
          lanDnsCustomInput.focus();
          return;
        }
        saveDeviceSettings('LAN DNS saved');
      });
    });
    lanDnsCustomInput.addEventListener('keydown', e => {
      if (e.key === 'Enter') {
        e.preventDefault();
        lanDnsCustomRadio.checked = true;
        saveDeviceSettings('LAN DNS saved');
      }
    });
    lanDnsCustomInput.addEventListener('blur', () => {
      if (lanDnsCustomRadio.checked) saveDeviceSettings('LAN DNS saved');
    });
    saveSecurityBtn.addEventListener('click', saveSecurity);
    refreshEventsBtn.addEventListener('click', refreshEvents);
    saveRuleBtn.addEventListener('click', saveRule);
    ruleTypeSelect.addEventListener('change', populateRuleEventSelect);
    [ruleSensorInput, ruleTypeSelect, ruleEventSelect].forEach(input => {
      input.addEventListener('keydown', e => {
        if (e.key === 'Enter') {
          e.preventDefault();
          saveRule();
        }
      });
    });
    addPasswordBtn.addEventListener('click', () => {
      tokenInput.focus();
      tokenInput.scrollIntoView({block: 'center', behavior: 'smooth'});
    });
    dismissSecurityBtn.addEventListener('click', () => {
      securityNoticeDismissed = true;
      sessionStorage.setItem('doorbellSecurityNoticeDismissed', '1');
      updateSecurityUi(false);
    });
    tabButtons.forEach(btn => {
      btn.addEventListener('click', () => setActiveTab(btn.dataset.tab));
    });

    // Init
    setActiveTab('chimes');
    uploadBtn.disabled = true;
    updateFileInfo();
    populateRuleEventSelect();
    refreshStatus();
    refreshSounds();
    refreshEvents();
    refreshRules();
    setInterval(refreshStatus, 10000);
    setInterval(refreshEvents, 10000);
  </script>
</body>
</html>
)rawliteral";

void handleUploadForm(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", UPLOAD_PAGE_HTML);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void handleUploadDone(AsyncWebServerRequest *request) {
  if (!requireAdminAuth(request)) return;
  if (!uploadSucceeded) {
    if (uploadError.length() > 0) {
      request->send(413, "text/plain", uploadError);
    } else {
      request->send(500, "text/plain", "Upload failed");
    }
    return;
  }

  const char* okPage = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="utf-8" />
    <meta http-equiv="refresh" content="0; url=/" />
    <title>Upload Complete</title>
    <style>
      body { font-family: system-ui, -apple-system, sans-serif; padding: 1rem; }
    </style>
  </head>
  <body>
    Redirecting…
    <noscript><br/><a href="/">Return to main page</a></noscript>
  </body>
</html>
)rawliteral";
  request->send(200, "text/html", okPage);
}

// ── Upload handler ─────────────────────────────────────────────────────────
void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    uploadSucceeded = false;
    uploadError = "";
    uploadTargetPath = "";
    uploadDisplayName = "";
    if (!tokenMatches(request)) {
      uploadError = "Forbidden";
      return;
    }
    String lower = filename;
    lower.toLowerCase();
    String ext = lower.endsWith(".mp3") ? ".mp3" : ".wav";
    String base = filename;
    int dot = base.lastIndexOf('.');
    if (dot > 0) base = base.substring(0, dot);
    base = sanitizeBase(base);
    uploadTargetPath = makeUniquePath(base, ext);
    uploadDisplayName = filename;
    if (uploadDisplayName.length() > 64) uploadDisplayName = uploadDisplayName.substring(0, 61) + "...";

    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    size_t freeBytes = totalBytes >= usedBytes ? (totalBytes - usedBytes) : 0;
    size_t needed = request->contentLength();
    if (needed > 0 && freeBytes < (needed + 4096)) {
      uploadError = "Not enough space for upload";
      return;
    }

    Serial.printf("[UPLOAD] %s → %s\n", filename.c_str(), uploadTargetPath.c_str());
    request->_tempFile = SPIFFS.open(uploadTargetPath, "w");
  }

  if (len && request->_tempFile) {
    request->_tempFile.write(data, len);
  }

  if (final) {
    if (request->_tempFile) {
      request->_tempFile.close();
      Serial.printf("[UPLOAD] Done – %u bytes\n", (unsigned int)(index + len));
      activeFilePath = uploadTargetPath;
      saveSoundsConfig(activeFilePath, uploadDisplayName);
      loadSoundsConfig();
      uploadSucceeded = true;
    }
  }
}

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Doorbell Chime – Full Reset & Clean ===\n");

  pinMode(LED_PIN, OUTPUT);
  for (int i = 0; i < 3; ++i) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  out = new AudioOutputI2S();
  out->SetPinout(4, 5, 6);
  out->SetGain(currentGain);

  wav = new AudioGeneratorWAV();
  mp3 = new AudioGeneratorMP3();
  playBootSound();

  if (!SPIFFS.begin(false)) {
    Serial.println("SPIFFS mount failed, formatting...");
    if (!SPIFFS.format() || !SPIFFS.begin(false)) {
      Serial.println("SPIFFS failed!");
      while (true) delay(1);
    }
  }

  // One-time migration from old /test.wav
  if (SPIFFS.exists("/test.wav") && !SPIFFS.exists(activeFilePath)) {
    activeFilePath = "/chime.wav";
    SPIFFS.rename("/test.wav", activeFilePath);
    Serial.println("Migrated old test.wav → chime.wav");
    saveSoundsConfig(activeFilePath, "chime.wav");
  }

  loadSoundsConfig();
  loadDeviceConfig();
  mdnsName = buildMdnsName(deviceLabel);
#if CLEAR_AUTH_ON_BOOT
  if (authToken.length() > 0 || playbackAuth) {
    authToken = "";
    playbackAuth = false;
    saveDeviceConfig(deviceLabel);
    Serial.println("[RECOVERY] Cleared shared token and playback auth");
  }
#endif

  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(onConfigSaved);
  wifiManager.setConnectTimeout(20);
  wifiManager.setConfigPortalTimeout(180);
  String labelDefault = deviceLabel;
  char labelBuf[32];
  labelDefault.toCharArray(labelBuf, sizeof(labelBuf));
  WiFiManagerParameter labelParam("label", "Room/Label (e.g. front-door)", labelBuf, 31);
  wifiManager.addParameter(&labelParam);
  WiFi.setHostname(mdnsName.c_str());
  WiFi.setAutoReconnect(true);
  Serial.println("WiFiManager: starting autoConnect");
  bool wifiOk = wifiManager.autoConnect("DoorbellChimeSetup", "config123");
  Serial.println(wifiOk ? "WiFiManager: connected (portal not active)" : "WiFiManager: failed (portal timeout or error)");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // lower latency, higher power
  if (wifiOk) {
    wifiWasConnected = true;
  } else {
    lastWiFiReconnectMs = millis() - WIFI_RECONNECT_INTERVAL_MS;
  }

  String newLabel = sanitizeLabel(String(labelParam.getValue()));
  if (newLabel != deviceLabel) {
    deviceLabel = newLabel;
    saveDeviceConfig(deviceLabel);
  }

  // If captive portal saved new settings, reboot after persisting custom label.
  if (shouldReboot) {
    delay(500);
    ESP.restart();
  }

  mdnsName = buildMdnsName(deviceLabel);
  WiFi.setHostname(mdnsName.c_str());

  Serial.print("IP: "); Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ handleRoot(request); });
  server.on("/chime", HTTP_GET, [](AsyncWebServerRequest *request){ handleChime(request); });
  server.on("/trigger", HTTP_GET, [](AsyncWebServerRequest *request){ handleSensorTrigger(request); });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){ handleStatus(request); });
  server.on("/events", HTTP_GET, [](AsyncWebServerRequest *request){ handleEvents(request); });
  server.on("/rules", HTTP_GET, [](AsyncWebServerRequest *request){ handleRulesGet(request); });
  server.on("/rules", HTTP_POST, [](AsyncWebServerRequest *request){ handleRulesPost(request); });
  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request){ handleList(request); });
  server.on("/setgain", HTTP_GET, [](AsyncWebServerRequest *request){ handleSetGain(request); });
  server.on("/setlabel", HTTP_POST, [](AsyncWebServerRequest *request){ handleSetLabel(request); });
  server.on("/setsecurity", HTTP_POST, [](AsyncWebServerRequest *request){ handleSetSecurity(request); });
  server.on("/resetwifi", HTTP_POST, [](AsyncWebServerRequest *request){ handleResetWiFi(request); });
  server.on("/play", HTTP_GET, [](AsyncWebServerRequest *request){ handlePlayByKey(request); });
  server.on("/setactive", HTTP_GET, [](AsyncWebServerRequest *request){ handleSetActive(request); });
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){ handleDelete(request); });
  server.on("/manage", HTTP_GET, [](AsyncWebServerRequest *request){ handleUploadForm(request); });
  server.on("/upload", HTTP_POST,
            [](AsyncWebServerRequest *request){ handleUploadDone(request); },
            [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
              handleFileUpload(request, filename, index, data, len, final);
            });
  server.on("/clean", HTTP_POST, [](AsyncWebServerRequest *request){ handleClean(request); });
  server.onNotFound([](AsyncWebServerRequest *request){
    String url = request->url(); // expected: /1, /2, ...
    if (url.length() >= 2 && url[0] == '/') {
      bool isNumeric = true;
      for (size_t i = 1; i < url.length(); ++i) {
        char c = url[i];
        if (c < '0' || c > '9') {
          isNumeric = false;
          break;
        }
      }
      if (isNumeric) {
        int idx = url.substring(1).toInt();
        if (idx > 0) {
          String path;
          String unusedName;
          if (resolveSoundByIndex((size_t)idx, path, unusedName)) {
            if (!requirePlaybackAuth(request)) return;
            if (!applyGainParam(request)) {
              sendTriggerResponse(request, 400, "Invalid gain");
              return;
            }
            playChimePath(path);
            recordChimeEvent("", "", "chime", "play-index", "http", String(idx), path);
            sendTriggerResponse(request, 200, "Chime triggered OK");
            return;
          }
          sendTriggerResponse(request, 404, "Sound index not found");
          return;
        }
      }
    }
    request->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("Server ready");

  // wait for Wi-Fi + IP before starting mDNS
  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED || WiFi.localIP() == INADDR_NONE) {
    if (millis() - startMs > 5000) break;
    delay(50);
  }

  delay(1500);
  startMdnsNow(mdnsName);
}

// ── Loop ───────────────────────────────────────────────────────────────────
void loop() {
  if (pendingRestart && millis() >= restartAtMs) {
    ESP.restart();
  }

  maintainWiFiConnection();

#if defined(ESP8266)
  MDNS.update(); // keep mDNS responder alive on ESP8266
#endif

  bool pressed = digitalRead(BUTTON_PIN) == LOW;
  if (pressed && !wasPressed) {
    delay(25);
    if (digitalRead(BUTTON_PIN) == LOW) {
      playChime();
      recordChimeEvent("", "", "chime", "local-button", "button", "", activeFilePath);
    }
  }
  wasPressed = pressed;
  maintainEventLed();

  if (wav && wav->isRunning() && !wav->loop()) {
    wav->stop();
    stopPlayback();
  }
  if (mp3 && mp3->isRunning() && !mp3->loop()) {
    mp3->stop();
    stopPlayback();
  }
  // Retry mDNS if it didn't start yet
  if (!mdnsOk && WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastMdnsTryMs > 5000) {
      Serial.printf("mDNS: retry begin %s\n", mdnsName.c_str());
      startMdnsNow(mdnsName);
    }
  }
  delay(1);
}
