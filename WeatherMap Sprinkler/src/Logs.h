#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

class Logs {
  String logs[32];
  int count = 0;
public:
  void add(const String& txt) {
    if(count < 32) logs[count++] = txt;
    else {
      for(int i=1;i<32;i++) logs[i-1] = logs[i];
      logs[31] = txt;
    }
  }
  void clear() { count = 0; }
  void toJson(JsonDocument& doc) {
    JsonArray arr = doc["logs"].to<JsonArray>();  // NOWA WERSJA
    for(int i=0;i<count;i++) arr.add(logs[i]);
  }
};
