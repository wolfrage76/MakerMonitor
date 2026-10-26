#include "settings.h"

#include <Preferences.h>
#include <time.h>

static const char* const BRIGHT_N[] = {"20%", "40%", "60%", "80%", "100%"};
static const int BRIGHT_V[] = {20, 40, 60, 80, 100};
static const char* const CYCLE_N[] = {"Off", "Every 15 s", "Every 30 s", "Every 60 s"};
static const int CYCLE_V[] = {0, 15, 30, 60};
// Bits: boosts, likes, collects, downloads, prints, comments, ratings.
static const char* const NOTIFY_N[] = {"Everything", "All but likes", "Boosts, prints, ratings, comments", "Off"};
static const int NOTIFY_V[] = {0x7F, 0x7D, 0x71, 0x00};
// Quiet hours on/off; the window itself is qstart/qend below.
static const char* const ONOFF_N[] = {"Off", "On"};
static const int ONOFF_V[] = {0, 1};
static const char* const POLL_N[] = {"2 min", "5 min", "10 min", "15 min", "30 min"};
static const int POLL_V[] = {120, 300, 600, 900, 1800};
static const char* const IDLE_N[] = {"Never", "after 30 s", "after 60 s", "after 2 min"};
static const int IDLE_V[] = {0, 30, 60, 120};
static const char* const BANNER_N[] = {"5 s", "8 s", "15 s", "30 s"};
static const int BANNER_V[] = {5, 8, 15, 30};
static const char* const DIM_N[] = {"Off", "1 min", "5 min", "15 min", "30 min"};
static const int DIM_V[] = {0, 60, 300, 900, 1800};
// During Quiet hours, once idle: backlight off, or very low.
static const char* const NIGHT_N[] = {"Screen off", "Dim"};
static const int NIGHT_V[] = {0, 1};
#if AUDIO_I2S
// Peak amplitude out of 32767.
static const char* const VOLUME_N[] = {"Low", "Medium", "High"};
static const int VOLUME_V[] = {2500, 7000, 16000};
#endif

const SettingDef SETTINGS[NSET] = {
  {"bright", "Brightness", 5, BRIGHT_N, BRIGHT_V, 3},
  {"cycle", "Auto-cycle screens", 4, CYCLE_N, CYCLE_V, 0},
  {"notify", "Notify on", 4, NOTIFY_N, NOTIFY_V, 0},
  {"quieton", "Quiet hours", 2, ONOFF_N, ONOFF_V, 0},
  {"poll", "Poll MakerWorld every", 5, POLL_N, POLL_V, 2},
  {"idle", "Return to Today", 4, IDLE_N, IDLE_V, 2},
  {"banner", "Notification time", 4, BANNER_N, BANNER_V, 1},
  {"dim", "Dim after", 5, DIM_N, DIM_V, 2},
  {"night", "Night mode", 2, NIGHT_N, NIGHT_V, 0},
#if AUDIO_I2S
  {"sound", "Sound", 2, ONOFF_N, ONOFF_V, 1},
  {"volume", "Volume", 3, VOLUME_N, VOLUME_V, 1},
#endif
};

static Preferences prefs;
static uint32_t pollSeq = 0;  // RAM only (see poll_now_request)

// Everything the panel saves, with its defaults, in one place. NVS keys and
// formats are the ones earlier versions used (mmcolor namespace), so an
// upgrade keeps every value.
struct Stored {
  uint8_t idx[NSET];              // menu settings: index into each option list
  bool autoOta = true;            // autoota
  bool betaOta = false;           // betaota
  bool mdns = true;               // mdns
  bool guideDone = false;         // guided: first-run guide finished or hidden
  bool ghMissing = false;         // gh404: release check found no repo
  char otaPass[48] = "";          // otapass
  char host[33] = "";             // host ("" = MAC default)
  uint16_t quietStart = 22 * 60;  // qstart, minutes since local midnight
  uint16_t quietEnd = 7 * 60;     // qend
  uint32_t installedAt = 0;       // instat, UTC epoch
  char installedVer[16] = "";     // instver
};
static Stored cfg;

// Up to 0.4.3, "quiet" held one preset index (Off, 22-07, 23-07, 00-08).
// Copied once into quieton/qstart/qend; the old key stays so a downgrade
// still finds it.
static void migrateQuiet() {
  if (prefs.isKey("quieton")) return;
  static const uint16_t S[] = {22 * 60, 22 * 60, 23 * 60, 0}, E[] = {7 * 60, 7 * 60, 7 * 60, 8 * 60};
  uint8_t old = prefs.getUChar("quiet", 0);
  if (old > 3) old = 0;
  prefs.putUChar("quieton", old ? 1 : 0);
  prefs.putUShort("qstart", S[old]);
  prefs.putUShort("qend", E[old]);
}

void settings_load() {
  prefs.begin("mmcolor", false);
  migrateQuiet();
  const Stored d{};  // defaults
  for (int i = 0; i < NSET; i++) {
    uint8_t v = prefs.getUChar(SETTINGS[i].key, SETTINGS[i].def);
    cfg.idx[i] = v < SETTINGS[i].count ? v : SETTINGS[i].def;
  }
  cfg.autoOta = prefs.getBool("autoota", d.autoOta);
  cfg.betaOta = prefs.getBool("betaota", d.betaOta);
  cfg.mdns = prefs.getBool("mdns", d.mdns);
  cfg.guideDone = prefs.getBool("guided", d.guideDone);
  cfg.ghMissing = prefs.getBool("gh404", d.ghMissing);
  prefs.getString("otapass", cfg.otaPass, sizeof(cfg.otaPass));
  char host[64] = "";
  prefs.getString("host", host, sizeof(host));
  settings_sanitize_hostname(host, cfg.host, sizeof(cfg.host));
  cfg.quietStart = prefs.getUShort("qstart", d.quietStart) % 1440;
  cfg.quietEnd = prefs.getUShort("qend", d.quietEnd) % 1440;
  cfg.installedAt = prefs.getULong("instat", d.installedAt);
  prefs.getString("instver", cfg.installedVer, sizeof(cfg.installedVer));
  // Keys earlier versions used and no longer need.
  prefs.remove("pollseq");  // poll-now counter, RAM only now
  if (prefs.isKey("tz")) prefs.remove("tz");  // the clock follows the poller's UTC offset
}

