#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

class Zones {
  int numZones;
  bool states[8];
  int pins[8] = {13,12,14,27,26,25,33,32};
  unsigned long endTime[8] = {0}; // kiedy wyłączyć

  // Nazwy stref
  String zoneNames[8];

  void loadZoneNames() {
    if (!LittleFS.exists("/zones-names.json")) {
      // Ustaw domyślne
      for (int i = 0; i < 8; ++i) zoneNames[i] = "Strefa " + String(i + 1);
      saveZoneNames(); // od razu zapisz domyślne
      return;
    }
    File f = LittleFS.open("/zones-names.json", "r");
    if (!f) {
      for (int i = 0; i < 8; ++i) zoneNames[i] = "Strefa " + String(i + 1);
      return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      for (int i = 0; i < 8; ++i) zoneNames[i] = "Strefa " + String(i + 1);
      return;
    }
    JsonArray arr = doc.as<JsonArray>();
    for (int i = 0; i < 8; ++i) {
      if (i < arr.size() && arr[i].is<const char*>()) {
        zoneNames[i] = arr[i].as<const char*>();
      } else {
        zoneNames[i] = "Strefa " + String(i + 1);
      }
    }
  }

public:
  Zones(int n) : numZones(n) {
    // Domyślne
    for (int i = 0; i < 8; ++i) zoneNames[i] = "Strefa " + String(i + 1);
  }

  void begin() {
    for(int i=0; i<numZones; i++) {
      pinMode(pins[i], OUTPUT);
      digitalWrite(pins[i], LOW);
      states[i] = false;
      endTime[i] = 0;
    }
    loadZoneNames();
  }

  void toJson(JsonDocument& doc) {
    for(int i=0;i<numZones;i++) {
      JsonObject z = doc.add<JsonObject>();
      z["id"] = i;
      z["active"] = states[i];
      z["time_left"] = states[i] ? max(0, (int)((endTime[i] - millis())/1000)) : 0; // <--- POPRAWKA TU
      z["name"] = zoneNames[i];
    }
  }

  void startZone(int idx, int durationSec) {
    if(idx<0||idx>=numZones) return;
    Serial.printf("startZone(%d, %d)\n", idx, durationSec);
    states[idx] = true;
    digitalWrite(pins[idx], HIGH);
    endTime[idx] = millis() + durationSec*1000UL;
  }

  void stopZone(int idx) {
    if(idx<0||idx>=numZones) return;
    Serial.printf("stopZone(%d)\n", idx);
    states[idx] = false;
    digitalWrite(pins[idx], LOW);
    endTime[idx] = 0;
  }

  void toggleZone(int idx) {
    if(idx<0||idx>=numZones) return;
    Serial.printf("toggleZone(%d) - before: %d\n", idx, states[idx]);
    if (states[idx]) stopZone(idx);
    else startZone(idx, 600); // domyślnie 10 min w manualu
    Serial.printf("toggleZone(%d) - after: %d\n", idx, states[idx]);
  }

  void loop() {
    unsigned long now = millis();
    for(int i=0; i<numZones; i++) {
      if(states[i] && now > endTime[i]) stopZone(i);
    }
  }

  bool getZoneState(int idx) { 
    if(idx<0||idx>=numZones) return false; 
    return states[idx]; 
  }

  // --- Nazwy stref ---

  // Zwraca nazwę strefy o podanym indeksie
  String getZoneName(int idx) {
    if(idx<0||idx>=numZones) return "";
    return zoneNames[idx];
  }

  // Zmienia nazwę strefy (nie zapisuje automatycznie!)
  void setZoneName(int idx, const String& name) {
    if(idx<0||idx>=numZones) return;
    zoneNames[idx] = name;
  }

  // Zapisuje aktualne nazwy do pliku
  void saveZoneNames() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < 8; ++i) arr.add(zoneNames[i]);
    File f = LittleFS.open("/zones-names.json", "w");
    if (f) { serializeJson(doc, f); f.close(); }
  }

  // Zwraca wszystkie nazwy jako tablicę JSON
  void toJsonNames(JsonArray& arr) {
    for (int i = 0; i < 8; ++i) arr.add(zoneNames[i]);
  }

  // Ustawia wszystkie nazwy na raz (i od razu zapisuje)
  void setAllZoneNames(const JsonArray& arr) {
    for (int i = 0; i < 8; ++i) {
      if (i < arr.size() && arr[i].is<const char*>()) {
        zoneNames[i] = arr[i].as<const char*>();
      } else {
        zoneNames[i] = "Strefa " + String(i + 1);
      }
    }
    saveZoneNames();
  }
};
