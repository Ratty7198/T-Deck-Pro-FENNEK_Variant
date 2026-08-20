// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

#include "mesh_client.h"
#include "config.h"
#include "core/board.h"
#include "core/battery.h"
#include "core/settings.h"

#include "services/battlog.h"
#include "services/timesync.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include <Mesh.h>
#include <SHA256.h>     // rweather/Crypto (Hashtag-Channel-PSK)
#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/IdentityStore.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/AdvertDataHelpers.h>   // AdvertDataBuilder (Self-Advert mit Telemetrie)
#include <helpers/radiolib/CustomSX1262.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>

namespace {

// --- Timing constants (from simple_secure_chat) --------------------------------
constexpr uint32_t SEND_TIMEOUT_BASE_MILLIS        = 500;
constexpr float    FLOOD_SEND_TIMEOUT_FACTOR       = 16.0f;
constexpr float    DIRECT_SEND_PERHOP_FACTOR       = 6.0f;
constexpr uint32_t DIRECT_SEND_PERHOP_EXTRA_MILLIS = 250;

// Andy's public channel (MeshCore default).
constexpr const char* PUBLIC_GROUP_PSK = "izOH6cXN6mrJ5e26oRXNcg==";

// --- Message ring buffer (PSRAM) ----------------------------------------------
constexpr int kMaxMsgs = 128;
mesh_client::MsgView* s_msgs = nullptr;
int      s_msgHead = 0;        // nächster Schreibplatz
int      s_msgLen  = 0;
uint32_t s_changes = 0;

// --- SD message log -------------------------------------------------------------
// /meshcore/messages.log: "epoch \t kind \t from \t text" — readable on a PC.
// If there is no SD card, this is silently skipped.
constexpr const char* kLogDir  = "/meshcore";
constexpr const char* kLogPath = "/meshcore/messages.log";
constexpr const char* kLogOld  = "/meshcore/messages.old";
constexpr uint32_t    kLogRotateBytes = 256 * 1024;

// SD persistence for contacts + joined hashtag channels (12 June 2026).
// Contacts: SPIFFS remains the primary storage (works without SD); the SD maintains a
// mirror in the same binary format — the SD takes precedence when loading (allows import/
// backup via the web file manager, similar to identity.hex). The mirror is
// NOT written in the mesh callbacks (which run under spiLock!), but
// deferred in mesh_client::loop() — same technique as flushLog().
constexpr const char* kContactsSdPath = "/meshcore/contacts.bin";
constexpr const char* kChannelsPath   = "/meshcore/channels.txt";
bool s_contactsSdDirty = false;

// Validity limits for persisted contact timestamps. Lower limit =
// Jan 2024 (same EPOCH_MIN_SANE as BaseChatMesh), upper limit ~year 2046.
// Intercepts invalid values from corrupted contacts.bin data sets (e.g. “5437 days ago”).
constexpr uint32_t kEpochMinSane = 1704067200UL;
constexpr uint32_t kEpochMaxSane = 2400000000UL;

// Validates a record read from contacts.bin before it is imported.
// SD card corruption (aborted writes) causes records to be shifted → resulting in invalid names/times.
// Criteria: non-empty public key, printable null-terminated name (umlauts
// ≥0x80 permitted), timestamp 0 (= ‘unknown’) or within a plausible range.
static bool isSaneContact(const ContactInfo& c) {
  bool keyNonZero = false;
  for (int i = 0; i < 32; i++) if (c.id.pub_key[i]) { keyNonZero = true; break; }
  if (!keyNonZero) return false;

  if (c.name[0] == 0) return false;               // leerer Name = Müll/leerer Slot
  bool terminated = false;
  for (int i = 0; i < 32; i++) {
    uint8_t ch = (uint8_t)c.name[i];
    if (ch == 0) { terminated = true; break; }
    if (ch < 0x20) return false;                  // Steuerzeichen → korrupt
  }
  if (!terminated) return false;                  // kein \0 in 32 Bytes → korrupt

  uint32_t ts = c.last_advert_timestamp;
  if (ts != 0 && (ts < kEpochMinSane || ts > kEpochMaxSane)) return false;
  return true;
}

// Optional, user-specified MeshCore identity from the SD card.
// Format (text file): Line 1 = private key (128 hex characters / 64 bytes),
// Line 2 = public key (64 hex characters / 32 bytes). If the file exists and
// is valid, it takes precedence over the SPIFFS identity and is transferred there.
constexpr const char* kIdentityPath = "/meshcore/identity.hex";

// Hex string → bytes; returns the number of bytes written or -1 if
// an invalid character is encountered. Deliberately local (no library dependency, only used during initialisation).
int hexToBytes(uint8_t* out, int maxLen, const char* hex) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int n = 0;
  for (int i = 0; hex[i] && hex[i + 1] && n < maxLen; i += 2) {
    int hi = nib(hex[i]), lo = nib(hex[i + 1]);
    if (hi < 0 || lo < 0) return -1;
    out[n++] = (uint8_t)((hi << 4) | lo);
  }
  return n;
}

// Reads the two hex lines from /meshcore/identity.hex. The CALLER holds
// spiLock (MeshClient::begin runs entirely under the lock
// mesh_client::begin) — acquiring spiLock() here would result in a self-deadlock
// on the non-recursive mutex (see logToSd/pushMsg comment).
// true only if both lines are long enough.
bool readIdentityFile(char* prvOut, size_t prvLen, char* pubOut, size_t pubLen) {
  if (!board::sdReady()) return false;
  bool ok = false;
  if (SD.exists(kIdentityPath)) {
    File f = SD.open(kIdentityPath);
    if (f) {
      String l1 = f.readStringUntil('\n');
      String l2 = f.readStringUntil('\n');
      f.close();
      l1.trim();  // entfernt auch ein evtl. \r (CRLF)
      l2.trim();
      strncpy(prvOut, l1.c_str(), prvLen - 1); prvOut[prvLen - 1] = '\0';
      strncpy(pubOut, l2.c_str(), pubLen - 1); pubOut[pubLen - 1] = '\0';
      ok = (strlen(prvOut) >= 128 && strlen(pubOut) >= 64);
    }
  }
  return ok;
}

