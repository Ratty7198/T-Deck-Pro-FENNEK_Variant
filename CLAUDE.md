# CLAUDE.md — English Translation

## Project

**Fennek** — Multi-app handheld firmware for the **LilyGO T-Deck Pro V1.1** (ESP32-S3, 8 MB PSRAM), PlatformIO/Arduino. Remote: `danst0/fennek`. Version: `FENNEK_VERSION` in `src/config.h`, log prefix `[FENNEK]`, NVS namespace `fennek`, SD cache `/.fennek`. Apps: Music, Audiobook, Reading, Mesh, Games, Files, Notes, Alarms, Maps, Math Quiz, Flashcards, Calendar, Todo. Code/docs/serial output **in English**; text output via `gui::print` (UTF-8 → CP437).

- Active code in `src/`, only build environment is `fennek`. `lib/meshcore/` + `lib/ed25519/` = vendored MeshCore. Legacy Meck firmware in git history.
- `boards/t-deck_pro.json`, partition `default_16MB.csv` (6.5 MB app slots, 3.4 MB SPIFFS, 20 KB NVS).

## Build & Flash

```bash
pio run -e fennek              # build
pio run -e fennek -t upload    # flash (/dev/ttyACM0, 921600 baud)
pio device monitor -b 115200  # boot log + exception decoder
```

Boots completely **without SD card**.

Debug flags (`-D`): `AUDIO_DEBUG_GAP`, `PLAYLIST_SELFTEST`, `MESH_SMOKE_TEST`, `GAMES_SMOKE_TEST`, `SLEEP_WAKE_TEST`, `BATTLOG`.

## Toolchain — do not update

`espressif32@6.11.0` (Arduino-ESP32 2.0.17 / IDF 4.4), `ESP32-audioI2S#2.0.6`, RadioLib ^7.3 (`RADIOLIB_GODMODE=1`), rweather/Crypto, densaugeo/base64. Upgrades only on explicit request.

## Architecture — Anti-Stutter Invariants

**E-Ink, SD and LoRa-SX1262 share the same SPI bus** (HSPI, SCLK 36/MOSI 33/MISO 47).

```
Core 0:  Audio decode task (priority 4) ── audio.loop()
Core 1:  Arduino loop() ── appmgr ── g_spiMutex (core/board.h)
Core 1:  Mesh pump (mesh_app::background → spiLock)
```

1. **Every SPI access (SD, E-Ink, LoRa) under `spiLock()`/`spiUnlock()`.**
2. **`audio.*` calls only in audio task** — UI sends commands via queue (`services/audio.cpp`).
3. **SPI mutex release during E-Ink BUSY** (`setBusyCallback` in `core/display.cpp`).
4. **256 KB PSRAM read-ahead** (`audio.setBufsize(-1, 256*1024)`).
5. **SPI at 4 MHz** (`SPI_BUS_HZ`) — 8 MHz corrupts SD writes.
6. **LoRa CS (GPIO 3) idle HIGH**; radio uses existing `g_spi` instance (`P_LORA_SCLK` NOT defined).
7. **E-Ink refresh only on change** (dirty flag); progress/status line only as `renderRegion`.
8. **NVS for settings/bookmarks** (never SPI bus). SD for bulk caches + mesh data. Mesh identity on SPIFFS.
9. **WiFi active ⇒ `audio::stop()` + `mesh_client::setSuspended(true)`** (webfm rule — all WLAN services).

## Modules (`src/`)

