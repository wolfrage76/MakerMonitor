#include "settings.h"

#include <Preferences.h>

static const char* const BRIGHT_N[] = {"20%", "40%", "60%", "80%", "100%"};
static const int BRIGHT_V[] = {20, 40, 60, 80, 100};
static const char* const CYCLE_N[] = {"Off", "Every 15 s", "Every 30 s", "Every 60 s"};
static const int CYCLE_V[] = {0, 15, 30, 60};
// Bits: boosts, likes, collects, downloads, prints, comments.
static const char* const NOTIFY_N[] = {"Everything", "All but likes", "Boosts, prints, comments", "Off"};
static const int NOTIFY_V[] = {0x3F, 0x3D, 0x31, 0x00};
// Quiet start*100 + end hour; 0 = off.
static const char* const QUIET_N[] = {"Off", "22:00 - 07:00", "23:00 - 07:00", "00:00 - 08:00"};
static const int QUIET_V[] = {0, 2207, 2307, 2408};
static const char* const POLL_N[] = {"2 min", "5 min", "10 min", "15 min", "30 min"};
static const int POLL_V[] = {120, 300, 600, 900, 1800};
static const char* const IDLE_N[] = {"Never", "after 30 s", "after 60 s", "after 2 min"};
static const int IDLE_V[] = {0, 30, 60, 120};
static const char* const BANNER_N[] = {"5 s", "8 s", "15 s", "30 s"};
static const int BANNER_V[] = {5, 8, 15, 30};
static const char* const DIM_N[] = {"Off", "1 min", "5 min", "15 min", "30 min"};
static const int DIM_V[] = {0, 60, 300, 900, 1800};
#if AUDIO_I2S
static const char* const SOUND_N[] = {"Off", "On"};
static const int SOUND_V[] = {0, 1};
// Peak amplitude out of 32767.
static const char* const VOLUME_N[] = {"Low", "Medium", "High"};
static const int VOLUME_V[] = {2500, 7000, 16000};
#endif

const SettingDef SETTINGS[NSET] = {
  {"bright", "Brightness", 5, BRIGHT_N, BRIGHT_V, 3},
  {"cycle", "Auto-cycle screens", 4, CYCLE_N, CYCLE_V, 0},
  {"notify", "Notify on", 4, NOTIFY_N, NOTIFY_V, 0},
  {"quiet", "Quiet hours", 4, QUIET_N, QUIET_V, 0},
  {"poll", "Poll MakerWorld every", 5, POLL_N, POLL_V, 2},
  {"idle", "Return to Today", 4, IDLE_N, IDLE_V, 2},
  {"banner", "Notification time", 4, BANNER_N, BANNER_V, 1},
  {"dim", "Dim after", 5, DIM_N, DIM_V, 2},
#if AUDIO_I2S
  {"sound", "Sound", 2, SOUND_N, SOUND_V, 1},
  {"volume", "Volume", 3, VOLUME_N, VOLUME_V, 1},
#endif
};

static Preferences prefs;
static uint8_t idx[NSET];
static uint32_t pollSeq = 0;
static bool autoOta = true;
static bool betaOta = false;
static char otaPass[48] = "";

void settings_load() {
  prefs.begin("mmcolor", false);
  for (int i = 0; i < NSET; i++) {
    uint8_t v = prefs.getUChar(SETTINGS[i].key, SETTINGS[i].def);
    idx[i] = v < SETTINGS[i].count ? v : SETTINGS[i].def;
  }
  pollSeq = prefs.getUInt("pollseq", 0);
  autoOta = prefs.getBool("autoota", true);
  betaOta = prefs.getBool("betaota", false);
  String pw = prefs.getString("otapass", "");
  strlcpy(otaPass, pw.c_str(), sizeof(otaPass));
}

uint8_t setting_index(SettingId id) { return idx[id]; }
int setting_value(SettingId id) { return SETTINGS[id].values[idx[id]]; }
const char* setting_name(SettingId id) { return SETTINGS[id].names[idx[id]]; }

void setting_next(SettingId id) {
  idx[id] = (idx[id] + 1) % SETTINGS[id].count;
  prefs.putUChar(SETTINGS[id].key, idx[id]);
}

void setting_set_value(SettingId id, int val) {
  for (uint8_t j = 0; j < SETTINGS[id].count; j++) {
    if (SETTINGS[id].values[j] == val) {
      idx[id] = j;
      prefs.putUChar(SETTINGS[id].key, j);
      return;
    }
  }
}

bool settings_auto_ota() { return autoOta; }
void settings_set_auto_ota(bool on) {
  autoOta = on;
  prefs.putBool("autoota", on);
}

bool settings_beta_ota() { return betaOta; }
void settings_set_beta_ota(bool on) {
  betaOta = on;
  prefs.putBool("betaota", on);
}

const char* settings_ota_password() { return otaPass; }
void settings_set_ota_password(const char* pw) {
  strlcpy(otaPass, pw ? pw : "", sizeof(otaPass));
  prefs.putString("otapass", otaPass);
}

bool notify_enabled(int kind) { return (setting_value(SET_NOTIFY) >> kind) & 1; }

bool in_quiet_hours(int minuteOfDay) {
  int q = setting_value(SET_QUIET);
  if (!q) return false;
  int start = (q / 100 % 24) * 60, end = (q % 100) * 60;
  return start <= end ? (minuteOfDay >= start && minuteOfDay < end)
                      : (minuteOfDay >= start || minuteOfDay < end);
}

uint32_t poll_now_seq() { return pollSeq; }

void poll_now_request() {
  pollSeq++;
  prefs.putUInt("pollseq", pollSeq);
}