void logToSd(const mesh_client::MsgView& m) {
  if (!board::sdReady()) return;
  spiLock();
  if (!SD.exists(kLogDir)) SD.mkdir(kLogDir);

  // Turnover: too high -> move it aside as .old, start afresh.
  if (SD.exists(kLogPath)) {
    File f = SD.open(kLogPath);
    if (f) {
      uint32_t sz = f.size();
      f.close();
      if (sz > kLogRotateBytes) {
        if (SD.exists(kLogOld)) SD.remove(kLogOld);
        SD.rename(kLogPath, kLogOld);
      }
    }
  }

  File f = SD.open(kLogPath, FILE_APPEND);
  if (f) {
    f.printf("%lu\t%u\t%s\t%s\n", (unsigned long)m.timestamp, m.kind,
             m.from, m.text);
    f.close();
  }
  spiUnlock();
}

// Writing to the SD card does NOT take place in `pushMsg`: `pushMsg` is called, amongst other things, from
// the mesh loop, which already holds `spiLock()` — a direct
// `logToSd()` (which would acquire `spiLock` again) would cause a self-deadlock on the
// non-recursive mutex. Instead, just mark it; `flushLog()` writes
// later outside the lock (mesh_client::loop).
bool s_needLog[kMaxMsgs] = {};

void pushMsg(const mesh_client::MsgView& m, bool log = true) {
  if (!s_msgs) return;
  s_msgs[s_msgHead] = m;
  s_needLog[s_msgHead] = log;
  s_msgHead = (s_msgHead + 1) % kMaxMsgs;
  if (s_msgLen < kMaxMsgs) s_msgLen++;
  s_changes++;
}

// Write pending log entries to the SD card (from oldest to newest).
// The caller must NOT hold the spiLock.
void flushLog() {
  if (!s_msgs) return;
  for (int i = 0; i < s_msgLen; i++) {
    int idx = (s_msgHead - s_msgLen + i + kMaxMsgs) % kMaxMsgs;
    if (s_needLog[idx]) {
      s_needLog[idx] = false;
      logToSd(s_msgs[idx]);
    }
  }
}

// When Mesh starts, load the latest messages from the SD log into the history
// (the chat history survives reboots). Only reads the end of the file (<=16 KB).
void loadHistoryFromSd() {
  if (!board::sdReady()) return;
  constexpr size_t kTail = 16384;
  constexpr int    kLoad = 50;

  char* buf = (char*)heap_caps_malloc(kTail + 1, MALLOC_CAP_SPIRAM);
  if (!buf) buf = (char*)malloc(kTail + 1);
  if (!buf) return;

  int rd = 0;
  bool truncated = false;   // Start reading from the middle of the file -> first line truncated
  spiLock();
  if (!SD.exists(kLogPath)) { spiUnlock(); free(buf); return; }
  File f = SD.open(kLogPath);
  if (f) {
    uint32_t sz = f.size();
    uint32_t from = (sz > kTail) ? sz - kTail : 0;
    truncated = (from > 0);
    f.seek(from);
    rd = f.read((uint8_t*)buf, kTail);
    f.close();
  }
  spiUnlock();
  if (rd <= 0) { free(buf); return; }
  buf[rd] = '\0';

  // Collect lines (sliding window over the last 64).
  char* lines[64];
  int nLines = 0;
  char* p = buf;
  bool skipFirst = truncated;
  while (p && *p) {
    char* nl = strchr(p, '\n');
    if (nl) *nl = '\0';
    if (skipFirst) {
      skipFirst = false;            // Discard the first line if it is truncated
    } else if (*p) {
      if (nLines < 64) lines[nLines++] = p;
      else {
        memmove(lines, lines + 1, sizeof(lines[0]) * 63);
        lines[63] = p;
      }
    }
    p = nl ? nl + 1 : nullptr;
  }

  int start = (nLines > kLoad) ? nLines - kLoad : 0;
  int loaded = 0;
  for (int i = start; i < nLines; i++) {
    // Format: epoch \t kind \t from \t text
    char* l = lines[i];
    char* t1 = strchr(l, '\t');  if (!t1) continue;
    char* t2 = strchr(t1 + 1, '\t'); if (!t2) continue;
    char* t3 = strchr(t2 + 1, '\t'); if (!t3) continue;
    *t1 = *t2 = *t3 = '\0';

    mesh_client::MsgView m{};
    m.timestamp  = (uint32_t)strtoul(l, nullptr, 10);
    m.kind       = (uint8_t)atoi(t1 + 1);
    m.ackState   = 0;       // unknown after reboot
    m.contactIdx = 0xFF;    // Contact mapping not guaranteed after a reboot
    strncpy(m.from, t2 + 1, sizeof(m.from) - 1);
    strncpy(m.text, t3 + 1, sizeof(m.text) - 1);
    if (m.kind > 2) continue;
    pushMsg(m, /*log=*/false);
    loaded++;
  }
  free(buf);
  if (loaded) Serial.printf("[MESH] %d Nachricht(en) aus SD-Log geladen\n", loaded);
}

// Base64-Encoder (nur fürs Hashtag-Channel-PSK; base64.hpp ist nicht
// include-safe — BaseChatMesh.cpp definiert dessen Funktionen bereits).
void b64encode(const uint8_t* in, int len, char* out) {
  static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int o = 0;
  for (int i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
    if (i + 2 < len) v |= in[i + 2];
    out[o++] = k[(v >> 18) & 63];
    out[o++] = k[(v >> 12) & 63];
    out[o++] = (i + 1 < len) ? k[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < len) ? k[v & 63] : '=';
  }
  out[o] = '\0';
}

// --- MainBoard-Adapter (instead of the bulky ESP32 board) ------------------------
class TDeckMeshBoard : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override { return battery::milliVolts(); }
  uint8_t  getBatteryPercent() override { return battery::percent(); }
  const char* getManufacturerName() const override { return "LilyGo T-Deck Pro"; }
  void reboot() override { ESP.restart(); }
  uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }
};

struct NodePrefs {
  float  airtime_factor;
  char   node_name[32];
  double node_lat, node_lon;
  float  freq;
  uint8_t tx_power_dbm;
  uint8_t unused[3];
};

