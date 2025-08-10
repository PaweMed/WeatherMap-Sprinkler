#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

class Settings {
  Preferences prefs;
  String ssid, pass, owmApiKey, owmLocation, pushoverUser, pushoverToken;
  // MQTT
  String mqttServer, mqttUser, mqttPass, mqttClientId;
  int mqttPort = 1883;

  // Strefa czasowa
  String timezone = "Europe/Warsaw";

  // Tryb automatyczny (pogoda wpływa na podlewanie)
  bool autoMode = true;

public:  // <-- PUBLIC!
  String getSSID() { return ssid; }
  String getPass() { return pass; }

  void load() {
    prefs.begin("ews", true);
    ssid = prefs.getString("ssid", "");
    pass = prefs.getString("pass", "");
    owmApiKey = prefs.getString("owmApiKey", "");
    owmLocation = prefs.getString("owmLocation", "Szczecin,PL");
    pushoverUser = prefs.getString("pushoverUser", "");
    pushoverToken = prefs.getString("pushoverToken", "");
    // MQTT
    mqttServer = prefs.getString("mqttServer", "");
    mqttUser = prefs.getString("mqttUser", "");
    mqttPass = prefs.getString("mqttPass", "");
    mqttClientId = prefs.getString("mqttClientId", "");
    mqttPort = prefs.getInt("mqttPort", 1883);
    // Strefa czasowa
    timezone = prefs.getString("timezone", "Europe/Warsaw");
    // Auto mode
    autoMode = prefs.getBool("autoMode", true);
    prefs.end();
  }

  void saveFromJson(JsonDocument& doc) {
    prefs.begin("ews", false);
    ssid = doc["ssid"].as<const char*>(); prefs.putString("ssid", ssid);
    pass = doc["pass"].as<const char*>(); prefs.putString("pass", pass);
    owmApiKey = doc["owmApiKey"].as<const char*>(); prefs.putString("owmApiKey", owmApiKey);
    owmLocation = doc["owmLocation"].as<const char*>(); prefs.putString("owmLocation", owmLocation);
    pushoverUser = doc["pushoverUser"].as<const char*>(); prefs.putString("pushoverUser", pushoverUser);
    pushoverToken = doc["pushoverToken"].as<const char*>(); prefs.putString("pushoverToken", pushoverToken);
    // MQTT
    mqttServer = doc["mqttServer"] | mqttServer; prefs.putString("mqttServer", mqttServer);
    mqttUser = doc["mqttUser"] | mqttUser; prefs.putString("mqttUser", mqttUser);
    mqttPass = doc["mqttPass"] | mqttPass; prefs.putString("mqttPass", mqttPass);
    mqttClientId = doc["mqttClientId"] | mqttClientId; prefs.putString("mqttClientId", mqttClientId);
    mqttPort = doc["mqttPort"] | mqttPort; prefs.putInt("mqttPort", mqttPort);
    // Strefa czasowa
    timezone = doc["timezone"] | timezone; prefs.putString("timezone", timezone);
    // Auto mode
    autoMode = doc["autoMode"] | autoMode; prefs.putBool("autoMode", autoMode);
    prefs.end();
  }

  void toJson(JsonDocument& doc) {
    doc["ssid"] = ssid; doc["pass"] = pass;
    doc["owmApiKey"] = owmApiKey; doc["owmLocation"] = owmLocation;
    doc["pushoverUser"] = pushoverUser; doc["pushoverToken"] = pushoverToken;
    // MQTT
    doc["mqttServer"] = mqttServer;
    doc["mqttUser"] = mqttUser;
    doc["mqttPass"] = mqttPass;
    doc["mqttClientId"] = mqttClientId;
    doc["mqttPort"] = mqttPort;
    // Strefa czasowa
    doc["timezone"] = timezone;
    // Auto mode
    doc["autoMode"] = autoMode;
  }

  String getOwmApiKey() { return owmApiKey; }
  String getOwmLocation() { return owmLocation; }
  String getPushoverUser() { return pushoverUser; }
  String getPushoverToken() { return pushoverToken; }

  // GETTERY MQTT
  String getMqttServer() { return mqttServer; }
  int getMqttPort() { return mqttPort; }
  String getMqttUser() { return mqttUser; }
  String getMqttPass() { return mqttPass; }
  String getMqttClientId() { return mqttClientId; }

  // --- Obsługa strefy czasowej ---
  String getTimezone() { return timezone; }
  void setTimezone(const String& tz) { timezone = tz; }

  // --- Auto mode ---
  bool getAutoMode() const { return autoMode; }
  void setAutoMode(bool v) { autoMode = v; }
};
