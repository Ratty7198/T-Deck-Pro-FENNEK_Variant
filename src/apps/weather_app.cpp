// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

#include "weather_app.h"
#include "config.h"
#include "core/gui.h"
#include "core/i18n.h"
#include "core/appmgr.h"
#include "core/settings.h"
#include "services/timesync.h"
#include "services/wifi.h"
#include "services/gps.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <GxEPD2_BW.h>
#include <string.h>
#include <time.h>

namespace {

using gui::Rect;

constexpr int W = EINK_W;
constexpr int TOP = appmgr::CONTENT_Y;
const Rect kBtn1 {6, 274, 72, 42};
const Rect kBtn2 {84, 274, 72, 42};

struct WeatherData {
  float temp;
  int humidity;
  float windSpeed;
  const char* condition;
  uint32_t lastFetch;
};

WeatherData s_weather = {0, 0, 0, "Loading...", 0};
enum Screen { CURRENT, FORECAST };
int s_screen = CURRENT;

void markDirty() { appmgr::markDirty(); }

const char* conditionFromCode(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1:
    case 2: return "Partly Cloudy";
    case 3: return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55:
    case 56:
    case 57: return "Drizzle";
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82: return "Rain";
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86: return "Snow";
    case 95:
    case 96:
    case 99: return "Thunderstorm";
    default: return "Weather";
  }
}

bool extractFloat(const String& body, const char* key, float& out) {
  const char* p = strstr(body.c_str(), key);
  if (!p) return false;
  p = strchr(p, ':');
  if (!p) return false;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  char* end = nullptr;
  float v = strtof(p, &end);
  if (end == p) return false;
  out = v;
  return true;
}

bool extractInt(const String& body, const char* key, int& out) {
  const char* p = strstr(body.c_str(), key);
  if (!p) return false;
  p = strchr(p, ':');
  if (!p) return false;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  char* end = nullptr;
  long v = strtol(p, &end, 10);
  if (end == p) return false;
  out = (int)v;
  return true;
}

void fetchWeather() {
  s_weather.condition = "Connecting...";
  markDirty();

  if (!wifi::connect(20000)) {
    s_weather.condition = "WiFi unavailable";
    s_weather.lastFetch = 0;
    markDirty();
    return;
  }

  const gps::Fix& fix = gps::current();
  double lat = 52.52, lon = 13.41;
  if (fix.valid) {
    lat = fix.lat;
    lon = fix.lon;
  } else {
    settings::weatherPos(&lat, &lon);
  }

  char url[220];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&timezone=auto",
           lat, lon);

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  if (!http.begin(client, url)) {
    s_weather.condition = "HTTP failed";
    wifi::disconnect();
    markDirty();
    return;
  }

  http.setConnectTimeout(15000);
  http.setTimeout(20000);
  int code = http.GET();

  if (code == HTTP_CODE_OK) {
    String body = http.getString();
    float temp = 0.0f; float wind = 0.0f; int humidity = 0; int codeVal = 0;

    if (extractFloat(body, "\"temperature_2m\"", temp) &&
        extractFloat(body, "\"wind_speed_10m\"", wind) &&
        extractInt(body, "\"relative_humidity_2m\"", humidity) &&
        extractInt(body, "\"weather_code\"", codeVal)) {
      s_weather.temp = temp;
      s_weather.humidity = humidity;
      s_weather.windSpeed = wind;
      s_weather.condition = conditionFromCode(codeVal);
      s_weather.lastFetch = timesync::now();
    } else {
      s_weather.temp = 0.0f;
      s_weather.humidity = 0;
      s_weather.windSpeed = 0.0f;
      s_weather.condition = "Parse failed";
    }
  } else {
    s_weather.condition = "API error";
  }

  http.end();
  wifi::disconnect();
  markDirty();
}

void drawCurrentWeather(Adafruit_GFX& g) {
  g.setTextColor(GxEPD_BLACK);
  g.setTextSize(1);
  
  char title[64];
  snprintf(title, sizeof(title), "Weather");
  gui::printAt(g, 10, TOP + 10, title, 2);
  
  char tempStr[32];
  snprintf(tempStr, sizeof(tempStr), "%.1f°C", s_weather.temp);
  gui::printAt(g, 10, TOP + 40, tempStr, 3);
  
  char condStr[128];
  snprintf(condStr, sizeof(condStr), "%s | Humidity: %d%% | Wind: %.1f m/s",
           s_weather.condition, s_weather.humidity, s_weather.windSpeed);
  gui::printAt(g, 10, TOP + 80, condStr, 1);
  
  char lastSync[64];
  time_t tt = (time_t)s_weather.lastFetch;
  struct tm lt;
  localtime_r(&tt, &lt);
  snprintf(lastSync, sizeof(lastSync), "Last updated: %02d:%02d", 
           (int)lt.tm_hour, (int)lt.tm_min);
  gui::printAt(g, 10, TOP + 120, lastSync, 1);
  
  gui::drawButton(g, kBtn1, "Refresh", false);
  gui::drawButton(g, kBtn2, "Home", false);
}

class WeatherApp : public App {
 public:
  const char* id() const override { return "weather"; }
  const char* name() const override { return "Weather"; }
  
  void onEnter() override {
    s_screen = CURRENT;
    markDirty();
  }
  
  void onLeave() override {}
  
  void tick() override {}
  
  void background() override {}
  
  void handleInput(const InputEvent& e) override {
    if (e.type == InputEvent::TAP) {
      if (kBtn1.hit(e.x, e.y)) {
        fetchWeather();
        return;
      }
      if (kBtn2.hit(e.x, e.y)) {
        appmgr::goHome();
        return;
      }
      return;
    }

    if (e.type == InputEvent::KEY && e.key == 'R') {
      fetchWeather();
    } else if (e.type == InputEvent::KEY && (e.key == 'L' || e.key == 'q' || e.key == 'Q' || e.key == '\b')) {
      appmgr::goHome();
    }
  }
  
  void draw(Adafruit_GFX& g) override {
    if (s_screen == CURRENT) {
      drawCurrentWeather(g);
    }
  }
};

WeatherApp g_weatherApp;

}  // namespace

namespace weather_app {

App* get() { return &g_weatherApp; }

}  // namespace weather_app