// Clock based on the ESP32 system time (settimeofday/gettimeofday). Unlike the
// millis()-based VolatileRTCClock, this survives deep sleep: the ESP32’s RTC counter
// continues to run whilst in sleep mode, and the system time jumps by the
// Sleep duration before (RTC oscillator drift only). NTP (configTime) and Mesh adverts
// both write the same system time → automatically consistent. Time is only lost
// in the event of a complete power failure/reset/reflash (in which case the NVS fallback in
// services/timesync takes effect). tick() is therefore a no-op.
class SystemRTCClock : public mesh::RTCClock {
public:
  uint32_t getCurrentTime() override { return (uint32_t)time(nullptr); }
  void setCurrentTime(uint32_t t) override {
    // Upper limit to prevent corrupt future timestamps (faulty advert, trap #3)
    // and the 2038 signed overflow of `time_t`: never accept implausible times (>=2038)
    // — otherwise the rubbish will be perpetuated via the NVS fallback.
    if (t >= 2145916800UL) {   // 2038-01-01 UTC
      Serial.printf("[TIME] Discard implausible times %lu (>=2038)\n", (unsigned long)t);
      return;
    }
    struct timeval tv = { (time_t)t, 0 };
    settimeofday(&tv, nullptr);
  }
  void tick() override {}
};

// --- Statische Mesh-Objekte ----------------------------------------------------------
TDeckMeshBoard       s_board;
CustomSX1262*        s_radio = nullptr;
CustomSX1262Wrapper* s_radioDrv = nullptr;
StdRNG               s_rng;
SystemRTCClock       s_rtc;
bool                 s_clockConfident = false;  // First confirmed synchronisation since boot?
SimpleMeshTables*    s_tables = nullptr;

class MeshClient;
MeshClient* s_mesh = nullptr;
bool s_ready = false;

class MeshClient : public BaseChatMesh {
  fs::FS* _fs;
  NodePrefs _prefs;
  ChannelDetails* _public;
  // Pending DM ACKs. Multiple parallel DMs must maintain their delivery status
  // independently — a single slot would, upon the second transmission,
  // overwrite the first and cause its ACK to be lost. Ring allocation.
  static constexpr int kAckSlots = 8;
  struct AckSlot { uint32_t ack; int msgIdx; };
  AckSlot _acks[kAckSlots];
  int _lastAckSlot;        // Most recently acquired slot (for onSendTimeout)

  // Runs under the caller’s spiLock (MeshClient::begin) — do NOT
  // acquire your own spiLock here (Pitfall 2)! SD takes precedence over SPIFFS.
  void loadContacts() {
    File file;
    if (board::sdReady() && SD.exists(kContactsSdPath)) {
      file = SD.open(kContactsSdPath);
      if (file) Serial.println("[MESH] Kontakte von SD geladen (/meshcore/contacts.bin)");
    }
    if (!file) {
      if (!_fs->exists("/contacts")) return;
      file = _fs->open("/contacts");
    }
    if (!file) return;
    bool full = false;
    int skipped = 0;
    while (!full) {
      ContactInfo c;
      uint8_t pub_key[32];
      uint8_t unused;
      uint32_t reserved;
      bool ok = (file.read(pub_key, 32) == 32);
      ok = ok && (file.read((uint8_t*)&c.name, 32) == 32);
      ok = ok && (file.read(&c.type, 1) == 1);
      ok = ok && (file.read(&c.flags, 1) == 1);
      ok = ok && (file.read(&unused, 1) == 1);
      ok = ok && (file.read((uint8_t*)&reserved, 4) == 4);
      ok = ok && (file.read((uint8_t*)&c.out_path_len, 1) == 1);
      ok = ok && (file.read((uint8_t*)&c.last_advert_timestamp, 4) == 4);
      ok = ok && (file.read(c.out_path, 64) == 64);
      c.gps_lat = c.gps_lon = 0;
      if (!ok) break;
      c.id = mesh::Identity(pub_key);
      // Discard corrupted records (SD trap: truncated writes shift the
      // records → invalid names/times) rather than displaying them. The file remains
      // 140-byte-aligned, so carry on reading (continue, not break).
      if (!isSaneContact(c)) { skipped++; continue; }
      // lastmod = advert time, so that bootstrapRTCfromContacts() can set the clock
      // (lastmod=0 would leave the RTC set to May 2024 — bots
      // would then discard our messages as out of date).
      c.lastmod = c.last_advert_timestamp;
      if (!addContact(c)) full = true;
    }
    file.close();
    // Write back the cleaned-up state (SPIFFS immediately, SD deferred via loop()),
    // so that the rubbish isn’t restored the next time the data is saved.
    if (skipped > 0) {
      Serial.printf("[MESH] %d korrupte Kontakte verworfen\n", skipped);
      saveContacts();
    }
  }

  void writeContactRecords(File& file) {
    ContactsIterator iter;
    ContactInfo c;
    uint8_t unused = 0;
    uint32_t reserved = 0;
    while (iter.hasNext(this, c)) {
      bool ok = (file.write(c.id.pub_key, 32) == 32);
      ok = ok && (file.write((uint8_t*)&c.name, 32) == 32);
      ok = ok && (file.write(&c.type, 1) == 1);
      ok = ok && (file.write(&c.flags, 1) == 1);
      ok = ok && (file.write(&unused, 1) == 1);
      ok = ok && (file.write((uint8_t*)&reserved, 4) == 4);
      ok = ok && (file.write((uint8_t*)&c.out_path_len, 1) == 1);
      ok = ok && (file.write((uint8_t*)&c.last_advert_timestamp, 4) == 4);
      ok = ok && (file.write(c.out_path, 64) == 64);
      if (!ok) break;
    }
  }

  // Called from mesh callbacks (under spiLock): SPIFFS immediately (internal
  // flash, no SPI bus), just mark the SD mirror — flushing causes a loop().
  void saveContacts() {
    File file = _fs->open("/contacts", "w", true);
    if (!file) return;
    writeContactRecords(file);
    file.close();
    s_contactsSdDirty = true;
  }

  int indexOfContact(const ContactInfo& c) {
    ContactsIterator iter;
    ContactInfo tmp;
    int i = 0;
    while (iter.hasNext(this, tmp)) {
      if (memcmp(tmp.id.pub_key, c.id.pub_key, PUB_KEY_SIZE) == 0) return i;
      i++;
    }
    return -1;
  }

protected:
  float getAirtimeBudgetFactor() const override { return _prefs.airtime_factor; }
  int calcRxDelay(float score, uint32_t air_time) const override { return 0; }
  bool allowPacketForward(const mesh::Packet* packet) override { return true; }
  uint8_t getPathHashSize() const override { return 2; }

