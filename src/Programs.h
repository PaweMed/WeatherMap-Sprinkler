#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>   // ArduinoJson v7: używaj JsonDocument
#include <LittleFS.h>
#include <time.h>
#include "Zones.h"
#include "Weather.h"
#include "Logs.h"
#include "PushoverClient.h"
#include "Config.h"

struct Program {
  uint8_t  zone = 0;
  String   time;         // "HH:MM"
  uint16_t duration = 0; // minuty
  String   days;         // CSV: "0,1,2"
  bool     active = true;
  time_t   lastRun = 0;  // UNIX time ostatniego uruchomienia (persist)
};

class Programs {
  static constexpr int MAX_PROGS = 32;

  Program progs[MAX_PROGS];
  int     numProgs = 0;

  Zones*          zones    = nullptr;
  Weather*        weather  = nullptr;
  Logs*           logs     = nullptr;
  PushoverClient* pushover = nullptr;
  Config*         config   = nullptr; // wskaźnik na Config

  static bool containsDay(const String& daysStr, int today) {
    int lastPos = 0, pos;
    while ((pos = daysStr.indexOf(',', lastPos)) != -1) {
      int dayNum = daysStr.substring(lastPos, pos).toInt();
      if (dayNum == today) return true;
      lastPos = pos + 1;
    }
    if (lastPos < (int)daysStr.length()) {
      int dayNum = daysStr.substring(lastPos).toInt();
      if (dayNum == today) return true;
    }
    return false;
  }

  static String daysArrayToCsv(JsonArray arr) {
    String d;
    for (auto v : arr) {
      if (d.length() > 0) d += ",";
      d += String((int)v);
    }
    return d;
  }

  static void csvToDaysArray(const String& csv, JsonArray out) {
    int lastPos = 0, pos;
    while ((pos = csv.indexOf(',', lastPos)) != -1) {
      out.add(csv.substring(lastPos, pos).toInt());
      lastPos = pos + 1;
    }
    if (lastPos < (int)csv.length()) out.add(csv.substring(lastPos).toInt());
  }

public:
  void begin(Zones* z, Weather* w, Logs* l, PushoverClient* p, Config* c) {
    zones = z;
    weather = w;
    logs = l;
    pushover = p;
    config = c;
    loadFromFS();
  }

  int size() const { return numProgs; }

