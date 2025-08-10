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

// --- Pomocnicza konwersja offsetu "+HH:MM"/"-HH:MM" na POSIX TZ ("GMT-2", "GMT+3:30", ...)
// Uwaga: w POSIX znak jest odwrócony względem UTC (UTC+2 => "GMT-2")
static String posixFromOffset(const String& tzOff) {
  if (tzOff.length() < 2) return String(); // nie parsuj

  char s = tzOff.charAt(0);
  if (s != '+' && s != '-') return String();

  int colon = tzOff.indexOf(':');
  int h = 0, m = 0;

  if (colon > 0) {
    h = tzOff.substring(1, colon).toInt();
    m = tzOff.substring(colon + 1).toInt();
  } else {
    // format np. "+02" lub "-3"
    h = tzOff.substring(1).toInt();
    m = 0;
  }

  // POSIX: znak odwrotny
  char posixSign = (s == '+') ? '-' : '+';

  String out = "GMT";
  out += posixSign;
  out += String(h);
  if (m > 0) {
    out += ":";
    if (m < 10) out += "0";
    out += String(m);
  }
  return out;
}

// --- Ustalanie poprawnego ciągu TZ na podstawie ustawienia z configu
static String resolvePosixTZ(const String& tz) {
  // Popularne nazwy IANA z regułami DST:
  if (tz == "" || tz == "Europe/Warsaw") {
    // CET-1, CEST z przełączeniami (ostatnia niedziela marca/października)
    return "CET-1CEST,M3.5.0,M10.5.0/3";
  }
  if (tz == "America/New_York") {
    return "EST+5EDT,M3.2.0/2,M11.1.0/2";
  }
  if (tz == "UTC" || tz == "Etc/UTC") {
    return "UTC0";
  }
  if (tz == "Europe/London") {
    return "GMT0BST,M3.5.0/1,M10.5.0";
  }
  if (tz == "Asia/Tokyo") {
    return "JST-9";
  }

  // Offsety typu +HH, +HH:MM, -HH, -HH:MM
  if (tz.startsWith("+") || tz.startsWith("-")) {
    String posix = posixFromOffset(tz);
    if (posix.length() > 0) return posix;
  }

  // Jeśli użytkownik podał już gotowy POSIX TZ – użyj bez zmian
  // (np. "CET-1CEST,M3.5.0,M10.5.0/3" albo "GMT-2")
  return tz;
}

// --- Funkcja do ustawiania strefy czasowej ---
void setTimezone() {
  String tz = config.getTimezone();
  Serial.print("Strefa czasowa ustawiana na: ");
  Serial.println(tz);

  String posixTZ = resolvePosixTZ(tz);
  if (posixTZ == "") {
    // awaryjnie UTC0
    posixTZ = "UTC0";
  }

  setenv("TZ", posixTZ.c_str(), 1);
  tzset();

  Serial.print("Aktualny TZ z getenv: ");
  Serial.println(getenv("TZ"));

  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char buf[64];
  snprintf(buf, sizeof(buf), "Czas lokalny po zmianie strefy: %04d-%02d-%02d %02d:%02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  Serial.println(buf);
}

// --- Funkcja synchronizująca czas NTP (tylko na starcie) ---
void syncNtp() {
  Serial.println("Synchronizacja czasu NTP...");
  // NTP zawsze w UTC, offset/dst robi TZ
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  unsigned long t0 = millis();
  while (now < 8 * 3600 * 2 && millis() - t0 < 20000) { // czekaj max 20 s na NTP
    delay(500);
    now = time(nullptr);
    Serial.print("[NTP] Synchronizacja czasu... ");
    Serial.println(now);
  }
  struct tm t;
  localtime_r(&now, &t);
  char buf[64];
  snprintf(buf, sizeof(buf), "Czas lokalny po synchronizacji: %04d-%02d-%02d %02d:%02d:%02d",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  Serial.println(buf);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  LittleFS.begin();

  // --- Wypisz listę plików w LittleFS ---
  Serial.println("Pliki w LittleFS:");
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.print("  ");
    Serial.print(file.name());
    Serial.print(" (");
    Serial.print(file.size());
    Serial.println(" bajtów)");
    file = root.openNextFile();
  }

  config.load();
  config.initWiFi(&pushover);

  // Poczekaj chwilę, aż WiFi się zestawi w pełni, zanim rozpoczniesz NTP
  delay(2000);

  zones.begin();
  weather.begin(config.getOwmApiKey(), config.getOwmLocation());
  pushover.begin();
  programs.begin(&zones, &weather, &logs, &pushover);

  // Najpierw NTP (UTC), potem TZ (lokalizacja)
  syncNtp();      // NTP tylko raz na starcie!
  setTimezone();  // Ustaw strefę

  WebServerUI::begin(
    &config,
    nullptr,
    &zones,
    &weather,
    &pushover,
    &programs,
    &logs
  );

  mqtt.begin(
    &zones,
    &programs,
    &weather,
    &logs,
    &config
  );

  Serial.println("[MAIN] System uruchomiony.");
}

// Funkcja udostępniana do WebServerUI.h (dla POST /api/settings)
extern "C" void setTimezoneFromWeb() { setTimezone(); }

void loop() {
  config.wifiLoop();
  zones.loop();
  programs.loop();
  weather.loop();
  mqtt.loop();
}
