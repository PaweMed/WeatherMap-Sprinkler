#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "RainHistory.h"

class Weather {
  String apiKey, location;

  // Dane aktualne
  float temp = 0, feels_like = 0, temp_min = 0, temp_max = 0;
  float humidity = 0, pressure = 0, wind = 0, wind_deg = 0, clouds = 0, visibility = 0;
  String weather_desc = "", icon = "";
  float rain = 0;

  // Prognozy
  float rain_1h_forecast = 0, rain_6h_forecast = 0;
  float temp_min_tomorrow = 0, temp_max_tomorrow = 0;
  float humidity_tomorrow_max = 0; // maksymalna prognozowana wilgotność na jutro

  // Wschód/zachód
  String sunrise = "", sunset = "";

  // Sterowanie
  bool   enabled = true;
  unsigned long intervalMs = 60UL * 60UL * 1000UL;  // domyślnie 1h

  // Terminy pobrań
  unsigned long nextWeatherDue  = 0;
  unsigned long nextForecastDue = 0;

  // Stan
  bool everSucceededWeather  = false;
  bool everSucceededForecast = false;

  // Cache GEO
  float cachedLat = 0.0f;
  float cachedLon = 0.0f;
  bool  coordsValid = false;

  RainHistory rainHistory; // historia opadów (rolling 6h, trwała w LittleFS)

  bool resolveCoords() {
    if (coordsValid && cachedLat != 0.0f && cachedLon != 0.0f) return true;
    if (apiKey.isEmpty() || location.isEmpty()) {
      Serial.println("[Weather] Brak apiKey lub location – pomijam GEO.");
      return false;
    }

    String urlGeo = "http://api.openweathermap.org/geo/1.0/direct?q=" + location + "&limit=1&appid=" + apiKey;
    HTTPClient httpGeo;
    httpGeo.begin(urlGeo);
    int codeGeo = httpGeo.GET();
    if (codeGeo > 0) {
      String respGeo = httpGeo.getString();
      JsonDocument docGeo;
      DeserializationError err = deserializeJson(docGeo, respGeo);
      if (!err) {
        if (docGeo.is<JsonArray>() && docGeo.size() > 0) {
          JsonObject obj = docGeo[0];
          cachedLat = obj["lat"].as<float>();
          cachedLon = obj["lon"].as<float>();
          coordsValid = (cachedLat != 0.0f || cachedLon != 0.0f);
        }
      } else {
        Serial.print("[Weather] Błąd JSON GEO: "); Serial.println(err.c_str());
      }
    } else {
      Serial.print("[Weather] Błąd pobierania GEO! Kod HTTP: "); Serial.println(codeGeo);
    }
    httpGeo.end();
    if (!coordsValid) Serial.println("[Weather] Błąd: Brak współrzędnych!");
    return coordsValid;
  }

  int ydayTomorrow() {
    time_t now_ts = time(nullptr);
    now_ts += 24 * 60 * 60;
    struct tm t;
    localtime_r(&now_ts, &t);
    return t.tm_yday;
  }

  void scheduleRetryEarly(bool forWeather) {
    if (forWeather) {
      if (!everSucceededWeather) nextWeatherDue = millis() + 60000UL;
      else                       nextWeatherDue = millis() + intervalMs;
    } else {
      if (!everSucceededForecast) nextForecastDue = millis() + 60000UL;
      else                        nextForecastDue = millis() + intervalMs;
    }
  }

public:
  void begin(const String& key, const String& loc, bool en=true, int intervalMin=60) {
    apiKey = key;
    location = loc;
    enabled = en;
    if (intervalMin < 5) intervalMin = 5;
    intervalMs = (unsigned long)intervalMin * 60UL * 1000UL;

    nextWeatherDue  = 0;
    nextForecastDue = 0;
    everSucceededWeather  = false;
    everSucceededForecast = false;

    coordsValid = false;
    cachedLat = cachedLon = 0.0f;

    rainHistory.begin(); // wczytaj historię z pliku
  }

  void applySettings(const String& key, const String& loc, bool en, int intervalMin) {
    begin(key, loc, en, intervalMin);
  }