- `config.h` — pins & constants.
- `core/board.*` — SPI bus, SD mount, `g_spiMutex`, `loraPower`, `gpsPower`. GPS (GPIO39) + DRV2605 (GPIO2) off at boot. No 4G modem (this variant = audio, PCM5102A DAC).
- `core/display.*` — GxEPD2 E-Ink, `render(fn,full)` / `renderRegion(fn,y,h)`, BUSY callback.
- `core/gui.*` — `toCp437`/`print`/`printAt`/`textBounds`, `drawButton`, `drawRowText`, `Rect`.
- `core/touch.*` + `core/hyn/` — CST328 (I2C 13/14). **hyn/ should not be refactored.**
- `core/keyboard.*` — TCA8418 (I2C 0x34), sticky shift/alt/sym, key repeat 400/150 ms.
- `core/battery.*` — BQ27220 fuel gauge (read-only).
- `core/settings.*` — NVS: volume, last app, resume positions, bookmarks. **WiFi profiles** (up to 20) as compact blob `wifis` (migration from legacy keys `wssid`/`wpass` = slot 0); `wifiCount/wifiSsidAt/wifiPassAt/wifiSet/wifiRemove`, slot 0 = legacy simple access (`wifiSsid`/`setWifiSsid`, ini export/webfm).
- `core/console.*` — USB debug console (`help`). Commands: `status`, `time`/`tz`, `mesh init`, `mesh eco`, `advert [flood]`, `public`/`join`/`chan`/`dm`, `pos`, `gps [scan|off]`, `wifi`, `alarm`, `ollama`, `podcast`, `ota`, `todo`, `cal`, `calibre`, `nav`, `scrobble`, `notes`, `rm`. Console input extends auto-standby (`power::noteActivity()`).
- `core/power.*` — button (short=key lock, long=standby), auto standby, deep sleep. **CPU clock governor:** base clock 80 MHz from `boostBegin()` (setup end); `boostLock()/boostUnlock()` (counter) → 240 MHz for audio decode, WiFi (centrally via WiFi events — services do nothing), chess AI, EPUB conversion, console commands. APB remains 80 MHz, SPI/UART/I2S untouched. **Two side buttons: TOP=GPIO0 (firmware), BOTTOM=RST (hardware reset — immediate, firmware sees nothing).** Timer wake: minimal path (banner only), every 12th wake full screen. Alarm wake: `handleTimerWake()` returns `false` → full boot. Safety nets (after 3 silent deep discharges 06-07/26): task WDT around minimal path + `enterStandby()`, lossy-wait loop timeout, `noteBoot()` crash loop brake (3 abnormal resets → emergency standby, button-wake only), low battery protection (≤5% → standby without hour wake), sleep diagnostics in RTC RAM (`logSleepDebug()` dumps phase + wake history to battlog).
- `core/appmgr.*` — app lifecycle, status bar, dirty coalescing, 30-s persistence. Tap status bar = home.
- `services/timesync.*` — no hardware RTC; canonical clock = ESP32 system time. Sources (priority): GPS > NTP > mesh adverts. Drift learning, pre-standby sync, timezone as POSIX TZ (NVS `tz`, default Europe/Berlin).
- `services/audio.*` — path-based queue (PSRAM staging), owner token (music/book/podcast), shuffle/repeat, `seekRel`, sleep timer.
- `services/scrobble.*` — Navidrome/Subsonic. WiFi ⊥ audio → batch before auto-standby. Queue → `/.fennek/scrobbles.tsv`. NVS `nscro/nurl/nuser/npass`.
- `services/notes_ai.*` — Ollama polish of past notes. Batch before auto-standby, max 20/run. Done list `/.fennek/notes_ai.done` (SHA256). NVS `oai/ourl/omod`.
- `services/ota.*` — OTA via GitHub releases API (`danst0/fennek`). Self-resolves 302 redirect. Flashes to inactive OTA slot. NVS `otau`. Release: `gh release create vX.Y.Z .pio/build/fennek/firmware.bin`.
- `services/podcast.*` — **DISABLED (v2.5.9)**. Code remains for reactivation. Subscriptions `/podcasts/feeds.txt`, only latest episode per feed.
- `services/library.*` — music `/music`, PSRAM blocks (no track limit), cache `/.fennek/tracks.bin`. `TRACK_PATH_LEN`=256 (longer skip).
- `services/id3.*` — minimal ID3v1/v2 reader (TIT2/TPE1/TALB).
- `services/gps.*` — u-blox MIA-M10Q, UART1 38400 baud. Parses GGA/RMC. UBX aiding (time + rough position) at startup. GPS only active while maps app is foreground.
- `services/maps_tiles.*` — 1-bit tiles `/maps/{z}/{x}/{y}.bin` (8192 B). PSRAM LRU cache (16 slots). `ensureViewport()` only from `tick()`.
- `services/textdoc.*` — streaming pagination, offset index cache `/.fennek/idx/`.
- `services/epubzip.h`/`epubproc.*` — EPUB→TXT, cache `/books/.epub_cache/`.
- `services/alarmclock.*` — 4 alarms (NVS `alarm`), snooze (RTC RAM). Alarm tone `/.fennek/alarm.wav`, backlight blink (GPIO42). Signal mode per alarm (sound/blink/both). Auto-acknowledge 5 min.
- `services/calendar.*` + `apps/ical_core.h` — iCal subscriptions, read-only. Feeds `/calendar/feeds.txt`. Conditional GET (ETag/304), streaming parser, window now−1…+56 days. RRULE DAILY/WEEKLY/MONTHLY. NVS toggle `calauto`.
- `services/reinschrift.*` + `apps/reinschrift_core.h` — todo sync via WebDAV (Nextcloud). Op queue `/.fennek/todo_ops.tsv` + ETag-based PUT with conflict merge (412→GET+merge). NVS `todourl/todousr/todopw/todopath`.
- `services/calibre_books.*` — e-book pull-sync from **Calibre-Web** via OPDS (calibre.dumke.me; NOT native content server). Bookshelf (shelf, default "Fennek") → `/opds/shelfindex` + `/opds/download/<id>/epub/` → `/books`. Append-only; manifest `/.fennek/calibre.tsv`. Basic auth (= Calibre-Web login). Auto-sync before standby at most every 6 h (podcast lesson), max 8 downloads/run. NVS `cburl/cbusr/cbpw/cbshelf/cbauto`. Console `calibre`.
- `services/wifi.*` — central WiFi connection. `connect(timeout)` enforces WiFi rule (audio stop + mesh suspend), scans and selects the **strongest known network in range** (profiles in `settings::wifi*`, up to 20; fallback to slot 0 = hidden SSID), `disconnect()` as counterpart, `pickBest()` for webfm (own state machine). Modem sleep off for download throughput. All services (timesync/scrobble/ota/podcast/calendar/calibre/notes_ai/reinschrift/webfm) use this helper instead of own `WiFi.begin()` sequence.
- `services/webfm.*` — WiFi file server (`http://fennek.local`), JSON API + OTA endpoints. SD access under spiLock, network I/O after spiUnlock.
- `apps/launcher.*` — 2 pages × 10 tiles. S1: music/audiobook/reading/mesh/options/games/files/notes/alarms/maps. S2: location/math/learning/calendar/todo.
- `apps/music_app.*` — artist/album/playlist browser + player.
- `apps/book_app.*` — audiobooks `/audiobooks`, NVS bookmarks, ±30 s.
- `apps/reader_app.*` — books `/books` (.txt/.epub), progress bar.
- `apps/mesh_client.*` — `MeshClient : BaseChatMesh`. SD persistence: `/meshcore/messages.log` (256-KB rotation), `/meshcore/contacts.bin`, `/meshcore/channels.txt`. SD writes deferred (5-s throttle, never from mesh callback).
- `apps/mesh_app.*` — channel/contact/DM screens. Radio init lazy on first entry.
- `apps/games_app.*` — 2048, Minesweeper, Chess (Negamax+AB, FreeRTOS task priority 1), Tic-Tac-Toe, Sudoku (iterative generator, recursion-free). Logic in Arduino-free `*_core.h`, host-tested.
- `apps/files_app.*` — WebFM start/stop, SSID/IP display.
- `apps/notes_app.*` — daily notes `/notes/YYYY-MM-DD.md`.
- `apps/alarms_app.*` — alarm UI, digit direct input, ring screen.
- `apps/maps_app.*` — maps + GPS marker. Tile load in `tick()`, blit in `draw()`. Follow at >24 px movement. Mesh position tracking (≥30 s & >10 m).
- `apps/mathquiz_app.*` — math quiz. CFO mode (SHARE/GROWTH/MARKUP/DISCOUNT/PCT/RULE72). Adaptive difficulty (skill counter, per mode persistent, NVS `mqlvl`). **Time target ~20 s/problem:** number ranges capped + estimation tolerance (`Problem.tol`/`toleranceFor`/`accept` in `mathquiz_core.h`) — for estimable problems (large products at medium level, money % , SHARE/GROWTH) a good approximation counts as correct ("Good estimate!", exact value shown); add/subtract/exact division/rule72 remain exact. Feedback shows seconds used.
- `apps/flashcards_app.*` — Leitner boxes 0–4, decks `/flashcards/*.txt`, progress `/flashcards/.progress/`.
- `tools/host_test_games.cpp` / `host_test_apps.cpp` — host tests of Arduino-free cores.
- `main.cpp` — init: `handleTimerWake()` → power → display → touch/KB/battery/settings → SD/library → audio → apps.

