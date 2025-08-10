#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

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

  // Wschód/zachód słońca
  String sunrise = "", sunset = "";

  // Interwały odpytywania (30 min)
  static constexpr unsigned long WEATHER_INTERVAL  = 30UL * 60UL * 1000UL;
  static constexpr unsigned long FORECAST_INTERVAL = 30UL * 60UL * 1000UL;

  unsigned long lastWeather  = 0;
  unsigned long lastForecast = 0;

  // Cache współrzędnych dla OWM GEO
  float cachedLat = 0.0f;
  float cachedLon = 0.0f;
  bool  coordsValid = false;

  // --- Historia opadów (ostatnie 6h) ---
  static constexpr const char* RAIN_FILE = "/rain-history.json";
  static constexpr time_t SIX_HOURS = 6 * 3600;

  bool resolveCoords() {
    // Jeżeli mamy już poprawne współrzędne i lokalizacja się nie zmieniła – użyj cache
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
      Serial.print("[Weather] Odpowiedź OWM GEO: "); Serial.println(respGeo);
      DynamicJsonDocument docGeo(1024);
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

  // pomocnicza funkcja wyliczenia „jutro” bez pułapek na przełomach miesiąca/roku
  int ydayTomorrow() {
    time_t now_ts = time(nullptr);
    now_ts += 24 * 60 * 60; // +1 dzień
    struct tm t;
    localtime_r(&now_ts, &t);
    return t.tm_yday;
  }

  // --- OPERACJE NA HISTORII OPADÓW ---

  // Wczytaj historię do doc (tablica), przy okazji przytnij do 6h (ale nie zapisuj od razu)
  void loadRainHistoryDoc(DynamicJsonDocument& doc) {
    doc.clear();
    JsonArray arr = doc.to<JsonArray>(); // zaczniemy od pustej listy

    if (!LittleFS.exists(RAIN_FILE)) {
      return;
    }

    File f = LittleFS.open(RAIN_FILE, "r");
    if (!f) {
      Serial.println("[Weather] Nie można otworzyć /rain-history.json do odczytu.");
      return;
    }

    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
      Serial.print("[Weather] Błąd odczytu rain-history JSON: ");
      Serial.println(err.c_str());
      doc.clear();
      doc.to<JsonArray>();
      return;
    }

    // Przycięcie do ostatnich 6h (na bazie czasu 'teraz')
    time_t now_ts = time(nullptr);
    time_t cutoff = now_ts - SIX_HOURS;

    DynamicJsonDocument outDoc(4096);
    JsonArray outArr = outDoc.to<JsonArray>();
    for (JsonVariant v : doc.as<JsonArray>()) {
      time_t ts = (time_t)(v["ts"] | 0);
      float mm = v["mm"] | 0.0f;
      if (ts >= cutoff) {
        JsonObject o = outArr.add<JsonObject>();
        o["ts"] = ts;
        o["mm"] = mm;
      }
    }

    doc.clear();
    deserializeJson(doc, outDoc); // skopiuj wynik do doc
  }

  // Zapisz dokument (tablicę) jako /rain-history.json
  void saveRainHistoryDoc(DynamicJsonDocument& doc) {
    File f = LittleFS.open(RAIN_FILE, "w");
    if (!f) {
      Serial.println("[Weather] Nie można otworzyć /rain-history.json do zapisu.");
      return;
    }
    serializeJson(doc, f);
    f.close();
  }

  // Dopisz nową próbkę i od razu zapisz, trzymając tylko ostatnie 6h
  void appendRainSample(float mm) {
    // mm = opad z ostatniej godziny wg OWM (doc["rain"]["1h"])
    DynamicJsonDocument doc(4096);
    loadRainHistoryDoc(doc);
    JsonArray arr = doc.as<JsonArray>();

    time_t now_ts = time(nullptr);

    // Dodaj próbkę
    {
      JsonObject o = arr.add<JsonObject>();
      o["ts"] = now_ts;
      o["mm"] = mm;
    }

    // Jeszcze raz przytnij do 6h (gdyby zegar cofnął się itp.)
    DynamicJsonDocument pruned(4096);
    JsonArray out = pruned.to<JsonArray>();
    time_t cutoff = now_ts - SIX_HOURS;
    for (JsonVariant v : arr) {
      time_t ts = (time_t)(v["ts"] | 0);
      float m = v["mm"] | 0.0f;
      if (ts >= cutoff) {
        JsonObject o = out.add<JsonObject>();
        o["ts"] = ts;
        o["mm"] = m;
      }
    }

    saveRainHistoryDoc(pruned);
  }

