#pragma once
#include <Arduino.h>

#include "knob.h"
#include "model.h"

struct NetInfo {
  bool connected;
  bool portal;      // Wi-Fi setup portal is open
  char ip[16];
  char ssid[33];
  char host[33];    // makermonitor-xxxxx, or a name set on the web console
  bool mdns;        // <host>.local is being announced
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
void ui_update_notice(const char* title, const char* sub);
// Full-screen "Updating firmware" with a progress bar, for builds whose
// display stays on during flash writes (XIP). pct < 0: size unknown.
void ui_update_progress(int pct);
bool ui_updating();
// "Starting" screen right after the panel is up, before the pages are built.
void ui_splash();  // drawn at once, before display_suspend()
void ui_refresh_from_web();         // re-apply settings/update state changed in web console
void wifi_start_setup();            // opens the on-screen Wi-Fi page (main.cpp)

// On-device Wi-Fi setup (touch boards). The page lives in ui.cpp; scanning,
// connecting and the saved network are in main.cpp.
struct WifiAp {
  char ssid[33];
  int rssi;
  bool open;
};
void ui_wifi_page(bool open);
bool net_saved_ssid(char* out, size_t cap);
int net_scan(WifiAp* out, int max);       // -1 while scanning (starts one if needed), else count
void net_connect(const char* ssid, const char* pass);  // pass == nullptr: saved credentials
int net_connect_status(char* msg, size_t cap);         // 0 idle, 1 connecting, 2 ok, 3 failed
int net_http_get(const char* url, char* out, size_t cap);  // small poller GETs; body length or -1
void net_apply_hostname();   // re-read hostname / mDNS settings and re-announce (main.cpp)
// "Set up from phone" hotspot (main.cpp): start returns the SSID/password.
bool net_ap_start(char* ssid, size_t ssidCap, char* pass, size_t passCap);
void net_ap_stop();
int net_ap_status(bool* joined, int* result, char* msg, size_t cap);  // 1 while the hotspot is on
// /v2/ui/show and /v2/ui/pages: open a page (with a Creator tab, a Settings
// picker or a model's detail) for testing and screenshot capture. `shown` gets
// what is on screen now, e.g. "creator/launch". False for an unknown name.
bool ui_show(const char* page, const char* tab, const char* open, char* shown, size_t cap, bool slide = false);
void ui_pages_json(String& out);
bool ui_overlay_open();             // true when Settings, Status, About, or Update page is open
// Wakes a dimmed screen; true if it was dimmed (the caller drops that input).
bool ui_wake();
// True while a finger is dragging the page sideways (touch boards).
bool ui_dragging();

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