## Radio Parameters

Default **"EU/UK Narrow"**: **869.618 MHz, BW 62.5 kHz, SF 8, CR 4/5**, 22 dBm, DIO2 RF-switch, TCXO 2.4 V. Authoritative are NVS values (`settings::meshParams()`), settings app "options → radio". `initContacts()` **must** be called in `MeshClient::begin()` — else NULL `contacts` crash.

**RX eco mode** (NVS `mesheco`, default on, console `mesh eco on|off`): SX1262 duty-cycle RX instead of always RX (~2/3 RX current saved). Implementation in vendored wrapper (`RadioLibWrappers.*`/`CustomSX1262Wrapper.h`): `startRx()` virtual, TIMEOUT also on DIO1 (re-arm after false preamble), noise floor sampling in duty mode off (SPI probes wake the sleeping chip). Prerequisite: senders use ≥ our preamble length. No raw `s_radio->startReceive()` — always `s_radioDrv->restartRecv()`.

## Known Pitfalls (do not reintroduce)

1. **`initContacts()` in `MeshClient::begin()`** — missing → NULL crash on first advert.
2. **No `spiLock()` from code already under lock** — non-recursive mutex → complete freeze (loop, touch, KB, serial, no watchdog). Note: `MeshClient::begin()`, all mesh callbacks and `App::draw()` already run under spiLock.
3. **Clock sync: don't blindly ratchet forward.** Rules: forward <1 h, backward only after 3 adverts >1 h behind us. `s_clockConfident` bootstrap for first plausible jump post-boot. ESP32 system time survives deep sleep; `millis()` doesn't.
4. **Contact table full (MAX_CONTACTS=64):** `shouldOverwriteWhenFull() = true` — oldest non-favorite replaced.
5. **Auto-standby knows console:** `power::noteActivity()` in `console::handleLine()`.
6. **Standby tests on battery:** USB-JTAG DTR may interpret GPIO0 press as reset.
7. **Flood instead of zero-hop for mesh position:** zero-hop doesn't reach 1-hop bridges.
8. **GPS baud rate 38400** (MIA-M10Q) — at 9600 only garbage.
