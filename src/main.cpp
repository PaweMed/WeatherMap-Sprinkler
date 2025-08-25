#include <Arduino.h>
#include "FS.h"
#include "LittleFS.h"
#include <time.h>

#include "Config.h"
#include "Zones.h"
#include "Programs.h"
#include "Weather.h"
#include "Logs.h"
#include "PushoverClient.h"
#include "WebServerUI.h"
#include "MQTTClient.h"

// --- Obiekty globalne ---
Config config;
Zones zones(8);
Weather weather;
Logs logs;
PushoverClient pushover(config.getSettingsPtr());
Programs programs;
MQTTClient mqtt;  // JEDYNA definicja globalnego klienta MQTT

// Pomocnicza konwersja "+HH[:MM]" / "-HH[:MM]" -> POSIX "UTC-xx[:yy]"
static String offsetToPosixTZ(const String& tz)
{
  if (tz.length() < 2) return "";
  char s = tz.charAt(0);
  if (s != '+' && s != '-') return "";

  int hh = 0, mm = 0;
  int colon = tz.indexOf(':');
  bool ok = true;

  String hpart = "";
  String mpart = "";
  if (colon > 0) { hpart = tz.substring(1, colon); mpart = tz.substring(colon + 1); }
  else { hpart = tz.substring(1); }

  for (size_t i = 0; i < hpart.length(); i++) { if (!isDigit(hpart[i])) { ok = false; break; } }
  if (!ok || hpart.length() == 0) return "";
  hh = hpart.toInt(); if (hh < 0 || hh > 23) return "";

  if (mpart.length() > 0) {
    for (size_t i = 0; i < mpart.length(); i++) { if (!isDigit(mpart[i])) { ok = false; break; } }
    if (!ok) return "";
    mm = mpart.toInt(); if (mm < 0 || mm > 59) return "";
  }

  char buf[16];
  char outSign = (s == '+') ? '-' : '+';
  if (mm > 0) snprintf(buf, sizeof(buf), "UTC%c%02d:%02d", outSign, hh, mm);
  else        snprintf(buf, sizeof(buf), "UTC%c%d", outSign, hh);
  return String(buf);
}

void setTimezone() {
  String tz = config.getTimezone();
  Serial.print("Strefa czasowa ustawiana na: "); Serial.println(tz);

  if (tz == "" || tz == "Europe/Warsaw") {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  } else if (tz == "America/New_York") {
    setenv("TZ", "EST+5EDT,M3.2.0/2,M11.1.0/2", 1);
  } else if (tz == "UTC" || tz == "Etc/UTC") {
    setenv("TZ", "UTC0", 1);
  } else if (tz == "Europe/London") {
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
  } else if (tz == "Asia/Tokyo") {
    setenv("TZ", "JST-9", 1);
  } else {
    String posix = offsetToPosixTZ(tz);
    if (posix.length() > 0) setenv("TZ", posix.c_str(), 1);
    else setenv("TZ", tz.c_str(), 1);
  }

  tzset();
  Serial.print("Aktualny TZ z getenv: "); Serial.println(getenv("TZ"));

  time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  char buf[64];
  snprintf(buf, sizeof(buf), "Czas lokalny po zmianie strefy: %04d-%02d-%02d %02d:%02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  Serial.println(buf);
}

void syncNtp() {
  Serial.println("Synchronizacja czasu NTP...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  unsigned long t0 = millis();
  while (now < 8 * 3600 * 2 && millis() - t0 < 20000) {
    delay(500);
    now = time(nullptr);
    Serial.print("[NTP] Synchronizacja czasu... "); Serial.println(now);
  }
  struct tm t; localtime_r(&now, &t);
  char buf[64];
  snprintf(buf, sizeof(buf), "Czas lokalny po synchronizacji: %04d-%02d-%02d %02d:%02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  Serial.println(buf);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  LittleFS.begin();

  Serial.println("Pliki w LittleFS:");
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while(file){
      Serial.print("  "); Serial.print(file.name());
      Serial.print(" ("); Serial.print(file.size()); Serial.println(" bajtów)");
      file = root.openNextFile();
  }

  config.load();
  config.initWiFi(&pushover);

  delay(2000);

  zones.begin();

  // *** WAŻNE: wczytaj trwałe logi z /logs.json ***
  logs.begin();

  // Weather: natychmiastowa próba, retry po 60s, potem co X min wg ustawień
  weather.begin(
    config.getOwmApiKey(),
    config.getOwmLocation(),
    config.getEnableWeatherApi(),
    config.getWeatherUpdateIntervalMin()
  );

  pushover.begin();

  // Programs – teraz z dostępem do Config
  programs.begin(&zones, &weather, &logs, &pushover, &config);

  syncNtp();
  setTimezone();

  WebServerUI::begin(
    &config, nullptr, &zones, &weather, &pushover, &programs, &logs
  );

  mqtt.begin(&zones, &programs, &weather, &logs, &config);

  Serial.println("[MAIN] System uruchomiony.");
}

extern "C" void setTimezoneFromWeb() { setTimezone(); }

void loop() {
  config.wifiLoop();
  zones.loop();
  programs.loop();
  weather.loop();
  mqtt.loop();
}
