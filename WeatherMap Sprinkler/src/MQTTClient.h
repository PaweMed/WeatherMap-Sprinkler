#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include "Zones.h"
#include "Programs.h"
#include "Weather.h"
#include "Logs.h"
#include "Config.h"

class MQTTClient;
extern MQTTClient mqtt; // globalny obiekt zdefiniowany w main.cpp

class MQTTClient {
  WiFiClient espClient;
  PubSubClient mqttClient;
  Zones* zones = nullptr;
  Programs* programs = nullptr;
  Weather* weather = nullptr;
  Logs* logs = nullptr;
  Config* config = nullptr;

  // konfig
  String mqttServer, mqttUser, mqttPass, mqttClientId, baseTopic;
  int mqttPort = 1883;
  bool enabled = true;

  unsigned long lastReconnectAttempt = 0;
  unsigned long lastStatusUpdate = 0;

  String topic(const String& leaf) {
    String t = baseTopic;
    if (t.length() == 0) t = "sprinkler";
    if (t.endsWith("/")) return t + leaf;
    return t + "/" + leaf;
  }

public:
  MQTTClient(): mqttClient(espClient) {}

  void begin(Zones* z, Programs* p, Weather* w, Logs* l, Config* c) {
    zones = z; programs = p; weather = w; logs = l; config = c;
    updateConfig();
  }

  void updateConfig() {
    if (!config) return;
    enabled      = config->getEnableMqtt();
    mqttServer   = config->getMqttServer();
    mqttPort     = config->getMqttPort();
    mqttUser     = config->getMqttUser();
    mqttPass     = config->getMqttPass();
    mqttClientId = config->getMqttClientId();
    baseTopic    = config->getMqttTopicBase();
    if (mqttServer == "" || !enabled) {
      mqttClient.disconnect();
      return;
    }
    mqttClient.setServer(mqttServer.c_str(), mqttPort);
    mqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) { this->onMessage(topic, payload, length); });
    mqttClient.disconnect(); // przeładuj po zmianie serwera
  }

  void loop() {
    if (!enabled || mqttServer == "") {
      if (mqttClient.connected()) mqttClient.disconnect();
      return;
    }
    if (!mqttClient.connected()) {
      unsigned long now = millis();
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        if (reconnect()) lastReconnectAttempt = 0;
      }
    } else {
      mqttClient.loop();
      publishStatus();
    }
  }

  bool reconnect() {
    if (mqttClient.connect(mqttClientId.c_str(), mqttUser.c_str(), mqttPass.c_str())) {
      subscribeTopics();
      publishStatus(true);
      if (logs) logs->add("MQTT: Połączono z brokerem.");
      return true;
    } else {
      if (logs) logs->add("MQTT: Próba połączenia nieudana.");
      return false;
    }
  }

  void subscribeTopics() {
    mqttClient.subscribe((topic("zones/+/set")).c_str());
    mqttClient.subscribe((topic("global/refresh")).c_str());
    // kolejne tematy wg potrzeb
  }

  void onMessage(char* topicC, byte* payload, unsigned int length) {
    String top = String(topicC);
    String msg;
    msg.reserve(length);
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

    String prefix = topic("");
    // dopasowanie: <base>/zones/{id}/set
    String zpref = topic("zones/");
    if (top.startsWith(zpref)) {
      int idx1 = zpref.length();
      int idx2 = top.indexOf('/', idx1);
      int zoneId = top.substring(idx1, idx2).toInt();
      if (top.endsWith("/set")) {
        if (msg == "1" || msg == "ON" || msg == "on" || msg == "true") {
          if (zones) zones->startZone(zoneId, 600);
          if (logs)  logs->add("MQTT: Włączono strefę " + String(zoneId+1));
        } else if (msg == "0" || msg == "OFF" || msg == "off" || msg == "false") {
          if (zones) zones->stopZone(zoneId);
          if (logs)  logs->add("MQTT: Wyłączono strefę " + String(zoneId+1));
        }
      }
      return;
    }
    if (top == topic("global/refresh")) {
      publishStatus(true);
      return;
    }
  }

  void publishStatus(bool force = false) {
    unsigned long now = millis();
    if (!force && now - lastStatusUpdate < 10000) return; // co 10s
    lastStatusUpdate = now;

    // Status każdej strefy
    for (int i = 0; i < 8; i++) {
      bool active = zones ? zones->getZoneState(i) : false;
      mqttClient.publish((topic("zones/" + String(i) + "/status")).c_str(), active ? "1" : "0", true);
    }

    // Status globalny
    String json = "{";
    json += "\"wifi\":\"";
    json += (WiFi.status() == WL_CONNECTED) ? "Połączono" : "Brak połączenia";
    json += "\",";
    json += "\"ip\":\"";
    json += (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "-";
    json += "\",";
    json += "\"time\":\"";
    time_t nowt = time(nullptr);
    struct tm t;
    localtime_r(&nowt, &t);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min);
    json += buf;
    json += "\"}";
    mqttClient.publish((topic("global/status")).c_str(), json.c_str(), true);

    // Pogoda
    if (weather) {
      JsonDocument doc;
      weather->toJson(doc);
      String weatherJson;
      serializeJson(doc, weatherJson);
      mqttClient.publish((topic("weather")).c_str(), weatherJson.c_str(), true);
    }
  }
};