uint8_t setting_index(SettingId id) { return cfg.idx[id]; }
int setting_value(SettingId id) { return SETTINGS[id].values[cfg.idx[id]]; }
const char* setting_name(SettingId id) { return SETTINGS[id].names[cfg.idx[id]]; }

void setting_next(SettingId id) {
  cfg.idx[id] = (cfg.idx[id] + 1) % SETTINGS[id].count;
  prefs.putUChar(SETTINGS[id].key, cfg.idx[id]);
}

void setting_set_value(SettingId id, int val) {
  for (uint8_t j = 0; j < SETTINGS[id].count; j++) {
    if (SETTINGS[id].values[j] == val) {
      cfg.idx[id] = j;
      prefs.putUChar(SETTINGS[id].key, j);
      return;
    }
  }
}

void settings_reset_defaults() {
  const Stored d{};
  for (int i = 0; i < NSET; i++) {
    if (cfg.idx[i] == SETTINGS[i].def) continue;
    cfg.idx[i] = SETTINGS[i].def;
    prefs.putUChar(SETTINGS[i].key, cfg.idx[i]);
  }
  settings_set_quiet(d.quietStart, d.quietEnd);
}

bool settings_auto_ota() { return cfg.autoOta; }
void settings_set_auto_ota(bool on) {
  cfg.autoOta = on;
  prefs.putBool("autoota", on);
}

bool settings_beta_ota() { return cfg.betaOta; }
void settings_set_beta_ota(bool on) {
  cfg.betaOta = on;
  prefs.putBool("betaota", on);
}

// Lowercase a-z, 0-9 and '-', at most 32 characters, no leading/trailing '-'.
void settings_sanitize_hostname(const char* in, char* out, size_t cap) {
  size_t o = 0;
  for (; in && *in && o + 1 < cap && o < 32; in++) {
    char c = *in;
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c == ' ' || c == '_' || c == '.') c = '-';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-' && o)) out[o++] = c;
  }
  while (o && out[o - 1] == '-') o--;
  out[o] = 0;
}

const char* settings_hostname() { return cfg.host; }
void settings_set_hostname(const char* host) {
  settings_sanitize_hostname(host, cfg.host, sizeof(cfg.host));
  prefs.putString("host", cfg.host);
}

bool settings_mdns() { return cfg.mdns; }
void settings_set_mdns(bool on) {
  cfg.mdns = on;
  prefs.putBool("mdns", on);
}


const char* settings_ota_password() { return cfg.otaPass; }
void settings_set_ota_password(const char* pw) {
  strlcpy(cfg.otaPass, pw ? pw : "", sizeof(cfg.otaPass));
  prefs.putString("otapass", cfg.otaPass);
}

uint32_t settings_installed_at() { return cfg.installedAt; }

void settings_stamp_install(const char* version, uint32_t now) {
  if (!strcmp(cfg.installedVer, version)) return;
  strlcpy(cfg.installedVer, version, sizeof(cfg.installedVer));
  cfg.installedAt = now;
  prefs.putString("instver", cfg.installedVer);
  prefs.putULong("instat", cfg.installedAt);
}

bool settings_github_missing() { return cfg.ghMissing; }
void settings_set_github_missing(bool on) {
  if (cfg.ghMissing == on) return;
  cfg.ghMissing = on;
  prefs.putBool("gh404", on);
}

bool settings_guide_done() { return cfg.guideDone; }
void settings_set_guide_done() {
  if (cfg.guideDone) return;
  cfg.guideDone = true;
  prefs.putBool("guided", true);
}

bool notify_enabled(int kind) { return (setting_value(SET_NOTIFY) >> kind) & 1; }

int settings_quiet_start() { return cfg.quietStart; }
int settings_quiet_end() { return cfg.quietEnd; }
void settings_set_quiet(int start, int end) {
  start = ((start % 1440) + 1440) % 1440;
  end = ((end % 1440) + 1440) % 1440;
  if (start != cfg.quietStart) prefs.putUShort("qstart", start);
  if (end != cfg.quietEnd) prefs.putUShort("qend", end);
  cfg.quietStart = start;
  cfg.quietEnd = end;
}

// A window may cross midnight (22:00-07:00). Start == end is an empty window.
bool in_quiet_hours(int minuteOfDay) {
  if (!setting_value(SET_QUIET)) return false;
  int start = cfg.quietStart, end = cfg.quietEnd;
  return start <= end ? (minuteOfDay >= start && minuteOfDay < end)
                      : (minuteOfDay >= start || minuteOfDay < end);
}

uint32_t poll_now_seq() { return pollSeq; }

// RAM only: an NVS write on every tap means SPI flash writes during the RGB
// scan-out. After a reboot the count starts over, which the poller may take
// as one extra "poll now".
void poll_now_request() { pollSeq++; }
