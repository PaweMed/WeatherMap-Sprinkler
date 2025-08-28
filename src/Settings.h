#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

class Settings {
  Preferences prefs;

  // WiFi
  String ssid, pass;

  // OpenWeatherMap
  String owmApiKey, owmLocation;

  // Pushover
  String pushoverUser, pushoverToken;
  bool   enablePushover = true;

  // MQTT
  String mqttServer, mqttUser, mqttPass, mqttClientId;
  int    mqttPort = 1883;
  bool   enableMqtt = true;
  String mqttTopicBase = "sprinkler"; // baza topiców

  // Automatyka
  bool   autoMode = true;

  // Strefa czasowa
  String timezone = "Europe/Warsaw";

  // Pogoda – sterowanie
  bool   enableWeatherApi = true;
  int    weatherUpdateIntervalMin = 60; // minuty

public:  // GETTERY (używane przez Config/MQTT/Weather/itp.)
  String getSSID() { return ssid; }
  String getPass() { return pass; }

  String getOwmApiKey() { return owmApiKey; }
  String getOwmLocation() { return owmLocation; }

  String getPushoverUser() { return pushoverUser; }
  String getPushoverToken() { return pushoverToken; }
  bool   getEnablePushover() { return enablePushover; }

  String getMqttServer() { return mqttServer; }
  int    getMqttPort()   { return mqttPort; }
  String getMqttUser()   { return mqttUser; }
  String getMqttPass()   { return mqttPass; }
  String getMqttClientId() { return mqttClientId; }
  bool   getEnableMqtt() { return enableMqtt; }
  String getMqttTopicBase() { return mqttTopicBase; }

  bool   getAutoMode() { return autoMode; }

  String getTimezone() { return timezone; }
  void   setTimezone(const String& tz) { timezone = tz; }

  bool   getEnableWeatherApi() { return enableWeatherApi; }
  int    getWeatherUpdateIntervalMin() { return weatherUpdateIntervalMin; }

  // --- LOAD/SAVE ---
  void load() {
    prefs.begin("ews", true);
    ssid = prefs.getString("ssid", "");
    pass = prefs.getString("pass", "");

    owmApiKey   = prefs.getString("owmApiKey", "");
    owmLocation = prefs.getString("owmLocation", "Szczecin,PL");

    pushoverUser   = prefs.getString("pushoverUser", "");
    pushoverToken  = prefs.getString("pushoverToken", "");
    enablePushover = prefs.getBool("enablePushover", true);

    mqttServer    = prefs.getString("mqttServer", "");
    mqttUser      = prefs.getString("mqttUser", "");
    mqttPass      = prefs.getString("mqttPass", "");
    mqttClientId  = prefs.getString("mqttClientId", "");
    mqttPort      = prefs.getInt("mqttPort", 1883);
    enableMqtt    = prefs.getBool("enableMqtt", true);
    mqttTopicBase = prefs.getString("mqttTopicBase", "sprinkler");

    autoMode  = prefs.getBool("autoMode", true);

    timezone  = prefs.getString("timezone", "Europe/Warsaw");

    enableWeatherApi          = prefs.getBool("enableWeatherApi", true);
    weatherUpdateIntervalMin  = prefs.getInt("weatherUpdMin", 60);
    prefs.end();
  }

