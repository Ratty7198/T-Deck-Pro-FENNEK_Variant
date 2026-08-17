// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// bootsplash.h — einmaliger Vollbild-Begrüßungsscreen beim Kaltstart.
//
// Zeigt Badge + Gerätename + Frequenz für draw()-Aufrufer via display::render().
// Reiner Zeichen-Code, kein State — nichts zu initialisieren.
// =============================================================================
#pragma once

#include <Adafruit_GFX.h>

namespace bootsplash {

// Zeichnet den kompletten Begrüßungsscreen (Badge, Titel, Tagline, Frequenz)
// auf die übergebene Fläche. Aufrufer ist verantwortlich für den Refresh
// (display::render(bootsplash::draw, /*full=*/true)).
void draw(Adafruit_GFX& g);

}  // namespace bootsplash