  void toJson(JsonDocument& doc) {
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < numProgs; i++) {
      JsonObject p = arr.add<JsonObject>();
      p["id"]       = i;
      p["zone"]     = progs[i].zone;
      p["time"]     = progs[i].time;
      p["duration"] = progs[i].duration;
      p["active"]   = progs[i].active;

      JsonArray daysArr = p["days"].to<JsonArray>();
      csvToDaysArray(progs[i].days, daysArr);
    }
  }

  bool edit(int idx, JsonDocument& doc, bool save=true, bool logIt=true) {
    if (idx < 0 || idx >= numProgs) return false;
    Program &P = progs[idx];
    if (doc["zone"].is<uint8_t>())      P.zone = doc["zone"].as<uint8_t>();
    if (doc["time"].is<const char*>())  P.time = doc["time"].as<const char*>();
    if (doc["duration"].is<uint16_t>()) P.duration = doc["duration"].as<uint16_t>();
    if (doc["active"].is<bool>())       P.active = doc["active"].as<bool>();
    if (doc["days"].is<JsonArray>())    P.days = daysArrayToCsv(doc["days"].as<JsonArray>());

    if (save) saveToFS();
    if (logIt) {
      if (logs)     logs->add("Edytowano program strefy " + String(P.zone + 1));
      if (pushover) pushover->send("Edytowano program strefy " + String(P.zone + 1));
    }
    return true;
  }

  bool remove(int idx, bool logIt=true) {
    if (idx < 0 || idx >= numProgs) return false;
    for (int i = idx; i < numProgs - 1; i++) progs[i] = progs[i + 1];
    numProgs--;
    saveToFS();
    if (logIt) {
      if (logs)     logs->add("Usunięto program " + String(idx));
      if (pushover) pushover->send("Usunięto program " + String(idx));
    }
    return true;
  }

  void clear() {
    numProgs = 0;
    saveToFS();
    if (logs)     logs->add("Wyczyszczono wszystkie programy");
    if (pushover) pushover->send("Wyczyszczono wszystkie programy");
  }

  void addFromJson(JsonDocument& doc) {
    if (doc.is<JsonArray>()) {
      importFromJson(doc);
      return;
    }

    int idx = -1;
    if (doc["id"].is<int>())    idx = doc["id"].as<int>();
    if (doc["index"].is<int>()) idx = doc["index"].as<int>();
    if (idx >= 0) {
      edit(idx, doc, true, true);
      return;
    }

    if (numProgs < MAX_PROGS) {
      Program P;
      P.zone     = doc["zone"].as<uint8_t>();
      P.time     = doc["time"].as<const char*>();
      P.duration = doc["duration"].as<uint16_t>();
      P.days     = daysArrayToCsv(doc["days"].as<JsonArray>());
      P.active   = doc["active"].isNull() ? true : doc["active"].as<bool>();
      P.lastRun  = 0;

      progs[numProgs++] = P;
      saveToFS();
    } else {
      if (logs)     logs->add("Nie dodano programu – maksymalna liczba programów");
      if (pushover) pushover->send("Nie dodano programu – maksymalna liczba programów");
    }
  }

  void importFromJson(JsonDocument& doc) {
    JsonArray arr = doc.as<JsonArray>();
    numProgs = 0;
    for (auto el : arr) {
      if (numProgs >= MAX_PROGS) break;
      Program P;
      P.zone     = el["zone"]     | 0;
      P.time     = el["time"]     | "06:00";
      P.duration = el["duration"] | 10;
      if (el["days"].is<JsonArray>()) {
        P.days = daysArrayToCsv(el["days"].as<JsonArray>());
      } else {
        P.days = el["days"] | "0,1,2,3,4,5,6";
      }
      P.active   = el["active"].isNull() ? true : el["active"].as<bool>();
      P.lastRun  = 0;
      progs[numProgs++] = P;
    }
    saveToFS();
    if (logs)     logs->add("Zaimportowano programy");
    if (pushover) pushover->send("Zaimportowano programy");
  }

  void saveToFS() {
    File f = LittleFS.open("/programs.json", "w");
    if (!f) return;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < numProgs; i++) {
      JsonObject p = arr.add<JsonObject>();
      p["zone"]     = progs[i].zone;
      p["time"]     = progs[i].time;
      p["duration"] = progs[i].duration;
      p["days"]     = progs[i].days;
      p["active"]   = progs[i].active;
      p["lastRun"]  = (long)progs[i].lastRun;
    }
    serializeJson(doc, f);
    f.close();
  }

  void loadFromFS() {
    numProgs = 0;
    if (!LittleFS.exists("/programs.json")) return;
    File f = LittleFS.open("/programs.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f)) {
      f.close();
      return;
    }
    f.close();
    if (doc.is<JsonArray>()) {
      for (auto el : doc.as<JsonArray>()) {
        if (numProgs >= MAX_PROGS) break;
        Program P;
        P.zone     = el["zone"]     | 0;
        P.time     = el["time"]     | "06:00";
        P.duration = el["duration"] | 10;
        if (el["days"].is<JsonArray>()) {
          P.days = daysArrayToCsv(el["days"].as<JsonArray>());
        } else {
          P.days = el["days"] | "0,1,2,3,4,5,6";
        }
        P.active  = el["active"].isNull() ? true : el["active"].as<bool>();
        if (!el["lastRun"].isNull()) {
          P.lastRun = (time_t)(el["lastRun"].as<long>());
        } else {
          P.lastRun = 0;
        }
        progs[numProgs++] = P;
      }
    }
  }

  void loop() {
    if (!config || !config->getAutoMode()) return;

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 10000) return;
    lastCheck = millis();

    time_t now = ::time(nullptr);
    struct tm nowTm{};
    localtime_r(&now, &nowTm);

    int today   = nowTm.tm_wday;
    int nowMins = nowTm.tm_hour * 60 + nowTm.tm_min;

    for (int i = 0; i < numProgs; i++) {
      const Program& P = progs[i];
      if (!P.active) continue;

      bool runToday = containsDay(P.days, today);
      if (!runToday) continue;

      int pHour    = atoi(P.time.substring(0, 2).c_str());
      int pMin     = atoi(P.time.substring(3, 5).c_str());
      int progMins = pHour * 60 + pMin;

      int lastYday = -1;
      if (P.lastRun != 0) {
        struct tm lastTm{};
        localtime_r(&P.lastRun, &lastTm);
        lastYday = lastTm.tm_yday;
      }

      if (nowMins == progMins && (P.lastRun == 0 || lastYday != nowTm.tm_yday)) {
        int baseDuration = P.duration;
        int actualDuration = baseDuration;
        int wateringPercent = weather ? weather->getWateringPercent() : 100;

        // Zbierz dane pogodowe do logów (jeśli dostępne)
        float rain6h = weather ? weather->getLast6hRain() : -1.0f;
        float tMax   = weather ? weather->getDailyMaxTemp() : -1000.0f;
        int   hMax   = weather ? weather->getDailyHumidityForecast() : -1;

        if (wateringPercent == 0) {
          if (logs) logs->add(
            String("Podlewanie odwołane – warunki pogodowe. ")
            + "6h=" + String(rain6h, 1) + "mm, "
            + "Tmax=" + String(tMax, 1) + "°C, "
            + "Hmax=" + String(hMax) + "%, "
            + "plan=" + String(baseDuration) + "min → 0min"
          );
          if (pushover && config && config->getEnablePushover())
            pushover->send(
              String("Automat: odwołano (6h=") + String(rain6h,1) + "mm, "
              "Tmax=" + String(tMax,1) + "°C, Hmax=" + String(hMax) + "%)"
            );
          continue;
        } else {
          actualDuration = (actualDuration * wateringPercent) / 100;
          if (logs) logs->add(
            String("Automat: Strefa ") + String(P.zone + 1)
            + ": bazowo " + String(baseDuration) + "min, "
            + "współczynnik " + String(wateringPercent) + "% → "
            + String(actualDuration) + "min "
            + "(6h=" + String(rain6h, 1) + "mm, "
            + "Tmax=" + String(tMax, 1) + "°C, "
            + "Hmax=" + String(hMax) + "%)"
          );
          if (pushover && config && config->getEnablePushover()) {
            pushover->send(
              String("Strefa ") + String(P.zone + 1) + ": "
              + String(baseDuration) + "min → "
              + String(actualDuration) + "min (" + String(wateringPercent) + "%). "
              + "6h=" + String(rain6h,1) + "mm, Tmax=" + String(tMax,1) + "°C, Hmax=" + String(hMax) + "%."
            );
          }
        }

        zones->startZone(P.zone, actualDuration * 60);
        progs[i].lastRun = now;
        saveToFS();

        if (logs) logs->add("Automat: Start strefy " + String(P.zone + 1) + " na " + String(actualDuration) + "min");
        if (pushover && config && config->getEnablePushover()) {
          pushover->send("Start strefy " + String(P.zone + 1) + " na " + String(actualDuration) + "min");
        }
      }
    }
  }
};
