// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// wifi_monitor_app.h — WiFi signal strength and network monitor.
//
// Scans and displays visible WiFi networks, signal strength, channel info,
// and trends over time. Helps diagnose coverage and interference.
// =============================================================================
#pragma once

#include "core/appmgr.h"

namespace wifi_monitor_app {

App* get();

}  // namespace wifi_monitor_app