  void loop() {
    if (!enabled) return;
    unsigned long nowMs = millis();

    // --- AKTUALNA ---
    if (nowMs >= nextWeatherDue) {
      if (apiKey.isEmpty() || location.isEmpty()) {
        Serial.println("[Weather] Pomijam aktualne dane – brak apiKey/location.");
        nextWeatherDue = nowMs + intervalMs;
      } else if (resolveCoords()) {
        Serial.println("[Weather] Pobieranie AKTUALNEJ pogody OWM...");
        String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + String(cachedLat, 6) +
                     "&lon=" + String(cachedLon, 6) + "&units=metric&appid=" + apiKey + "&lang=pl";
        HTTPClient http;
        http.begin(url);
        int code = http.GET();
        if (code > 0) {
          String resp = http.getString();
          JsonDocument doc;
          DeserializationError err = deserializeJson(doc, resp);
          if (!err) {
            temp        = doc["main"]["temp"]        | 0.0;
            feels_like  = doc["main"]["feels_like"]  | 0.0;
            temp_min    = doc["main"]["temp_min"]    | 0.0;
            temp_max    = doc["main"]["temp_max"]    | 0.0;
            humidity    = doc["main"]["humidity"]    | 0.0;
            pressure    = doc["main"]["pressure"]    | 0.0;
            wind        = doc["wind"]["speed"]       | 0.0;
            wind_deg    = doc["wind"]["deg"]         | 0.0;
            clouds      = doc["clouds"]["all"]       | 0.0;
            visibility  = doc["visibility"]          | 0.0;
            rain        = doc["rain"]["1h"]          | 0.0;

            weather_desc = "";
            icon = "";
            if (doc["weather"].is<JsonArray>() && doc["weather"].size() > 0) {
              weather_desc = doc["weather"][0]["description"].as<const char*>();
              icon         = doc["weather"][0]["icon"].as<const char*>();
            }

            // Wschód/zachód
            time_t sunrise_ts = (time_t)(doc["sys"]["sunrise"] | 0);
            time_t sunset_ts  = (time_t)(doc["sys"]["sunset"]  | 0);
            char buf[8];
            struct tm t;
            if (sunrise_ts) {
              localtime_r(&sunrise_ts, &t);
              snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
              sunrise = String(buf);
            } else sunrise = "";
            if (sunset_ts) {
              localtime_r(&sunset_ts, &t);
              snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
              sunset = String(buf);
            } else sunset = "";

            // aktualizacja historii opadów (rolling 6h)
            rainHistory.addRainMeasurement(rain);

            everSucceededWeather = true;
            nextWeatherDue = nowMs + intervalMs;
          } else {
            Serial.print("[Weather] Błąd JSON weather: "); Serial.println(err.c_str());
            scheduleRetryEarly(true);
          }
        } else {
          Serial.print("[Weather] Błąd pobierania weather! Kod HTTP: "); Serial.println(code);
          scheduleRetryEarly(true);
        }
        http.end();
      } else {
        scheduleRetryEarly(true);
      }
    }

    // --- PROGNOZA ---
    if (nowMs >= nextForecastDue) {
      if (apiKey.isEmpty() || location.isEmpty()) {
        Serial.println("[Weather] Pomijam prognozę – brak apiKey/location.");
        nextForecastDue = nowMs + intervalMs;
      } else if (resolveCoords()) {
        Serial.println("[Weather] Pobieranie prognozy OWM...");
        String urlF = "http://api.openweathermap.org/data/2.5/forecast?lat=" + String(cachedLat, 6) +
                      "&lon=" + String(cachedLon, 6) + "&appid=" + apiKey + "&units=metric";
        HTTPClient httpF;
        httpF.begin(urlF);
        int codeF = httpF.GET();
        if (codeF > 0) {
          String respF = httpF.getString();
          JsonDocument docF;
          DeserializationError err = deserializeJson(docF, respF);
          if (!err) {
            rain_1h_forecast = 0;
            rain_6h_forecast = 0;
            temp_min_tomorrow = 0;
            temp_max_tomorrow = 0;
            humidity_tomorrow_max = 0;

            if (docF["list"].is<JsonArray>() && docF["list"].size() >= 2) {
              float rain3h_0 = docF["list"][0]["rain"]["3h"] | 0.0;
              float rain3h_1 = docF["list"][1]["rain"]["3h"] | 0.0;
              rain_1h_forecast = rain3h_0 / 3.0f;
              rain_6h_forecast = rain3h_0 + rain3h_1;
            }

            int targetYday = ydayTomorrow();
            float min_t = 1000.0f, max_t = -1000.0f;
            float max_h = 0.0f;

            for (JsonVariant v : docF["list"].as<JsonArray>()) {
              time_t ts = (time_t)(v["dt"] | 0);
              struct tm tt;
              localtime_r(&ts, &tt);
              if (tt.tm_yday == targetYday) {
                float t_min = v["main"]["temp_min"] | 0.0;
                float t_max = v["main"]["temp_max"] | 0.0;
                float h_val = v["main"]["humidity"] | 0.0;
                if (t_min < min_t) min_t = t_min;
                if (t_max > max_t) max_t = t_max;
                if (h_val > max_h) max_h = h_val;
              }
            }
            temp_min_tomorrow = (min_t < 1000.0f) ? min_t : 0.0f;
            temp_max_tomorrow = (max_t > -1000.0f) ? max_t : 0.0f;
            humidity_tomorrow_max = max_h;

            everSucceededForecast = true;
            nextForecastDue = nowMs + intervalMs;
          } else {
            Serial.print("[Weather] Błąd JSON forecast: "); Serial.println(err.c_str());
            scheduleRetryEarly(false);
          }
        } else {
          Serial.print("[Weather] Błąd pobierania forecast! Kod HTTP: "); Serial.println(codeF);
          scheduleRetryEarly(false);
        }
        httpF.end();
      } else {
        scheduleRetryEarly(false);
      }
    }
  }