  void saveFromJson(JsonDocument& doc) {
    prefs.begin("ews", false);

    // WiFi
    if (doc["ssid"].is<const char*>()) { ssid = doc["ssid"].as<const char*>(); prefs.putString("ssid", ssid); }
    if (doc["pass"].is<const char*>()) { pass = doc["pass"].as<const char*>(); prefs.putString("pass", pass); }

    // OWM
    if (doc["owmApiKey"].is<const char*>())   { owmApiKey = doc["owmApiKey"].as<const char*>(); prefs.putString("owmApiKey", owmApiKey); }
    if (doc["owmLocation"].is<const char*>()) { owmLocation = doc["owmLocation"].as<const char*>(); prefs.putString("owmLocation", owmLocation); }

    // Pushover
    if (doc["pushoverUser"].is<const char*>())  { pushoverUser = doc["pushoverUser"].as<const char*>(); prefs.putString("pushoverUser", pushoverUser); }
    if (doc["pushoverToken"].is<const char*>()) { pushoverToken = doc["pushoverToken"].as<const char*>(); prefs.putString("pushoverToken", pushoverToken); }
    if (doc["enablePushover"].is<bool>())       { enablePushover = doc["enablePushover"].as<bool>(); prefs.putBool("enablePushover", enablePushover); }

    // MQTT
    if (doc["mqttServer"].is<const char*>())   { mqttServer = doc["mqttServer"].as<const char*>(); prefs.putString("mqttServer", mqttServer); }
    if (doc["mqttUser"].is<const char*>())     { mqttUser = doc["mqttUser"].as<const char*>(); prefs.putString("mqttUser", mqttUser); }
    if (doc["mqttPass"].is<const char*>())     { mqttPass = doc["mqttPass"].as<const char*>(); prefs.putString("mqttPass", mqttPass); }
    if (doc["mqttClientId"].is<const char*>()) { mqttClientId = doc["mqttClientId"].as<const char*>(); prefs.putString("mqttClientId", mqttClientId); }
    if (doc["mqttPort"].is<int>())             { mqttPort = doc["mqttPort"].as<int>(); prefs.putInt("mqttPort", mqttPort); }
    if (doc["enableMqtt"].is<bool>())          { enableMqtt = doc["enableMqtt"].as<bool>(); prefs.putBool("enableMqtt", enableMqtt); }
    if (doc["mqttTopic"].is<const char*>())    { mqttTopicBase = doc["mqttTopic"].as<const char*>(); prefs.putString("mqttTopicBase", mqttTopicBase); }

    // Automatyka
    if (doc["autoMode"].is<bool>()) { autoMode = doc["autoMode"].as<bool>(); prefs.putBool("autoMode", autoMode); }

    // Strefa czasowa
    if (doc["timezone"].is<const char*>()) { timezone = doc["timezone"].as<const char*>(); prefs.putString("timezone", timezone); }

    // Pogoda
    if (doc["enableWeatherApi"].is<bool>()) { enableWeatherApi = doc["enableWeatherApi"].as<bool>(); prefs.putBool("enableWeatherApi", enableWeatherApi); }
    if (doc["weatherUpdateInterval"].is<int>()) {
      weatherUpdateIntervalMin = doc["weatherUpdateInterval"].as<int>();
      if (weatherUpdateIntervalMin < 5) weatherUpdateIntervalMin = 5; // minimalne 5 min
      prefs.putInt("weatherUpdMin", weatherUpdateIntervalMin);
    }

    prefs.end();
  }

  void toJson(JsonDocument& doc) {
    // WiFi
    doc["ssid"] = ssid; doc["pass"] = pass;

    // OWM
    doc["owmApiKey"]   = owmApiKey;
    doc["owmLocation"] = owmLocation;

    // Pushover
    doc["pushoverUser"]   = pushoverUser;
    doc["pushoverToken"]  = pushoverToken;
    doc["enablePushover"] = enablePushover;

    // MQTT
    doc["mqttServer"]    = mqttServer;
    doc["mqttUser"]      = mqttUser;
    doc["mqttPass"]      = mqttPass;
    doc["mqttClientId"]  = mqttClientId;
    doc["mqttPort"]      = mqttPort;
    doc["enableMqtt"]    = enableMqtt;
    doc["mqttTopic"]     = mqttTopicBase;

    // Automatyka
    doc["autoMode"] = autoMode;

    // Strefa czasowa
    doc["timezone"] = timezone;

    // Pogoda
    doc["enableWeatherApi"]      = enableWeatherApi;
    doc["weatherUpdateInterval"] = weatherUpdateIntervalMin;
  }
};
