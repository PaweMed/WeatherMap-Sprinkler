#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

class Weather {
  String apiKey, location;
  float temp=0, humidity=0, rain1h=0, rain6h=0, wind=0;
  float forecast1h=0, forecast6h=0;
  unsigned long lastWeather = 0;
  unsigned long lastForecast = 0;
public:
  void begin(const String& key, const String& loc) {
    apiKey = key; location = loc;
  }

  void loop() {
    unsigned long now = millis();
    // --- HISTORIA POGODY: co 30 min ---
    if (now - lastWeather > 10000UL) {
      lastWeather = now;
      Serial.println("[Weather] Pobieranie danych historycznych OWM...");

      // KROK 1: GEO - zamiana lokalizacji na lat/lon
      float lat = 0, lon = 0;
      String urlGeo = "http://api.openweathermap.org/geo/1.0/direct?q=" + location + "&limit=1&appid=" + apiKey;
      HTTPClient httpGeo;
      httpGeo.begin(urlGeo);
      int codeGeo = httpGeo.GET();
      if (codeGeo > 0) {
        String respGeo = httpGeo.getString();
        Serial.print("[Weather] Odpowiedź geo: ");
        Serial.println(respGeo);
        JsonDocument docGeo;
        DeserializationError err = deserializeJson(docGeo, respGeo);
        if (err) {
          Serial.print("[Weather] Błąd JSON geo: ");
          Serial.println(err.c_str());
          httpGeo.end();
          return;
        }
        if (docGeo.is<JsonArray>() && docGeo.size() > 0) {
          JsonArray arr = docGeo.as<JsonArray>();
          JsonObject obj = arr[0];
          if (obj["lat"].is<float>() && obj["lon"].is<float>()) {
            lat = obj["lat"].as<float>();
            lon = obj["lon"].as<float>();
            Serial.print("[Weather] Współrzędne: lat=");
            Serial.print(lat, 6);
            Serial.print(" lon=");
            Serial.println(lon, 6);
          } else {
            Serial.println("[Weather] Błąd: Brak/nieprawidłowe pola lat/lon w geo!");
          }
        } else {
          Serial.println("[Weather] Błąd: geo nie jest tablicą lub jest pusta!");
        }
      } else {
        Serial.print("[Weather] Błąd HTTP geo: ");
        Serial.println(codeGeo);
        httpGeo.end();
        return;
      }
      httpGeo.end();

      if (lat == 0 && lon == 0) {
        Serial.println("[Weather] Nie udało się pobrać współrzędnych dla lokalizacji!");
        return;
      }

      // KROK 2: ONECALL - pobranie danych
      String url = "http://api.openweathermap.org/data/2.5/onecall?lat=" + String(lat,6) + "&lon=" + String(lon,6) + "&units=metric&exclude=minutely,daily,alerts,current&appid=" + apiKey;
      HTTPClient http;
      http.begin(url);
      int code = http.GET();
      if (code > 0) {
        String resp = http.getString();
        Serial.print("[Weather] Odpowiedź onecall: ");
        Serial.println(resp);
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, resp);
        if (err) {
          Serial.print("[Weather] Błąd JSON onecall: ");
          Serial.println(err.c_str());
          http.end();
          return;
        }
        // temp/humidity z aktualnej godziny
        temp = doc["hourly"][0]["temp"].as<float>();
        humidity = doc["hourly"][0]["humidity"].as<float>();
        wind = doc["hourly"][0]["wind_speed"].as<float>();
        // Opady z ostatnich 6 godzin
        rain6h = 0; rain1h = 0;
        for (int i = 0; i < 6; i++) {
          float r = doc["hourly"][i]["rain"]["1h"].as<float>();
          if (i == 0) rain1h = r;
          rain6h += r;
        }
        Serial.print("[Weather] temp=");
        Serial.print(temp);
        Serial.print("C, humidity=");
        Serial.print(humidity);
        Serial.print("%, rain1h=");
        Serial.print(rain1h);
        Serial.print("mm, rain6h=");
        Serial.print(rain6h);
        Serial.print("mm, wind=");
        Serial.print(wind);
        Serial.println("m/s");
      } else {
        Serial.print("[Weather] Błąd HTTP onecall: ");
        Serial.println(code);
        http.end();
        return;
      }
      http.end();
    }