  void toJson(JsonDocument& doc) {
    doc["temp"] = temp;
    doc["feels_like"] = feels_like;
    doc["humidity"] = humidity;
    doc["pressure"] = pressure;
    doc["wind"] = wind;
    doc["wind_deg"] = wind_deg;
    doc["clouds"] = clouds;
    doc["visibility"] = (int)(visibility / 1000);
    doc["weather_desc"] = weather_desc;
    doc["icon"] = icon;
    doc["rain"] = rain;
    doc["rain_1h_forecast"] = rain_1h_forecast;
    doc["rain_6h_forecast"] = rain_6h_forecast;
    doc["sunrise"] = sunrise;
    doc["sunset"] = sunset;
    doc["temp_min"] = temp_min;
    doc["temp_max"] = temp_max;
    doc["temp_min_tomorrow"] = temp_min_tomorrow;
    doc["temp_max_tomorrow"] = temp_max_tomorrow;
    doc["humidity_tomorrow_max"] = humidity_tomorrow_max;
  }

  // API dla WebServerUI / innych modułów
  void rainHistoryToJson(JsonDocument& doc) const { rainHistory.toJson(doc); }
  float getLast6hRain() const { return rainHistory.getLast6hRain(); }
  float getDailyMaxTemp() const { return temp_max_tomorrow; }
  float getDailyHumidityForecast() const { return humidity_tomorrow_max; }

  // *** LOGIKA PROCENTOWA – zgodnie z ustaleniami ***
  // ≥5mm (6h)           -> 0%
  // 2–5mm (6h)          -> 40%
  // 0–2mm (6h) i H_now>70%  -> 80%
  // T_now>27°C i H_now<50%  -> 120%
  // pozostałe            -> 100%
  int getWateringPercent() {
    const float rain6h = getLast6hRain(); // suma z ostatnich 6h (RainHistory)
    const float H_now  = humidity;        // BIEŻĄCA wilgotność
    const float T_now  = temp;            // BIEŻĄCA temperatura

    // 1) Opady – najwyższy priorytet
    if (rain6h >= 5.0f) return 0;
    if (rain6h >= 2.0f) return 40;

    // 2) Gorąco i sucho – zwiększamy
    if (T_now > 27.0f && H_now < 50.0f) return 120;

    // 3) Mało opadów i bardzo wilgotno – skracamy
    if (rain6h < 2.0f && H_now > 70.0f) return 80;

    // 4) Domyślnie
    return 100;
  }

  bool wateringAllowed() { return getWateringPercent() > 0; }
};
