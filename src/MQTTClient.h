#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "Zones.h"
#include "Programs.h"
#include "Weather.h"
#include "Logs.h"
#include "Config.h"

// Tematy MQTT (base = np. "sprinkler/esp32-001"):
//  - <base>/global/status           (retained JSON)
//  - <base>/weather                 (retained JSON)
//  - <base>/zones                   (retained JSON array: [{id,active,remaining,name}, ...])
//  - <base>/programs                (retained JSON array)
//  - <base>/logs                    (retained JSON object {"logs":[...]})
//  - <base>/settings/public         (retained JSON object – bez haseł itp.)
//  - <base>/rain-history            (retained JSON array/object – zależnie od Twojej impl.)
//  - <base>/watering-percent        (retained JSON object)
// Kompatybilnie per-strefa:
//  - <base>/zones/<id>/status       (retained "0"/"1")
//  - <base>/zones/<id>/remaining    (retained sekundy)
//
// Komendy z backendu (subskrybowane):
//  - <base>/global/refresh          (pusty payload) → publikacja wszystkich snapshotów
//  - <base>/cmd/zones/<id>/toggle   ("1"|"" )       → start/stop/toggle
//  - <base>/cmd/zones/<id>/start    (liczba sekund) → start na czas
//  - <base>/cmd/zones/<id>/stop     (pusty)         → stop
//  - <base>/cmd/zones-names/set     (JSON array)    → zmiana nazw stref
//  - <base>/cmd/programs/import     (JSON)          → import programów
//  - <base>/cmd/programs/edit/<id>  (JSON)          → edycja programu
//  - <base>/cmd/programs/delete/<id>(pusty)         → kasowanie programu
//  - <base>/cmd/logs/clear          (pusty)         → czyszczenie logów
//  - <base>/cmd/settings/set        (JSON)          → zapis ustawień PUBLICZNYCH

class MQTTClient {
public:
  MQTTClient() : mqttClient(espClientTLS) {}

  void begin(Zones* z, Programs* p, Weather* w, Logs* l, Config* c) {
    zones = z; programs = p; weather = w; logs = l; config = c;
    loadConfig();
  }

  // Używane przez WebServerUI.h → przeładuj konfigurację z Config i przełącz połączenie
  void updateConfig() {
    if (!config) return;
    // zapamiętaj czy byłeś połączony
    const bool wasConnected = mqttClient.connected();
    // przeładuj ustawienia z Config
    loadConfig();

    // jeśli byłeś połączony – rozłącz, by wymusić nową sesję z nowymi parametrami
    if (wasConnected) {
      mqttClient.disconnect();
    }
    // ustaw znacznik, by pętla po 100 ms spróbowała połączyć się ponownie
    lastReconnectAttempt = millis() - 1000;
  }

  void loadConfig() {
    if (!config) return;
    enabled      = config->getEnableMqtt();
    mqttServer   = config->getMqttServer();    // np. b74e....s1.eu.hivemq.cloud
    mqttPort     = config->getMqttPort();      // 8883
    mqttUser     = config->getMqttUser();      // np. sprinkler-app
    mqttPass     = config->getMqttPass();
    mqttClientId = config->getMqttClientId();  // np. sprinkler-esp32-001
    baseTopic    = config->getMqttTopicBase(); // np. sprinkler/esp32-001

    // TLS – bez weryfikacji CA (na start; docelowo możesz dodać CA brokera)
    espClientTLS.setInsecure();

    mqttClient.setServer(mqttServer.c_str(), mqttPort);
    mqttClient.setBufferSize(2048); // większe pakiety JSON
    mqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
      this->onMessage(topic, payload, length);
    });
  }

  void loop() {
    if (!enabled || mqttServer.length() == 0) {
      if (mqttClient.connected()) mqttClient.disconnect();
      return;
    }
    if (!mqttClient.connected()) {
      const unsigned long now = millis();
      if (now - lastReconnectAttempt > 500) {
        lastReconnectAttempt = now;
        (void)reconnect(); // próba; wynik i tak logujemy/obsługujemy dalej
      }
    } else {
      mqttClient.loop();
      publishGlobalStatus();      // co ~10s
      publishAllSnapshots();      // co ~15s
    }
  }

  void updateAfterZonesChange()        { publishZonesSnapshot(); publishGlobalStatus(true); }
  void updateAfterProgramsChange()     { publishProgramsSnapshot(); }
  void updateAfterLogsChange()         { publishLogsSnapshot(); }
  void updateAfterSettingsChange()     { publishSettingsPublicSnapshot(); publishGlobalStatus(true); }
  void updateAfterWeatherChange()      { publishWeatherSnapshot(); publishWateringPercentSnapshot(); }
  void updateAfterRainHistoryChange()  { publishRainHistorySnapshot(); }

