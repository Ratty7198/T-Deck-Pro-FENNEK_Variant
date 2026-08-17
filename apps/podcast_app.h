// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

// =============================================================================
// podcast_app.h — "Podcast" (launcher tile, page 2): feed list + player.
//
// Subscriptions are stored in /podcasts/feeds.txt (editable via WebFM); for each feed, the
// latest episode is loaded and retained (services/podcast). Playback via the
// audio queue (Owner::Podcast), resume position via NVS bookmark as in book_app.
// =============================================================================
#pragma once

#include "core/appmgr.h"

namespace podcast_app {
App* get();
}
