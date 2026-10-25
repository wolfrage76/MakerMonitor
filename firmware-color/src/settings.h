#pragma once
#include <stdint.h>

#include "board.h"

// Menu-editable settings, kept in NVS. Each is an index into its option list.
enum SettingId {
  SET_BRIGHTNESS,
  SET_AUTOCYCLE,
  SET_NOTIFY,
  SET_QUIET,
  SET_POLL,
  SET_IDLE,
  SET_BANNER,
  SET_DIM,
  SET_NIGHT,
#if AUDIO_I2S
  SET_SOUND,
  SET_VOLUME,
#endif
  NSET
};

struct SettingDef {
  const char* key;     // NVS key
  const char* label;   // menu label
  uint8_t count;
  const char* const* names;
  const int* values;
  uint8_t def;
};

extern const SettingDef SETTINGS[NSET];

void settings_load();
uint8_t setting_index(SettingId id);
int setting_value(SettingId id);
const char* setting_name(SettingId id);
void setting_next(SettingId id);   // cycles and saves
void setting_set_value(SettingId id, int val);
// Menu settings and the quiet window back to their defaults. Wi-Fi, hostname,
// mDNS, the console password and update preferences are kept.
void settings_reset_defaults();

// OTA & web console preferences kept in NVS.
bool settings_auto_ota();
void settings_set_auto_ota(bool on);
bool settings_beta_ota();
void settings_set_beta_ota(bool on);
const char* settings_ota_password();
void settings_set_ota_password(const char* pw);

// Empty hostname means the MAC default (makermonitor- plus 5 hex digits).
const char* settings_hostname();
void settings_set_hostname(const char* host);   // sanitized before saving
void settings_sanitize_hostname(const char* in, char* out, size_t cap);
bool settings_mdns();                           // announce <hostname>.local
void settings_set_mdns(bool on);
// Index 0 follows the poller's offset. Any other choice is a POSIX zone.

// When this firmware version first saw a real clock (UTC epoch, 0 = not yet).
// One NVS write per version, on the first call with a valid clock.
uint32_t settings_installed_at();
void settings_stamp_install(const char* version, uint32_t now);

// The GitHub release check got a 404 (private repo): automatic checks pause
// until a manual one succeeds.
bool settings_github_missing();
void settings_set_github_missing(bool on);

// First-run guide (connect the poller): shown until the first state push or
// "Don't show again".
bool settings_guide_done();
void settings_set_guide_done();

// Notify mask bit per kind (0-4 stats, 5 comments).
bool notify_enabled(int kind);
// Quiet hours; minutes since local midnight. SET_QUIET turns them on/off.
bool in_quiet_hours(int minuteOfDay);
int settings_quiet_start();
int settings_quiet_end();
void settings_set_quiet(int start, int end);   // saves only what changed

// Bumped by "poll now" on the Status screen; the poller watches it.
uint32_t poll_now_seq();
void poll_now_request();