private:
  WiFiClientSecure espClientTLS;
  PubSubClient mqttClient;

  Zones*    zones    = nullptr;
  Programs* programs = nullptr;
  Weather*  weather  = nullptr;
  Logs*     logs     = nullptr;
  Config*   config   = nullptr;

  String mqttServer, mqttUser, mqttPass, mqttClientId, baseTopic;
  int    mqttPort  = 8883;
  bool   enabled   = true;

  unsigned long lastReconnectAttempt = 0;
  unsigned long lastStatusUpdate     = 0;
  unsigned long lastSnapshotUpdate   = 0;

  // ---- Utils ----
  String topic(const String& leaf) const {
    if (baseTopic.length() == 0) return leaf;
    if (baseTopic.endsWith("/")) return baseTopic + leaf;
    return baseTopic + "/" + leaf;
  }

  static bool parseIntSafe(const String& s, int& out) {
    if (s.length() == 0) return false;
    bool neg = false; long v = 0; int i = 0;
    if (s[0] == '-') { neg = true; i = 1; }
    for (; i < (int)s.length(); ++i) {
      char c = s[i]; if (c < '0' || c > '9') return false;
      v = v * 10 + (c - '0'); if (v > 10000000) return false;
    }
    out = (int)(neg ? -v : v);
    return true;
  }

  // ---- Połączenie i subskrypcje ----
  bool reconnect() {
    bool ok = false;
    if (mqttUser.length() > 0) ok = mqttClient.connect(mqttClientId.c_str(), mqttUser.c_str(), mqttPass.c_str());
    else                       ok = mqttClient.connect(mqttClientId.c_str());
    if (ok) {
      subscribeTopics();
      publishGlobalStatus(true);
      publishAllSnapshots(true);
      if (logs) logs->add("MQTT: połączono z brokerem");
    } else {
      if (logs) logs->add("MQTT: błąd połączenia");
    }
    return ok;
  }

  void subscribeTopics() {
    // Komendy
    mqttClient.subscribe(topic("global/refresh").c_str());
    mqttClient.subscribe(topic("cmd/zones/+/toggle").c_str());
    mqttClient.subscribe(topic("cmd/zones/+/start").c_str());
    mqttClient.subscribe(topic("cmd/zones/+/stop").c_str());
    mqttClient.subscribe(topic("cmd/zones-names/set").c_str());
    mqttClient.subscribe(topic("cmd/programs/import").c_str());
    mqttClient.subscribe(topic("cmd/programs/edit/+").c_str());
    mqttClient.subscribe(topic("cmd/programs/delete/+").c_str());
    mqttClient.subscribe(topic("cmd/logs/clear").c_str());
    mqttClient.subscribe(topic("cmd/settings/set").c_str());
  }

  // ---- Publikacje (retained) ----
  void publishJsonRetained(const String& t, const JsonDocument& doc) {
    String s; serializeJson(doc, s);
    mqttClient.publish(t.c_str(), s.c_str(), true);
  }
  void publishStringRetained(const String& t, const String& s) {
    mqttClient.publish(t.c_str(), s.c_str(), true);
  }

  void publishGlobalStatus(bool force=false) {
    const unsigned long now = millis();
    if (!force && now - lastStatusUpdate < 10000) return; // co 10s
    lastStatusUpdate = now;

    JsonDocument doc;
    doc["wifi"]   = (WiFi.status() == WL_CONNECTED) ? "Połączono" : "Brak połączenia";
    doc["ip"]     = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "-";

    time_t tnow = time(nullptr);
    struct tm t; localtime_r(&tnow, &t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min);
    doc["time"]   = buf;
    doc["online"] = true;

    publishJsonRetained(topic("global/status"), doc);
  }

  void publishZonesSnapshot() {
    if (!zones) return;

    // 1) Pobierz pełny JSON stref z istniejącej implementacji:
    JsonDocument doc;
    zones->toJson(doc); // oczekujemy tablicy [{id,active,remaining,name}, ...]

    // 2) Opublikuj całą tablicę:
    publishJsonRetained(topic("zones"), doc);

    // 3) Dodatkowo opublikuj per-strefa na podstawie TEGO SAMEGO JSON-a
    if (doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      int idx = 0;
      for (JsonVariant v : arr) {
        int id = v.containsKey("id") ? (int)v["id"].as<int>() : idx;
        bool active = v.containsKey("active") ? v["active"].as<bool>() : false;
        int remaining = v.containsKey("remaining") ? v["remaining"].as<int>() : 0;

        publishStringRetained(topic("zones/" + String(id) + "/status"), active ? "1" : "0");
        publishStringRetained(topic("zones/" + String(id) + "/remaining"), String(remaining));
        ++idx;
      }
    }
  }

  void publishProgramsSnapshot() {
    if (!programs) return;
    JsonDocument doc;
    programs->toJson(doc); // tablica/obiekt – zależnie od Twojej implementacji
    publishJsonRetained(topic("programs"), doc);
  }

  void publishLogsSnapshot() {
    if (!logs) return;
    JsonDocument doc;
    logs->toJson(doc); // {"logs":[...]}
    publishJsonRetained(topic("logs"), doc);
  }

  void publishSettingsPublicSnapshot() {
    if (!config) return;
    JsonDocument doc;
    config->toJson(doc);
    // Usuń wrażliwe pola:
    doc["pass"]          = "";
    doc["mqttPass"]      = "";
    doc["owmApiKey"]     = "";
    doc["pushoverUser"]  = "";
    doc["pushoverToken"] = "";
    publishJsonRetained(topic("settings/public"), doc);
  }

  void publishWeatherSnapshot() {
    if (!weather) return;
    JsonDocument doc;
    weather->toJson(doc);
    publishJsonRetained(topic("weather"), doc);
  }

  void publishRainHistorySnapshot() {
    if (!weather) return;
    JsonDocument doc;
    weather->rainHistoryToJson(doc);
    publishJsonRetained(topic("rain-history"), doc);
  }

  void publishWateringPercentSnapshot() {
    if (!weather) return;
    JsonDocument doc;
    doc["percent"] = weather->getWateringPercent();
    doc["rain_6h"] = weather->getLast6hRain();
    doc["daily_max_temp"] = weather->getDailyMaxTemp();
    doc["daily_humidity_forecast"] = weather->getDailyHumidityForecast();
    publishJsonRetained(topic("watering-percent"), doc);
  }

  void publishAllSnapshots(bool force=false) {
    const unsigned long now = millis();
    if (!force && now - lastSnapshotUpdate < 15000) return; // co 15s
    lastSnapshotUpdate = now;

    publishGlobalStatus(true);
    publishZonesSnapshot();
    publishProgramsSnapshot();
    publishLogsSnapshot();
    publishSettingsPublicSnapshot();
    publishWeatherSnapshot();
    publishRainHistorySnapshot();
    publishWateringPercentSnapshot();
  }

  // ---- Obsługa komend ----
  void onMessage(char* topicC, byte* payload, unsigned int length) {
    const String top = String(topicC);
    String msg; msg.reserve(length);
    for (unsigned int i=0; i<length; ++i) msg += (char)payload[i];

    if (top == topic("global/refresh")) {
      if (logs) logs->add("MQTT CMD: global/refresh");
      publishAllSnapshots(true);
      return;
    }

    if (top == topic("cmd/zones-names/set")) {
      if (zones) {
        JsonDocument doc;
        if (deserializeJson(doc, msg) == DeserializationError::Ok && doc.is<JsonArray>()) {
          zones->setAllZoneNames(doc.as<JsonArray>());
          if (logs) logs->add("MQTT CMD: zmieniono nazwy stref");
          publishZonesSnapshot();
        }
      }
      return;
    }

    // cmd/zones/<id>/...
    const String p = topic("cmd/zones/");
    if (top.startsWith(p)) {
      const int idx1 = p.length();
      const int idx2 = top.indexOf('/', idx1);
      if (idx2 > idx1) {
        const int id = top.substring(idx1, idx2).toInt();
        const String action = top.substring(idx2 + 1);

        if (zones) {
          if (action == "toggle") {
            if (msg == "1" || msg == "ON" || msg == "on" || msg == "true") {
              zones->startZone(id, 600); // domyślnie 600 s
            } else if (msg.length() == 0) {
              zones->toggleZone(id);
            } else {
              zones->stopZone(id);
            }
            if (logs) logs->add("MQTT CMD: toggle strefa " + String(id+1));
          } else if (action == "start") {
            int secs = 0;
            if (parseIntSafe(msg, secs) && secs > 0) {
              zones->startZone(id, secs);
              if (logs) logs->add("MQTT CMD: start strefa " + String(id+1) + " na " + String(secs) + "s");
            }
          } else if (action == "stop") {
            zones->stopZone(id);
            if (logs) logs->add("MQTT CMD: stop strefa " + String(id+1));
          }
          publishZonesSnapshot();
        }
      }
      return;
    }

    if (top == topic("cmd/programs/import")) {
      if (programs) {
        JsonDocument doc;
        if (deserializeJson(doc, msg) == DeserializationError::Ok) {
          programs->importFromJson(doc);
          if (logs) logs->add("MQTT CMD: import programów");
          publishProgramsSnapshot();
        }
      }
      return;
    }

    const String pe = topic("cmd/programs/edit/");
    if (top.startsWith(pe)) {
      if (programs) {
        const int id = top.substring(pe.length()).toInt();
        JsonDocument doc;
        if (deserializeJson(doc, msg) == DeserializationError::Ok) {
          programs->edit(id, doc, true, true);
          if (logs) logs->add("MQTT CMD: edytuj program " + String(id));
          publishProgramsSnapshot();
        }
      }
      return;
    }

    const String pd = topic("cmd/programs/delete/");
    if (top.startsWith(pd)) {
      if (programs) {
        const int id = top.substring(pd.length()).toInt();
        programs->remove(id, true);
        if (logs) logs->add("MQTT CMD: usuń program " + String(id));
        publishProgramsSnapshot();
      }
      return;
    }

    if (top == topic("cmd/logs/clear")) {
      if (logs) logs->clear();
      if (logs) logs->add("MQTT CMD: wyczyszczono logi");
      publishLogsSnapshot();
      return;
    }

    if (top == topic("cmd/settings/set")) {
      if (config && weather) {
        JsonDocument doc;
        if (deserializeJson(doc, msg) == DeserializationError::Ok) {
          config->saveFromJson(doc);
          weather->applySettings(
            config->getOwmApiKey(),
            config->getOwmLocation(),
            config->getEnableWeatherApi(),
            config->getWeatherUpdateIntervalMin()
          );
          if (logs) logs->add("MQTT CMD: zapisano ustawienia (publiczne)");
          publishSettingsPublicSnapshot();
          publishGlobalStatus(true);
        }
      }
      return;
    }
  }
};
