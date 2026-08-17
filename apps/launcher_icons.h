// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// launcher_icons.h — App icons for the home screen, created entirely using GFX primitives.
//
// Each icon is drawn in black and centred at (cx, cy), with a bounding box of ~38x38
// px (fits into a 108x46 tile with a border). No bitmap blobs: all glyphs
// consist of lines/circles/rectangles/triangles, so that they remain 1-bit sharp and
// do not require PROGMEM data. draw() returns false if there is no
// icon for the ID — the caller then falls back to the text label.
// =============================================================================
#pragma once

#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>   // GxEPD_BLACK / GxEPD_WHITE
#include "core/i18n.h"

namespace launcher_icons {

// 2-px line (thickened horizontally) — appears bolder on E-Ink than drawLine.
inline void line2(Adafruit_GFX& g, int x0, int y0, int x1, int y1) {
  g.drawLine(x0, y0, x1, y1, GxEPD_BLACK);
  g.drawLine(x0 + 1, y0, x1 + 1, y1, GxEPD_BLACK);
}
inline void hbar(Adafruit_GFX& g, int x, int y, int w) { g.fillRect(x, y, w, 2, GxEPD_BLACK); }
inline void vbar(Adafruit_GFX& g, int x, int y, int h) { g.fillRect(x, y, 2, h, GxEPD_BLACK); }

// --- Music: Eighth note -------------------------------------------------------
inline void music(Adafruit_GFX& g, int cx, int cy) {
  g.fillCircle(cx - 6, cy + 9, 5, GxEPD_BLACK);     // Notenkopf
  g.fillRect(cx - 2, cy - 13, 2, 22, GxEPD_BLACK);  // Hals
  line2(g, cx, cy - 13, cx + 7, cy - 7);            // Fähnchen
  line2(g, cx, cy - 8, cx + 7, cy - 2);
}

// --- Audiobook: Headphones ------------------------------------------------------
inline void headphones(Adafruit_GFX& g, int cx, int cy) {
  for (int r = 13; r <= 15; r++)
    g.drawCircleHelper(cx, cy + 2, r, 0x3, GxEPD_BLACK);  // Bügel (obere Hälfte)
  g.fillRoundRect(cx - 17, cy + 1, 7, 13, 2, GxEPD_BLACK);
  g.fillRoundRect(cx + 10, cy + 1, 7, 13, 2, GxEPD_BLACK);
}

// --- Reading: an open book --------------------------------------------
inline void book(Adafruit_GFX& g, int cx, int cy) {
  vbar(g, cx - 1, cy - 8, 20);                                  // Buchrücken
  g.drawLine(cx - 1, cy - 8, cx - 16, cy - 11, GxEPD_BLACK);    // linke Seite
  g.drawLine(cx - 16, cy - 11, cx - 16, cy + 8, GxEPD_BLACK);
  g.drawLine(cx - 16, cy + 8, cx - 1, cy + 11, GxEPD_BLACK);
  g.drawLine(cx + 1, cy - 8, cx + 16, cy - 11, GxEPD_BLACK);    // rechte Seite
  g.drawLine(cx + 16, cy - 11, cx + 16, cy + 8, GxEPD_BLACK);
  g.drawLine(cx + 16, cy + 8, cx + 1, cy + 11, GxEPD_BLACK);
  for (int i = 0; i < 3; i++) {                                 // Textzeilen
    g.drawFastHLine(cx - 13, cy - 4 + i * 5, 9, GxEPD_BLACK);
    g.drawFastHLine(cx + 5, cy - 4 + i * 5, 9, GxEPD_BLACK);
  }
}

// --- MeshCore: Nodes + Connections --------------------------------------------
static const unsigned char PROGMEM icon_meshcore_bits[] = {
  0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x20, 0x0E, 0x00, 0x00, 0x70,
  0x1E, 0x00, 0x00, 0x78, 0x1C, 0x00, 0x00, 0x38, 0x38, 0xC0, 0x03, 0x1C,
  0x39, 0xC0, 0x03, 0x9C, 0x71, 0xC0, 0x03, 0x8E, 0x73, 0x83, 0xC1, 0xCE,
  0x73, 0x87, 0xE1, 0xCE, 0x73, 0x8F, 0xF1, 0xCE, 0x73, 0x8F, 0xF1, 0xCE,
  0x73, 0x8F, 0xF1, 0xCE, 0x73, 0x8F, 0xF1, 0xCE, 0x73, 0x87, 0xE1, 0xCE,
  0x73, 0x87, 0xE1, 0xCE, 0x71, 0xC7, 0xE3, 0x8E, 0x39, 0xC7, 0xE3, 0x9C,
  0x38, 0xCF, 0xF3, 0x1C, 0x1C, 0x0E, 0x70, 0x38, 0x1C, 0x0E, 0x70, 0x38,
  0x0E, 0x1E, 0x78, 0x70, 0x0C, 0x1C, 0x38, 0x30, 0x00, 0x1C, 0x38, 0x00,
  0x00, 0x3C, 0x3C, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x3F, 0xFC, 0x00,
  0x00, 0x7F, 0xFE, 0x00, 0x00, 0x70, 0x0E, 0x00, 0x00, 0x70, 0x0E, 0x00,
  0x00, 0x70, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00,
};

inline void mesh(Adafruit_GFX& g, int cx, int cy) {
  g.drawBitmap(cx - 16, cy - 16, icon_meshcore_bits, 32, 32, GxEPD_BLACK);
}

// --- Settings: Spanner/Screwdriver -------------------------------------------------------
// --- Options: MDI ‘tools’ (F1064, pictogrammers.com/library/mdi/icon/tools)
// Sole exception to the ‘primitives only’ rule above: 32x32 1-bit bitmap instead of
// GFX shapes, to match the MDI glyph exactly. 128 B PROGMEM.
static const unsigned char PROGMEM icon_tools_bits[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0xE0, 0x00, 0x70, 0x00, 0xF0, 0x01, 0xF8, 0x00, 0x78, 0x07, 0xF8,
  0x00, 0x78, 0x07, 0xF8, 0x10, 0xF8, 0x07, 0xF0, 0x19, 0xF8, 0x07, 0xF0,
  0x1F, 0xF8, 0x0F, 0xE0, 0x1F, 0xFC, 0x1F, 0xE0, 0x0F, 0xFE, 0x1E, 0x00,
  0x07, 0xFF, 0x0C, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x1F, 0xC0, 0x00,
  0x00, 0x0F, 0xE0, 0x00, 0x00, 0x07, 0xF0, 0x00, 0x00, 0x03, 0xF8, 0x00,
  0x00, 0x21, 0xFC, 0x00, 0x00, 0x70, 0xFE, 0x00, 0x00, 0xF8, 0x7F, 0x00,
  0x01, 0xFC, 0x3F, 0x80, 0x03, 0xF8, 0x1F, 0xC0, 0x07, 0xF0, 0x0F, 0xE0,
  0x0F, 0xE0, 0x07, 0xF0, 0x1F, 0xC0, 0x03, 0xF8, 0x1F, 0x80, 0x01, 0xF8,
  0x1F, 0x00, 0x00, 0xF8, 0x0E, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

inline void gear(Adafruit_GFX& g, int cx, int cy) {
  g.drawBitmap(cx - 16, cy - 16, icon_tools_bits, 32, 32, GxEPD_BLACK);
}

// --- Podcast: equalizer bars -----------------------------------------------
static const unsigned char PROGMEM icon_podcast_bits[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x01, 0xfc, 0x00, 
	0x00, 0x07, 0xf8, 0x00, 0x00, 0x3f, 0xc0, 0x00, 0x00, 0xff, 0x00, 0x00, 0x03, 0xf8, 0x00, 0x00, 
	0x0f, 0xff, 0xff, 0xf0, 0x1f, 0xff, 0xff, 0xf8, 0x1f, 0xff, 0xff, 0xf8, 0x18, 0x00, 0x00, 0x18, 
	0x18, 0x00, 0x00, 0x18, 0x18, 0x00, 0x07, 0x18, 0x18, 0x00, 0x07, 0x18, 0x18, 0x00, 0x07, 0x18, 
	0x1f, 0xff, 0xff, 0xf8, 0x1f, 0xff, 0xff, 0xf8, 0x1f, 0xff, 0xff, 0xf8, 0x1e, 0x1f, 0xff, 0xf8, 
	0x1c, 0x0f, 0xff, 0xf8, 0x1c, 0x07, 0xff, 0xf8, 0x18, 0x07, 0xff, 0xf8, 0x1c, 0x07, 0xff, 0xf8, 
	0x1c, 0x07, 0xff, 0xf8, 0x1e, 0x0f, 0xff, 0xf8, 0x1f, 0xbf, 0xff, 0xf8, 0x1f, 0xff, 0xff, 0xf8, 
	0x1f, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

inline void radio(Adafruit_GFX& g, int cx, int cy) {
  g.drawBitmap(cx - 16, cy - 16, icon_podcast_bits, 32, 32, GxEPD_BLACK);
}

// --- Weather: cloud with precipitation -----------------------------------
static const unsigned char PROGMEM icon_weather_bits[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x02, 0x00, 0xf0, 0x00, 0x03, 0xe0, 0x70, 0x00, 
	0x03, 0x80, 0x10, 0x00, 0x02, 0x0f, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x00, 0x00, 0x7f, 0xe0, 0x00, 
	0x00, 0xf0, 0xf0, 0x00, 0x01, 0xe0, 0x79, 0x00, 0x01, 0xc0, 0x38, 0xc0, 0x01, 0xc0, 0x38, 0xe0, 
	0x11, 0x87, 0xf8, 0xc0, 0x31, 0xdf, 0xf8, 0x80, 0x31, 0xff, 0xfc, 0x00, 0x19, 0xfc, 0x3e, 0x00, 
	0x00, 0xf0, 0x0e, 0x00, 0x03, 0xf0, 0x0f, 0x00, 0x0f, 0xe0, 0x07, 0x00, 0x0f, 0xe0, 0x07, 0x00, 
	0x1e, 0x00, 0x07, 0xe0, 0x1c, 0x00, 0x07, 0xf8, 0x18, 0x00, 0x07, 0xf8, 0x1c, 0x06, 0x00, 0x38, 
	0x1c, 0x6e, 0x00, 0x38, 0x1e, 0x7d, 0x81, 0xf8, 0x0e, 0x3f, 0x99, 0xf8, 0x00, 0xff, 0x3d, 0xf0, 
	0x00, 0xfe, 0x3c, 0x00, 0x00, 0x1f, 0x3c, 0x00, 0x00, 0x1b, 0x3c, 0x00, 0x00, 0x18, 0x3c, 0x00
};

inline void weatherCloud(Adafruit_GFX& g, int cx, int cy) {
  g.drawBitmap(cx - 16, cy - 16, icon_weather_bits, 32, 32, GxEPD_BLACK);
}

// --- WiFi Monitor: signal arcs with gear ------------------------------------
static const unsigned char PROGMEM icon_wifi_bits[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x3f, 0xf8, 0x00, 0x01, 0xff, 0xff, 0x80, 0x07, 0xff, 0xff, 0xe0, 0x1f, 0xff, 0xff, 0xf8, 
	0x3f, 0xf0, 0x0f, 0xfc, 0x3f, 0x80, 0x01, 0xfc, 0x1e, 0x00, 0x00, 0x78, 0x18, 0x00, 0x00, 0x18, 
	0x00, 0x1f, 0xf8, 0x00, 0x00, 0x7f, 0xfe, 0x00, 0x01, 0xff, 0xff, 0x80, 0x03, 0xff, 0xff, 0xc0, 
	0x01, 0xf8, 0x1e, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 
	0x00, 0x07, 0xc1, 0xe0, 0x00, 0x1f, 0x8f, 0xfe, 0x00, 0x0f, 0x9f, 0xff, 0x00, 0x0f, 0x1f, 0xbf, 
	0x00, 0x07, 0x0f, 0x1e, 0x00, 0x03, 0x06, 0x1c, 0x00, 0x01, 0x0f, 0x1e, 0x00, 0x01, 0x1f, 0xff, 
	0x00, 0x00, 0x1f, 0xfe, 0x00, 0x00, 0x0f, 0xfe, 0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x00, 0xe0
};

inline void wifiSignal(Adafruit_GFX& g, int cx, int cy) {
  g.drawBitmap(cx - 16, cy - 16, icon_wifi_bits, 32, 32, GxEPD_BLACK);
}

// --- Games: Dice (5-sided) ---------------------------------------------
inline void dice(Adafruit_GFX& g, int cx, int cy) {
  g.drawRoundRect(cx - 14, cy - 14, 28, 28, 4, GxEPD_BLACK);
  const int p[5][2] = {{-7, -7}, {7, -7}, {0, 0}, {-7, 7}, {7, 7}};
  for (auto& d : p) g.fillCircle(cx + d[0], cy + d[1], 2, GxEPD_BLACK);
}

// --- Files: Folders ---------------------------------------------------------
inline void folder(Adafruit_GFX& g, int cx, int cy) {
  g.drawLine(cx - 15, cy - 7, cx - 15, cy - 12, GxEPD_BLACK);   // Reiter
  g.drawLine(cx - 15, cy - 12, cx - 6, cy - 12, GxEPD_BLACK);
  g.drawLine(cx - 6, cy - 12, cx - 3, cy - 7, GxEPD_BLACK);
  g.drawRect(cx - 15, cy - 7, 30, 18, GxEPD_BLACK);             // Korpus
}

// --- Notes: Sheet of paper with pen ------------------------------------------------
inline void notes(Adafruit_GFX& g, int cx, int cy) {
  g.drawRect(cx - 13, cy - 14, 20, 28, GxEPD_BLACK);            // Blatt
  for (int i = 0; i < 3; i++) g.drawFastHLine(cx - 9, cy - 7 + i * 6, 11, GxEPD_BLACK);
  line2(g, cx + 3, cy - 16, cx + 13, cy - 3);                  // Stift
  g.fillTriangle(cx + 11, cy - 5, cx + 15, cy - 2, cx + 11, cy + 1, GxEPD_BLACK);  // Spitze
}

// --- Alarm clock: a clock with bells -------------------------------------------------
inline void alarm(Adafruit_GFX& g, int cx, int cy) {
  g.fillCircle(cx - 10, cy - 10, 4, GxEPD_BLACK);   // Glocken
  g.fillCircle(cx + 10, cy - 10, 4, GxEPD_BLACK);
  g.drawCircle(cx, cy + 2, 12, GxEPD_BLACK);        // Zifferblatt
  g.drawCircle(cx, cy + 2, 11, GxEPD_BLACK);
  line2(g, cx, cy + 2, cx, cy - 5);                 // Zeiger
  line2(g, cx, cy + 2, cx + 5, cy + 4);
  g.drawLine(cx - 11, cy + 13, cx - 14, cy + 16, GxEPD_BLACK);  // Füße
  g.drawLine(cx + 11, cy + 13, cx + 14, cy + 16, GxEPD_BLACK);
}

// --- Maps: Location pin ----------------------------------------------------
inline void mapsPin(Adafruit_GFX& g, int cx, int cy) {
  g.fillCircle(cx, cy - 4, 9, GxEPD_BLACK);
  g.fillTriangle(cx - 8, cy + 1, cx + 8, cy + 1, cx, cy + 14, GxEPD_BLACK);
  g.fillCircle(cx, cy - 4, 4, GxEPD_WHITE);   // Loch
}

// --- Position: Gyroscope ----------------------------------------------------------
inline void gyro(Adafruit_GFX& g, int cx, int cy) {
  g.drawCircle(cx, cy, 14, GxEPD_BLACK);
  g.drawCircle(cx, cy, 13, GxEPD_BLACK);
  g.drawCircle(cx, cy, 6, GxEPD_BLACK);
  line2(g, cx - 13, cy + 8, cx + 13, cy - 8);  // gekippte Achse
  g.fillCircle(cx, cy, 2, GxEPD_BLACK);
}

// --- Calculator: four operators (2x2) -----------------------------------------
inline void math(Adafruit_GFX& g, int cx, int cy) {
  // + (oben links)
  hbar(g, cx - 12, cy - 8, 8); vbar(g, cx - 9, cy - 11, 8);
  // − (oben rechts)
  hbar(g, cx + 4, cy - 8, 8);
  // × (unten links)
  line2(g, cx - 12, cy + 4, cx - 5, cy + 11);
  line2(g, cx - 5, cy + 4, cx - 12, cy + 11);
  // ÷ (unten rechts)
  hbar(g, cx + 4, cy + 7, 8);
  g.fillCircle(cx + 8, cy + 4, 1, GxEPD_BLACK);
  g.fillCircle(cx + 8, cy + 11, 1, GxEPD_BLACK);
}

// --- Learning: stacked flashcards ----------------------------------------
inline void cards(Adafruit_GFX& g, int cx, int cy) {
  g.drawRoundRect(cx - 6, cy - 13, 20, 22, 3, GxEPD_BLACK);     // hintere Karte
  g.fillRoundRect(cx - 13, cy - 7, 20, 22, 3, GxEPD_WHITE);     // vordere (verdeckt)
  g.drawRoundRect(cx - 13, cy - 7, 20, 22, 3, GxEPD_BLACK);
  for (int i = 0; i < 2; i++) g.drawFastHLine(cx - 9, cy + i * 6, 12, GxEPD_BLACK);
}

// --- Calender ----------------------------------------------------------------
inline void calendar(Adafruit_GFX& g, int cx, int cy) {
  vbar(g, cx - 8, cy - 15, 5);                       // Ringe
  vbar(g, cx + 6, cy - 15, 5);
  g.drawRect(cx - 13, cy - 11, 26, 24, GxEPD_BLACK);
  g.fillRect(cx - 13, cy - 11, 26, 6, GxEPD_BLACK);  // Kopfleiste
  for (int r = 0; r < 2; r++)                         // Tagesraster
    for (int c = 0; c < 3; c++)
      g.fillRect(cx - 9 + c * 8, cy - 1 + r * 7, 4, 4, GxEPD_BLACK);
}

// --- Todo: Checkbox + Lists --------------------------------------------------
inline void todo(Adafruit_GFX& g, int cx, int cy) {
  g.drawRect(cx - 15, cy - 9, 13, 13, GxEPD_BLACK);  // Box
  line2(g, cx - 13, cy - 3, cx - 9, cy + 1);         // Haken
  line2(g, cx - 9, cy + 1, cx - 3, cy - 7);
  hbar(g, cx + 1, cy - 6, 14);                        // Listenzeilen
  hbar(g, cx + 1, cy + 1, 14);
}

// --- Node monitor: connected nodes ---------------------------------------------------
inline void nodeMonitor(Adafruit_GFX& g, int cx, int cy) {
  g.fillCircle(cx - 8, cy - 4, 3, GxEPD_BLACK);
  g.fillCircle(cx + 8, cy - 4, 3, GxEPD_BLACK);
  g.fillCircle(cx, cy + 8, 3, GxEPD_BLACK);
  g.drawLine(cx - 8, cy - 1, cx, cy + 5, GxEPD_BLACK);
  g.drawLine(cx + 8, cy - 1, cx, cy + 5, GxEPD_BLACK);
}


// Dispatch by Tile ID. false -> no icon; the caller draws a text label.
inline bool draw(Adafruit_GFX& g, i18n::Str id, int cx, int cy) {
  switch (id) {
    case i18n::Str::TileMusic:    music(g, cx, cy);          return true;
    case i18n::Str::TileBook:     headphones(g, cx, cy);     return true;
    case i18n::Str::TileReader:   book(g, cx, cy);           return true;
    case i18n::Str::TileMesh:     mesh(g, cx, cy);           return true;
    case i18n::Str::TileSettings: gear(g, cx, cy);           return true;
    case i18n::Str::TileGames:    dice(g, cx, cy);           return true;
    case i18n::Str::TileFiles:    folder(g, cx, cy);         return true;
    case i18n::Str::TileNotes:    notes(g, cx, cy);          return true;
    case i18n::Str::TileAlarm:    alarm(g, cx, cy);          return true;
    case i18n::Str::TileMaps:     mapsPin(g, cx, cy);        return true;
    case i18n::Str::TileGyro:     gyro(g, cx, cy);           return true;
    case i18n::Str::TileMath:     math(g, cx, cy);           return true;
    case i18n::Str::TileCards:    cards(g, cx, cy);          return true;
    case i18n::Str::TileCalendar: calendar(g, cx, cy);       return true;
    case i18n::Str::TileTodo:     todo(g, cx, cy);           return true;
    case i18n::Str::TilePodcast:  radio(g, cx, cy);          return true;
    case i18n::Str::TileWeather:  weatherCloud(g, cx, cy);   return true;
    case i18n::Str::TileWiFi:     wifiSignal(g, cx, cy);     return true;
    case i18n::Str::TileNodeMon:  nodeMonitor(g, cx, cy);    return true;
    default: return false;
  }
}

}  // namespace launcher_icons