    // --- PROGNOZA: co 30 min ---
    if (now - lastForecast > 10000UL) {
      lastForecast = now;
      Serial.println("[Weather] Pobieranie prognozy OWM...");

      // KROK 1: GEO - zamiana lokalizacji na lat/lon
      float lat = 0, lon = 0;
      String urlGeo = "http://api.openweathermap.org/geo/1.0/direct?q=" + location + "&limit=1&appid=" + apiKey;
      HTTPClient httpGeo;
      httpGeo.begin(urlGeo);
      int codeGeo = httpGeo.GET();
      if (codeGeo > 0) {
        String respGeo = httpGeo.getString();
        Serial.print("[Weather] Odpowiedź geo (prognoza): ");
        Serial.println(respGeo);
        JsonDocument docGeo;
        DeserializationError err = deserializeJson(docGeo, respGeo);
        if (err) {
          Serial.print("[Weather] Błąd JSON geo (prognoza): ");
          Serial.println(err.c_str());
          httpGeo.end();
          return;
        }
        if (docGeo.is<JsonArray>() && docGeo.size() > 0) {
          JsonArray arr = docGeo.as<JsonArray>();
          JsonObject obj = arr[0];
          if (obj["lat"].is<float>() && obj["lon"].is<float>()) {
            lat = obj["lat"].as<float>();
            lon = obj["lon"].as<float>();
            Serial.print("[Weather] Współrzędne (prognoza): lat=");
            Serial.print(lat, 6);
            Serial.print(" lon=");
            Serial.println(lon, 6);
          } else {
            Serial.println("[Weather] Błąd: Brak/nieprawidłowe pola lat/lon w geo (prognoza)!");
          }
        } else {
          Serial.println("[Weather] Błąd: geo (prognoza) nie jest tablicą lub jest pusta!");
        }
      } else {
        Serial.print("[Weather] Błąd HTTP geo (prognoza): ");
        Serial.println(codeGeo);
        httpGeo.end();
        return;
      }
      httpGeo.end();

      if (lat == 0 && lon == 0) {
        Serial.println("[Weather] Nie udało się pobrać współrzędnych (prognoza)!");
        return;
      }

      // KROK 2: forecast - pobranie prognozy 3h
      String urlF = "http://api.openweathermap.org/data/2.5/forecast?lat=" + String(lat,6) + "&lon=" + String(lon,6) + "&appid=" + apiKey + "&units=metric";
      HTTPClient httpF;
      httpF.begin(urlF);
      int codeF = httpF.GET();
      if (codeF > 0) {
        String respF = httpF.getString();
        Serial.print("[Weather] Odpowiedź forecast: ");
        Serial.println(respF);
        JsonDocument docF;
        DeserializationError err = deserializeJson(docF, respF);
        if (err) {
          Serial.print("[Weather] Błąd JSON forecast: ");
          Serial.println(err.c_str());
          httpF.end();
          return;
        }
        forecast1h = 0; forecast6h = 0;
        // Każda prognoza to blok co 3 godziny - sumujemy pierwsze 2 (6h), wyciągamy max z pierwszego (1h)
        for (int i = 0; i < 2 && i < docF["list"].size(); i++) {
          float rainf = docF["list"][i]["rain"]["3h"].as<float>();
          if (i == 0) forecast1h = rainf / 3.0; // zaokrąglamy do mm/h
          forecast6h += rainf;
        }
        Serial.print("[Weather] forecast1h=");
        Serial.print(forecast1h);
        Serial.print("mm/h, forecast6h=");
        Serial.print(forecast6h);
        Serial.println("mm");
      } else {
        Serial.print("[Weather] Błąd HTTP forecast: ");
        Serial.println(codeF);
        httpF.end();
        return;
      }
      httpF.end();
    }
  }

  void toJson(JsonDocument& doc) {
    doc["temp"] = temp;
    doc["humidity"] = humidity;
    doc["rain1h"] = rain1h;
    doc["rain6h"] = rain6h;
    doc["wind"] = wind;
    doc["forecast1h"] = forecast1h;
    doc["forecast6h"] = forecast6h;
    doc["watering_percent"] = getWateringPercent();
    doc["watering_allowed"] = wateringAllowed();
  }

  // Algorytm do sterowania podlewaniem zgodnie z założeniami użytkownika
  int getWateringPercent() {
    if (forecast1h > 4.0) return 0; // nie podlewaj
    if (forecast6h > 2.0 && forecast6h <= 4.0) return 50; // podlewaj 50%
    return 100; // pełne podlewanie
  }
  bool wateringAllowed() {
    return getWateringPercent() > 0;
  }
};