  // Contacts table full (MAX_CONTACTS=64): overwrite the oldest non-favourites
  // rather than discarding new adverts — otherwise
  // new nodes (Echobot & Co.) will never appear in the list (found 12 June 2026).
  bool shouldOverwriteWhenFull() const override { return true; }

  void onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len,
                           const uint8_t* path) override {
    Serial.printf("[MESH] Advert von '%s'%s\n", contact.name, is_new ? " (neu)" : "");
    // Synchronise the clock opportunistically from (signed) adverts — the
    // device has no buffered RTC; without real-time data, our
    // messages carry out-of-date timestamps and are ignored by bots.
    // But do NOT trust a single advert: a node with a future clock
    // pulled us along by +28 hours on 12 June AND again on 13 June 2026. Rules:
    //   1. Cold start (clock still in 2024) OR minor forward correction
    //      (< 1 h): apply immediately.
    //   2. Large forward jump on the first synchronisation since boot (NVS was far
    //      behind OR an outlier): only accept after confirmation by a second,
    //      roughly (±5 min) matching advert — a single future-time
    //      node should not be able to drag us along when booting/waking up.
    //   3. Looking backwards (Advert > 1 hour in the past): after 3 Adverts, adjust to the MEDIAN
    //      — never to the maximum — as the median is robust against a
    //      single, relatively recent outlier in the future.
    constexpr uint32_t kSaneEpoch = 1781000000UL;  // ≈ 09.06.2026, Build era
    uint32_t t   = contact.last_advert_timestamp;
    uint32_t now = getRTCClock()->getCurrentTime();
    static uint32_t s_behind[3]   = {0, 0, 0};  // collected ‘behind us’ timestamps
    static int      s_behindCnt   = 0;
    static uint32_t s_fwdCandidate = 0;         // Pending major leap forward
    // Time source priority: If Wi-Fi access details are stored, the clock
    // synchronises exclusively via NTP (services/timesync). Mesh adverts must then
    // NOT be sent — otherwise, a single faulty advert had previously
    // pushed us forward to the year 2101. Only WITHOUT Wi-Fi is the mesh the sole time source and takes over.
    char wssid[33];
    settings::wifiSsid(wssid, sizeof(wssid));
    if (wssid[0] == '\0' && t > kSaneEpoch) {
      bool smallFwd = (t > now && t - now < 3600);
      if (now < kSaneEpoch || smallFwd) {
        getRTCClock()->setCurrentTime(t + 1);
        s_clockConfident = true;
        s_behindCnt = 0; s_fwdCandidate = 0;
        timesync::onExternalSync(t + 1, "Mesh");
        Serial.printf("[MESH] Clock set from Advert: %lu\n", (unsigned long)t);
      } else if (!s_clockConfident && t > now) {
        // Major leap forward, still unconfirmed since boot — awaiting confirmation.
        if (s_fwdCandidate && t + 300 > s_fwdCandidate && t < s_fwdCandidate + 300) {
          getRTCClock()->setCurrentTime(t + 1);
          s_clockConfident = true;
          s_behindCnt = 0; s_fwdCandidate = 0;
          timesync::onExternalSync(t + 1, "Mesh");
          Serial.printf("[MESH] Clock set from Advert (confirmed): %lu\n",
                        (unsigned long)t);
        } else {
          s_fwdCandidate = t;
          Serial.printf("[MESH] Major leap forward %lu flagged (awaiting confirmation)\n",
                        (unsigned long)t);
        }
      } else if (t + 3600 < now) {
        s_behind[s_behindCnt % 3] = t;
        if (++s_behindCnt >= 3) {
          uint32_t a = s_behind[0], b = s_behind[1], c = s_behind[2];
          uint32_t med = a < b ? (b < c ? b : (a < c ? c : a))
                               : (a < c ? a : (b < c ? c : b));
          getRTCClock()->setCurrentTime(med + 1);
          timesync::onExternalSync(med + 1, "Mesh");
          Serial.printf("[MESH] Clock reset (median of 3 adverts): %lu\n",
                        (unsigned long)med);
          s_behindCnt = 0;
        }
      } else {
        s_behindCnt = 0;       // Advert within ±1 hour — time is correct
        s_fwdCandidate = 0;
      }
    }
    saveContacts();
    s_changes++;
  }

  void onContactPathUpdated(const ContactInfo& contact) override {
    saveContacts();
    s_changes++;
  }

  ContactInfo* processAck(const uint8_t* data) override {
    for (int i = 0; i < kAckSlots; i++) {
      if (_acks[i].ack && memcmp(data, &_acks[i].ack, 4) == 0) {
        if (s_msgs && _acks[i].msgIdx >= 0) {
          s_msgs[_acks[i].msgIdx].ackState = 2;   // zugestellt
          s_changes++;
        }
        _acks[i].ack = 0;
        _acks[i].msgIdx = -1;
        break;
      }
    }
    return NULL;
  }

  void onSendTimeout() override {
    // The base stack only times out the current in-flight transmission — that is,
    // the most recently occupied slot. Older pending ACKs remain ‘pending’
    // (accepted limit: their delivery may still be received via processAck).
    if (_lastAckSlot >= 0 && _acks[_lastAckSlot].ack) {
      int mi = _acks[_lastAckSlot].msgIdx;
      if (s_msgs && mi >= 0 && s_msgs[mi].ackState == 1) {
        s_msgs[mi].ackState = 3;     // Timeout
        s_changes++;
      }
      _acks[_lastAckSlot].ack = 0;
      _acks[_lastAckSlot].msgIdx = -1;
    }
    _lastAckSlot = -1;
  }

  void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt,
                     uint32_t sender_timestamp, const char* text) override {
    mesh_client::MsgView m{};
    m.kind = 1;
    strncpy(m.from, from.name, sizeof(m.from) - 1);
    strncpy(m.text, text, sizeof(m.text) - 1);
    m.timestamp = sender_timestamp;
    int idx = indexOfContact(from);
    m.contactIdx = (idx >= 0) ? (uint8_t)idx : 0xFF;
    pushMsg(m);
    Serial.printf("[MESH] DM von '%s': %s\n", from.name, text);
  }

  void onChannelMessageRecv(const mesh::GroupChannel& channel, mesh::Packet* pkt,
                            uint32_t timestamp, const char* text) override {
    mesh_client::MsgView m{};
    m.kind = 0;
    char cleaned[sizeof(m.text)];
    snprintf(cleaned, sizeof(cleaned), "%s", text ? text : "");
    if (cleaned[0] == '[') {
      char* end = strchr(cleaned, ']');
      if (end) {
        char* rest = end + 1;
        while (*rest == ' ') rest++;
        snprintf(cleaned, sizeof(cleaned), "%s", rest);
      }
    }
    strncpy(m.text, cleaned, sizeof(m.text) - 1);
    m.text[sizeof(m.text) - 1] = '\0';
    m.timestamp = timestamp;
    m.contactIdx = 0xFF;
    m.channelIdx = (uint8_t)(findChannelIdx(channel) < 0 ? 0 : findChannelIdx(channel));
    pushMsg(m);
    Serial.printf("[MESH] Channel: %s\n", m.text);
  }

  void onCommandDataRecv(const ContactInfo&, mesh::Packet*, uint32_t, const char*) override {}
  void onSignedMessageRecv(const ContactInfo&, mesh::Packet*, uint32_t, const uint8_t*,
                           const char*) override {}
  uint8_t onContactRequest(const ContactInfo&, uint32_t, const uint8_t*, uint8_t,
                           uint8_t*) override { return 0; }
  void onContactResponse(const ContactInfo&, const uint8_t*, uint8_t) override {}

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override {
    return SEND_TIMEOUT_BASE_MILLIS + (uint32_t)(FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
  }
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override {
    return SEND_TIMEOUT_BASE_MILLIS +
           (uint32_t)((pkt_airtime_millis * DIRECT_SEND_PERHOP_FACTOR +
                       DIRECT_SEND_PERHOP_EXTRA_MILLIS) * (path_len + 1));
  }

public:
  MeshClient(mesh::Radio& radio, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
      : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables) {
    memset(&_prefs, 0, sizeof(_prefs));
    _prefs.airtime_factor = 2.0f;
    strcpy(_prefs.node_name, "T-Deck");
    _prefs.freq = LORA_FREQ;
    _prefs.tx_power_dbm = LORA_TX_POWER;
    _public = NULL;
    for (int i = 0; i < kAckSlots; i++) { _acks[i].ack = 0; _acks[i].msgIdx = -1; }
    _lastAckSlot = -1;
  }

  const char* name() const { return _prefs.node_name; }
  float freqPref() const { return _prefs.freq; }
  uint8_t txPowerPref() const { return _prefs.tx_power_dbm; }

  // Discard all contacts + delete persistent mirrors (console “contacts
  // reset”). Does NOT run under spiLock (caller = console) → Lock the SD card yourself.
  void clearPersistedContacts() {
    resetContacts();                       // num_contacts = 0 (lib)
    if (_fs) _fs->remove("/contacts");     // SPIFFS (master) — no SPI bus
    if (board::sdReady()) {
      spiLock();
      if (SD.exists(kContactsSdPath)) SD.remove(kContactsSdPath);
      spiUnlock();
    }
    s_contactsSdDirty = false;             // Discard pending mirror write
  }

  void begin(fs::FS& fs) {
    _fs = &fs;
    BaseChatMesh::begin();
    // REQUIRED before loadContacts/Advert reception: Create a contact array in PSRAM
    // (not possible in the constructor — PSRAM is not yet available there).
    initContacts();

    IdentityStore store(fs, "/identity");

    // 1) Priority: identity specified by the user on the SD card (kIdentityPath).
    //    If it is valid, it is transferred to the SPIFFS store (and thus survives
    //    even without the SD card) and overwrites any existing random identity.
    bool fromSd = false;
    {
      char prvHex[160] = {0}, pubHex[96] = {0};
      if (readIdentityFile(prvHex, sizeof(prvHex), pubHex, sizeof(pubHex))) {
        uint8_t prv[64];
        if (hexToBytes(prv, 64, prvHex) == 64 &&
            mesh::LocalIdentity::validatePrivateKey(prv)) {
          self_id = mesh::LocalIdentity(prvHex, pubHex);
          store.save("_main", self_id);
          fromSd = true;
          Serial.println("[MESH] Identität von SD geladen (/meshcore/identity.hex)");
        } else {
          Serial.println("[MESH] identity.hex ungültig (Key-Prüfung fehlgeschlagen) — ignoriert");
        }
      }
    }

    // 2) Otherwise, load from SPIFFS; 3) otherwise, recreate on first boot.
    if (!fromSd && !store.load("_main", self_id, _prefs.node_name, sizeof(_prefs.node_name))) {
      // First boot: Generate identity from hardware entropy (do not wait for the serial connection).
      s_rng.begin((long)esp_random());
      self_id = mesh::LocalIdentity(getRNG());
      int count = 0;
      while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) {
        self_id = mesh::LocalIdentity(getRNG());
        count++;
      }
      store.save("_main", self_id);
      Serial.println("[MESH] Neue Identität erzeugt");
    }

    // The node name comes from the NVS settings (Settings app), not from SPIFFS.
    settings::meshName(_prefs.node_name, sizeof(_prefs.node_name));
    settings::meshPos(&_prefs.node_lat, &_prefs.node_lon);

    loadContacts();
    bootstrapRTCfromContacts();
    _public = addChannel("Public", PUBLIC_GROUP_PSK);
  }

  void setName(const char* name) {
    strncpy(_prefs.node_name, name, sizeof(_prefs.node_name) - 1);
    _prefs.node_name[sizeof(_prefs.node_name) - 1] = '\0';
  }

  void setPosition(double lat, double lon) {
    _prefs.node_lat = lat;
    _prefs.node_lon = lon;
  }

  // Join a hashtag channel (Mesh Rhineland convention):
  // PSK = sha256("#<name>")[:16]. Idempotent; returns the channel index or -1.
  int joinHash(const char* nameIn) {
    char full[40];
    snprintf(full, sizeof(full), "%s%s", (nameIn[0] == '#') ? "" : "#", nameIn);

    // Have you joined yet?
    ChannelDetails cd;
    for (int i = 0; getChannel(i, cd) && cd.name[0]; i++) {
      if (strcasecmp(cd.name, full) == 0) return i;
    }

    uint8_t digest[32];
    SHA256 sha;
    sha.update(full, strlen(full));
    sha.finalize(digest, sizeof(digest));

    char b64[32];
    b64encode(digest, 16, b64);   // PSK = erste 16 Byte von sha256("#name")

    ChannelDetails* added = addChannel(full, b64);
    if (!added) return -1;
    int idx = findChannelIdx(added->channel);
    Serial.printf("[MESH] Kanal '%s' beigetreten (Index %d)\n", full, idx);
    return idx;
  }

  // Send message to channel index (0 = Public, otherwise hashtag channels).
  bool sendToChannel(int idx, const char* text) {
    ChannelDetails cd;
    if (!getChannel(idx, cd)) return false;
    return sendGroupMessage(getRTCClock()->getCurrentTime(), cd.channel,
                            _prefs.node_name, text, strlen(text));
  }

  bool getChannelInfo(int i, ChannelDetails& cd) { return getChannel(i, cd); }

  // Write the SD mirror of the contacts. Uses spiLock itself — the caller
  // must NOT hold it (hence deferred from mesh_client::loop()).
  void saveContactsToSd() {
    if (!board::sdReady()) return;
    spiLock();
    if (!SD.exists(kLogDir)) SD.mkdir(kLogDir);
    File file = SD.open(kContactsSdPath, FILE_WRITE);
    if (file) {
      writeContactRecords(file);
      file.close();
    }
    spiUnlock();
  }

  // Creates a self-advert with battery telemetry (FEAT1 = voltage in mV, FEAT2 =
  // state of charge in % in the low byte + charge bit as the MSB). The position is only included
  // if set (0/0 is considered undefined — otherwise we would be advertising “Null Island”
  //). Custom builder instead of createSelfAdvert(), so that the vendored
  // lib/meshcore remains untouched; self_id + createAdvert() inherits from MeshClient
  // from the mesh base. The encoder omits a FEAT field if it is 0 —
  // in which case the receiver reads 0 (mV/% not available), which is acceptable.
  mesh::Packet* buildSelfAdvert() {
    bool hasPos = (_prefs.node_lat != 0.0 || _prefs.node_lon != 0.0);
    AdvertDataBuilder builder = hasPos
        ? AdvertDataBuilder(ADV_TYPE_CHAT, _prefs.node_name, _prefs.node_lat, _prefs.node_lon)
        : AdvertDataBuilder(ADV_TYPE_CHAT, _prefs.node_name);
    uint16_t mv = battery::milliVolts();
    if (mv) builder.setFeat1(mv);
    uint16_t feat2 = (uint16_t)battery::percent() | (battery::charging() ? 0x8000 : 0);
    if (feat2) builder.setFeat2(feat2);
    uint8_t app_data[MAX_ADVERT_DATA_SIZE];
    uint8_t len = builder.encodeTo(app_data);
    return createAdvert(self_id, app_data, len);
  }

  void sendSelfAdvertNow() {
    auto pkt = buildSelfAdvert();
    if (pkt) sendZeroHop(pkt);
  }

  // Same as `sendSelfAdvertNow`, but as a flood (supports multi-hop) — also reaches nodes
  // that are not direct radio neighbours (e.g. a bridge one hop away).
  void sendSelfAdvertFlood() {
    auto pkt = buildSelfAdvert();
    if (pkt) sendFlood(pkt);
  }

  bool sendPublic(const char* text) {
    if (!_public) return false;
    mesh::GroupChannel ch = _public->channel;
    return sendGroupMessage(getRTCClock()->getCurrentTime(), ch,
                            _prefs.node_name, text, strlen(text));
  }

  // Send DM; returns the circular buffer index of the message sent (-1 on error).
  int sendDirect(int contactIdx, const char* text) {
    ContactInfo c;
    if (!getContactByIdx(contactIdx, c)) return -1;
    uint32_t est_timeout;
    uint32_t ack = 0;
    int result = sendMessage(c, getRTCClock()->getCurrentTime(), 0, text, ack, est_timeout);
    if (result == MSG_SEND_FAILED) return -1;

    mesh_client::MsgView m{};
    m.kind = 2;
    m.ackState = 1;   // ausstehend
    strncpy(m.from, c.name, sizeof(m.from) - 1);
    strncpy(m.text, text, sizeof(m.text) - 1);
    m.timestamp = getRTCClock()->getCurrentTime();
    m.contactIdx = (uint8_t)contactIdx;
    int slot = s_msgHead;
    pushMsg(m);
    // Allocate a free ACK slot (otherwise overwrite the oldest one — round-robin behaviour).
    int as = -1;
    for (int i = 0; i < kAckSlots; i++) if (!_acks[i].ack) { as = i; break; }
    if (as < 0) as = (_lastAckSlot + 1) % kAckSlots;
    _acks[as].ack = ack;
    _acks[as].msgIdx = slot;
    _lastAckSlot = as;
    return slot;
  }
};

}  // namespace

