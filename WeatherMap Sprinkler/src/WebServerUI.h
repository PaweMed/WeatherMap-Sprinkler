#pragma once
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "Zones.h"
#include "Weather.h"
#include "PushoverClient.h"
#include "Programs.h"
#include "Logs.h"
#include "MQTTClient.h"

// Deklaracja funkcji z main.cpp do dynamicznej zmiany strefy czasowej
extern "C" void setTimezoneFromWeb();

const char MAIN_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>WeatherMap Sprinkler – Panel awaryjny</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; background: #f3f3f3 !important; margin:0; }
    #header { background: #3685c9; color:#fff; padding:20px 0; text-align:center; box-shadow:0 2px 6px #bbb;}
    .card { max-width: 500px; margin: 40px auto; background: #fff; border-radius: 12px; box-shadow:0 2px 10px #bbb; padding: 30px 24px;}
    h2 { color: #3685c9;}
    .warn { color: #c93c36; font-weight: bold; }
    .info { color: #555; }
    .zone { margin: 12px 0; padding: 10px; border-radius: 6px; background: #f7fafc; display: flex; justify-content: space-between; align-items: center;}
    .zone.on { background: #d1edc1; }
    .zone.off { background: #fae3e3; }
    .zone button { min-width:90px; padding: 7px 0; border-radius: 6px; border: none; font-size:1em; cursor:pointer; }
    .zone.on button { background: #c93c36; color: #fff; }
    .zone.off button { background: #3685c9; color: #fff; }
    .weather-box { margin: 24px 0 12px 0; background: #e7f2fa; border-radius: 8px; padding: 14px;}
    .weather-row { margin: 5px 0;}
    .status-row { margin: 6px 0;}
    @media (max-width:550px) {
      .card { max-width: 98vw; padding: 10vw 2vw;}
    }
  </style>
</head>
<body>
  <div id="header">
    <h1>WeatherMap Sprinkler</h1>
  </div>
  <div class="card">
    <h2>Panel awaryjny</h2>
    <div class="warn" style="margin-bottom:12px;">Brak pliku <b>index.html</b> w pamięci LittleFS.<br>Wyświetlana jest wersja zapasowa panelu!</div>
    <div class="status-row"><b>Status WiFi:</b> <span id="wifi"></span></div>
    <div class="status-row"><b>Adres IP:</b> <span id="ip"></span></div>
    <div class="status-row"><b>Czas:</b> <span id="czas"></span></div>
    <hr>
    <div id="weather" class="weather-box">
      <b>Dane pogodowe:</b>
      <div class="weather-row">Temperatura: <span id="temp">?</span> &deg;C</div>
      <div class="weather-row">Wilgotność: <span id="humidity">?</span> %</div>
      <div class="weather-row">Deszcz (1h): <span id="rain">?</span> mm</div>
      <div class="weather-row">Wiatr: <span id="wind">?</span> m/s</div>
    </div>
    <hr>
    <b>Sterowanie strefami:</b>
    <div id="zones"></div>
    <hr>
    <div class="info">
      <a href="/wifi">Konfiguracja WiFi</a>
      <br><br>
      <button onclick="location.reload()">Odśwież</button>
    </div>
  </div>
  <script>
    function loadStatus() {
      fetch('/api/status').then(r=>r.json()).then(d=>{
        document.getElementById('wifi').textContent = d.wifi;
        document.getElementById('ip').textContent = d.ip;
        document.getElementById('czas').textContent = d.time;
      });
    }
    function loadWeather() {
      fetch('/api/weather').then(r=>r.json()).then(d=>{
        document.getElementById('temp').textContent = d.temp !== undefined ? d.temp : '?';
        document.getElementById('humidity').textContent = d.humidity !== undefined ? d.humidity : '?';
        document.getElementById('rain').textContent = d.rain !== undefined ? d.rain : '?';
        document.getElementById('wind').textContent = d.wind !== undefined ? d.wind : '?';
      });
    }
    function loadZones() {
      fetch('/api/zones').then(r=>r.json()).then(zs=>{
        let html = '';
        for (let z of zs) {
          html += `<div class="zone ${z.active?'on':'off'}">
            <span>Strefa #${z.id+1} ${z.active?'(WŁ)':'(WYŁ)'}</span>
            <button onclick="toggleZone(${z.id}, this)">${z.active?'Wyłącz':'Włącz'}</button>
          </div>`;
        }
        document.getElementById('zones').innerHTML = html;
      });
    }
    function toggleZone(id, btn) {
      btn.disabled = true;
      fetch('/api/zones', {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify({id: id, toggle: true})
      }).then(r=>r.json()).then(()=>{ setTimeout(loadZones, 400); btn.disabled = false; });
    }
    loadStatus();
    loadWeather();
    loadZones();
  </script>
</body>
</html>
)rawliteral";

const char WIFI_CONFIG_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Konfiguracja WiFi – WeatherMap Sprinkler</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body { height: 100%; margin: 0; padding: 0; font-family: Arial, sans-serif; background: #f3f3f3 !important; }
    body { min-height: 100vh; display: flex; flex-direction: column; background: #f3f3f3 !important; }
    #header { background: #3685c9; color: #fff; padding: 20px 0; text-align: center; box-shadow: 0 2px 6px #bbb; flex-shrink: 0; width: 100%; }
    .centerbox-outer { flex: 1 1 auto; display: flex; justify-content: center; align-items: center; min-height: 0; min-width: 0; }
    .card { width: 100%; max-width: 380px; background: #fff; border-radius: 12px; box-shadow: 0 2px 10px #bbb; padding: 32px 24px 24px 24px; margin: 0 auto; display: flex; flex-direction: column; justify-content: center; align-items: stretch; }
    label { font-size: 1.1em; display:block; margin-top: 1em; }
    input[type=text], input[type=password] { width: 100%; padding: 10px; margin-top:6px; margin-bottom:12px; border: 1px solid #bbb; border-radius: 6px; font-size: 1em; }
    button { width: 100%; background: #3685c9; color:#fff; font-size:1.1em; padding:10px 0; border:none; border-radius:6px; cursor:pointer; margin-top: 16px; }
    .msg { color: #3685c9; margin-bottom: 16px; min-height: 1.3em; }
  </style>
</head>
<body>
  <div id="header">
    <h1>Konfiguracja WiFi – WeatherMap Sprinkler</h1>
  </div>
  <div class="centerbox-outer">
    <div class="card">
      <form id="wifiForm" autocomplete="off">
        <div class="msg" id="msg"></div>
        <label for="ssid">Nazwa sieci WiFi (SSID):</label>
        <input type="text" id="ssid" name="ssid" required maxlength="32" autocomplete="off">
        <label for="pass">Hasło WiFi:</label>
        <input type="password" id="pass" name="pass" maxlength="64" autocomplete="off">
        <button type="submit">Zapisz i połącz</button>
      </form>
    </div>
  </div>
  <script>
    document.getElementById('wifiForm').addEventListener('submit', async function(e){
      e.preventDefault();
      const msg = document.getElementById('msg');
      msg.textContent = '';
      const ssid = document.getElementById('ssid').value.trim();
      const pass = document.getElementById('pass').value;
      if (!ssid) { msg.textContent = 'Wpisz nazwę sieci!'; return; }
      msg.textContent = 'Zapisywanie...';
      try {
        const resp = await fetch('/api/wifi', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ ssid: ssid, pass: pass })
        });
        const data = await resp.json();
        if (data.ok) {
          msg.textContent = 'Dane zapisane! Urządzenie połączy się z nową siecią WiFi i uruchomi ponownie za chwilę.';
          setTimeout(function(){ location.reload(); }, 4000);
        } else {
          msg.textContent = 'Błąd: ' + (data.error || 'nieznany');
        }
      } catch (e) {
        msg.textContent = 'Błąd połączenia: ' + e;
      }
    });
  </script>
</body>
</html>
)rawliteral";

const char OTA_PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Aktualizacja OTA – WeatherMap Sprinkler</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body { font-family: Arial, sans-serif; background: #f3f3f3; margin: 0; }
    #header { background: #3685c9; color: #fff; padding: 20px 0; text-align: center; box-shadow: 0 2px 10px #bbb; }
    .card { max-width: 400px; margin: 40px auto; background: #fff; border-radius: 12px; box-shadow: 0 2px 10px #bbb; padding: 32px 24px 24px 24px; }
    h2 { color: #3685c9; }
    .msg { color: #c93c36; margin-bottom: 16px; min-height: 1.3em; }
    input[type=file] { width: 100%; margin: 14px 0; }
    button { width: 100%; background: #3685c9; color:#fff; font-size:1.1em; padding:10px 0; border:none; border-radius:6px; cursor:pointer; margin-top: 16px; }
    .progress { width: 100%; background: #eee; height: 18px; border-radius: 8px; margin-top: 10px; }
    .bar { height: 18px; border-radius: 8px; width: 0%; background: #3685c9; }
  </style>
</head>
<body>
  <div id="header"><h1>Aktualizacja OTA</h1></div>
  <div class="card">
    <h2>Wgraj nowy firmware</h2>
    <div class="msg" id="msg"></div>
    <form id="otaForm">
      <input type="file" id="firmware" name="firmware" accept=".bin" required>
      <div class="progress"><div id="bar" class="bar"></div></div>
      <button type="submit">Wyślij i zaktualizuj</button>
    </form>
  </div>
  <script>
    const form = document.getElementById('otaForm');
    const msg = document.getElementById('msg');
    const bar = document.getElementById('bar');
    form.addEventListener('submit', function(e){
      e.preventDefault();
      msg.style.color = "#c93c36";
      msg.textContent = 'Wgrywanie...';
      const fw = document.getElementById('firmware').files[0];
      if (!fw) { msg.textContent = 'Wybierz plik .bin'; return; }
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/api/ota');
      xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
          let percent = Math.round((e.loaded / e.total) * 100);
          bar.style.width = percent + '%';
        }
      };
      xhr.onload = function() {
        if (xhr.status == 200) {
          msg.style.color = "#3685c9";
          msg.textContent = 'Aktualizacja zakończona sukcesem! Urządzenie zostanie zrestartowane za 3 sekundy.';
          setTimeout(()=>location.reload(), 3500);
        } else {
          msg.textContent = 'Błąd: ' + xhr.responseText;
          bar.style.width = '0%';
        }
      };
      xhr.onerror = function() {
        msg.textContent = 'Błąd połączenia z serwerem OTA!';
        bar.style.width = '0%';
      };
      const fd = new FormData();
      fd.append('firmware', fw);
      xhr.send(fd);
    });
  </script>
</body>
</html>
)rawliteral";

// --- Funkcja autoryzacji
inline bool checkAuth(AsyncWebServerRequest *request) {
  if (!request->authenticate("admin", "admin")) {
    request->requestAuthentication();
    return false;
  }
  return true;
}

namespace WebServerUI {

  static AsyncWebServer* server = nullptr;

  void begin(
      Config* config,
      void* /*Scheduler* scheduler,*/,
      Zones* relays,
      Weather* weather,
      PushoverClient* pushover,
      Programs* programs,
      Logs* logs
  ) {
    server = new AsyncWebServer(80);

    // --- Strona główna
    server->on("/", HTTP_GET, [config](AsyncWebServerRequest *req) {
      Serial.println("[HTTP] GET /");
      if (config->isInAPMode()) {
        req->redirect("/wifi");
        Serial.println("[HTTP] Redirected to /wifi");
        return;
      }
      if (LittleFS.exists("/index.html")) {
        req->send(LittleFS, "/index.html", "text/html");
        Serial.println("[HTTP] Served /index.html");
      } else {
        req->send(200, "text/html", MAIN_PAGE_HTML);
        Serial.println("[HTTP] Served emergency MAIN_PAGE_HTML");
      }
    });

    // --- Strona konfiguracji WiFi
    server->on("/wifi", HTTP_GET, [config](AsyncWebServerRequest *req) {
      Serial.println("[HTTP] GET /wifi");
      if (config->isInAPMode()) {
        req->send(200, "text/html", WIFI_CONFIG_PAGE_HTML);
        Serial.println("[HTTP] Served WIFI_CONFIG_PAGE_HTML");
      } else {
        req->redirect("/");
        Serial.println("[HTTP] Redirected to /");
      }
    });

    // --- RAW BODY obsługuje wszystkie POST/PUT! ---
    server->onRequestBody([config, relays, programs, logs](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t) {
      String url = request->url();
      auto method = request->method();
      Serial.printf("[onRequestBody] %s %s len=%u\n",
          (method==HTTP_POST ? "POST" : method==HTTP_PUT ? "PUT" : method==HTTP_DELETE ? "DELETE" : "INNE"),
          url.c_str(), (unsigned)len);
      Serial.print("[onRequestBody] Body: ");
      for (size_t i=0; i<len; ++i) Serial.print((char)data[i]);
      Serial.println();

      // --- /api/wifi
      if (url == "/api/wifi" && method == HTTP_POST) {
        DynamicJsonDocument doc(256);
        if (deserializeJson(doc, (const char*)data, len)) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"Błąd JSON\"}"); return;
        }
        String ssid = doc["ssid"] | "";
        String pass = doc["pass"] | "";
        if (ssid == "") { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Brak SSID\"}"); return; }
        DynamicJsonDocument cfg(128);
        cfg["ssid"] = ssid; cfg["pass"] = pass;
        config->saveFromJson(cfg);
        if (logs) logs->add("Zmieniono ustawienia WiFi (SSID: " + ssid + ")");
        request->send(200, "application/json", "{\"ok\":true}");
        delay(1000);
        ESP.restart();
        return;
      }

      // --- /api/settings
      if (url == "/api/settings" && method == HTTP_POST) {
        DynamicJsonDocument doc(384);
        if (deserializeJson(doc, (const char*)data, len)) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"Błąd JSON\"}"); return;
        }

        // Sprawdź zmiany kluczowych pól przed zapisaniem!
        String oldOwmApiKey = config->getOwmApiKey();
        String oldOwmLocation = config->getOwmLocation();
        String oldTimezone = config->getTimezone();

        config->saveFromJson(doc);

        // Logowanie zmian:
        if (doc.containsKey("owmApiKey") && doc["owmApiKey"].as<String>() != oldOwmApiKey) {
          if (logs) logs->add("Zmieniono klucz OWM API");
        }
        if (doc.containsKey("owmLocation") && doc["owmLocation"].as<String>() != oldOwmLocation) {
          if (logs) logs->add("Zmieniono lokalizację OWM: " + doc["owmLocation"].as<String>());
        }
        if (doc.containsKey("timezone") && doc["timezone"].as<String>() != oldTimezone) {
          if (logs) logs->add("Zmieniono strefę czasową na: " + doc["timezone"].as<String>());
        }

        setTimezoneFromWeb();
        if (logs) logs->add("Zapisano ustawienia systemu");
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }

      // --- /api/zones (toggle strefy)
      if (url == "/api/zones" && method == HTTP_POST) {
        DynamicJsonDocument doc(128);
        if (deserializeJson(doc, (const char*)data, len)) {
          request->send(400, "application/json", "{\"ok\":false}"); return;
        }
        int id = doc["id"] | -1;
        bool toggle = doc["toggle"] | false;
        if (id < 0 || id >= 8) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        if (toggle) {
          bool wasActive = relays->getZoneState(id);
          relays->toggleZone(id);
          bool isActive = relays->getZoneState(id);
          if (logs) {
            if (!wasActive && isActive) {
              logs->add("Ręcznie włączono strefę #" + String(id+1));
            } else if (wasActive && !isActive) {
              logs->add("Ręcznie wyłączono strefę #" + String(id+1));
            } else {
              logs->add("Ręcznie przełączono strefę #" + String(id+1));
            }
          }
        }
        DynamicJsonDocument resp(1024);
        relays->toJson(resp);
        String out;
        serializeJson(resp, out);
        request->send(200, "application/json", out);
        return;
      }

      // --- /api/zones-names
      if (url == "/api/zones-names" && method == HTTP_POST) {
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, (const char*)data, len) || !doc["names"].is<JsonArray>()) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"Błąd JSON lub brak tablicy 'names'\"}"); return;
        }
        relays->setAllZoneNames(doc["names"].as<JsonArray>());
        if (logs) logs->add("Zmieniono nazwy stref");
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }

      // --- /api/programs (dodawanie programu)
      if (url == "/api/programs" && method == HTTP_POST) {
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, (const char*)data, len)) {
          request->send(400, "application/json", "{\"ok\":false}"); return;
        }
        programs->addFromJson(doc);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }

      // --- /api/programs/import (import programów)
      if (url == "/api/programs/import" && method == HTTP_POST) {
        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc, (const char*)data, len)) {
          request->send(400, "application/json", "{\"ok\":false}"); return;
        }
        programs->importFromJson(doc);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }

      // --- PUT /api/programs/<id> (edycja programu)
      String prog_prefix = "/api/programs/";
      if (url.startsWith(prog_prefix) && method == HTTP_PUT) {
        int idx = url.substring(prog_prefix.length()).toInt();
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, (const char*)data, len)) {
          request->send(400, "application/json", "{\"ok\":false}"); return;
        }
        programs->edit(idx, doc);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }
    });

    // --- Status systemu
    server->on("/api/status", HTTP_GET, [config](AsyncWebServerRequest *req) {
      Serial.println("[API] GET /api/status");
      String json = "{";
      json += "\"wifi\":\"";
      json += (WiFi.status() == WL_CONNECTED) ? "Połączono" : "Brak połączenia";
      json += "\",";
      json += "\"ip\":\"";
      json += (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "-";
      json += "\",";
      json += "\"time\":\"";
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      char buf[32];
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min);
      json += buf;
      json += "\"}";
      req->send(200, "application/json", json);
    });

    // --- Dane pogodowe
    server->on("/api/weather", HTTP_GET, [weather](AsyncWebServerRequest *req) {
      Serial.println("[API] GET /api/weather");
      DynamicJsonDocument doc(512);
      weather->toJson(doc);
      String out;
      serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- Historia opadów (ostatnie 6h)
    server->on("/api/rain-history", HTTP_GET, [weather](AsyncWebServerRequest *req) {
      Serial.println("[API] GET /api/rain-history");
      DynamicJsonDocument doc(4096);
      weather->toJsonRainHistory(doc); // tablica [{ts, mm}, ...]
      String out;
      serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- Lista stref
    server->on("/api/zones", HTTP_GET, [relays](AsyncWebServerRequest *req) {
      Serial.println("[API] GET /api/zones");
      DynamicJsonDocument doc(1024);
      relays->toJson(doc);
      String out;
      serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- TRWAŁE NAZWY STREF
    server->on("/api/zones-names", HTTP_GET, [relays](AsyncWebServerRequest *req) {
      Serial.println("[API] GET /api/zones-names");
      DynamicJsonDocument doc(512);
      JsonArray names = doc["names"].to<JsonArray>();
      relays->toJsonNames(names);
      String out;
      serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- HARMONOGRAMY (programs) GET
    server->on("/api/programs", HTTP_GET, [programs](AsyncWebServerRequest *req){
      Serial.println("[API] GET /api/programs");
      DynamicJsonDocument doc(4096);
      programs->toJson(doc);
      String json;
      serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    // --- HARMONOGRAMY (programs) EXPORT
    server->on("/api/programs/export", HTTP_GET, [programs](AsyncWebServerRequest *req){
      Serial.println("[API] GET /api/programs/export");
      DynamicJsonDocument doc(4096);
      programs->toJson(doc);
      String json;
      serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    // --- Kasowanie programu po ID (DELETE)
    server->on("/api/programs", HTTP_DELETE, [programs](AsyncWebServerRequest *req) {
      if (!req->hasParam("id")) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"Brak parametru id\"}");
        return;
      }
      int idx = req->getParam("id")->value().toInt();
      programs->remove(idx);
      req->send(200, "application/json", "{\"ok\":true}");
    });

    // --- LOGI
    if (logs) {
      server->on("/api/logs", HTTP_GET, [logs](AsyncWebServerRequest *req){
        Serial.println("[API] GET /api/logs");
        DynamicJsonDocument doc(512);
        logs->toJson(doc);
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
      });
      server->on("/api/logs", HTTP_DELETE, [logs](AsyncWebServerRequest *req){
        Serial.println("[API] DELETE /api/logs");
        logs->clear();
        req->send(200, "application/json", "{\"ok\":true}");
      });
    }

    // --- USTAWIENIA GET
    server->on("/api/settings", HTTP_GET, [config](AsyncWebServerRequest *req){
      Serial.println("[API] GET /api/settings");
      DynamicJsonDocument doc(512);
      config->toJson(doc);
      String json;
      serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    // --- OTA – STRONA
    server->on("/ota", HTTP_GET, [](AsyncWebServerRequest *request) {
      Serial.println("[HTTP] GET /ota");
      if (!checkAuth(request)) return;
      request->send(200, "text/html", OTA_PAGE_HTML);
    });

    // --- OTA – upload bin
    server->on("/api/ota", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        Serial.println("[API] POST /api/ota - start upload");
        if (!checkAuth(request)) return;
      },
      [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
        Serial.printf("[API] OTA upload: file=%s idx=%u len=%u final=%d\n", filename.c_str(), (unsigned)index, (unsigned)len, final ? 1 : 0);
        if (!checkAuth(request)) return;
        static bool otaError = false;
        if (!index) {
          otaError = false;
          if (!filename.endsWith(".bin")) {
            otaError = true;
            Serial.println("[API] OTA error: bad file extension");
            request->send(400, "text/plain", "Zły typ pliku! Wgraj .bin.");
            return;
          }
          if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
            otaError = true;
            Serial.println("[API] OTA error: Update.begin failed");
            request->send(500, "text/plain", "Błąd OTA: nie można rozpocząć aktualizacji");
            return;
          }
        }
        if (!otaError && !Update.write(data, len)) {
          otaError = true;
          Serial.println("[API] OTA error: Update.write failed");
          request->send(500, "text/plain", "Błąd OTA w trakcie zapisu");
          return;
        }
        if (final) {
          if (!otaError && Update.end(true)) {
            Serial.println("[API] OTA update completed. Restart za 3000ms.");
            request->send(200, "text/plain", "OK");
            delay(3000); // 3 sekundy
            ESP.restart();
          } else if (!otaError) {
            Serial.println("[API] OTA error: Update.end failed");
            request->send(500, "text/plain", "Błąd kończenia OTA");
          }
        }
      }
    );

    server->serveStatic("/", LittleFS, "/");
    server->begin();
    Serial.println("[HTTP] Serwer wystartował na porcie 80");
  }

  void loop() { }
}
