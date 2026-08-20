// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

#include "files_app.h"
#include "core/gui.h"
#include "core/i18n.h"
#include "core/settings.h"
#include "services/webfm.h"
#include "core/board.h"
#include "core/power.h"
#include "services/audio.h"
#include "apps/reader_app.h"

#include <Arduino.h>
#include <GxEPD2_BW.h>   // GxEPD_BLACK / GxEPD_WHITE
#include <stdio.h>
#include <string.h>
#include <SD.h>

namespace {

using gui::Rect;

enum Screen {
  FILE_LIST,
  WIFI_SERVER
};

Screen s_screen = FILE_LIST;
char   s_curDir[192] = "/";

struct FileEntry {
  char name[64];
  uint32_t size;
  bool isDir;
};

constexpr int kMaxEntries = 256;
FileEntry* s_entries = nullptr;
int s_entryCount = -1;
int s_sel = 0;
int s_off = 0;
bool s_confirmDelete = false;

// Layout
constexpr int HEADER_Y = 32;
constexpr int LIST_Y   = 56;
constexpr int ROW_H    = 24;
constexpr int VISIBLE  = 8;

// Soft Buttons
constexpr Rect kBtnBack   = {6, 280, 72, 32};
constexpr Rect kBtnWlan   = {84, 280, 72, 32};
constexpr Rect kBtnDelete = {162, 280, 72, 32};

// WiFi Server buttons
constexpr Rect kBtnWlanStart = {70, 150, 100, 40};
constexpr Rect kBtnWlanBack  = {6, 280, 72, 32};

// Last drawn state for WebFM tick
webfm::State s_shownState = webfm::State::OFF;
uint32_t     s_shownReqs = 0;
uint32_t     s_lastReqDraw = 0;

bool hasCreds() {
  char ssid[33];
  settings::wifiSsid(ssid, sizeof(ssid));
  return ssid[0] != '\0';
}

void centered(Adafruit_GFX& g, int y, const char* text, uint8_t size) {
  g.setTextSize(size);
  uint16_t w, h;
  gui::textBounds(g, text, &w, &h);
  gui::printAt(g, (240 - (int)w) / 2, y, text, size);
}

int cmpEntry(const void* x, const void* y) {
  const FileEntry* a = (const FileEntry*)x;
  const FileEntry* b = (const FileEntry*)y;
  if (a->isDir != b->isDir) {
    return a->isDir ? -1 : 1;
  }
  return strcasecmp(a->name, b->name);
}

void scanDir() {
  if (!s_entries) {
    s_entries = (FileEntry*)heap_caps_malloc(sizeof(FileEntry) * kMaxEntries, MALLOC_CAP_SPIRAM);
    if (!s_entries) s_entries = (FileEntry*)malloc(sizeof(FileEntry) * kMaxEntries);
  }
  s_entryCount = 0;
  s_sel = 0;
  s_off = 0;
  s_confirmDelete = false;
  if (!s_entries || !board::sdReady()) return;

  spiLock();
  File d = SD.open(s_curDir);
  if (d && d.isDirectory()) {
    File f;
    while ((f = d.openNextFile()) && s_entryCount < kMaxEntries) {
      FileEntry& e = s_entries[s_entryCount];
      strncpy(e.name, f.name(), sizeof(e.name) - 1);
      e.name[sizeof(e.name) - 1] = '\0';
      e.size = f.size();
      e.isDir = f.isDirectory();
      s_entryCount++;
      f.close();
    }
  }
  if (d) d.close();
  spiUnlock();

  qsort(s_entries, s_entryCount, sizeof(FileEntry), cmpEntry);
}

void goUp() {
  if (strcmp(s_curDir, "/") == 0) return;
  char* lastSlash = strrchr(s_curDir, '/');
  if (lastSlash == s_curDir) {
    strcpy(s_curDir, "/");
  } else if (lastSlash) {
    *lastSlash = '\0';
  }
  scanDir();
  appmgr::markDirty();
}

void enterDir(const char* name) {
  char temp[192];
  if (strcmp(s_curDir, "/") == 0) {
    snprintf(temp, sizeof(temp), "/%s", name);
  } else {
    snprintf(temp, sizeof(temp), "%s/%s", s_curDir, name);
  }
  strncpy(s_curDir, temp, sizeof(s_curDir) - 1);
  s_curDir[sizeof(s_curDir) - 1] = '\0';
  scanDir();
  appmgr::markDirty();
}

bool hasExt(const char* name, const char* ext) {
  size_t nl = strlen(name);
  size_t el = strlen(ext);
  if (nl < el) return false;
  return strcasecmp(name + nl - el, ext) == 0;
}

void openFile(const FileEntry& e) {
  char fullPath[192];
  if (strcmp(s_curDir, "/") == 0) {
    snprintf(fullPath, sizeof(fullPath), "/%s", e.name);
  } else {
    snprintf(fullPath, sizeof(fullPath), "%s/%s", s_curDir, e.name);
  }

  if (hasExt(e.name, ".txt") || hasExt(e.name, ".epub")) {
    reader_app::openFile(fullPath);
    appmgr::launch(reader_app::get());
  } else if (hasExt(e.name, ".mp3") || hasExt(e.name, ".wav")) {
    audio::stop();
    audio::queueBegin(audio::Owner::Book);
    audio::queueAdd(fullPath);
    audio::queueCommit(0, 0);
    Serial.printf("[FILES] Playing audio: %s\n", fullPath);
  }
}

bool localRmRecursive(const char* path) {
  File f = SD.open(path);
  if (!f) return false;
  bool isDir = f.isDirectory();
  f.close();
  if (!isDir) return SD.remove(path);

  for (;;) {
    File d = SD.open(path);
    File c = d.openNextFile();
    if (!c) { d.close(); break; }
    char cp[192];
    const char* sep = path[strlen(path) - 1] == '/' ? "" : "/";
    snprintf(cp, sizeof(cp), "%s%s%s", path, sep, c.name());
    bool childDir = c.isDirectory();
    c.close();
    d.close();
    if (!(childDir ? localRmRecursive(cp) : SD.remove(cp))) return false;
  }
  return SD.rmdir(path);
}

void deleteSelected() {
  if (s_entryCount <= 0 || s_sel < 0 || s_sel >= s_entryCount) return;
  const FileEntry& e = s_entries[s_sel];
  char fullPath[192];
  if (strcmp(s_curDir, "/") == 0) {
    snprintf(fullPath, sizeof(fullPath), "/%s", e.name);
  } else {
    snprintf(fullPath, sizeof(fullPath), "%s/%s", s_curDir, e.name);
  }

  spiLock();
  bool ok = localRmRecursive(fullPath);
  spiUnlock();

  s_confirmDelete = false;
  if (ok) {
    Serial.printf("[FILES] Deleted: %s\n", fullPath);
    scanDir();
  } else {
    Serial.printf("[FILES] Delete failed: %s\n", fullPath);
  }
  appmgr::markDirty();
}

void moveSel(int delta) {
  if (s_entryCount <= 0) return;
  int ns = s_sel + delta;
  if (ns < 0) ns = s_entryCount - 1;
  if (ns >= s_entryCount) ns = 0;
  s_sel = ns;
  if (s_sel < s_off) s_off = s_sel;
  if (s_sel >= s_off + VISIBLE) s_off = s_sel - VISIBLE + 1;
  appmgr::markDirty();
}

// --- WLAN WebFM View Drawers -------------------------------------------------

void drawNoCreds(Adafruit_GFX& g) {
  centered(g, 90, i18n::tr(i18n::Str::FilesNoCreds1), 1);
  centered(g, 112, i18n::tr(i18n::Str::FilesNoCreds2), 1);
  gui::printAt(g, 40, 128, "WLAN-SSID / WLAN-Password", 1);
  centered(g, 152, i18n::tr(i18n::Str::FilesNoCreds3), 1);
  gui::printAt(g, 40, 168, "wifi ssid <AP Name>", 1);
  gui::printAt(g, 40, 184, "wifi pass <Password>", 1);
}

void drawIdle(Adafruit_GFX& g) {
  char ssid[33], line[48];
  webfm::ssid(ssid, sizeof(ssid));
  snprintf(line, sizeof(line), i18n::tr(i18n::Str::FmtFilesSsid), ssid);
  centered(g, 100, line, 1);
  gui::drawButton(g, kBtnWlanStart, i18n::tr(i18n::Str::BtnStart), true);
  centered(g, 210, i18n::tr(i18n::Str::FilesStartHint), 1);
}

void drawConnecting(Adafruit_GFX& g) {
  char ssid[33], line[48];
  webfm::ssid(ssid, sizeof(ssid));
  snprintf(line, sizeof(line), i18n::tr(i18n::Str::FmtFilesSsid), ssid);
  centered(g, 100, line, 1);
  centered(g, 140, i18n::tr(i18n::Str::FilesConnecting), 2);
}

void drawRunning(Adafruit_GFX& g) {
  char ip[16], line[48];
  webfm::ipStr(ip, sizeof(ip));
  centered(g, 90, i18n::tr(i18n::Str::FilesBrowserHint), 1);
  snprintf(line, sizeof(line), "http://%s/", ip);
  centered(g, 110, line, 2);
  centered(g, 140, "http://jarvis.local/", 1);
  snprintf(line, sizeof(line), i18n::tr(i18n::Str::FmtRequests),
           (unsigned long)webfm::requestCount());
  centered(g, 190, line, 1);
  centered(g, 230, i18n::tr(i18n::Str::FilesStopHint), 1);
}

void drawFailed(Adafruit_GFX& g) {
  centered(g, 110, i18n::tr(i18n::Str::FilesFailed), 1);
  centered(g, 140, i18n::tr(i18n::Str::FilesRetryHint), 1);
}

// --- App Implementation ------------------------------------------------------

class FilesApp : public App {
 public:
  const char* id()   const override { return "Files"; }
  const char* name() const override { return i18n::tr(i18n::Str::AppFiles); }

