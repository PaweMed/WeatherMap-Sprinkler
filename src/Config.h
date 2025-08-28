#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include "Settings.h"
#include "PushoverClient.h"

class Config {
  Settings settings;
  bool wifiConfigured = false;
  bool inAPMode = false;
  unsigned long lastWiFiCheck = 0;
  unsigned int failedWiFiAttempts = 0;
  static const int maxWiFiAttempts = 10;
  String lastWiFiError = "";
  PushoverClient* pushover = nullptr;

public:
  void load() { settings.load(); }

  // WiFi
  bool isWiFiConfigured() { return settings.getSSID() != "" && settings.getPass() != ""; }
  String getSSID() { return settings.getSSID(); }
  String getPass() { return settings.getPass(); }

  // OWM
  String getOwmApiKey() { return settings.getOwmApiKey(); }
  String getOwmLocation() { return settings.getOwmLocation(); }

  // Pushover
  String getPushoverUser() { return settings.getPushoverUser(); }
  String getPushoverToken() { return settings.getPushoverToken(); }
  bool   getEnablePushover() { return settings.getEnablePushover(); }

  // MQTT
  String getMqttServer()    { return settings.getMqttServer(); }
  int    getMqttPort()      { return settings.getMqttPort(); }
  String getMqttUser()      { return settings.getMqttUser(); }
  String getMqttPass()      { return settings.getMqttPass(); }
  String getMqttClientId()  { return settings.getMqttClientId(); }
  bool   getEnableMqtt()    { return settings.getEnableMqtt(); }
  String getMqttTopicBase() { return settings.getMqttTopicBase(); }

  // Automatyka
  bool   getAutoMode() { return settings.getAutoMode(); }

  // TZ
  String getTimezone() { return settings.getTimezone(); }
  void   setTimezone(const String& tz) { settings.setTimezone(tz); }

  // Pogoda
  bool getEnableWeatherApi() { return settings.getEnableWeatherApi(); }
  int  getWeatherUpdateIntervalMin() { return settings.getWeatherUpdateIntervalMin(); }

  void saveFromJson(JsonDocument& doc) { settings.saveFromJson(doc); }
  void toJson(JsonDocument& doc) { settings.toJson(doc); }

  // WiFi init
  void initWiFi(PushoverClient* pClient = nullptr) {
    pushover = pClient;
    if (!isWiFiConfigured()) {
      setupWiFiAPMode();
    } else {
      connectWiFi();
    }
  }

  void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(getSSID().c_str(), getPass().c_str());
    Serial.print("[WiFi] Connecting to "); Serial.println(getSSID());
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(100); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      wifiConfigured = true; inAPMode = false; failedWiFiAttempts = 0;
      Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
      lastWiFiError = "";
      if (pushover && getEnablePushover()) pushover->send("OpenWeatherMap Sprinkler: " + WiFi.localIP().toString());
    } else {
      wifiConfigured = false;
      lastWiFiError = "Timeout connecting to WiFi";
      Serial.println("\n[WiFi] Connection failed!");
      failedWiFiAttempts++;
      if (failedWiFiAttempts >= maxWiFiAttempts) {
        Serial.println("[WiFi] Too many failures, switching to AP mode!");
        if (pushover && getEnablePushover()) pushover->send("ESP32: nieudane połączenie WiFi, przejście w tryb AP.");
        setupWiFiAPMode();
      }
    }
  }

  void setupWiFiAPMode() {
    inAPMode = true; wifiConfigured = false;
    WiFi.mode(WIFI_AP);
    String apName = "Sprinkler-Setup";
    WiFi.softAP(apName.c_str(), "12345678");
    Serial.println("[WiFi] AP Mode: " + apName + " IP: " + WiFi.softAPIP().toString());
    if (pushover && getEnablePushover()) pushover->send("ESP32 w trybie AP: " + WiFi.softAPIP().toString());
    lastWiFiError = "AP Mode enabled (no WiFi config)";
  }

  void wifiLoop() {
    if (!inAPMode && millis() - lastWiFiCheck > 10000) {
      lastWiFiCheck = millis();
      if (WiFi.status() != WL_CONNECTED) {
        failedWiFiAttempts++;
        Serial.print("[WiFi] Lost connection! Attempt: "); Serial.println(failedWiFiAttempts);
        connectWiFi();
      } else {
        failedWiFiAttempts = 0;
      }
    }
  }

  bool isInAPMode() const { return inAPMode; }
  String getWiFiStatus() const {
    if (inAPMode) return "Tryb AP";
    if (WiFi.status() == WL_CONNECTED) return "Połączono";
    return "Brak połączenia";
  }
  String getWiFiError() const { return lastWiFiError; }
  int getFailedAttempts() const { return failedWiFiAttempts; }

  // Dla PushoverClient (w main.cpp)
  Settings* getSettingsPtr() { return &settings; }
};
