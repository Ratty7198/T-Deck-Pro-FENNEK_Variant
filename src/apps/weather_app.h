// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// weather_app.h — Weather forecast and current conditions via Open-Meteo.
//
// Fetches weather data via WiFi (Open-Meteo free API), caches to SD,
// displays current temperature, conditions, and forecast on e-ink.
// =============================================================================
#pragma once

#include "core/appmgr.h"

namespace weather_app {

App* get();

}  // namespace weather_app
