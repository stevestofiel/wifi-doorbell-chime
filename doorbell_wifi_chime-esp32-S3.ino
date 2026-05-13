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

float currentGain = 1.0f;

String activeFilePath        = "/chime.wav";
const char* SOUNDS_FILE      = "/sounds.json";
const char* DEVICE_FILE      = "/device.json";
String      displayFilename  = "No chime loaded";
bool        uploadSucceeded  = false;
String      uploadError      = "";
String      uploadTargetPath = "";
String      uploadDisplayName = "";
String      deviceLabel      = "";
String      mdnsName         = "doorbell";
bool        mdnsOk           = false;
unsigned long lastMdnsTryMs  = 0;
bool        shouldReboot     = false;
bool        pendingRestart   = false;
unsigned long restartAtMs    = 0;
bool        wifiWasConnected = false;
unsigned long lastWiFiReconnectMs = 0;

const unsigned long WIFI_RECONNECT_INTERVAL_MS = 10000;

void onConfigSaved() {
  shouldReboot = true;
}

String buildMdnsName(const String &label) {
  String name = "doorbell";
  if (label.length() > 0) name += "-" + label;
  return name;
}

bool startMdnsNow(const String &name) {
  Serial.printf("mDNS: begin %s\n", name.c_str());
  bool ok = MDNS.begin(name.c_str());
  if (ok) {
    Serial.println("mDNS: started");
    MDNS.addService("http", "tcp", 80);
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

// ── Status JSON ────────────────────────────────────────────────────────────
void handleStatus(AsyncWebServerRequest *request) {
  int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  int signalPct = rssi <= -100 ? 0 : (rssi >= -50 ? 100 : (rssi + 100) * 2);

  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  size_t freeBytes = totalBytes >= usedBytes ? (totalBytes - usedBytes) : 0;
  int usedPct = totalBytes ? int((usedBytes * 100) / totalBytes) : 0;

  StaticJsonDocument<256> doc;
  doc["rssi"] = rssi;
  doc["signalPct"] = signalPct;
  doc["fsTotalKB"] = totalBytes / 1024.0;
  doc["fsFreeKB"] = freeBytes / 1024.0;
  doc["fsUsedPct"] = usedPct;
  doc["wifi"] = (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
  doc["ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : "";
  doc["mdns"] = buildMdnsName(deviceLabel) + ".local";
  doc["deviceLabel"] = deviceLabel;
  doc["gain"] = currentGain;
  doc["activeName"] = displayFilename;
  doc["activePath"] = activeFilePath;

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

void handleSetActive(AsyncWebServerRequest *request) {
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
  if (!SPIFFS.exists(DEVICE_FILE)) return;
  File f = SPIFFS.open(DEVICE_FILE, "r");
  if (!f) return;

  StaticJsonDocument<128> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();

  if (!error && doc.containsKey("label")) {
    deviceLabel = doc["label"].as<String>();
  }
}

void saveDeviceConfig(const String &label) {
  StaticJsonDocument<128> doc;
  doc["label"] = label;
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
    sendTriggerResponse(request, 200, "Chime triggered OK");
    return;
  }

  playChime();
  sendTriggerResponse(request, 200, "Chime triggered OK");
}

// ── Set gain ───────────────────────────────────────────────────────────────
void handleSetGain(AsyncWebServerRequest *request) {
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
    saveDeviceConfig(deviceLabel);
  }

  mdnsName = buildMdnsName(deviceLabel);
  if (mdnsOk) MDNS.end();
  startMdnsNow(mdnsName);

  StaticJsonDocument<256> doc;
  doc["ok"] = true;
  doc["label"] = deviceLabel;
  doc["mdns"] = mdnsName + ".local";
  String payload;
  serializeJson(doc, payload);
  request->send(200, "application/json", payload);
}

void handleResetWiFi(AsyncWebServerRequest *request) {
  WiFi.disconnect(true, true); // clear credentials from NVS
  scheduleRestart(600);
  request->send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
}

// ── Clean all user files ───────────────────────────────────────────────────
void handleClean(AsyncWebServerRequest *request) {
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
    body {font-family:system-ui,sans-serif; max-width:520px; margin:2rem auto; padding:0 1rem; background:#f9fafb; color:#111; text-align:center;}
    h1 {color:#2563eb;}
    .card {background:white; border-radius:12px; padding:2rem; box-shadow:0 4px 14px rgba(0,0,0,0.1); margin-bottom:1.5rem;}
    button {background:#2563eb; color:white; border:none; padding:1.2rem 2rem; border-radius:12px; font-size:1.3rem; cursor:pointer; margin:1rem; min-width:220px;}
    button:hover {background:#1d4ed8;}
    button:disabled {background:#9ca3af; cursor:not-allowed;}
    .manage-wrap {margin-top:0.6rem; text-align:center;}
    .manage-btn {
      background:#e5e7eb;
      color:#374151;
      border:1px solid #d1d5db;
      padding:0.55rem 0.9rem;
      border-radius:9px;
      font-size:0.95rem;
      min-width:auto;
      margin:0;
    }
    .manage-btn:hover {background:#d1d5db;}
    .status {font-size:1.1rem; margin:1.5rem 0; color:#4b5563;}
    .big {font-size:1.4rem; font-weight:bold; color:#111;}
    .file-info {font-size:1rem; color:#6b7280; margin-top:0.5rem;}
    .ip-info {font-size:0.82rem; color:#9ca3af; margin-top:0.2rem;}
    .info-box {margin:1.2rem 0; text-align:left;}
    .info-row {display:flex; justify-content:space-between; align-items:center; font-size:0.95rem; color:#374151; margin-bottom:0.4rem;}
    .bar {width:100%; height:10px; background:#e5e7eb; border-radius:999px; overflow:hidden;}
    .fill {height:100%; background:#2563eb;}
    .fill.warn {background:#f59e0b;}
    .fill.bad {background:#ef4444;}
  </style>
</head>
<body>
  <div class="card">
    <h1>Doorbell Chime</h1>
    <div class="status">
      <div class="big">__STATUS__</div>
      <div class="file-info" id="fileInfo">__FILEINFO__</div>
      <div class="ip-info">IP: __IPADDR__</div>
    </div>
    __PLAYBTN__

    <div class="info-box" id="wifiBox">
      <div class="info-row">
        <span>Wi‑Fi Signal</span>
        <span id="wifiText">__WIFITEXT__</span>
      </div>
      <div class="bar"><div id="wifiBar" class="fill__WIFICLASS__" style="width:__WIFIPCT__%"></div></div>
    </div>

    <div class="info-box" id="fsBox">
      <div class="info-row">
        <span>Available Storage</span>
      <span id="fsText">__FSTEXT__</span>
      </div>
      <div class="bar"><div id="fsBar" class="fill__FSCLASS__" style="width:__FSPCT__%"></div></div>
    </div>

    <div class="manage-wrap">
      <a href="/upload"><button class="manage-btn">Manage Chimes</button></a>
    </div>
  </div>

  <script>
    const wifiText = document.getElementById('wifiText');
    const wifiBar  = document.getElementById('wifiBar');
    const fsText   = document.getElementById('fsText');
    const fsBar    = document.getElementById('fsBar');

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
    "<button onclick=\"fetch('/chime')\">Play Current Chime</button>" :
    "<button disabled>Play Current Chime (not available)</button>";

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
      --primary: #2563eb;
      --primary-dark: #1d4ed8;
      --bg: #f9fafb;
      --card: white;
      --text: #111827;
      --text-light: #6b7280;
      --danger: #dc2626;
      --border-rest: #9ca3af;
      --border-hover: #60a5fa;
      --drop-bg: #ffffff;
      --drop-hover: #eff6ff;
      --drop-active: #dbeafe;
    }
    * { box-sizing: border-box; }
    body {
      font-family: system-ui, -apple-system, sans-serif;
      background: var(--bg);
      color: var(--text);
      margin: 0;
      padding: 1.5rem 1rem;
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: flex-start;
    }
    .card {
      background: var(--card);
      border-radius: 16px;
      padding: 2rem;
      box-shadow: 0 8px 20px rgba(0,0,0,0.08);
      width: 100%;
      max-width: 500px;
    }
    h1 {
      color: var(--primary);
      font-size: 1.8rem;
      margin: 0 0 1.5rem;
      text-align: center;
      font-weight: 600;
    }
    .status {
      text-align: center;
      margin-bottom: 1rem;
    }
    .status .big {
      font-size: 1.2rem;
      font-weight: 700;
      color: var(--text);
    }
    .status .sub {
      color: var(--text-light);
      margin-top: 0.25rem;
      font-size: 0.95rem;
      word-break: break-word;
    }
    .status .tiny {
      color: #9ca3af;
      margin-top: 0.2rem;
      font-size: 0.8rem;
    }
    .info-box {margin:1rem 0; text-align:left;}
    .info-row {display:flex; justify-content:space-between; align-items:center; font-size:0.95rem; color:#374151; margin-bottom:0.4rem;}
    .bar {width:100%; height:10px; background:#e5e7eb; border-radius:999px; overflow:hidden;}
    .fill {height:100%; background:#2563eb;}
    .fill.warn {background:#f59e0b;}
    .fill.bad {background:#ef4444;}
    .section {
      margin-top: 1.25rem;
      padding-top: 1rem;
      border-top: 1px solid #e5e7eb;
    }
    .section h2 {
      margin: 0 0 0.8rem;
      font-size: 1rem;
      color: #374151;
      text-align: left;
    }
    .list-item {display:flex; align-items:center; gap:0.4rem; padding:2px 0; line-height:1.2; text-align:left;}
    .list-item input[type=radio] {margin:0 4px 0 0;}
    .list-item label {cursor:pointer; flex:1; margin:0;}
    .trash-btn {
      background: transparent;
      border: none;
      color: #9ca3af;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      padding: 2px;
      margin: 0;
      height: 20px;
      width: 20px;
      line-height: 1;
    }
    .trash-btn:hover { color:#ef4444; }
    .trash-btn svg { width: 14px; height: 14px; display:block; }
    .play-btn {
      background: transparent;
      border: none;
      color: #2563eb;
      cursor: pointer;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      padding: 2px;
      margin: 0;
      height: 20px;
      width: 20px;
      line-height: 1;
    }
    .play-btn:hover { color:#1d4ed8; }
    .play-btn svg { width: 14px; height: 14px; display:block; }
    .network-box {text-align:left; background:#f8fafc; border:1px solid #e5e7eb; border-radius:10px; padding:0.8rem;}
    .network-title {font-size:0.9rem; font-weight:600; color:#374151; margin-bottom:0.5rem;}
    .network-row {display:flex; gap:0.4rem; align-items:center;}
    .network-row input {flex:1; border:1px solid #d1d5db; border-radius:8px; padding:0.45rem 0.55rem; font-size:0.9rem;}
    .network-row button {width:auto; min-width:auto; margin:0; padding:0.5rem 0.9rem; font-size:0.9rem; border-radius:8px;}
    .network-help {margin-top:0.45rem; font-size:0.78rem; color:#6b7280; text-align:left;}
    .btn-secondary {background:#64748b;}
    .btn-secondary:hover:not(:disabled) {background:#475569;}
    .volume-box {margin-top: 0.5rem; padding:1rem; background:#f1f5f9; border-radius:8px; text-align:center;}
    .slider-container {margin:0.4rem 0;}
    input[type=range] {width:90%; max-width:400px; height:12px;}
    #gainDisplay {font-size:1.4em; font-weight:bold; margin:0.5rem 0;}
    #fileName {
      font-weight: 600;
      color: var(--primary);
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
    }
    button {
      background: var(--primary);
      color: white;
      border: none;
      padding: 1rem;
      border-radius: 12px;
      font-size: 1.2rem;
      font-weight: 500;
      cursor: pointer;
      width: 100%;
      margin-top: 1.5rem;
      transition: all 0.2s;
    }
    button:hover:not(:disabled) {
      background: var(--primary-dark);
    }
    button:disabled {
      background: #cbd5e1;
      color: #64748b;
      cursor: not-allowed;
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
      background: #e5e7eb;
      border-radius: 999px;
      overflow: hidden;
      margin-top: 0.4rem;
    }
    .progress-fill {
      height: 100%;
      width: 0%;
      background: var(--primary);
      transition: width 0.15s linear;
    }
    .danger {
      margin-top: 1.5rem;
      border-top: 1px solid #e5e7eb;
      padding-top: 1rem;
      text-align: center;
    }
    .danger button {
      background: transparent;
      color: var(--danger);
      border: 1px solid rgba(220,38,38,0.4);
    }
    .danger button:hover:not(:disabled) {
      background: rgba(220,38,38,0.08);
    }
    .back {
      text-align: center;
      margin-top: 2rem;
      color: var(--text-light);
      font-size: 0.95rem;
    }
    .back a {
      color: var(--primary);
      text-decoration: none;
      font-weight: 500;
    }
    .back a:hover { text-decoration: underline; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Manage Chimes</h1>
    <div class="status">
      <div class="big" id="deviceStatus">Loading…</div>
      <div class="sub" id="deviceActive">No chime loaded</div>
      <div class="tiny">IP: <span id="deviceIp">—</span></div>
      <div class="tiny">mDNS: <span id="deviceMdns">doorbell.local</span></div>
    </div>

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

    <div class="section">
      <h2>Upload New Sound</h2>
    <form id="uploadForm" action="/upload" method="POST" enctype="multipart/form-data">
      <label for="fileInput" class="prompt">Choose a WAV or MP3 file</label>
      <input type="file" id="fileInput" name="file" accept=".wav,.mp3" required>
      <div id="fileName"></div>
      <div id="fileSize"></div>
      <button type="submit" id="uploadBtn" disabled>
        <span class="spin btn-spin" aria-hidden="true"></span>
        <span id="uploadText">Upload</span>
      </button>
      <div class="progress" id="progressBox" style="display:none;">
        <div id="progressText">Starting…</div>
        <div class="progress-bar"><div class="progress-fill" id="progressFill"></div></div>
      </div>
    </form>
    </div>

    <div class="section">
      <h2>Available Chimes</h2>
      <div id="soundList" style="text-align:left;">Loading…</div>
    </div>

    <div class="section">
      <h2>Volume</h2>
      <div class="volume-box">
        <div class="slider-container">
          <label>Volume Gain (0.0 – 3.0):</label><br>
          <input type="range" min="0" max="300" value="100" step="1" id="gainSlider">
          <div id="gainDisplay">1.00</div>
        </div>
      </div>
    </div>

    <div class="section">
      <h2>Network</h2>
      <div class="network-box">
        <div class="network-title">Device Name</div>
        <div class="network-row">
          <input id="labelInput" type="text" value="" maxlength="24" placeholder="front-door">
          <button id="saveLabelBtn" type="button">Save Name</button>
          <button id="resetWifiBtn" class="btn-secondary" type="button">Reset Wi‑Fi</button>
        </div>
        <div class="network-help">mDNS: <span id="mdnsHost">doorbell.local</span></div>
      </div>
    </div>
    <div class="danger">
      <button id="cleanBtn" type="button">Delete All Files</button>
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
    const deviceStatus = document.getElementById('deviceStatus');
    const deviceActive = document.getElementById('deviceActive');
    const deviceIp = document.getElementById('deviceIp');
    const deviceMdns = document.getElementById('deviceMdns');
    const soundList = document.getElementById('soundList');
    const gainSlider = document.getElementById('gainSlider');
    const gainDisplay = document.getElementById('gainDisplay');
    const labelInput = document.getElementById('labelInput');
    const saveLabelBtn = document.getElementById('saveLabelBtn');
    const resetWifiBtn = document.getElementById('resetWifiBtn');
    const mdnsHost = document.getElementById('mdnsHost');
    let maxBytes = 3000 * 1024;

    function barClass(pct, warnAt, badAt) {
      if (pct < badAt) return 'fill bad';
      if (pct < warnAt) return 'fill warn';
      return 'fill';
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
      fetch(`/setgain?value=${val}`).catch(() => {});
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
      xhr.open('POST', '/upload', true);
      xhr.send(formData);
    });

    cleanBtn.addEventListener('click', () => {
      if (!confirm('Delete ALL uploaded files? Cannot be undone!')) return;
      fetch('/clean', {method:'POST'})
        .then(() => location.href = '/')
        .catch(() => {});
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
          deviceStatus.textContent = hasActive ? 'Manage Chimes' : 'No Chime Loaded';
          deviceActive.textContent = activeName;
          deviceIp.textContent = s.ip || 'not connected';
          deviceMdns.textContent = s.mdns || 'doorbell.local';
          mdnsHost.textContent = s.mdns || 'doorbell.local';
          labelInput.value = (s.deviceLabel ?? labelInput.value ?? '');
          const gain = Number(s.gain ?? 1);
          if (document.activeElement !== gainSlider) {
            gainSlider.value = Math.round(gain * 100);
            gainDisplay.textContent = gain.toFixed(2);
          }
        })
        .catch(() => {});
    }

    function refreshSounds() {
      fetch('/list')
        .then(r => r.json())
        .then(s => {
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
            radio.checked = (item.path === s.active);
            radio.addEventListener('change', () => {
              fetch(`/setactive?path=${encodeURIComponent(item.path)}`)
                .then(() => { refreshSounds(); refreshStatus(); })
                .catch(() => {});
            });
            const label = document.createElement('label');
            label.textContent = item.name || item.path;
            const play = document.createElement('button');
            play.className = 'play-btn';
            play.title = 'Play';
            play.type = 'button';
            play.innerHTML = `<svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
              <path d="M8 5v14l11-7z"/>
            </svg>`;
            play.addEventListener('click', () => {
              const endpoint = item.endpoint || `/play?key=${encodeURIComponent(item.key || '')}`;
              fetch(endpoint).catch(() => {});
            });
            const del = document.createElement('button');
            del.className = 'trash-btn';
            del.title = 'Delete';
            del.type = 'button';
            del.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">
              <path d="M3 6h18"/><path d="M8 6V4h8v2"/><path d="M19 6l-1 14H6L5 6"/><path d="M10 11v6M14 11v6"/>
            </svg>`;
            del.addEventListener('click', () => {
              if (!confirm(`Delete "${item.name || item.path}"?`)) return;
              fetch(`/delete?path=${encodeURIComponent(item.path)}`)
                .then(() => { refreshSounds(); refreshStatus(); })
                .catch(() => {});
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

    function saveLabel() {
      const body = `label=${encodeURIComponent(labelInput.value)}`;
      fetch('/setlabel', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body
      })
      .then(r => r.json())
      .then(resp => {
        if (resp && resp.ok) {
          labelInput.value = resp.label || '';
          mdnsHost.textContent = resp.mdns || 'doorbell.local';
          deviceMdns.textContent = resp.mdns || 'doorbell.local';
        }
      })
      .catch(() => {});
    }

    function resetWiFi() {
      if (!confirm('Reset Wi-Fi credentials and reboot to captive portal?')) return;
      fetch('/resetwifi', {method:'POST'})
        .then(() => {
          deviceStatus.textContent = 'Rebooting…';
        })
        .catch(() => {});
    }

    saveLabelBtn.addEventListener('click', saveLabel);
    labelInput.addEventListener('keydown', e => {
      if (e.key === 'Enter') {
        e.preventDefault();
        saveLabel();
      }
    });
    resetWifiBtn.addEventListener('click', resetWiFi);

    // Init
    uploadBtn.disabled = true;
    updateFileInfo();
    refreshStatus();
    refreshSounds();
    setInterval(refreshStatus, 10000);
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

  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(onConfigSaved);
  wifiManager.setConnectTimeout(20);
  wifiManager.setConfigPortalTimeout(180);
  String labelDefault = deviceLabel;
  char labelBuf[32];
  labelDefault.toCharArray(labelBuf, sizeof(labelBuf));
  WiFiManagerParameter labelParam("label", "Room/Label (e.g. front-door)", labelBuf, 31);
  wifiManager.addParameter(&labelParam);
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

  Serial.print("IP: "); Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ handleRoot(request); });
  server.on("/chime", HTTP_GET, [](AsyncWebServerRequest *request){ handleChime(request); });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){ handleStatus(request); });
  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request){ handleList(request); });
  server.on("/setgain", HTTP_GET, [](AsyncWebServerRequest *request){ handleSetGain(request); });
  server.on("/setlabel", HTTP_POST, [](AsyncWebServerRequest *request){ handleSetLabel(request); });
  server.on("/resetwifi", HTTP_POST, [](AsyncWebServerRequest *request){ handleResetWiFi(request); });
  server.on("/play", HTTP_GET, [](AsyncWebServerRequest *request){ handlePlayByKey(request); });
  server.on("/setactive", HTTP_GET, [](AsyncWebServerRequest *request){ handleSetActive(request); });
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request){ handleDelete(request); });
  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request){ handleUploadForm(request); });
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
            if (!applyGainParam(request)) {
              sendTriggerResponse(request, 400, "Invalid gain");
              return;
            }
            playChimePath(path);
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
    if (digitalRead(BUTTON_PIN) == LOW) playChime();
  }
  wasPressed = pressed;

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
