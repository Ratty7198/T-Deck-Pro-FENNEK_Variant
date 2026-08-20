// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

#include "wifi_monitor_app.h"
#include "config.h"
#include "core/gui.h"
#include "core/i18n.h"
#include "core/appmgr.h"

#include <Arduino.h>
#include <WiFi.h>
#include <GxEPD2_BW.h>
#include <string.h>

namespace {

using gui::Rect;

constexpr int W = EINK_W;
constexpr int TOP = appmgr::CONTENT_Y;
constexpr int ROW_H = 20;
constexpr int MAX_NETWORKS = 10;
const Rect kBtn1 {6, 274, 72, 42};
const Rect kBtn2 {84, 274, 72, 42};

struct WiFiNetwork {
  char ssid[33];
  int8_t rssi;
  uint8_t channel;
  uint8_t security;
};

WiFiNetwork s_networks[MAX_NETWORKS];
int s_networkCount = 0;
uint32_t s_lastScan = 0;
int s_sel = 0;
enum Screen { LIST, DETAIL };
int s_screen = LIST;

void markDirty() { appmgr::markDirty(); }

void scanNetworks() {
  int n = WiFi.scanNetworks();
  s_networkCount = (n > MAX_NETWORKS) ? MAX_NETWORKS : n;
  
  for (int i = 0; i < s_networkCount; i++) {
    strncpy(s_networks[i].ssid, WiFi.SSID(i).c_str(), sizeof(s_networks[i].ssid) - 1);
    s_networks[i].rssi = WiFi.RSSI(i);
    s_networks[i].channel = WiFi.channel(i);
    s_networks[i].security = WiFi.encryptionType(i);
  }
  s_lastScan = millis();
  s_sel = 0;
  markDirty();
}

const char* securityType(uint8_t enc) {
  switch (enc) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "?";
  }
}

int8_t rssiToBars(int8_t rssi) {
  if (rssi >= -50) return 4;
  if (rssi >= -60) return 3;
  if (rssi >= -70) return 2;
  if (rssi >= -80) return 1;
  return 0;
}

void drawList(Adafruit_GFX& g) {
  g.setTextColor(GxEPD_BLACK);
  g.setTextSize(1);
  
  gui::printAt(g, 10, TOP + 10, "WiFi Networks", 2);
  
  if (s_networkCount == 0) {
    gui::printAt(g, 10, TOP + 50, "No networks found", 1);
  } else {
    int y = TOP + 40;
    for (int i = 0; i < s_networkCount && i < 8; i++) {
      if (i == s_sel) {
        g.fillRect(0, y - 2, W, ROW_H, GxEPD_BLACK);
        g.setTextColor(GxEPD_WHITE);
      }
      
      char line[80];
      int8_t bars = rssiToBars(s_networks[i].rssi);
      snprintf(line, sizeof(line), "[%d] %s (%d dBm, Ch:%d)",
               bars, s_networks[i].ssid, s_networks[i].rssi, s_networks[i].channel);
      gui::printAt(g, 6, y, line, 1);
      
      if (i == s_sel) g.setTextColor(GxEPD_BLACK);
      y += ROW_H;
    }
  }
  
  char scanInfo[64];
  snprintf(scanInfo, sizeof(scanInfo), "Networks: %d | Scan: %lu s ago",
           s_networkCount, (millis() - s_lastScan) / 1000);
  gui::printAt(g, 10, TOP + 220, scanInfo, 1);
  
  gui::drawButton(g, kBtn1, "Scan", false);
  gui::drawButton(g, kBtn2, "Home", false);
}

void drawDetail(Adafruit_GFX& g) {
  if (s_sel >= s_networkCount) return;
  
  g.setTextColor(GxEPD_BLACK);
  g.setTextSize(1);
  
  WiFiNetwork& net = s_networks[s_sel];
  
  gui::printAt(g, 10, TOP + 10, "Network Details", 2);
  
  char line1[80];
  snprintf(line1, sizeof(line1), "SSID: %s", net.ssid);
  gui::printAt(g, 10, TOP + 40, line1, 1);
  
  char line2[80];
  snprintf(line2, sizeof(line2), "Signal: %d dBm (%d bars)", net.rssi, rssiToBars(net.rssi));
  gui::printAt(g, 10, TOP + 60, line2, 1);
  
  char line3[80];
  snprintf(line3, sizeof(line3), "Channel: %d", net.channel);
  gui::printAt(g, 10, TOP + 80, line3, 1);
  
  char line4[80];
  snprintf(line4, sizeof(line4), "Security: %s", securityType(net.security));
  gui::printAt(g, 10, TOP + 100, line4, 1);
  
  gui::drawButton(g, kBtn1, "Back", false);
  gui::drawButton(g, kBtn2, "Home", false);
}

class WiFiMonitorApp : public App {
 public:
  const char* id() const override { return "wifimon"; }
  const char* name() const override { return "WiFi Monitor"; }
  
  void onEnter() override {
    s_screen = LIST;
    scanNetworks();
  }
  
  void onLeave() override {}
  
  void tick() override {}
  
  void background() override {}
  
  void handleInput(const InputEvent& e) override {
    if (e.type == InputEvent::TAP) {
      if (s_screen == LIST && kBtn1.hit(e.x, e.y)) {
        scanNetworks();
        return;
      }
      if (s_screen == LIST && kBtn2.hit(e.x, e.y)) {
        appmgr::goHome();
        return;
      }
      if (s_screen == DETAIL && kBtn1.hit(e.x, e.y)) {
        s_screen = LIST;
        markDirty();
        return;
      }
      if (s_screen == DETAIL && kBtn2.hit(e.x, e.y)) {
        appmgr::goHome();
        return;
      }
      return;
    }

    if (e.type == InputEvent::KEY) {
      if (e.key == 'U' && s_screen == LIST && s_sel > 0) {
        s_sel--;
        markDirty();
      } else if (e.key == 'D' && s_screen == LIST && s_sel < s_networkCount - 1) {
        s_sel++;
        markDirty();
      } else if (e.key == 'R' && s_screen == LIST) {
        scanNetworks();
      } else if (e.key == 'R' && s_screen == DETAIL) {
        s_screen = LIST;
        markDirty();
      } else if (e.key == '\r' && s_screen == LIST && s_networkCount > 0) {
        s_screen = DETAIL;
        markDirty();
      } else if (e.key == 'L' || e.key == 'q' || e.key == 'Q' || e.key == '\b') {
        appmgr::goHome();
      }
    }
  }
  
  void draw(Adafruit_GFX& g) override {
    if (s_screen == LIST) {
      drawList(g);
    } else {
      drawDetail(g);
    }
  }
};

WiFiMonitorApp g_wifiMonitorApp;

}  // namespace

namespace wifi_monitor_app {

App* get() { return &g_wifiMonitorApp; }

}  // namespace wifi_monitor_app
