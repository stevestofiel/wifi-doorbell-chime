// SPDX-License-Identifier: MIT
// Wi-Fi remote sensor prototype for ESP32 Super Mini-style boards.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>

// ---- Saved configuration ---------------------------------------------------
Preferences prefs;
String chimeBaseUrl = "";
String chimeToken = "";
String sensorId = "bench-button";
String sensorType = "doorbell";
String sensorEvent = "press";
bool shouldSaveParams = false;
const char* SETUP_AP_SSID = "ChimeSensor";
const char* SETUP_AP_PASSWORD = "config123";

// ---- Wiring ----------------------------------------------------------------
// Button: one side to GPIO3, other side to GND. Uses internal pull-up.
const int BUTTON_PIN = 3;

// Touch module: G -> GND, V -> 3.3V, S -> GPIO4.
const int TOUCH_PIN = 4;
const int TOUCH_ACTIVE_LEVEL = HIGH;

// ---- Timing ----------------------------------------------------------------
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long TRIGGER_COOLDOWN_MS = 2500;
const unsigned long DEBOUNCE_MS = 40;
const unsigned long TOUCH_HOLD_MS = 180;
const unsigned long SETUP_HOLD_MS = 2000;

unsigned long lastTriggerMs = 0;
unsigned long lastButtonChangeMs = 0;
unsigned long lastTouchChangeMs = 0;
bool forceSetupPortal = false;
bool lastButtonRaw = HIGH;
bool buttonStable = HIGH;
bool lastTouchRaw = LOW;
bool touchStableActive = false;
bool touchTriggeredWhileActive = false;

void onSaveParams() {
  shouldSaveParams = true;
}

