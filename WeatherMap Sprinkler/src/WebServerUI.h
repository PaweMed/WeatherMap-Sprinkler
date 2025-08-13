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

// z main.cpp
extern "C" void setTimezoneFromWeb();

extern MQTTClient mqtt; // użyjemy do updateConfig po zapisaniu ustawień

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
            <span>Strefa #${z.id+1} ${z.active?'(WŁ)':'(WYŁ)'} ${z.name?(' - ' + z.name):''}</span>
            <button onclick="toggleZone(${z.id}, this)">${z.active?'Wyłącz':'Włącz'}</button>
          </div>`;
        }
        document.getElementById('zones').innerHTML = html;
      });
    }
    function toggleZone(id, btn) {
      btn.disabled = true;
      fetch('/api/zones', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({id, toggle:true}) })
        .then(r=>r.json()).then(()=>{ setTimeout(loadZones, 400); btn.disabled = false; });
    }
    loadStatus(); loadWeather(); loadZones();
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

// --- Auth
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
        return;
      }
      if (LittleFS.exists("/index.html")) {
        req->send(LittleFS, "/index.html", "text/html");
      } else {
        req->send(200, "text/html", "<h1>WeatherMap Sprinkler – Panel awaryjny</h1>");
      }
    });

    server->on("/wifi", HTTP_GET, [config](AsyncWebServerRequest *req) {
      if (config->isInAPMode()) {
        req->send(200, "text/html", "<h1>Konfiguracja WiFi</h1>");
      } else {
        req->redirect("/");
      }
    });

    // --- NOWE: API rain-history
    server->on("/api/rain-history", HTTP_GET, [weather](AsyncWebServerRequest *req){
      Serial.println("[API] GET /api/rain-history");
      JsonDocument doc;
      weather->rainHistoryToJson(doc);
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    // --- NOWE: API watering-percent
    server->on("/api/watering-percent", HTTP_GET, [weather](AsyncWebServerRequest *req){
      Serial.println("[API] GET /api/watering-percent");
      JsonDocument doc;
      doc["percent"] = weather->getWateringPercent();
      doc["rain_6h"] = weather->getLast6hRain();
      doc["daily_max_temp"] = weather->getDailyMaxTemp();
      doc["daily_humidity_forecast"] = weather->getDailyHumidityForecast();
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    // --- onRequestBody do obsługi POST/PUT
    server->onRequestBody([config, relays, programs, logs, weather](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t) {
      String url = request->url();
      auto method = request->method();

      // --- /api/wifi
      if (url == "/api/wifi" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Błąd JSON\"}"); return; }
        String ssid = doc["ssid"] | "";
        String pass = doc["pass"] | "";
        if (ssid == "") { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Brak SSID\"}"); return; }
        JsonDocument cfg;
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
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false,\"error\":\"Błąd JSON\"}"); return; }
        config->saveFromJson(doc);
        setTimezoneFromWeb();
        weather->applySettings(config->getOwmApiKey(), config->getOwmLocation(), config->getEnableWeatherApi(), config->getWeatherUpdateIntervalMin());
        mqtt.updateConfig();
        if (logs) logs->add("Zapisano ustawienia systemu");
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }

      // --- /api/zones
      if (url == "/api/zones" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        int id = doc["id"] | -1;
        bool toggle = doc["toggle"] | false;
        if (id < 0 || id >= 8) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        if (toggle) {
          bool wasActive = relays->getZoneState(id);
          relays->toggleZone(id);
          bool isActive = relays->getZoneState(id);
          if (logs) {
            if (!wasActive && isActive) logs->add("Ręcznie włączono strefę #" + String(id+1));
            else if (wasActive && !isActive) logs->add("Ręcznie wyłączono strefę #" + String(id+1));
          }
        }
        JsonDocument resp;
        relays->toJson(resp);
        String out; serializeJson(resp, out);
        request->send(200, "application/json", out);
        return;
      }

      // --- /api/zones-names
      if (url == "/api/zones-names" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len) || !doc["names"].is<JsonArray>()) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"Błąd JSON lub brak tablicy 'names'\"}"); return;
        }
        relays->setAllZoneNames(doc["names"].as<JsonArray>());
        if (logs) logs->add("Zmieniono nazwy stref");
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }

      // --- /api/programs
      if (url == "/api/programs" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        programs->addFromJson(doc);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }

      // --- /api/programs/import
      if (url == "/api/programs/import" && method == HTTP_POST) {
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        programs->importFromJson(doc);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }

      // --- PUT /api/programs/<id>
      String prog_prefix = "/api/programs/";
      if (url.startsWith(prog_prefix) && method == HTTP_PUT) {
        int idx = url.substring(prog_prefix.length()).toInt();
        JsonDocument doc;
        if (deserializeJson(doc, (const char*)data, len)) { request->send(400, "application/json", "{\"ok\":false}"); return; }
        programs->edit(idx, doc);
        request->send(200, "application/json", "{\"ok\":true}");
        return;
      }
    });

    // --- Status
    server->on("/api/status", HTTP_GET, [config](AsyncWebServerRequest *req) {
      JsonDocument doc;
      doc["wifi"] = (WiFi.status() == WL_CONNECTED) ? "Połączono" : "Brak połączenia";
      doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "-";
      time_t now = time(nullptr);
      struct tm t;
      localtime_r(&now, &t);
      char buf[32];
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min);
      doc["time"] = buf;
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    // --- Weather
    server->on("/api/weather", HTTP_GET, [weather](AsyncWebServerRequest *req) {
      JsonDocument doc;
      weather->toJson(doc);
      String out; serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- Zones
    server->on("/api/zones", HTTP_GET, [relays](AsyncWebServerRequest *req) {
      JsonDocument doc;
      relays->toJson(doc);
      String out; serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- Zones names
    server->on("/api/zones-names", HTTP_GET, [relays](AsyncWebServerRequest *req) {
      JsonDocument doc;
      JsonArray names = doc["names"].to<JsonArray>();
      relays->toJsonNames(names);
      String out; serializeJson(doc, out);
      req->send(200, "application/json", out);
    });

    // --- Programs GET/EXPORT/DELETE
    server->on("/api/programs", HTTP_GET, [programs](AsyncWebServerRequest *req){
      JsonDocument doc;
      programs->toJson(doc);
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    server->on("/api/programs/export", HTTP_GET, [programs](AsyncWebServerRequest *req){
      JsonDocument doc;
      programs->toJson(doc);
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    server->on("/api/programs", HTTP_DELETE, [programs](AsyncWebServerRequest *req) {
      if (!req->hasParam("id")) { req->send(400, "application/json", "{\"ok\":false,\"error\":\"Brak parametru id\"}"); return; }
      int idx = req->getParam("id")->value().toInt();
      programs->remove(idx);
      req->send(200, "application/json", "{\"ok\":true}");
    });

    // --- LOGS
    if (logs) {
      server->on("/api/logs", HTTP_GET, [logs](AsyncWebServerRequest *req){
        JsonDocument doc;
        logs->toJson(doc);
        String json; serializeJson(doc, json);
        req->send(200, "application/json", json);
      });
      server->on("/api/logs", HTTP_DELETE, [logs](AsyncWebServerRequest *req){
        logs->clear();
        req->send(200, "application/json", "{\"ok\":true}");
      });
    }

    // --- USTAWIENIA GET
    server->on("/api/settings", HTTP_GET, [config](AsyncWebServerRequest *req){
      JsonDocument doc;
      config->toJson(doc);
      String json; serializeJson(doc, json);
      req->send(200, "application/json", json);
    });

    server->serveStatic("/", LittleFS, "/");
    server->begin();
    Serial.println("[HTTP] Serwer wystartował na porcie 80");
  }

  void loop() { }
}