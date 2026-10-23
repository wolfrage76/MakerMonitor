#pragma once
#include "knob.h"
#include "model.h"

struct NetInfo {
  bool connected;
  bool portal;      // Wi-Fi setup portal is open
  char ip[16];
  char ssid[33];
  char host[24];    // makermonitor- plus the last 5 MAC hex digits
  int rssi;
};
extern NetInfo g_net;   // kept current by main.cpp

void ui_init();
void ui_set_state(const State& s);
const State& ui_get_state();
void ui_event(const Event& e);
void ui_input(const KnobInput& in);
void ui_tick();                     // call every loop
void ui_toast(const char* msg);
void ui_refresh_from_web();         // re-apply settings/update state changed in web console
bool ui_overlay_open();             // true when Settings, Status, About, or Update page is open
// Wakes a dimmed screen; true if it was dimmed (the caller drops that input).
bool ui_wake();

// GitHub OTA helpers implemented in main.cpp, used by both the web console and
// the on-screen Firmware Update page.
int ota_compare_versions(const char* a, const char* b);
bool ota_is_prerelease(const char* version);
bool ota_check_github_update(char* latestVer, size_t verCap, char* downloadUrl, size_t urlCap,
                             char* errOut, size_t errCap);
bool ota_install_github_update(const char* downloadUrl, void (*onProgress)(int pct),
                               char* errOut, size_t errCap);
bool ota_update_available();
const char* ota_cached_latest_version();
const char* ota_cached_download_url();
void ota_forget_latest();