public:
  void begin(const String& key, const String& loc) {
    apiKey = key;
    location = loc;
    lastWeather  = 0;   // natychmiastowe pobranie po starcie
    lastForecast = 0;   // jw.
    coordsValid = false;
    cachedLat = cachedLon = 0.0f;

    // Zainicjuj plik historii, jeśli nie istnieje
    if (!LittleFS.exists(RAIN_FILE)) {
      DynamicJsonDocument initDoc(256);
      initDoc.to<JsonArray>(); // pusta tablica
      saveRainHistoryDoc(initDoc);
    }
  }

  void loop() {
    unsigned long now = millis();

    // --- DANE AKTUALNE ---
    if (now - lastWeather > WEATHER_INTERVAL) {
      lastWeather = now;
      if (apiKey.isEmpty() || location.isEmpty()) {
        Serial.println("[Weather] Pomijam aktualne dane – brak apiKey/location.");
      } else if (resolveCoords()) {
        Serial.println("[Weather] Pobieranie AKTUALNEJ pogody OWM...");

        String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + String(cachedLat, 6) +
                     "&lon=" + String(cachedLon, 6) + "&units=metric&appid=" + apiKey + "&lang=pl";
        HTTPClient http;
        http.begin(url);
        int code = http.GET();
        if (code > 0) {
          String resp = http.getString();
          DynamicJsonDocument doc(4096);
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

            // Wschód/zachód (lokalny czas)
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

            // --- Zapisz próbkę opadów do historii (ostatnie 6h) ---
            appendRainSample(rain);

          } else {
            Serial.print("[Weather] Błąd JSON weather: "); Serial.println(err.c_str());
          }
        } else {
          Serial.print("[Weather] Błąd pobierania weather! Kod HTTP: "); Serial.println(code);
        }
        http.end();
      }
    }

    // --- PROGNOZA ---
    if (now - lastForecast > FORECAST_INTERVAL) {
      lastForecast = now;
      if (apiKey.isEmpty() || location.isEmpty()) {
        Serial.println("[Weather] Pomijam prognozę – brak apiKey/location.");
      } else if (resolveCoords()) {
        Serial.println("[Weather] Pobieranie prognozy OWM...");

        String urlF = "http://api.openweathermap.org/data/2.5/forecast?lat=" + String(cachedLat, 6) +
                      "&lon=" + String(cachedLon, 6) + "&appid=" + apiKey + "&units=metric";
        HTTPClient httpF;
        httpF.begin(urlF);
        int codeF = httpF.GET();
        if (codeF > 0) {
          String respF = httpF.getString();
          DynamicJsonDocument docF(12288); // 12 KB – bezpieczniej dla listy prognoz
          DeserializationError err = deserializeJson(docF, respF);
          if (!err) {
            rain_1h_forecast = 0;
            rain_6h_forecast = 0;
            temp_min_tomorrow = 0;
            temp_max_tomorrow = 0;

            // forecast co 3h – 2 pierwsze bloki ≈ 6h
            if (docF["list"].is<JsonArray>() && docF["list"].size() >= 2) {
              float rain3h_0 = docF["list"][0]["rain"]["3h"] | 0.0;
              float rain3h_1 = docF["list"][1]["rain"]["3h"] | 0.0;
              rain_1h_forecast = rain3h_0 / 3.0f;
              rain_6h_forecast = rain3h_0 + rain3h_1;
            }

            // Min/max na jutro (liczone po tm_yday „jutra”)
            int targetYday = ydayTomorrow();
            float min_t = 1000.0f, max_t = -1000.0f;

            for (JsonVariant v : docF["list"].as<JsonArray>()) {
              time_t ts = (time_t)(v["dt"] | 0);
              struct tm tt;
              localtime_r(&ts, &tt);
              if (tt.tm_yday == targetYday) {
                float t_min = v["main"]["temp_min"] | 0.0;
                float t_max = v["main"]["temp_max"] | 0.0;
                if (t_min < min_t) min_t = t_min;
                if (t_max > max_t) max_t = t_max;
              }
            }
            temp_min_tomorrow = (min_t < 1000.0f) ? min_t : 0.0f;
            temp_max_tomorrow = (max_t > -1000.0f) ? max_t : 0.0f;
          } else {
            Serial.print("[Weather] Błąd JSON forecast: "); Serial.println(err.c_str());
          }
        } else {
          Serial.print("[Weather] Błąd pobierania forecast! Kod HTTP: "); Serial.println(codeF);
        }
        httpF.end();
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
    doc["visibility"] = (int)(visibility / 1000); // w km
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
  }

  // Algorytm sterowania podlewaniem (bazowy)
  int getWateringPercent() {
    if (rain_1h_forecast > 4.0) return 0;                  // nie podlewaj
    if (rain_6h_forecast > 2.0 && rain_6h_forecast <= 4.0) return 50; // 50%
    return 100;                                            // pełne podlewanie
  }
  bool wateringAllowed() {
    return getWateringPercent() > 0;
  }

  // --- API do udostępnienia historii opadów przez WebServerUI ---
  void toJsonRainHistory(JsonDocument& out) {
    DynamicJsonDocument doc(4096);
    loadRainHistoryDoc(doc);
    // out = doc (tablica)
    out.clear();
    JsonArray outArr = out.to<JsonArray>();
    for (JsonVariant v : doc.as<JsonArray>()) {
      JsonObject o = outArr.add<JsonObject>();
      o["ts"] = (long)(v["ts"] | 0);
      o["mm"] = (float)(v["mm"] | 0.0f);
    }
  }
};
