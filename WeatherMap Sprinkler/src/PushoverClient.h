#pragma once
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "Settings.h"

class PushoverClient {
  Settings* settings;
public:
  PushoverClient(Settings* s) : settings(s) {}
  void begin() {}
  void send(const String& msg) {
    if (settings->getPushoverUser() == "" || settings->getPushoverToken() == "") return;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.pushover.net/1/messages.json");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String body = "token=" + settings->getPushoverToken() + "&user=" + settings->getPushoverUser() + "&message=" + msg;
    http.POST(body);
    http.end();
  }
};