  void onEnter() override {
    s_screen = FILE_LIST;
    s_shownState = webfm::state();
    s_shownReqs = webfm::requestCount();
    strcpy(s_curDir, "/");
    scanDir();
  }

  void onLeave() override {
    webfm::stop();
    if (s_entries) {
      free(s_entries);
      s_entries = nullptr;
    }
  }

  void handleInput(const InputEvent& e) override {
    if (s_screen == FILE_LIST) {
      if (e.type == InputEvent::TAP) {
        if (!board::sdReady()) {
          if (kBtnBack.hit(e.x, e.y)) {
            appmgr::goHome();
          } else if (kBtnWlan.hit(e.x, e.y)) {
            s_screen = WIFI_SERVER;
            appmgr::markDirty();
          } else if (kBtnDelete.hit(e.x, e.y)) {
            if (board::initSD()) scanDir();
            appmgr::markDirty();
          }
          return;
        }

        if (s_confirmDelete) {
          int boxW = 200, boxH = 90;
          int boxX = (240 - boxW) / 2;
          int boxY = (320 - boxH) / 2;
          if (e.x >= boxX && e.x <= boxX + boxW && e.y >= boxY && e.y <= boxY + boxH) {
            if (e.y > boxY + 50) {
              if (e.x < 120) {
                deleteSelected();
              } else {
                s_confirmDelete = false;
                appmgr::markDirty();
              }
            }
          } else {
            s_confirmDelete = false;
            appmgr::markDirty();
          }
          return;
        }

        if (kBtnBack.hit(e.x, e.y)) {
          if (strcmp(s_curDir, "/") == 0) {
            appmgr::goHome();
          } else {
            goUp();
          }
          return;
        }
        if (kBtnWlan.hit(e.x, e.y)) {
          s_screen = WIFI_SERVER;
          appmgr::markDirty();
          return;
        }
        if (kBtnDelete.hit(e.x, e.y)) {
          if (s_entryCount > 0 && s_sel >= 0 && s_sel < s_entryCount) {
            s_confirmDelete = true;
            appmgr::markDirty();
          }
          return;
        }

        if (e.y >= LIST_Y && e.y < LIST_Y + VISIBLE * ROW_H) {
          int r = (e.y - LIST_Y) / ROW_H;
          if (s_off + r < s_entryCount) {
            s_sel = s_off + r;
            const FileEntry& entry = s_entries[s_sel];
            if (entry.isDir) {
              enterDir(entry.name);
            } else {
              openFile(entry);
            }
          }
        }
      } else {
        // Keyboard inputs
        if (!board::sdReady()) {
          if (e.key == 'v' || e.key == 'V') {
            s_screen = WIFI_SERVER;
            appmgr::markDirty();
          } else if (e.key == 'd' || e.key == 'D' || e.key == '\r') {
            if (board::initSD()) scanDir();
            appmgr::markDirty();
          } else if (e.key == 'q' || e.key == 'Q' || e.key == '\b') {
            appmgr::goHome();
          }
          return;
        }

        if (s_confirmDelete) {
          if (e.key == '\r') {
            deleteSelected();
          } else if (e.key == '\b' || e.key == 'q' || e.key == 'Q') {
            s_confirmDelete = false;
            appmgr::markDirty();
          }
          return;
        }

        switch (e.key) {
          case 'w': case 'W': case 'i': case 'I': moveSel(-1); break;
          case 's': case 'S': case 'k': case 'K': moveSel(+1); break;
          case '\r':
            if (s_entryCount > 0 && s_sel >= 0 && s_sel < s_entryCount) {
              const FileEntry& entry = s_entries[s_sel];
              if (entry.isDir) {
                enterDir(entry.name);
              } else {
                openFile(entry);
              }
            }
            break;
          case '\b':
            if (strcmp(s_curDir, "/") != 0) {
              goUp();
            } else {
              appmgr::goHome();
            }
            break;
          case 'q': case 'Q':
            appmgr::goHome();
            break;
          case 'd': case 'D':
            if (s_entryCount > 0 && s_sel >= 0 && s_sel < s_entryCount) {
              s_confirmDelete = true;
              appmgr::markDirty();
            }
            break;
          case 'v': case 'V':
            s_screen = WIFI_SERVER;
            appmgr::markDirty();
            break;
          default: break;
        }
      }
    } else {
      // WIFI_SERVER screen inputs
      if (e.type == InputEvent::TAP && kBtnWlanBack.hit(e.x, e.y)) {
        if (webfm::state() != webfm::State::OFF) {
          webfm::stop();
        }
        s_screen = FILE_LIST;
        scanDir();
        appmgr::markDirty();
        return;
      }
      bool activate = false;
      if (e.type == InputEvent::TAP) {
        activate = (webfm::state() == webfm::State::OFF) ? kBtnWlanStart.hit(e.x, e.y)
                                                         : true;
      } else if (e.key == '\r') {
        activate = true;
      } else if (e.key == 'q' || e.key == 'Q' || e.key == '\b') {
        if (webfm::state() != webfm::State::OFF) {
          webfm::stop();
          appmgr::markDirty();
        } else {
          s_screen = FILE_LIST;
          scanDir();
          appmgr::markDirty();
        }
        return;
      }
      if (!activate) return;

      webfm::State st = webfm::state();
      if ((st == webfm::State::OFF || st == webfm::State::FAILED) && hasCreds()) {
        webfm::start();
        appmgr::markDirty();
      }
    }
  }