namespace mesh_client {

bool begin() {
  if (s_ready) return true;

  s_msgs = (MsgView*)heap_caps_calloc(kMaxMsgs, sizeof(MsgView), MALLOC_CAP_SPIRAM);
  if (!s_msgs) s_msgs = (MsgView*)calloc(kMaxMsgs, sizeof(MsgView));

  if (!SPIFFS.begin(true)) {
    Serial.println("[MESH] SPIFFS-Mount fehlgeschlagen");
    return false;
  }

  board::loraPower(true);

  // Radio on the shared bus: g_spi is passed via MIT (no separate spi.begin).
  spiLock();
  s_radio = new CustomSX1262(new Module(PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST,
                                        PIN_LORA_BUSY, g_spi,
                                        SPISettings(SPI_BUS_HZ, MSBFIRST, SPI_MODE0)));
  bool radioOk = s_radio->std_init(NULL);
  spiUnlock();
  if (!radioOk) {
    Serial.println("[MESH] Radio initialisation FAILED");
    board::loraPower(false);
    return false;
  }

  s_radioDrv = new CustomSX1262Wrapper(*s_radio, s_board);
  s_tables   = new SimpleMeshTables();
  s_rng.begin((long)esp_random());
  s_mesh = new MeshClient(*s_radioDrv, s_rng, s_rtc, *s_tables);

  spiLock();
  s_mesh->begin(SPIFFS);
  s_radioDrv->begin();
  spiUnlock();

  s_ready = true;
  BATTLOG_EVENT("Mesh", "Radio on");   // Debug battery logger (no-op without BATTLOG)

  // Apply radio parameters from the settings (std_init uses the compile-time defaults).
  applyRadioParams();

  settings::MeshParams p = settings::meshParams();
  Serial.printf("[MESH] bereit: '%s' @ %.3f MHz SF%u BW%.1f CR4/%u %udBm%s\n",
                s_mesh->name(), (double)p.freqMhz, p.sf, (double)p.bwKhz, p.cr, p.txDbm,
                settings::meshEco() ? " [RX-Eco]" : " (Noise-Floor-Kalibrierung läuft)");

  // Restore chat history from the SD log (if a card is inserted).
  loadHistoryFromSd();

  // Restore joined hashtag channels — otherwise joins are volatile
  // (12 June 2026: #test/#weather disappears after every reboot). First collect names under the
  // lock, then join (joinHash is read-only, but logs the joins).
  if (board::sdReady()) {
    constexpr int kMaxRestore = 8;   // MAX_GROUP_CHANNELS
    char names[kMaxRestore][40];
    int  n = 0;
    spiLock();
    if (SD.exists(kChannelsPath)) {
      File f = SD.open(kChannelsPath);
      if (f) {
        while (n < kMaxRestore && f.available()) {
          String l = f.readStringUntil('\n');
          l.trim();
          if (l.length()) {
            strncpy(names[n], l.c_str(), sizeof(names[n]) - 1);
            names[n][sizeof(names[n]) - 1] = '\0';
            n++;
          }
        }
        f.close();
      }
    }
    spiUnlock();
    for (int i = 0; i < n; i++) {
      spiLock();
      s_mesh->joinHash(names[i]);
      spiUnlock();
    }
  }

  // Boot advert with a slight delay (zero-hop follows user action).
  spiLock();
  auto pkt = s_mesh->buildSelfAdvert();
  if (pkt) s_mesh->sendFlood(pkt, 1500);
  spiUnlock();
  return true;
}

bool ready() { return s_ready; }

namespace { bool s_suspended = false; }

void setSuspended(bool sus) {
  s_suspended = sus;
  if (s_ready) Serial.printf("[MESH] Pumpe %s\n", sus ? "pausiert (WiFi aktiv)" : "läuft wieder");
}

void loop() {
  if (!s_ready || s_suspended) return;
  spiLock();
  s_mesh->loop();
  spiUnlock();
  // Now log messages generated in the mesh loop to the SD card (lock-free).
  flushLog();
  // Synchronise contact mirror to SD (mark callbacks only; throttled,
  // so that ad viewers do not write ~13 KB to the SD card with every packet).
  static uint32_t s_lastSdSave = 0;
  if (s_contactsSdDirty && board::sdReady() && millis() - s_lastSdSave >= 5000) {
    s_contactsSdDirty = false;
    s_lastSdSave = millis();
    s_mesh->saveContactsToSd();
  }
}

int msgCount() { return s_msgLen; }

bool msg(int i, MsgView& out) {
  if (!s_msgs || i < 0 || i >= s_msgLen) return false;
  int idx = (s_msgHead - s_msgLen + i + kMaxMsgs) % kMaxMsgs;
  out = s_msgs[idx];
  return true;
}

uint32_t changeCounter() { return s_changes; }

void resetContacts() {
  if (!s_ready) return;
  s_mesh->clearPersistedContacts();
  s_changes++;   // Redraw the Contacts screen from scratch
}

bool sendChannelMsg(const char* text) {
  if (!s_ready || !text || !text[0]) return false;
  spiLock();
  bool ok = s_mesh->sendPublic(text);
  spiUnlock();
  if (ok) {
    // Add your own message to the chat history (channel convention: "<Name>: <Text>").
    MsgView m{};
    m.kind = 0;
    snprintf(m.text, sizeof(m.text), "~%s: %s", s_mesh->name(), text);
    m.timestamp = 0;
    m.contactIdx = 0xFF;
    m.channelIdx = 0;   // Public
    pushMsg(m);
  }
  return ok;
}

// Send to a channel by index (0=Public). Uses the UI for the
// unified chat view: each channel is a separate chat.
bool sendChannelIdxMsg(int channelIdx, const char* text) {
  if (channelIdx <= 0) return sendChannelMsg(text);
  if (!s_ready || !text || !text[0]) return false;
  spiLock();
  bool ok = s_mesh->sendToChannel(channelIdx, text);
  spiUnlock();
  if (ok) {
    MsgView m{};
    m.kind = 0;
    snprintf(m.text, sizeof(m.text), "~%s: %s", s_mesh->name(), text);
    m.timestamp = 0;
    m.contactIdx = 0xFF;
    m.channelIdx = (uint8_t)channelIdx;
    pushMsg(m);
  }
  return ok;
}

bool sendDirectMsg(int contactIdx, const char* text) {
  if (!s_ready || !text || !text[0]) return false;
  spiLock();
  int slot = s_mesh->sendDirect(contactIdx, text);
  spiUnlock();
  return slot >= 0;
}

bool resendDirect(int contactIdx) {
  if (!s_ready) return false;
  // Find the most recent failed DM (ackState==3) for this contact (new->old).
  for (int i = s_msgLen - 1; i >= 0; i--) {
    MsgView m;
    if (!msg(i, m)) continue;
    if (m.kind == 2 && m.ackState == 3 && (int)m.contactIdx == contactIdx) {
      char text[sizeof(m.text)];
      strncpy(text, m.text, sizeof(text) - 1);
      text[sizeof(text) - 1] = '\0';
      spiLock();
      int slot = s_mesh->sendDirect(contactIdx, text);   // New transmission attempt
      spiUnlock();
      return slot >= 0;
    }
  }
  return false;
}

void sendAdvert() {
  if (!s_ready) return;
  spiLock();
  s_mesh->sendSelfAdvertNow();
  spiUnlock();
}

void sendAdvertFlood() {
  if (!s_ready) return;
  spiLock();
  s_mesh->sendSelfAdvertFlood();
  spiUnlock();
}

// Write subscribed hashtag channels to SD (index 0 = Public, fixed —
// is not persisted). One name per line; the PSK is derived from
// the name during the join, so the list of names is sufficient.
void saveChannelsToSd() {
  if (!s_ready || !board::sdReady()) return;
  spiLock();
  if (!SD.exists(kLogDir)) SD.mkdir(kLogDir);
  File f = SD.open(kChannelsPath, FILE_WRITE);
  if (f) {
    ChannelDetails cd;
    for (int i = 1; s_mesh->getChannelInfo(i, cd) && cd.name[0]; i++) {
      f.printf("%s\n", cd.name);
    }
    f.close();
  }
  spiUnlock();
}

int joinHashChannel(const char* name) {
  if (!s_ready || !name || !name[0]) return -1;
  int before = channelCount();
  spiLock();
  int idx = s_mesh->joinHash(name);
  spiUnlock();
  if (idx >= 0 && channelCount() != before) saveChannelsToSd();
  return idx;
}

bool sendHashChannelMsg(const char* name, const char* text) {
  if (!s_ready || !text || !text[0]) return false;
  int idx = joinHashChannel(name);
  if (idx < 0) return false;
  spiLock();
  bool ok = s_mesh->sendToChannel(idx, text);
  spiUnlock();
  if (ok) {
    MsgView m{};
    m.kind = 0;
    snprintf(m.text, sizeof(m.text), "~%s: %s", s_mesh->name(), text);
    m.timestamp = 0;
    m.contactIdx = 0xFF;
    m.channelIdx = (uint8_t)idx;
    pushMsg(m);
  }
  return ok;
}

int channelCount() {
  if (!s_ready) return 0;
  // getChannel liefert auch leere Slots (bis MAX_GROUP_CHANNELS) — Kanäle
  // werden sequentiell angelegt, also zählt bis zum ersten leeren Namen.
  ChannelDetails cd;
  int n = 0;
  while (s_mesh->getChannelInfo(n, cd) && cd.name[0]) n++;
  return n;
}

bool channelName(int i, char* out, size_t n) {
  if (!s_ready) return false;
  ChannelDetails cd;
  if (!s_mesh->getChannelInfo(i, cd) || !cd.name[0]) return false;
  strncpy(out, cd.name, n - 1);
  out[n - 1] = '\0';
  return true;
}

int contactCount() { return s_ready ? s_mesh->getNumContacts() : 0; }

bool contactName(int i, char* out, size_t n) {
  if (!s_ready) return false;
  ContactInfo c;
  if (!s_mesh->getContactByIdx(i, c)) return false;
  strncpy(out, c.name, n - 1);
  out[n - 1] = '\0';
  return true;
}

bool contactDetail(int i, uint8_t* hops, uint32_t* lastSeen, uint8_t* type) {
  if (!s_ready) return false;
  ContactInfo c;
  if (!s_mesh->getContactByIdx(i, c)) return false;
  if (hops)     *hops     = c.out_path_len;        // 0xFF = unbekannt (Flood)
  if (lastSeen) *lastSeen = c.last_advert_timestamp;
  if (type)     *type     = c.type;
  return true;
}

// Position eines Kontakts aus dem zuletzt empfangenen Advert (0/0 = keine
// bekannt). gps_lat/gps_lon füllt die lib beim Advert-Empfang (×1E6).
bool contactPos(int i, double* lat, double* lon) {
  if (!s_ready) return false;
  ContactInfo c;
  if (!s_mesh->getContactByIdx(i, c)) return false;
  if (c.gps_lat == 0 && c.gps_lon == 0) return false;
  if (lat) *lat = c.gps_lat / 1000000.0;
  if (lon) *lon = c.gps_lon / 1000000.0;
  return true;
}

const char* nodeName() { return s_ready ? s_mesh->name() : "-"; }

void setNodeName(const char* name) {
  if (!name || !name[0]) return;
  settings::setMeshName(name);
  if (s_ready) s_mesh->setName(name);
}

void nodePos(double* lat, double* lon) { settings::meshPos(lat, lon); }

void setNodePosition(double lat, double lon) {
  settings::setMeshPos(lat, lon);           // persistiert auch ohne aktives Mesh
  if (s_ready) s_mesh->setPosition(lat, lon);
}

void applyRadioParams() {
  if (!s_ready || !s_radio || !s_radioDrv) return;
  settings::MeshParams p = settings::meshParams();
  spiLock();
  s_radio->setFrequency(p.freqMhz);
  s_radio->setBandwidth(p.bwKhz);
  s_radio->setSpreadingFactor(p.sf);
  s_radio->setCodingRate(p.cr);
  // Längere Präambel bei niedrigem SF (kürzere Symbole) — vgl. MeshCore PR#1954.
  s_radio->setPreambleLength((p.sf <= 8) ? 32 : 16);
  s_radio->setOutputPower(p.txDbm);
  // RX-Sparmodus (SetRxDutyCycle) laut Settings übernehmen und den Empfang
  // über den Wrapper neu armieren (Parameterwechsel beendet laufendes RX;
  // ein rohes startReceive() würde den Duty-Cycle-Modus umgehen).
  s_radioDrv->setLowPowerRx(settings::meshEco());
  s_radioDrv->restartRecv();
  spiUnlock();
}

uint32_t rtcTime() {
  return s_ready ? s_rtc.getCurrentTime() : 0;
}

// Ungated (auch vor Mesh-Init): die Uhr ist die ESP32-Systemzeit, existiert ab Boot.
uint32_t clockNow() {
  return s_rtc.getCurrentTime();
}

void setRtcTime(uint32_t epoch, bool authoritative) {
  s_rtc.setCurrentTime(epoch);
  if (authoritative) s_clockConfident = true;
}

bool radioStats(int* noiseFloor, uint32_t* rxPkts, uint32_t* txPkts) {
  if (!s_ready || !s_radioDrv) return false;
  if (noiseFloor) *noiseFloor = s_radioDrv->getNoiseFloor();
  if (rxPkts)     *rxPkts     = s_radioDrv->getPacketsRecv();
  if (txPkts)     *txPkts     = s_radioDrv->getPacketsSent();
  return true;
}

}  // namespace mesh_client
