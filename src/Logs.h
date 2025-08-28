#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

class Logs {
  static const int MAX_LOGS = 50;
  String logs[MAX_LOGS];
  int count = 0;

public:
  void begin() {
    loadFromFS();
  }

  void add(const String& txt) {
    // Dodaj datę i godzinę
    String entry = getTimestamp() + " – " + txt;

    if (count < MAX_LOGS) {
      logs[count++] = entry;
    } else {
      // FIFO – przesunięcie w lewo
      for (int i = 1; i < MAX_LOGS; i++) {
        logs[i - 1] = logs[i];
      }
      logs[MAX_LOGS - 1] = entry;
    }
    saveToFS();
  }

  void clear() {
    count = 0;
    saveToFS();
  }

  void toJson(JsonDocument& doc) {
    JsonArray arr = doc["logs"].to<JsonArray>();
    for (int i = 0; i < count; i++) {
      arr.add(logs[i]);
    }
  }

private:
  String getTimestamp() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             t.tm_year + 1900,
             t.tm_mon + 1,
             t.tm_mday,
             t.tm_hour,
             t.tm_min,
             t.tm_sec);
    return String(buf);
  }

  void loadFromFS() {
    if (!LittleFS.exists("/logs.json")) {
      count = 0;
      return;
    }
    File f = LittleFS.open("/logs.json", "r");
    if (!f) {
      count = 0;
      return;
    }
    StaticJsonDocument<4096> doc; // powinno wystarczyć dla 30 wpisów
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      Serial.print("[Logs] Błąd odczytu logs.json: ");
      Serial.println(err.c_str());
      count = 0;
      return;
    }
    count = 0;
    for (JsonVariant v : doc.as<JsonArray>()) {
      if (count >= MAX_LOGS) break;
      logs[count++] = v.as<String>();
    }
  }

  void saveToFS() {
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < count; i++) {
      arr.add(logs[i]);
    }
    File f = LittleFS.open("/logs.json", "w");
    if (!f) {
      Serial.println("[Logs] Nie można otworzyć logs.json do zapisu!");
      return;
    }
    if (serializeJson(doc, f) == 0) {
      Serial.println("[Logs] Błąd zapisu logs.json!");
    }
    f.close();
  }
};