  void tick() override {
    if (s_screen == FILE_LIST) return;
    if (appmgr::isDirty()) return;
    webfm::State st = webfm::state();
    if (st != s_shownState) {
      s_shownState = st;
      appmgr::markDirty();
      return;
    }
    if (st == webfm::State::RUNNING && webfm::requestCount() != s_shownReqs &&
        millis() - s_lastReqDraw > 50000) {
      appmgr::markDirty();
    }
  }

  void draw(Adafruit_GFX& g) override {
    if (s_screen == FILE_LIST) {
      g.setTextColor(GxEPD_BLACK);
      g.setTextSize(2);
      centered(g, HEADER_Y - 24, i18n::tr(i18n::Str::AppFiles), 2);
      
      char pathBar[128];
      snprintf(pathBar, sizeof(pathBar), "Dir: %s", s_curDir);
      g.setTextSize(1);
      gui::printAt(g, 6, HEADER_Y + 4, pathBar, 1);
      g.drawFastHLine(0, HEADER_Y + 16, 240, GxEPD_BLACK);

      if (!board::sdReady()) {
        centered(g, 120, "No SD Card", 2);
        gui::drawButton(g, kBtnBack, "Home", false);
        gui::drawButton(g, kBtnWlan, "WLAN", false);
        gui::drawButton(g, kBtnDelete, "Retry SD", false);
        return;
      }

      if (s_entryCount == 0) {
        centered(g, 120, "(empty folder)", 1);
      } else {
        for (int r = 0; r < VISIBLE && s_off + r < s_entryCount; r++) {
          int idx = s_off + r;
          const FileEntry& e = s_entries[idx];
          
          char rowText[80];
          if (e.isDir) {
            snprintf(rowText, sizeof(rowText), "> %s", e.name);
          } else {
            char szStr[16];
            if (e.size >= 1048576) {
              snprintf(szStr, sizeof(szStr), "%.1fM", (float)e.size / 1048576.0);
            } else if (e.size >= 1024) {
              snprintf(szStr, sizeof(szStr), "%.1fK", (float)e.size / 1024.0);
            } else {
              snprintf(szStr, sizeof(szStr), "%uB", e.size);
            }
            snprintf(rowText, sizeof(rowText), "%s (%s)", e.name, szStr);
          }
          
          gui::drawRowText(g, LIST_Y + r * ROW_H, ROW_H, rowText, false);
        }
        
        int cr = s_sel - s_off;
        if (cr >= 0 && cr < VISIBLE) {
          int y = LIST_Y + cr * ROW_H;
          g.drawRect(0, y, 240, ROW_H, GxEPD_BLACK);
          g.drawRect(1, y + 1, 238, ROW_H - 2, GxEPD_BLACK);
        }
      }

      gui::drawButton(g, kBtnBack, (strcmp(s_curDir, "/") == 0) ? "Home" : "Up", false);
      gui::drawButton(g, kBtnWlan, "WLAN", false);
      if (s_entryCount > 0) {
        gui::drawButton(g, kBtnDelete, "Delete", false);
      }

      if (s_confirmDelete && s_sel >= 0 && s_sel < s_entryCount) {
        int boxW = 200, boxH = 90;
        int boxX = (240 - boxW) / 2;
        int boxY = (320 - boxH) / 2;
        
        g.fillRect(boxX, boxY, boxW, boxH, GxEPD_WHITE);
        g.drawRect(boxX, boxY, boxW, boxH, GxEPD_BLACK);
        g.drawRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, GxEPD_BLACK);
        
        centered(g, boxY + 12, "Confirm Delete?", 1);
        
        char nameTrunc[24];
        strncpy(nameTrunc, s_entries[s_sel].name, sizeof(nameTrunc) - 1);
        nameTrunc[sizeof(nameTrunc) - 1] = '\0';
        centered(g, boxY + 32, nameTrunc, 1);
        
        centered(g, boxY + 54, "Enter: Yes", 1);
        centered(g, boxY + 70, "Backspace: Cancel", 1);
      }
    } else {
      // WIFI_SERVER screen drawing
      g.setTextColor(GxEPD_BLACK);
      centered(g, 44, i18n::tr(i18n::Str::FilesTitle), 2);
      webfm::State st = webfm::state();
      s_shownState = st;
      s_shownReqs = webfm::requestCount();
      s_lastReqDraw = millis();
      switch (st) {
        case webfm::State::OFF:
          if (hasCreds()) drawIdle(g);
          else            drawNoCreds(g);
          break;
        case webfm::State::CONNECTING: drawConnecting(g); break;
        case webfm::State::RUNNING:    drawRunning(g); break;
        case webfm::State::FAILED:     drawFailed(g); break;
      }
      gui::drawButton(g, kBtnWlanBack, i18n::tr(i18n::Str::BtnBackShort), false);
    }
  }
};

FilesApp s_app;

}  // namespace

namespace files_app {

App* get() { return &s_app; }

}  // namespace files_app
