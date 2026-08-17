// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

#include "node_monitor_app.h"
#include "config.h"
#include "core/gui.h"
#include "core/i18n.h"
#include "core/appmgr.h"
#include "apps/mesh_client.h"

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <string.h>

namespace {

using gui::Rect;

constexpr int W = EINK_W;
constexpr int TOP = appmgr::CONTENT_Y;
constexpr int ROW_H = 22;
constexpr int MAX_SHOWN = 8;
const Rect kBtn1 {6, 274, 72, 42};
const Rect kBtn2 {84, 274, 72, 42};

enum Screen { LIST, DETAIL };
int s_screen = LIST;
int s_sel = 0;

void markDirty() { appmgr::markDirty(); }

int8_t rssiToBars(int8_t rssi) {
  if (rssi >= -80) return 4;
  if (rssi >= -100) return 3;
  if (rssi >= -120) return 2;
  if (rssi >= -140) return 1;
  return 0;
}

void drawList(Adafruit_GFX& g) {
  g.setTextColor(GxEPD_BLACK);
  g.setTextSize(1);
  
  gui::printAt(g, 10, TOP + 10, "Mesh Nodes", 2);
  
  int contactCnt = mesh_client::contactCount();
  if (contactCnt == 0) {
    gui::printAt(g, 10, TOP + 50, "No nodes in range", 1);
  } else {
    int y = TOP + 40;
    int shown = 0;
    for (int i = 0; i < contactCnt && shown < MAX_SHOWN; i++) {
      char name[48] = "";
      int8_t rssi = 0;
      // Note: in production, would fetch actual node data from mesh_client
      // For now, placeholder structure
      
      if (shown == s_sel) {
        g.fillRect(0, y - 2, W, ROW_H, GxEPD_BLACK);
        g.setTextColor(GxEPD_WHITE);
      }
      
      char line[80];
      snprintf(line, sizeof(line), "[%d] Node %d | %d dBm",
               rssiToBars(rssi), i, rssi);
      gui::printAt(g, 6, y, line, 1);
      
      if (shown == s_sel) g.setTextColor(GxEPD_BLACK);
      y += ROW_H;
      shown++;
    }
  }
  
  char info[64];
  snprintf(info, sizeof(info), "Total contacts: %d", contactCnt);
  gui::printAt(g, 10, TOP + 220, info, 1);
  
  gui::drawButton(g, kBtn1, "Refresh", false);
  gui::drawButton(g, kBtn2, "Home", false);
}

void drawDetail(Adafruit_GFX& g) {
  g.setTextColor(GxEPD_BLACK);
  g.setTextSize(1);
  
  gui::printAt(g, 10, TOP + 10, "Node Details", 2);
  
  char line1[80];
  snprintf(line1, sizeof(line1), "Node ID: %d", s_sel);
  gui::printAt(g, 10, TOP + 40, line1, 1);
  
  char line2[80];
  snprintf(line2, sizeof(line2), "Signal: %d dBm", -100 + (s_sel * 5));
  gui::printAt(g, 10, TOP + 60, line2, 1);
  
  char line3[80];
  snprintf(line3, sizeof(line3), "Status: Active");
  gui::printAt(g, 10, TOP + 80, line3, 1);
  
  gui::drawButton(g, kBtn1, "Back", false);
  gui::drawButton(g, kBtn2, "Home", false);
}

class NodeMonitorApp : public App {
 public:
  const char* id() const override { return "nodemon"; }
  const char* name() const override { return "Node Monitor"; }
  
  void onEnter() override {
    s_screen = LIST;
    s_sel = 0;
    markDirty();
  }
  
  void onLeave() override {}
  
  void tick() override {}
  
  void background() override {}
  
  void handleInput(const InputEvent& e) override {
    if (e.type == InputEvent::TAP) {
      if (s_screen == LIST && kBtn1.hit(e.x, e.y)) {
        markDirty();
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
      } else if (e.key == 'D' && s_screen == LIST) {
        s_sel++;
        markDirty();
      } else if (e.key == 'R' && s_screen == LIST) {
        markDirty();  // Refresh
      } else if (e.key == 'R' && s_screen == DETAIL) {
        s_screen = LIST;
        markDirty();
      } else if (e.key == '\r' && s_screen == LIST) {
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

NodeMonitorApp g_nodeMonitorApp;

}  // namespace

namespace node_monitor_app {

App* get() { return &g_nodeMonitorApp; }

}  // namespace node_monitor_app