String cleanId(const String& input, const String& fallback, size_t maxLen) {
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

String cleanUrl(const String& input) {
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

void loadSensorConfig() {
  prefs.begin("sensor", true);
  chimeBaseUrl = prefs.getString("chime", "");
  chimeToken = prefs.getString("token", "");
  sensorId = prefs.getString("id", sensorId);
  sensorType = prefs.getString("type", sensorType);
  sensorEvent = prefs.getString("event", sensorEvent);
  prefs.end();

  chimeBaseUrl = cleanUrl(chimeBaseUrl);
  sensorId = cleanId(sensorId, "bench-button", 31);
  sensorType = cleanId(sensorType, "doorbell", 23);
  sensorEvent = cleanId(sensorEvent, "press", 23);
}

void saveSensorConfig() {
  prefs.begin("sensor", false);
  prefs.putString("chime", chimeBaseUrl);
  prefs.putString("token", chimeToken);
  prefs.putString("id", sensorId);
  prefs.putString("type", sensorType);
  prefs.putString("event", sensorEvent);
  prefs.end();
  Serial.println("Config: saved sensor settings");
}

void clearSensorConfig() {
  prefs.begin("sensor", false);
  prefs.clear();
  prefs.end();
  chimeBaseUrl = "";
  chimeToken = "";
  sensorId = "bench-button";
  sensorType = "doorbell";
  sensorEvent = "press";
  Serial.println("Config: cleared sensor settings");
}

bool hasRequiredConfig() {
  return chimeBaseUrl.startsWith("http://") || chimeBaseUrl.startsWith("https://");
}

void copyParam(const String& value, char* buffer, size_t size) {
  value.toCharArray(buffer, size);
}

void configureWiFiAndSensor() {
  loadSensorConfig();
  if (forceSetupPortal) {
    WiFi.disconnect(true, true);
    clearSensorConfig();
  }

  char chimeBuf[96];
  char tokenBuf[65];
  char idBuf[32];
  char typeBuf[24];
  char eventBuf[24];
  copyParam(chimeBaseUrl, chimeBuf, sizeof(chimeBuf));
  copyParam(chimeToken, tokenBuf, sizeof(tokenBuf));
  copyParam(sensorId, idBuf, sizeof(idBuf));
  copyParam(sensorType, typeBuf, sizeof(typeBuf));
  copyParam(sensorEvent, eventBuf, sizeof(eventBuf));

  WiFiManager wm;
  wm.setSaveParamsCallback(onSaveParams);
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

  String newChime = cleanUrl(String(chimeParam.getValue()));
  String newToken = String(tokenParam.getValue());
  String newId = cleanId(String(idParam.getValue()), "bench-button", 31);
  String newType = cleanId(String(typeParam.getValue()), "doorbell", 23);
  String newEvent = cleanId(String(eventParam.getValue()), "press", 23);

  bool changed = newChime != chimeBaseUrl || newToken != chimeToken ||
                 newId != sensorId || newType != sensorType || newEvent != sensorEvent;
  chimeBaseUrl = newChime;
  chimeToken = newToken;
  sensorId = newId;
  sensorType = newType;
  sensorEvent = newEvent;
  if (changed || shouldSaveParams) saveSensorConfig();

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  Serial.printf("Config: chime=%s sensor=%s type=%s event=%s\n",
                chimeBaseUrl.c_str(), sensorId.c_str(), sensorType.c_str(), sensorEvent.c_str());
}

String withToken(const String& url) {
  if (chimeToken.length() == 0) return url;
  return url + (url.indexOf('?') >= 0 ? "&token=" : "?token=") + chimeToken;
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

void sendTrigger(const char* source) {
  unsigned long now = millis();
  if (now - lastTriggerMs < TRIGGER_COOLDOWN_MS) return;
  lastTriggerMs = now;

  Serial.printf("Trigger: %s\n", source);
  if (!ensureWiFi()) return;
  if (!hasRequiredConfig()) {
    Serial.println("Trigger: missing chime URL");
    return;
  }

  String triggerUrl = chimeBaseUrl +
    "/trigger?sensor=" + sensorId +
    "&type=" + sensorType +
    "&event=" + sensorEvent;

  int status = httpGet(withToken(triggerUrl));

  // Until the chime firmware has /trigger, fall back to the current endpoint.
  if (status == 404) {
    Serial.println("Trigger endpoint missing; falling back to /chime");
    httpGet(withToken(chimeBaseUrl + "/chime"));
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT);

  Serial.println();
  Serial.println("Wi-Fi sensor prototype starting");
  Serial.printf("Button GPIO: %d\n", BUTTON_PIN);
  Serial.printf("Touch GPIO: %d\n", TOUCH_PIN);
  Serial.printf("Touch hold: %lu ms\n", TOUCH_HOLD_MS);
  Serial.printf("Setup hold: %lu ms\n", SETUP_HOLD_MS);
  Serial.println("Raw input changes will print as button=<0|1> touch=<0|1>");

  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Setup gesture: button held, checking hold duration");
    unsigned long holdStart = millis();
    while (digitalRead(BUTTON_PIN) == LOW && millis() - holdStart < SETUP_HOLD_MS) {
      delay(20);
    }
    forceSetupPortal = millis() - holdStart >= SETUP_HOLD_MS;
    Serial.println(forceSetupPortal ? "Setup gesture: accepted" : "Setup gesture: ignored");
  }

  configureWiFiAndSensor();

  lastButtonRaw = digitalRead(BUTTON_PIN);
  buttonStable = lastButtonRaw;
  lastTouchRaw = digitalRead(TOUCH_PIN);
  touchStableActive = lastTouchRaw == TOUCH_ACTIVE_LEVEL;
  touchTriggeredWhileActive = touchStableActive;
  Serial.printf("Initial inputs: button=%d touch=%d\n", lastButtonRaw, lastTouchRaw);
}

void loop() {
  bool buttonRaw = digitalRead(BUTTON_PIN);
  bool touchRaw = digitalRead(TOUCH_PIN);
  unsigned long now = millis();

  if (buttonRaw != lastButtonRaw) {
    lastButtonRaw = buttonRaw;
    lastButtonChangeMs = now;
    Serial.printf("Input change: button=%d touch=%d\n", buttonRaw, touchRaw);
  }

  if ((now - lastButtonChangeMs) > DEBOUNCE_MS && buttonRaw != buttonStable) {
    buttonStable = buttonRaw;
    if (buttonStable == LOW) {
      sendTrigger("button");
    }
  }

  if (touchRaw != lastTouchRaw) {
    lastTouchRaw = touchRaw;
    lastTouchChangeMs = now;
    Serial.printf("Input change: button=%d touch=%d\n", buttonRaw, touchRaw);
  }

  bool touchActive = lastTouchRaw == TOUCH_ACTIVE_LEVEL;
  if ((now - lastTouchChangeMs) >= TOUCH_HOLD_MS && touchActive != touchStableActive) {
    touchStableActive = touchActive;
    if (!touchStableActive) {
      touchTriggeredWhileActive = false;
    }
  }

  if (touchStableActive && !touchTriggeredWhileActive) {
    sendTrigger("touch");
    touchTriggeredWhileActive = true;
  }

  delay(10);
}
