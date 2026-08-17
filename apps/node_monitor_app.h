// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// node_monitor_app.h — Mesh node monitor and signal strength tracker.
//
// Displays nearby mesh nodes with signal strength, position (if available),
// and connectivity status. Logs node activity to SD for range analysis.
// =============================================================================
#pragma once

#include "core/appmgr.h"

namespace node_monitor_app {

App* get();

}  // namespace node_monitor_app
