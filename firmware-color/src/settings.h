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

// OTA & web console preferences kept in NVS.
bool settings_auto_ota();
void settings_set_auto_ota(bool on);
bool settings_beta_ota();
void settings_set_beta_ota(bool on);
const char* settings_ota_password();
void settings_set_ota_password(const char* pw);

// Notify mask bit per kind (0-4 stats, 5 comments).
bool notify_enabled(int kind);
// Quiet hours; minutes since local midnight.
bool in_quiet_hours(int minuteOfDay);

// Bumped by "poll now" on the Status screen; the poller watches it.
uint32_t poll_now_seq();
void poll_now_request();
