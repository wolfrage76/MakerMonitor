// MakerMonitor Color: stats panel for the Waveshare ESP32-S3-LCD-3.16 and the
// Elecrow CrowPanel ESP32-S3 HMI 4.3" / 5" / 7" (board picked by the env).
// The poller pushes POST /v2/state (full stats) and /v2/event (one change);
// GET /v2/settings tells the poller the chosen poll interval / "poll now".
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_rom_sys.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <esp_mac.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "board.h"
#include <DNSServer.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <miniz.h>

#include "display.h"
#include "knob.h"
#include "sound.h"
#include "fw_version.h"
#include "model.h"
#include "board.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"

static char HOSTNAME[33] = "makermonitor";
static const char* AP_NAME = BOARD_AP;

// makermonitor- plus the last 5 hex digits of the STA MAC, e.g. makermonitor-e3b7d.
static void makeHostname() {
  const char* custom = settings_hostname();
  if (custom[0]) {
    strlcpy(HOSTNAME, custom, sizeof(HOSTNAME));
  } else {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(HOSTNAME, sizeof(HOSTNAME), "makermonitor-%01x%02x%02x", mac[3] & 0x0f, mac[4], mac[5]);
  }
  strlcpy(g_net.host, HOSTNAME, sizeof(g_net.host));
}

static const char* STATE_FILE = "/state.json";

NetInfo g_net = {};
static WebServer server(80);

static void logHeap(const char* where) {
  Serial.printf("heap @%s: internal free %u (largest %u), psram free %u (largest %u)\n", where,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

// Logged instead of silently returning NULL, so an out-of-memory crash names
// its allocation.
static void allocFailed(size_t size, uint32_t caps, const char* fn) {
  esp_rom_printf("ALLOC FAILED: %u bytes caps 0x%x in %s\n", (unsigned)size, (unsigned)caps, fn ? fn : "?");
}

// ---- on-device Wi-Fi (touch boards) ----------------------------------------
// Station mode only: no setup hotspot. The panel scans, the user picks a
// network on screen, and credentials are stored by the Wi-Fi driver (NVS), so
// the next boot reconnects with WiFi.begin().
static uint8_t lastDiscReason = 0;
static void deviceLog(const char* msg);
void deviceLogf(const char* fmt, ...);

bool net_saved_ssid(char* out, size_t cap) {
  wifi_config_t cfg = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK || !cfg.sta.ssid[0]) return false;
  strlcpy(out, (const char*)cfg.sta.ssid, cap);
  return true;
}

int net_scan(WifiAp* out, int max) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return -1;
  if (n == WIFI_SCAN_FAILED) {  // none yet (or failed): start one
    WiFi.scanNetworks(true, false);
    return -1;
  }
  int k = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;  // hidden
    int rssi = WiFi.RSSI(i), dup = -1;
    for (int j = 0; j < k; j++)
      if (!strcmp(out[j].ssid, ssid.c_str())) dup = j;
    if (dup >= 0) {  // same network on several access points: keep the strongest
      if (rssi > out[dup].rssi) out[dup].rssi = rssi;
      continue;
    }
    if (k >= max) continue;
    strlcpy(out[k].ssid, ssid.c_str(), sizeof(out[k].ssid));
    out[k].rssi = rssi;
    out[k].open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    k++;
  }
  WiFi.scanDelete();
  deviceLogf("Wi-Fi scan: %d networks (%d unique)", n, k);
  for (int i = 1; i < k; i++)  // strongest first
    for (int j = i; j > 0 && out[j].rssi > out[j - 1].rssi; j--) {
      WifiAp t = out[j];
      out[j] = out[j - 1];
      out[j - 1] = t;
    }
  return k;
}

static int connState = 0;  // 0 idle, 1 connecting, 2 connected, 3 failed
static uint32_t connStart = 0;
static char connMsg[96];
static wifi_config_t prevCfg;  // restored if the new network fails

void net_connect(const char* ssid, const char* pass) {
  esp_wifi_get_config(WIFI_IF_STA, &prevCfg);
  WiFi.scanDelete();
  lastDiscReason = 0;
  WiFi.disconnect(false);
  if (pass) WiFi.begin(ssid, pass[0] ? pass : nullptr);
  else WiFi.begin();  // the saved credentials
  connState = 1;
  connStart = millis();
  snprintf(connMsg, sizeof(connMsg), "Connecting to %s...", ssid);
  deviceLogf("Wi-Fi: connecting to %s", ssid);
}

int net_connect_status(char* msg, size_t cap) {
  strlcpy(msg, connMsg, cap);
  return connState;
}

// Saved network lost (or never joined since boot): retry in the background at
// 10 s, 30 s, then every 60 s. Never a hotspot; the UI shows a "tap to choose
// network" banner meanwhile. Only a running scan or a connect attempt from the
// Wi-Fi page holds it off, so an open page or menu never stalls reconnecting.
static uint32_t retryAt = 0;
static uint8_t retryCount = 0;

static bool apOn = false;  // "Set up from phone" hotspot running

static void netRetryTick() {
  if (apOn) return;  // a STA retry would move the channel under the phone
  if (WiFi.status() == WL_CONNECTED) {
    retryCount = 0;
    retryAt = 0;
    return;
  }
  char saved[33];
  if (connState == 1 || WiFi.scanComplete() == WIFI_SCAN_RUNNING || !net_saved_ssid(saved, sizeof(saved))) return;
  uint32_t now = millis();
  if (!retryAt) {
    static const uint16_t BACKOFF_S[] = {10, 30, 60};
    retryAt = now + BACKOFF_S[min<int>(retryCount, 2)] * 1000UL;
    return;
  }
  if ((int32_t)(now - retryAt) < 0) return;
  retryCount = min<int>(retryCount + 1, 250);
  retryAt = 0;
  deviceLogf("Wi-Fi: retrying %s (attempt %u)", saved, (unsigned)retryCount);
  WiFi.disconnect(false);
  WiFi.begin();
}

static void netConnTick() {
  if (connState != 1) return;
  if (WiFi.status() == WL_CONNECTED) {
    connState = 2;
    snprintf(connMsg, sizeof(connMsg), "Connected to %s  -  %s", WiFi.SSID().c_str(),
             WiFi.localIP().toString().c_str());
    deviceLogf("Wi-Fi: %s", connMsg);
    return;
  }
  const char* why = nullptr;
  uint8_t r = lastDiscReason;
  if (r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_HANDSHAKE_TIMEOUT ||
      r == WIFI_REASON_AUTH_EXPIRE || r == WIFI_REASON_802_1X_AUTH_FAILED)
    why = "Wrong password";
  else if (r == WIFI_REASON_NO_AP_FOUND)
    why = "Network not found";
  else if (millis() - connStart > 15000)
    why = "Timed out";
  if (!why) return;
  connState = 3;
  snprintf(connMsg, sizeof(connMsg), "%s  -  back on %s", why,
           prevCfg.sta.ssid[0] ? (const char*)prevCfg.sta.ssid : "no network");
  deviceLogf("Wi-Fi: %s", why);
  // Put the previous network back (and in NVS) so the panel stays reachable.
  WiFi.disconnect(false);
  if (prevCfg.sta.ssid[0]) WiFi.begin((const char*)prevCfg.sta.ssid, (const char*)prevCfg.sta.password);
}

void wifi_start_setup() { ui_wifi_page(true); }

// ---- "Set up from phone" hotspot ---------------------------------------------
// Started from the Wi-Fi page only (never automatically). WPA2 SoftAP
// "MakerMonitor-XXXX" with a fresh random password; DNS answers every name with
// 192.168.4.1 so phones open the setup page, served by the same WebServer.
// Stops after 10 minutes, on Cancel, or shortly after a successful join.
static DNSServer apDns;
static uint32_t apStart = 0, apOkAt = 0;
static char apSsid[24], apPass[12];
static bool apJoined = false, apEventsHooked = false;
static int apResult = 0;  // 0 idle, 1 connecting, 2 connected, 3 failed
static char apMsg[96];
static WifiAp apAps[20];
static int apN = 0;

static void logHeap(const char* where);

bool net_ap_start(char* ssid, size_t ssidCap, char* pass, size_t passCap) {
  if (!apOn) {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(apSsid, sizeof(apSsid), "MakerMonitor-%02X%02X", mac[4], mac[5]);
    static const char CH[] = "abcdefghjkmnpqrstuvwxyz23456789";  // no 0/O, 1/l/i
    for (int i = 0; i < 8; i++) apPass[i] = CH[esp_random() % (sizeof(CH) - 1)];
    apPass[8] = 0;
    // Networks for the phone page (blocking scan, a few seconds, before the AP).
    WiFi.scanDelete();
    int n = WiFi.scanNetworks(false, false);
    apN = 0;
    for (int i = 0; i < n && apN < 20; i++) {
      String id = WiFi.SSID(i);
      bool dup = !id.length();
      for (int j = 0; j < apN && !dup; j++) dup = id == apAps[j].ssid;
      if (dup) continue;
      strlcpy(apAps[apN].ssid, id.c_str(), sizeof(apAps[apN].ssid));
      apAps[apN].rssi = WiFi.RSSI(i);
      apAps[apN].open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
      apN++;
    }
    WiFi.scanDelete();
    if (!apEventsHooked) {
      apEventsHooked = true;
      WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { apJoined = true; }, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    }
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(apSsid, apPass)) {
      deviceLog("Setup hotspot: could not start");
      WiFi.mode(WIFI_STA);
      return false;
    }
    apDns.start(53, "*", WiFi.softAPIP());
    apOn = true;
    apJoined = false;
    apResult = 0;
    apStart = millis();
    deviceLogf("Setup hotspot %s up; heap internal %u", apSsid, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    logHeap("ap");
  }
  strlcpy(ssid, apSsid, ssidCap);
  strlcpy(pass, apPass, passCap);
  return true;
}

static void netAnnounce();
void net_ap_stop() {
  if (!apOn) return;
  apDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
  deviceLog("Setup hotspot stopped");
  if (WiFi.status() != WL_CONNECTED) WiFi.begin();
  else netAnnounce();
}

// 0 hotspot off, 1 on; joined = a phone is connected; result as apResult.
int net_ap_status(bool* joined, int* result, char* msg, size_t cap) {
  if (joined) *joined = apJoined;
  if (result) *result = apResult;
  if (msg) strlcpy(msg, apMsg, cap);
  return apOn ? 1 : 0;
}

static bool apClient() { return apOn && server.client().localIP() == WiFi.softAPIP(); }

static String htmlEscape(const String& s);
static void handleApPage() {
  String p = F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>MakerMonitor Wi-Fi</title><style>body{font:16px system-ui,sans-serif;background:#06090e;color:#e6edf3;"
               "margin:0;padding:20px}input,select,button{font:inherit;width:100%;padding:10px;margin:6px 0 14px;"
               "box-sizing:border-box;border-radius:6px;border:1px solid #1e2a38;background:#121a25;color:#e6edf3}"
               "button{background:#22d3ee;color:#06090e;border:0;font-weight:600}#r{color:#22d3ee}</style></head><body>"
               "<h2>Connect " BOARD_NAME " to Wi-Fi</h2><form id=f><label>Network</label><select name=pick "
               "onchange=\"document.getElementById('s').value=this.value\"><option value=''>Choose...</option>");
  for (int i = 0; i < apN; i++) {
    p += "<option>" + htmlEscape(apAps[i].ssid) + "</option>";
  }
  p += F("</select><label>or type its name</label><input id=s name=ssid maxlength=32 autocapitalize=none>"
         "<label>Password</label><input name=pass type=password maxlength=64>"
         "<button>Connect</button></form><p id=r></p><script>"
         "const r=document.getElementById('r');document.getElementById('f').onsubmit=async e=>{e.preventDefault();"
         "r.textContent='Connecting...';await fetch('/ap-connect',{method:'POST',body:new URLSearchParams(new FormData(e.target))});"
         "const t=setInterval(async()=>{try{const j=await (await fetch('/ap-status')).json();r.textContent=j.msg;"
         "if(j.state>=2)clearInterval(t)}catch(x){r.textContent='Connected - the panel is switching networks.';"
         "clearInterval(t)}},1000)}</script></body></html>");
  server.send(200, "text/html; charset=utf-8", p);
}

static void apTick() {
  if (!apOn) return;
  apDns.processNextRequest();
  char msg[96];
  int cs = net_connect_status(msg, sizeof(msg));
  if (apResult == 1 && cs >= 2) {
    apResult = cs;
    strlcpy(apMsg, msg, sizeof(apMsg));
    if (cs == 2) apOkAt = millis();
  }
  // Give the phone a few seconds to show "Connected", then drop the hotspot.
  if ((apResult == 2 && millis() - apOkAt > 8000) || millis() - apStart > 600000UL) net_ap_stop();
}

// Blocking GET for small JSON from the poller (model detail page). Returns
// the body length, or -1.
int net_http_get(const char* url, char* out, size_t cap) {
  if (!g_net.connected || !url[0]) return -1;
  HTTPClient http;
  http.setTimeout(3000);
  if (!http.begin(url)) return -1;
  int code = http.GET();
  int n = -1;
  if (code == 200) {
    String body = http.getString();
    n = min<int>(body.length(), (int)cap - 1);
    memcpy(out, body.c_str(), n);
    out[n] = 0;
  }
  http.end();
  return n;
}
static State incoming;
static bool netStarted = false;
static bool fsOk = false;
static int otaResult = 0;  // HTTP status for the /update upload in progress
static bool otaActive = false;
static uint32_t otaLastActivity = 0;  // last OTA start / progress (display-off watchdog)
// Why the last update ended, kept across the restart that follows it (RTC
// memory survives a software reset) and logged at the next boot.
static RTC_NOINIT_ATTR uint32_t otaNoteMagic;
static RTC_NOINIT_ATTR char otaNote[96];
static void otaRemember(const char* msg) {
  strlcpy(otaNote, msg, sizeof(otaNote));
  otaNoteMagic = 0x0A7E5EED;
}
static bool otaNoteAtBoot = false;

static bool xipActive();

// Every update path. With XIP (or a panel with its own GRAM) the display
// keeps running through the flash writes, so it shows a progress screen;
// otherwise say so on screen, then stop the panel scan before the first
// flash write. The speaker stops either way.
static void beforeFlashing() {
  if ((xipActive() || display_survives_flash()) && !display_is_suspended()) {
    sound_stop();
    display_update_mode(true);
    ui_update_progress(0);
    return;
  }
  if (!display_is_suspended())
    ui_update_notice("Updating firmware...", "Do not unplug. The screen stays dark until the panel restarts.");
  delay(600);
  sound_stop();
  display_suspend();
}
static bool otaAuth = false;        // password verified before the file arrived
static int otaPctShown = -10;

// ---- diagnostics log --------------------------------------------------------
static const int LOG_N = 32, LOG_W = 128;
static char logLines[LOG_N][LOG_W] = {};
static int logNext = 0, logCount = 0;

static void deviceLog(const char* msg) {
  if (!msg || !msg[0]) return;
  Serial.println(msg);
  snprintf(logLines[logNext], LOG_W, "[%10lu ms] %s", millis(), msg);
  logNext = (logNext + 1) % LOG_N;
  if (logCount < LOG_N) logCount++;
}

void deviceLogf(const char* fmt, ...) {
  char b[LOG_W];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  deviceLog(b);
}

static void logClear() {
  logNext = logCount = 0;
}

static const char* resetWhy = "?";

#if CONFIG_SPIRAM_FETCH_INSTRUCTIONS && CONFIG_SPIRAM_RODATA
extern "C" uint32_t esp_spiram_instruction_access_enabled(void);
extern "C" uint32_t esp_spiram_rodata_access_enabled(void);
#endif
// Code and read-only data run from PSRAM (XIP), so flash writes leave the
// cache on and the display keeps running through an update.
static bool xipActive() {
#if CONFIG_SPIRAM_FETCH_INSTRUCTIONS && CONFIG_SPIRAM_RODATA
  return esp_spiram_instruction_access_enabled() && esp_spiram_rodata_access_enabled();
#else
  return false;
#endif
}

// "3 h ago" style age of a UTC time; "not yet" before the clock is set.
static String agoText(uint32_t t) {
  time_t now = time(nullptr);
  if (!t || now < 1700000000 || (uint32_t)now < t) return F("not yet");
  uint32_t s = now - t;
  if (s < 120) return String(s) + " s ago";
  if (s < 7200) return String(s / 60) + " min ago";
  if (s < 172800) return String(s / 3600) + " h ago";
  return String(s / 86400) + " days ago";
}

static String logSnapshot() {
  String out;
  out.reserve(logCount * 80);
  int first = (logNext + LOG_N - logCount) % LOG_N;
  for (int i = 0; i < logCount; i++) {
    out += logLines[(first + i) % LOG_N];
    out += '\n';
  }
  return out;
}

// ---- GitHub release OTA -----------------------------------------------------
static char latestVersion[32] = "";
static char latestUrl[256] = "";
static uint32_t lastGithubCheck = 0, wifiUpSince = 0;
static bool otaDownloadActive = false;

static String cleanVersion(const String& value) {
  String s = value;
  s.trim();
  if (s.startsWith("v") || s.startsWith("V")) s.remove(0, 1);
  return s;
}

static void versionParts(const char* value, int out[3], String& pre) {
  String s = cleanVersion(value ? String(value) : String());
  int dash = s.indexOf('-');
  pre = dash >= 0 ? s.substring(dash + 1) : "";
  if (dash >= 0) s.remove(dash);
  out[0] = out[1] = out[2] = 0;
  sscanf(s.c_str(), "%d.%d.%d", &out[0], &out[1], &out[2]);
}

int ota_compare_versions(const char* a, const char* b) {
  int pa[3], pb[3];
  String ta, tb;
  versionParts(a, pa, ta);
  versionParts(b, pb, tb);
  for (int i = 0; i < 3; i++)
    if (pa[i] != pb[i]) return pa[i] - pb[i];
  if (ta == tb) return 0;
  if (!ta.length()) return 1;
  if (!tb.length()) return -1;
  return ta.compareTo(tb);
}

bool ota_is_prerelease(const char* v) {
  return v && strchr(v, '-') != nullptr;
}

bool ota_update_available() {
  return latestVersion[0] && ota_compare_versions(latestVersion, FW_VERSION) > 0;
}

const char* ota_cached_latest_version() { return latestVersion; }
const char* ota_cached_download_url() { return latestUrl; }

void ota_forget_latest() {
  latestVersion[0] = latestUrl[0] = 0;
  ui_refresh_from_web();
}

static bool parseRelease(const String& body, bool list, String& ver, String& url) {
  JsonDocument filter;
  if (list) {
    filter[0]["tag_name"] = true;
    filter[0]["draft"] = true;
    filter[0]["prerelease"] = true;
    filter[0]["assets"][0]["name"] = true;
    filter[0]["assets"][0]["browser_download_url"] = true;
  } else {
    filter["tag_name"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  JsonArray releases;
  if (list) releases = doc.as<JsonArray>();
  for (int ri = 0; ri < (list ? (int)releases.size() : 1); ri++) {
    JsonObject r = list ? releases[ri].as<JsonObject>() : doc.as<JsonObject>();
    if ((r["draft"] | false) || (list && !(r["prerelease"] | false))) continue;
    String candidate = cleanVersion(r["tag_name"] | "");
    if (!candidate.length()) continue;
    String fallback;
    for (JsonObject asset : r["assets"].as<JsonArray>()) {
      String name = asset["name"] | "";
      String assetUrl = asset["browser_download_url"] | "";
      if (name == BOARD_OTA_ASSET) {
        ver = candidate;
        url = assetUrl;
        return url.length();
      }
      if (name == "firmware.bin") fallback = assetUrl;
    }
    if (fallback.length()) {
      ver = candidate;
      url = fallback;
      return true;
    }
  }
  return false;
}

bool ota_check_github_update(char* verOut, size_t verCap, char* urlOut, size_t urlCap,
                             char* errOut, size_t errCap) {
  if (verOut && verCap) verOut[0] = 0;
  if (urlOut && urlCap) urlOut[0] = 0;
  if (errOut && errCap) errOut[0] = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (errOut) strlcpy(errOut, "Wi-Fi not connected", errCap);
    return false;
  }
  String endpoint = String("https://api.github.com/repos/") + PROJECT_GITHUB_REPO +
                    (settings_beta_ota() ? "/releases?per_page=5" : "/releases/latest");
  WiFiClientSecure tls;
  tls.setInsecure();
  tls.setHandshakeTimeout(15);
  HTTPClient http;
  http.setTimeout(12000);
  http.setReuse(false);
  http.useHTTP10(true);
  int code = -1;
  for (int attempt = 0; attempt < 3 && code < 0; attempt++) {
    if (attempt) {
      http.end();
      delay(1500);
    }
    if (!http.begin(tls, endpoint)) continue;
    http.addHeader("User-Agent", String("MakerMonitor/") + BOARD_ID);
    http.addHeader("Accept", "application/vnd.github+json");
    code = http.GET();
  }
  if (code != 200) {
    String detail = code < 0 ? HTTPClient::errorToString(code) : String("HTTP ") + code;
    if (errOut) snprintf(errOut, errCap, "GitHub %s", detail.c_str());
    http.end();
    deviceLogf("GitHub check %s; heap %u", detail.c_str(), (unsigned)ESP.getFreeHeap());
    // 404: no public releases to read (a private repo). Automatic checks stop
    // until a manual check gets through; each try costs ~45 KB of internal
    // RAM for TLS.
    if (code == 404) settings_set_github_missing(true);
    return false;
  }
  String body = http.getString();
  http.end();
  String ver, url;
  if (!parseRelease(body, settings_beta_ota(), ver, url)) {
    if (errOut) strlcpy(errOut, "No matching board firmware asset", errCap);
    return false;
  }
  strlcpy(latestVersion, ver.c_str(), sizeof(latestVersion));
  strlcpy(latestUrl, url.c_str(), sizeof(latestUrl));
  if (verOut) strlcpy(verOut, latestVersion, verCap);
  if (urlOut) strlcpy(urlOut, latestUrl, urlCap);
  lastGithubCheck = millis();
  deviceLogf("GitHub latest v%s (%s)", latestVersion,
             ota_update_available() ? "update available" : "current");
  ui_refresh_from_web();
  settings_set_github_missing(false);
  return true;
}

static String resolveGithubRedirect(const String& source) {
  if (!source.startsWith("https://github.com/")) return source;
  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(12000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(tls, source)) return source;
  http.addHeader("User-Agent", String("MakerMonitor/") + BOARD_ID);
  const char* keys[] = {"Location"};
  http.collectHeaders(keys, 1);
  int code = http.GET();
  String location = http.header("Location");
  http.end();
  return code >= 300 && code < 400 && location.startsWith("https://") ? location : source;
}

bool ota_install_github_update(const char* downloadUrl, void (*progress)(int),
                               char* errOut, size_t errCap) {
  if (errOut && errCap) errOut[0] = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (errOut) strlcpy(errOut, "Wi-Fi not connected", errCap);
    return false;
  }
  String url = resolveGithubRedirect(downloadUrl && downloadUrl[0] ? downloadUrl : latestUrl);
  if (!url.length()) {
    if (errOut) strlcpy(errOut, "No release download URL", errCap);
    return false;
  }
  WiFiClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  http.setReuse(false);
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(tls, url)) {
    if (errOut) strlcpy(errOut, "Download connection failed", errCap);
    return false;
  }
  http.addHeader("User-Agent", String("MakerMonitor/") + BOARD_ID);
  int code = http.GET();
  if (code != 200) {
    if (errOut) snprintf(errOut, errCap, "Download HTTP %d", code);
    http.end();
    return false;
  }
  int total = http.getSize();
  if (total > 0 && total < 200000) {
    if (errOut) strlcpy(errOut, "Invalid firmware size", errCap);
    http.end();
    return false;
  }
  // Stop the RGB scan before the first erase. Octal PSRAM and flash share
  // the cache, so the panel DMA panics once Update disables it.
  beforeFlashing();
  if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    if (errOut) snprintf(errOut, errCap, "Update.begin: %s", Update.errorString());
    http.end();
    return false;
  }
  otaDownloadActive = true;
  if (progress) progress(0);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[2048];
  size_t written = 0;
  int shown = 0;
  uint32_t lastData = millis();
  while (http.connected() && (total <= 0 || written < (size_t)total)) {
    int avail = stream->available();
    if (avail > 0) {
      int want = min(avail, (int)sizeof(buf));
      int n = stream->readBytes(buf, want);
      if (n > 0) {
        if (Update.write(buf, n) != (size_t)n) {
          if (errOut) snprintf(errOut, errCap, "Flash write: %s", Update.errorString());
          Update.abort();
          http.end();
          otaDownloadActive = false;
          return false;
        }
        written += n;
        lastData = millis();
        int pct = total > 0 ? (int)(written * 100ULL / total) : 0;
        if (progress && (pct >= shown + 5 || pct == 100)) {
          shown = pct;
          progress(pct);
        }
      }
    } else {
      if (millis() - lastData > 15000) {
        if (errOut) strlcpy(errOut, "Download timed out", errCap);
        Update.abort();
        http.end();
        otaDownloadActive = false;
        return false;
      }
      delay(2);
    }
    yield();
    esp_task_wdt_reset();
  }
  http.end();
  if (written < 200000 || !Update.end(true) || !Update.isFinished()) {
    if (errOut) snprintf(errOut, errCap, "Verify failed: %s", Update.errorString());
    otaDownloadActive = false;
    return false;
  }
  otaDownloadActive = false;
  if (progress) progress(100);
  deviceLogf("GitHub OTA wrote %u bytes", (unsigned)written);
  return true;
}

static String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    switch (in[i]) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      default: out += in[i];
    }
  }
  return out;
}

static bool requestAuthorized() {
  const char* pass = settings_ota_password();
  if (!pass[0]) return true;
  if (server.hasArg("token") && server.arg("token") == pass) return true;
  return server.authenticate("admin", pass);
}

static bool requireAuthorization() {
  if (requestAuthorized()) return true;
  server.requestAuthentication(BASIC_AUTH, "MakerMonitor");
  return false;
}

static String optionList(SettingId id) {
  String s;
  const SettingDef& d = SETTINGS[id];
  for (uint8_t i = 0; i < d.count; i++) {
    s += F("<option value='");
    s += d.values[i];
    if (i == setting_index(id)) s += F("' selected>");
    else s += F("'>");
    s += htmlEscape(d.names[i]);
    s += F("</option>");
  }
  return s;
}

// ---- diagnostics & screenshot ------------------------------------------------

static void handleDiagnostics() {
  if (!requireAuthorization()) return;
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["board"] = BOARD_NAME;
  d["uptimeSec"] = millis() / 1000;
  d["resetReason"] = resetWhy;
  d["installedAt"] = settings_installed_at();
  d["installed"] = agoText(settings_installed_at());
  d["heap"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  d["heapMin"] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  d["heapLargest"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  d["psram"] = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  d["rssi"] = g_net.connected ? WiFi.RSSI() : 0;
  d["host"] = String(g_net.host) + (g_net.mdns ? ".local" : "");
  d["displayOn"] = !display_is_suspended();
  d["xip"] = xipActive();
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

// PNG of what the panel shows, built in PSRAM with the ROM's deflate
// (Huffman only: small and quick). Native orientation, so a rotated panel's
// picture comes out sideways.
struct PngBuf {
  uint8_t* data = nullptr;
  size_t len = 0, cap = 0;
  bool ok = true;
};

static bool pngAppend(PngBuf& b, const void* src, size_t n) {
  if (!b.ok || !n) return b.ok;
  if (b.len + n > b.cap) {
    size_t cap = b.cap ? b.cap * 2 : 128 * 1024;
    while (cap < b.len + n) cap *= 2;
    auto* nd = (uint8_t*)heap_caps_realloc(b.data, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!nd) return b.ok = false;
    b.data = nd;
    b.cap = cap;
  }
  memcpy(b.data + b.len, src, n);
  b.len += n;
  return true;
}

static void putBe32(uint8_t* p, uint32_t v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

static bool pngChunk(PngBuf& b, const char* type, const uint8_t* data, size_t n) {
  uint8_t hdr[8], crc[4];
  putBe32(hdr, n);
  memcpy(hdr + 4, type, 4);
  mz_ulong c = mz_crc32(MZ_CRC32_INIT, hdr + 4, 4);
  if (n) c = mz_crc32(c, data, n);
  putBe32(crc, c);
  return pngAppend(b, hdr, 8) && (!n || pngAppend(b, data, n)) && pngAppend(b, crc, 4);
}

static mz_bool pngIdat(const void* buf, int len, void* user) {
  auto* b = (PngBuf*)user;
  if (len > 0) pngChunk(*b, "IDAT", (const uint8_t*)buf, len);
  return b->ok;
}

static void handleScreenshot() {
  if (!requireAuthorization()) return;
  const uint16_t* fbp = display_framebuffer();
  if (!fbp) {
    server.send(503, "text/plain", "Display is off");
    return;
  }
  const int W = LCD_NATIVE_W, H = LCD_NATIVE_H;
  const size_t rowBytes = 1 + W * 3;
  auto* comp = (tdefl_compressor*)heap_caps_malloc(sizeof(tdefl_compressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  auto* row = (uint8_t*)heap_caps_malloc(rowBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  PngBuf png;
  bool ok = comp && row;
  static const uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  uint8_t ihdr[13] = {};
  putBe32(ihdr, W);
  putBe32(ihdr + 4, H);
  ihdr[8] = 8;  // 8-bit RGB
  ihdr[9] = 2;
  ok = ok && pngAppend(png, SIG, 8) && pngChunk(png, "IHDR", ihdr, 13);
  ok = ok && tdefl_init(comp, pngIdat, &png, TDEFL_WRITE_ZLIB_HEADER | TDEFL_HUFFMAN_ONLY) == TDEFL_STATUS_OKAY;
  for (int y = 0; ok && y < H; y++) {
    row[0] = 0;  // filter: none
    const uint16_t* src = fbp + (size_t)y * W;
    for (int x = 0; x < W; x++) {
      uint16_t px = src[x];
      row[1 + x * 3] = ((px >> 11) & 0x1F) * 255 / 31;
      row[2 + x * 3] = ((px >> 5) & 0x3F) * 255 / 63;
      row[3 + x * 3] = (px & 0x1F) * 255 / 31;
    }
    ok = tdefl_compress_buffer(comp, row, rowBytes, TDEFL_NO_FLUSH) >= TDEFL_STATUS_OKAY && png.ok;
    if (!(y & 15)) yield();
  }
  ok = ok && tdefl_compress_buffer(comp, nullptr, 0, TDEFL_FINISH) == TDEFL_STATUS_DONE;
  ok = ok && png.ok && pngChunk(png, "IEND", nullptr, 0);
  heap_caps_free(comp);
  heap_caps_free(row);
  if (!ok) {
    heap_caps_free(png.data);
    server.send(500, "text/plain", "PNG encode failed (PSRAM?)");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(png.len);
  server.send(200, "image/png", "");
  // write() returns 0 while the socket's send buffer is full; wait for it
  // to drain (up to 3 s of no progress) rather than cut the image short.
  WiFiClient c = server.client();
  uint32_t lastProgress = millis();
  for (size_t o = 0; o < png.len && c.connected() && millis() - lastProgress < 3000;) {
    size_t n = c.write(png.data + o, min<size_t>(png.len - o, 4096));
    if (n) {
      o += n;
      lastProgress = millis();
    } else {
      delay(5);
    }
  }
  heap_caps_free(png.data);
  deviceLogf("Screenshot sent (%u KB)", (unsigned)(png.len / 1024));
}

// Every update path (ArduinoOTA, web upload, GitHub) ends here: the linker
// wraps esp_ota_set_boot_partition (-Wl,--wrap in platformio.ini), so this is
// the one place that sees an update finish. The display is already off for
// the flash writes; a failed switch leaves the old image bootable, so log it
// and restart to get the screen back.
extern "C" esp_err_t __real_esp_ota_set_boot_partition(const esp_partition_t* partition);
extern "C" esp_err_t __wrap_esp_ota_set_boot_partition(const esp_partition_t* partition) {
  display_suspend();
  esp_err_t err = __real_esp_ota_set_boot_partition(partition);
  if (err != ESP_OK) {
    deviceLogf("Update: boot switch failed (0x%x); restarting", (unsigned)err);
    otaRemember("boot partition switch failed");
    delay(300);
    ESP.restart();
  }
  deviceLogf("Update: boot partition is now %s", partition ? partition->label : "?");
  return err;
}

static void handleConsole() {
  if (!requireAuthorization()) return;
  const State& s = ui_get_state();
  String p;
  p.reserve(15000);
  p += F(
    "<!doctype html><html><head><meta charset=utf-8><meta name=viewport "
    "content='width=device-width,initial-scale=1'><title>MakerMonitor</title><style>"
    ":root{color-scheme:dark;--bg:#090c10;--panel:#121821;--line:#253142;--muted:#8d9bab;"
    "--ink:#f3f6fa;--accent:#36d17c;--blue:#55a6ff;--danger:#ff6b6b}"
    "*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 80% 0,#142237 0,"
    "var(--bg) 42%);color:var(--ink);font:15px system-ui,sans-serif}"
    "main{width:min(1100px,calc(100% - 28px));margin:auto;padding:26px 0 50px}"
    "header{display:flex;align-items:center;justify-content:space-between;gap:18px;margin-bottom:24px}"
    ".brand{font-size:23px;font-weight:800}.brand i{color:var(--accent);font-style:normal}"
    ".online{color:var(--accent);font-size:13px}.tabs{display:flex;gap:5px;overflow:auto;"
    "border-bottom:1px solid var(--line);margin-bottom:18px}.tabs button{border:0;background:none;"
    "color:var(--muted);padding:12px 15px;font-weight:700;white-space:nowrap;cursor:pointer}"
    ".tabs button.on{color:var(--ink);border-bottom:2px solid var(--accent)}"
    ".tab{display:none}.tab.on{display:block}.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:14px}"
    ".card{grid-column:span 6;background:color-mix(in srgb,var(--panel) 94%,transparent);"
    "border:1px solid var(--line);border-radius:14px;padding:18px}.wide{grid-column:span 12}"
    ".metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.metric{padding:14px;"
    "border-radius:10px;background:#0c121a}.metric b{display:block;font-size:22px;margin-top:5px}"
    "h2{font-size:17px;margin:0 0 15px}p{color:var(--muted);line-height:1.5}.row{display:flex;"
    "justify-content:space-between;align-items:center;gap:12px;padding:10px 0;border-bottom:1px solid #202a37}"
    ".row:last-child{border:0}.label small{display:block;color:var(--muted);margin-top:3px}"
    "select,input{background:#090f16;color:var(--ink);border:1px solid #314156;border-radius:8px;"
    "padding:9px;max-width:210px}button,.btn{display:inline-block;background:var(--accent);color:#041209;"
    "border:0;border-radius:9px;padding:10px 14px;font-weight:800;text-decoration:none;cursor:pointer}"
    ".secondary{background:#223044;color:var(--ink)}.danger{background:var(--danger);color:#1a0505}"
    ".actions{display:flex;gap:9px;flex-wrap:wrap;margin-top:14px}.version{font-size:31px;font-weight:800}"
    ".tag{display:inline-block;color:var(--blue);background:#14253a;padding:5px 8px;border-radius:6px;"
    "font-size:12px}progress{width:100%;accent-color:var(--accent)}pre{white-space:pre-wrap;max-height:280px;"
    "overflow:auto;color:#afbdcb;background:#080c11;padding:13px;border-radius:9px;font:12px ui-monospace,monospace}"
    ".warn{border:1px solid var(--warn);color:var(--warn);background:#2a1f05;border-radius:10px;padding:11px 14px;margin:-8px 0 18px}"
    ".warn a{color:var(--warn)}.pw{display:flex;gap:8px}.pw input{flex:1;max-width:none}"
    ".spin{display:inline-block;width:14px;height:14px;border:2px solid var(--muted);border-top-color:var(--accent);"
    "border-radius:50%;animation:sp 1s linear infinite;vertical-align:-2px;margin-right:8px}@keyframes sp{to{transform:rotate(360deg)}}"
    "@media(max-width:720px){main{width:min(100% - 18px,1100px);padding-top:16px}header{align-items:flex-start}"
    ".card{grid-column:span 12}.metrics{grid-template-columns:repeat(2,1fr)}.row{align-items:flex-start;"
    "flex-direction:column}select,input{max-width:100%;width:100%}}"
    "</style></head><body><main><header><div><div class=brand>Maker<i>Monitor</i></div><div style='color:var(--muted)'>");
  p += BOARD_NAME;
  p += F("</div></div><div class=online>● ");
  p += g_net.connected ? htmlEscape(g_net.ssid) : F("Offline");
  p += F("</div></header>");
  if (!settings_ota_password()[0])
    p += F("<div class=warn>" "\u26A0" " No console password: anyone on this network can change settings or install "
           "firmware. <a href=#network onclick=\"document.querySelector('[data-tab=network]').click()\">Set one</a></div>");
  p += F("<nav class=tabs><button class=on data-tab=status>Status</button>"
         "<button data-tab=firmware>Firmware</button><button data-tab=display>Settings</button>"
         "<button data-tab=network>Network</button></nav><section id=status class='tab on'><div class=grid>"
         "<div class='card wide'><h2>Today</h2><div class=metrics>");
  const char* mk[] = {"Boosts", "Likes", "Collects", "Downloads", "Prints", "Points"};
  for (int i = 0; i < 6; i++) {
    p += F("<div class=metric><span>");
    p += mk[i];
    p += F("</span><b>");
    if (i < 5) p += s.today[i];
    else p += String(s.todayPoints, 0);
    p += F("</b></div>");
  }
  p += F("</div></div><div class=card><h2>Account</h2><div class=row><span>MakerWorld</span><b>");
  p += s.valid ? htmlEscape(s.account) : F("Waiting for data");
  p += F("</b></div><div class=row><span>Followers</span><b>");
  p += s.followers;
  p += F("</b></div><div class=row><span>Models</span><b>");
  p += s.models;
  p += F("</b></div><div class=actions><button onclick=post('/poll-now')>Poll now</button>"
         "<a class='btn secondary' href='" PROJECT_MAKERWORLD_URL "' target=_blank>Open profile</a></div></div>"
         "<div class=card><h2>Device</h2><div class=row><span>Firmware</span><b>v" FW_VERSION "</b></div>"
         "<div class=row><span>Board</span><b>" BOARD_ID "</b></div><div class=row><span>IP address</span><b>");
  p += g_net.connected ? WiFi.localIP().toString() : F("Offline");
  p += F("</b></div><div class=row><span>Hostname</span><b>");
  p += g_net.host;
  p += F(".local</b></div><div class=row><span>Signal</span><b>");
  p += g_net.connected ? String(g_net.rssi) + " dBm" : String("—");
  p += F("</b></div><div class=row><span>Uptime</span><b>");
  p += String(millis() / 3600000UL);
  p += F(" h</b></div><div class=row><span>Last restart</span><b>");
  p += resetWhy;
  p += F("</b></div><div class=row><span>Internal RAM free / lowest</span><b>");
  p += String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) + " / " +
       String(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024) + " KB";
  p += F("</b></div><div class=row><span>PSRAM free</span><b>");
  p += String(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024) + " KB";
  p += F("</b></div><div class=row><span>Firmware installed</span><b>");
  p += agoText(settings_installed_at());
  p += F("</b></div><div class=actions><a class='btn secondary' href=/screenshot.png target=_blank>Screenshot</a>"
         "<a class='btn secondary' href=/diagnostics target=_blank>Diagnostics JSON</a></div></div>"
         "<div class='card wide'><h2>Recent device log</h2><pre id=logs>");
  p += htmlEscape(logSnapshot());
  p += F("</pre><div class=actions><button class=secondary onclick=loadLogs()>Refresh log</button>"
         "<button class=secondary onclick=clearLogs()>Clear log</button></div></div></div></section>"
         "<section id=firmware class=tab><div class=grid><div class=card><span class=tag>INSTALLED</span>"
         "<p class=version>v" FW_VERSION "</p><p>This device is running the " BOARD_OTA_ASSET " build, installed ");
  p += agoText(settings_installed_at());
  p += F(".</p></div>"
         "<div class=card><span class=tag>GITHUB RELEASE</span><p class=version id=latest>");
  p += latestVersion[0] ? String("v") + latestVersion : String("Not checked");
  p += F("</p><p id=updateState>");
  if (ota_update_available()) p += F("A newer compatible release is ready.");
  else if (latestVersion[0]) p += F("This device is up to date.");
  else p += F("Check GitHub for the newest compatible board build.");
  p += F("</p><progress id=prog max=100 value=0></progress><div class=actions>"
         "<button onclick=githubUpdate('check')>Check now</button><button onclick=githubUpdate('install')>Install update</button>"
         "</div></div><div class='card wide'><h2>Install local firmware</h2>"
         "<p>Use a local <code>firmware.bin</code> when testing or when no GitHub release is available.</p>"
         "<form id=upf method=post action=/update enctype=multipart/form-data><input type=file name=firmware accept=.bin required> "
         "<button>Upload &amp; install</button></form><progress id=upprog max=100 value=0 style=display:none></progress>"
         "<p id=upstate></p></div></div></section>"
         "<section id=display class=tab><form action=/save method=post><div class=grid>");
  // Same sections, order and labels as the panel's Settings page.
  auto sel = [&](SettingId id, const char* label, const char* hint) {
    p += F("<label class=row><span class=label>");
    p += label ? label : SETTINGS[id].label;
    if (hint) p += String("<small>") + hint + "</small>";
    p += F("</span><select name=");
    p += SETTINGS[id].key;
    p += F(">");
    p += optionList(id);
    p += F("</select></label>");
  };
  p += F("<div class=card><h2>Display</h2>");
  sel(SET_BRIGHTNESS, nullptr, nullptr);
  sel(SET_DIM, nullptr, nullptr);
  sel(SET_AUTOCYCLE, nullptr, nullptr);
  sel(SET_IDLE, nullptr, nullptr);
  char hm[8];
  p += F("</div><div class=card><h2>Quiet hours</h2><label class=row><span class=label>Quiet hours"
         "<small>No banners or chimes; the screen sleeps once idle. May cross midnight.</small></span>"
         "<input type=hidden name=quiet_form value=1><input type=checkbox name=quieton value=1");
  if (setting_value(SET_QUIET)) p += F(" checked");
  p += F("></label>");
  sel(SET_NIGHT, "During quiet hours", "Once idle in quiet hours");
  // 12-hour pickers in 15-minute steps; the value is minutes since midnight.
  auto timeSel = [&](const char* name, const char* label, int cur) {
    p += String("<label class=row><span class=label>") + label + "</span><select name=" + name + ">";
    for (int m = 0; m < 1440; m += 15) {
      int h = m / 60;
      snprintf(hm, sizeof(hm), "%d:%02d", h % 12 ? h % 12 : 12, m % 60);
      p += String("<option value=") + m + (m == cur - cur % 15 ? " selected>" : ">") + hm + (h < 12 ? " AM" : " PM") +
           "</option>";
    }
    p += F("</select></label>");
  };
  timeSel("qstart", "Quiet from", settings_quiet_start());
  timeSel("qend", "Quiet until", settings_quiet_end());
  p += F("</div><div class=card><h2>Alerts</h2>");
  sel(SET_NOTIFY, nullptr, "Banner and chime for these events");
  sel(SET_BANNER, nullptr, nullptr);
#if AUDIO_I2S
  sel(SET_SOUND, nullptr, nullptr);
  sel(SET_VOLUME, nullptr, nullptr);
#endif
  p += F("</div><div class=card><h2>Network</h2>");
  sel(SET_POLL, nullptr, "The poller picks this up within 15 s");
  p += F("</div><div class='card wide'><button>Save settings</button></div></div></form></section>"
         "<section id=network class=tab><div class=grid><div class=card><h2>Wi-Fi</h2>"
         "<div class=row><span>Network</span><b>");
  p += g_net.connected ? htmlEscape(g_net.ssid) : F("Offline");
  p += F("</b></div><div class=row><span>Address</span><b>");
  p += g_net.connected ? WiFi.localIP().toString() : F("—");
  p += F("</b></div><div class=row><span>Hostname</span><b>");
  p += g_net.host;
  p += F(".local</b></div><form action=/save-host method=post><label class=row><span class=label>Custom hostname"
         "<small>Blank keeps the MAC name. Lowercase letters, digits and '-'. Applies right away.</small></span>"
         "<input name=hostname maxlength=32 placeholder='makermonitor' value='");
  if (settings_hostname()[0]) p += htmlEscape(settings_hostname());
  p += F("'></label><label class=row><span class=label>Announce on the network (mDNS)</span>"
         "<input type=hidden name=mdns_form value=1><input type=checkbox name=mdns value=1");
  if (settings_mdns()) p += F(" checked");
  p += F("></label><div class=actions><button>Save hostname</button>"
         "<button name=clear value=1 class=secondary>Use default</button></div></form>"
         "<div class=actions><button onclick=\"post('/wifi-setup')\">Start Wi-Fi setup</button>"
         "<button class=danger onclick=\"post('/reset-wifi',true)\">Reset Wi-Fi</button></div></div>"
         "<div class=card><h2>Console security</h2><p>Leave blank for local-network access without a password. "
         "Set a password to protect this console and OTA uploads (username: <b>admin</b>).</p>"
         "<form action=/save-password method=post><div class=pw><input type=password id=pw name=password maxlength=47 "
         "placeholder='New password' autocomplete=new-password><button type=button class=secondary "
         "onclick=\"let i=qs('#pw');i.type=i.type=='password'?'text':'password';this.textContent=i.type=='password'?'Show':'Hide'\">"
         "Show</button></div><div class=actions><button>Save password</button>"
         "<button name=clear value=1 class=secondary>Clear password</button></div></form></div>"
         "<div class='card wide'><h2>GitHub update preferences</h2><form action=/save-ota method=post>"
         "<label class=row><span class=label>Automatic checks<small>Check hourly while online</small></span>"
         "<input type=checkbox name=auto value=1");
  if (settings_auto_ota()) p += F(" checked");
  p += F("></label><label class=row><span class=label>Beta releases<small>Include prerelease firmware</small></span>"
         "<input type=checkbox name=beta value=1");
  if (settings_beta_ota()) p += F(" checked");
  p += F("></label><div class=actions><button>Save update preferences</button></div></form></div></div></section>"
         "<script>const qs=s=>document.querySelector(s);document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>{"
         "document.querySelectorAll('.tabs button,.tab').forEach(x=>x.classList.remove('on'));b.classList.add('on');"
         "qs('#'+b.dataset.tab).classList.add('on');history.replaceState(0,'','#'+b.dataset.tab)});"
         "let h=location.hash.slice(1),tb=h&&document.querySelector(`[data-tab=\"${h}\"]`);if(tb)tb.click();"
         "async function post(u,confirmIt){if(confirmIt&&!confirm('Reset saved Wi-Fi and reboot?'))return;"
         "let r=await fetch(u,{method:'POST'});alert(await r.text());if(r.ok&&u=='/reset-wifi')location.reload()}"
         "async function githubUpdate(a){let b=qs('#updateState'),pr=qs('#prog');b.textContent=a=='check'?'Checking…':'Starting update…';"
         "let r=await fetch('/github-update?action='+a,{method:'POST'});let j=await r.json();"
         "b.textContent=j.message||'Request failed';if(j.version)qs('#latest').textContent='v'+j.version;"
         "if(j.progress)pr.value=j.progress;if(j.reboot)waitBack(b)}"
         "function waitBack(el){el.innerHTML='<span class=spin></span>Restarting - this page reloads when the panel is back';"
         "setTimeout(function t(){fetch('/ping',{cache:'no-store'}).then(r=>r.ok?location.reload():setTimeout(t,2000))"
         ".catch(()=>setTimeout(t,2000))},5000)}"
         "qs('#upf').onsubmit=e=>{e.preventDefault();let f=qs('#upf'),pr=qs('#upprog'),st=qs('#upstate'),x=new XMLHttpRequest();"
         "pr.style.display='block';st.textContent='Uploading...';x.upload.onprogress=v=>{if(v.lengthComputable){pr.value=v.loaded*100/v.total;"
         "st.textContent='Uploading '+Math.round(v.loaded*100/v.total)+'%'}};x.onload=()=>{if(x.status==200)waitBack(st);"
         "else st.textContent='Update failed: '+x.responseText};x.onerror=()=>waitBack(st);"
         "x.open('POST','/update');x.send(new FormData(f))};"
         "async function loadLogs(){qs('#logs').textContent=await(await fetch('/diagnostics/logs')).text()}"
         "async function clearLogs(){await fetch('/diagnostics/logs',{method:'DELETE'});loadLogs()}"
         "</script></main></body></html>");
  server.send(200, "text/html; charset=utf-8", p);
}

static void handleUpdateDone() {
  int code = otaResult ? otaResult : 500;
  otaResult = 0;
  otaActive = false;
  server.sendHeader("Connection", "close");
  server.send(code, "text/plain", code == 200 ? "OK - rebooting" : code == 401 ? "Authentication required" : "Update failed");
  if (code == 200 || display_is_suspended() || ui_updating()) {
    delay(300);
    ESP.restart();
  }
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  esp_task_wdt_reset();
  if (up.status == UPLOAD_FILE_START) {
    otaAuth = requestAuthorized();
    if (!otaAuth) {
      otaResult = 401;
      deviceLog("HTTP OTA authentication failed");
      return;
    }
    otaResult = 500;
    deviceLogf("HTTP OTA started: %s", up.filename.c_str());
    beforeFlashing();
    if (Update.begin(UPDATE_SIZE_UNKNOWN)) {
      otaActive = true;
    } else {
      Update.printError(Serial);
    }
  } else if (!otaActive) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
    otaLastActivity = millis();
    static int lastPct = -10;
    size_t len = server.clientContentLength();
    int pct = len ? (int)(up.totalSize * 100ULL / len) : -1;
    if (ui_updating() && (pct < 0 || pct >= lastPct + 5 || up.totalSize < up.currentSize * 2)) {
      lastPct = pct;
      ui_update_progress(pct);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      deviceLogf("HTTP OTA wrote %u bytes", up.totalSize);
      otaResult = 200;
    } else {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaActive = false;
  }
}

static void handleState() {
  String body = server.arg("plain");
  if (!parse_state(body.c_str(), body.length(), incoming)) {
    server.send(400, "text/plain", "bad state");
    return;
  }
  ui_set_state(incoming);
  server.send(200, "text/plain", "OK");
  // Cached so a reboot shows numbers before the next poll. Routine writes are
  // throttled so SPI flash erases don't glitch the RGB scanout, but a new
  // local day is written immediately. Otherwise a reboot keeps calling
  // yesterday's totals "today".
  static uint32_t lastCacheMs = 0;
  static int cachedDay = -1;
  uint32_t now = millis();
  int day = (int)((incoming.t + incoming.tz) / 86400);
  bool newDay = incoming.t && day != cachedDay;
  if (fsOk && (newDay || !lastCacheMs || now - lastCacheMs >= 15UL * 60 * 1000)) {
    lastCacheMs = now ? now : 1;
    cachedDay = day;
    File f = LittleFS.open(STATE_FILE, "w");
    if (f) {
      f.print(body);
      f.close();
    }
  }
}

static void handleEvent() {
  String body = server.arg("plain");
  Event e;
  if (!parse_event(body.c_str(), body.length(), e)) {
    server.send(400, "text/plain", "bad event");
    return;
  }
  ui_event(e);
  server.send(200, "text/plain", "OK");
}

static void handleSettings() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"pollSec\":%d,\"pollNow\":%lu,\"fw\":\"%s\",\"board\":\"%s\",\"heap\":%u,\"heapMin\":%u,\"rssi\":%d,"
           "\"quiet\":%d,\"quietStart\":%d,\"quietEnd\":%d}",
           setting_value(SET_POLL), (unsigned long)poll_now_seq(), FW_VERSION, BOARD_NAME,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (int)WiFi.RSSI(),
           setting_value(SET_QUIET), settings_quiet_start(), settings_quiet_end());
  server.send(200, "application/json", buf);
}

static void redirectConsole(const char* tab) {
  server.sendHeader("Location", String("/#") + tab, true);
  server.send(303, "text/plain", "Saved");
}

static void handleSaveSettings() {
  if (!requireAuthorization()) return;
  for (int i = 0; i < NSET; i++) {
    if (server.hasArg(SETTINGS[i].key))
      setting_set_value((SettingId)i, server.arg(SETTINGS[i].key).toInt());
  }
  if (server.hasArg("quiet_form")) {
    setting_set_value(SET_QUIET, server.hasArg("quieton") ? 1 : 0);
    // Minutes since midnight (the console's selects) or "HH:MM"; anything
    // unparsable keeps the old time.
    auto hhmm = [](const String& v, int keep) {
      int h, m;
      if (v.indexOf(':') < 0) {
        long mins = v.length() ? v.toInt() : -1;
        return mins >= 0 && mins < 1440 ? (int)mins : keep;
      }
      if (sscanf(v.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return keep;
      return h * 60 + m;
    };
    settings_set_quiet(hhmm(server.arg("qstart"), settings_quiet_start()),
                       hhmm(server.arg("qend"), settings_quiet_end()));
  }
  ui_refresh_from_web();
  deviceLog("Display and alert settings saved from web");
  redirectConsole("display");
}

static bool validHostname(const String& host) {
  if (!host.length() || host.length() > 32) return false;
  if (host[0] == '-' || host[host.length() - 1] == '-') return false;
  for (unsigned i = 0; i < host.length(); i++) {
    char c = host[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

static void handleSaveHost() {
  if (!requireAuthorization()) return;
  if (server.hasArg("clear")) {
    settings_set_hostname("");
  } else {
    String host = server.arg("hostname");
    host.trim();
    host.toLowerCase();
    if (host.length() && !validHostname(host)) {
      server.send(400, "text/plain", "Use 1-23 letters, digits, or hyphens");
      return;
    }
    settings_set_hostname(host.c_str());
  }
  if (server.hasArg("mdns_form")) settings_set_mdns(server.hasArg("mdns"));
  net_apply_hostname();
  server.send(200, "text/plain", String("Now ") + HOSTNAME + (settings_mdns() ? ".local" : " (mDNS off)"));
}

static void handleSaveOta() {
  if (!requireAuthorization()) return;
  settings_set_auto_ota(server.hasArg("auto"));
  bool oldBeta = settings_beta_ota();
  settings_set_beta_ota(server.hasArg("beta"));
  if (oldBeta != settings_beta_ota()) ota_forget_latest();
  deviceLog("GitHub update preferences saved");
  redirectConsole("network");
}

static void handleSavePassword() {
  if (!requireAuthorization()) return;
  if (server.hasArg("clear")) {
    settings_set_ota_password("");
    deviceLog("Web/OTA password cleared");
  } else {
    String pw = server.arg("password");
    pw.trim();
    settings_set_ota_password(pw.c_str());
    deviceLog(pw.length() ? "Web/OTA password changed" : "Web/OTA password left blank");
  }
  redirectConsole("network");
}

static void webOtaProgress(int pct) {
  otaLastActivity = millis();
  if (display_is_suspended()) return;
  if (ui_updating()) {
    ui_update_progress(pct);
    return;
  }
  char msg[40];
  snprintf(msg, sizeof(msg), "Downloading update... %d%%", pct);
  ui_toast(msg);
  ui_tick();
}

static void handleGithubUpdate() {
  if (!requireAuthorization()) return;
  char ver[32], url[256], err[96];
  bool checkOnly = server.arg("action") != "install";
  if (!ota_check_github_update(ver, sizeof(ver), url, sizeof(url), err, sizeof(err))) {
    String body = String("{\"ok\":false,\"message\":\"") + htmlEscape(err) + "\"}";
    server.send(502, "application/json", body);
    return;
  }
  if (checkOnly) {
    String msg = ota_update_available() ? "Update available" : "Already up to date";
    String body = String("{\"ok\":true,\"version\":\"") + latestVersion +
                  "\",\"message\":\"" + msg + "\"}";
    server.send(200, "application/json", body);
    return;
  }
  if (!ota_update_available()) {
    String body = String("{\"ok\":true,\"version\":\"") + latestVersion +
                  "\",\"message\":\"Already up to date\"}";
    server.send(200, "application/json", body);
    return;
  }
  deviceLogf("Installing GitHub release v%s", latestVersion);
  if (!ota_install_github_update(latestUrl, webOtaProgress, err, sizeof(err))) {
    deviceLogf("GitHub OTA failed: %s", err);
    String body = String("{\"ok\":false,\"message\":\"") + htmlEscape(err) + "\"}";
    server.send(500, "application/json", body);
    if (display_is_suspended() || ui_updating()) {
      delay(300);
      ESP.restart();
    }
    ui_toast("Update failed - still on v" FW_VERSION);
    return;
  }
  server.send(200, "application/json",
              String("{\"ok\":true,\"version\":\"") + latestVersion +
              "\",\"progress\":100,\"reboot\":true,\"message\":\"Installed; rebooting\"}");
  delay(500);
  ESP.restart();
}

static String cachedStateBody;

// Read while the RGB panel is still off. A flash read with the VSYNC ISR
// running panics, because that ISR restarts GDMA from code that is not in IRAM.
static void readCachedState() {
  cachedStateBody = "";
  if (!fsOk || !LittleFS.exists(STATE_FILE)) return;
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return;
  cachedStateBody = f.readString();
  f.close();
}

static void applyCachedState() {
  if (!cachedStateBody.length()) return;
  if (parse_state(cachedStateBody.c_str(), cachedStateBody.length(), incoming)) ui_set_state(incoming);
  cachedStateBody = "";
}

// (Re)announce <HOSTNAME>.local: http on 80, plus Arduino OTA. Called when
// services start, after every Wi-Fi reconnect and when the name or the mDNS
// switch changes, so no reboot is needed.
static void netAnnounce() {
  MDNS.end();
  g_net.mdns = false;
  if (!settings_mdns() || WiFi.status() != WL_CONNECTED) return;
  if (!MDNS.begin(HOSTNAME)) {
    deviceLogf("mDNS: could not start %s.local", HOSTNAME);
    return;
  }
  MDNS.addService("http", "tcp", 80);
  MDNS.enableArduino(3232, settings_ota_password()[0] != 0);
  g_net.mdns = true;
  deviceLogf("mDNS: %s.local", HOSTNAME);
}

// Apply a new hostname or mDNS setting right away.
void net_apply_hostname() {
  makeHostname();
  WiFi.setHostname(HOSTNAME);  // DHCP name from the next lease
  if (netStarted) {
    ArduinoOTA.end();
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.begin();
  }
  netAnnounce();
}

static void startNetServices() {
  netStarted = true;
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setMdnsEnabled(false);  // netAnnounce() owns the responder
  if (settings_ota_password()[0]) ArduinoOTA.setPassword(settings_ota_password());
  ArduinoOTA.onStart([]() {
    otaPctShown = -10;
    otaLastActivity = millis();
    deviceLog("ArduinoOTA started");
    beforeFlashing();
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    otaLastActivity = millis();
    esp_task_wdt_reset();
    int pct = total ? (int)(done * 100 / total) : 0;
    if (display_is_suspended()) return;
    if (ui_updating()) {
      if (pct >= otaPctShown + 5 || pct == 100) {  // few redraws: the bus is busy
        otaPctShown = pct;
        ui_update_progress(pct);
      }
      return;
    }
    if (pct >= otaPctShown + 10 || pct == 100) {
      otaPctShown = pct;
      char buf[32];
      snprintf(buf, sizeof(buf), "Updating firmware... %d%%", pct);
      ui_toast(buf);
    }
  });
  ArduinoOTA.onError([](ota_error_t err) {
    deviceLogf("ArduinoOTA error %u", (unsigned)err);
    char n[96];
    snprintf(n, sizeof(n), "ArduinoOTA error %u (%s) after %u%%: %s", (unsigned)err,
             err == OTA_AUTH_ERROR ? "auth" : err == OTA_BEGIN_ERROR ? "begin" : err == OTA_CONNECT_ERROR ? "connect"
             : err == OTA_RECEIVE_ERROR ? "receive" : err == OTA_END_ERROR ? "end" : "?",
             (unsigned)max(0, otaPctShown), Update.errorString());
    otaRemember(n);
    if (display_is_suspended() || ui_updating()) {
      delay(200);
      ESP.restart();
    } else {
      ui_toast("Update failed - still on v" FW_VERSION);
    }
  });
  ArduinoOTA.onEnd([]() { deviceLog("ArduinoOTA image verified"); });
  ArduinoOTA.begin();
  netAnnounce();
  logHeap("services");
}

static void updateNet() {
  bool up = WiFi.status() == WL_CONNECTED;
  bool wasUp = g_net.connected;
  g_net.connected = up;
  if (up && !wasUp && netStarted) netAnnounce();  // re-announce after a reconnect
  // No modem sleep: with it the radio wakes every ~100 ms beacon, so LAN round
  // trips run 60-800 ms and OTA uploads (one ack per 1.4 KB) crawl and time
  // out. The panels are mains powered.
  if (up && !wasUp) WiFi.setSleep(false);
  if (up) {
    g_net.portal = false;
    strlcpy(g_net.ip, WiFi.localIP().toString().c_str(), sizeof(g_net.ip));
    strlcpy(g_net.ssid, WiFi.SSID().c_str(), sizeof(g_net.ssid));
    g_net.rssi = WiFi.RSSI();
    if (!netStarted) startNetServices();
  }
}

void setup() {
  heap_caps_register_failed_alloc_callback(allocFailed);
  Serial.begin(115200);
  {
    static const char* const WHY[] = {"unknown", "power-on", "external", "software", "panic", "int-wdt", "task-wdt",
                                      "other-wdt", "deep-sleep", "brownout", "sdio"};
    int r = (int)esp_reset_reason();
    resetWhy = r >= 0 && r <= 10 ? WHY[r] : "?";
    deviceLogf("Reset reason: %s", resetWhy);
    if (otaNoteMagic == 0x0A7E5EED) {
      deviceLogf("Before restart: %s", otaNote);
      otaNoteAtBoot = true;
    }
    otaNoteMagic = 0;
  }
  logHeap("boot");
  settings_load();
  fsOk = LittleFS.begin(true);
  readCachedState();
  display_init();
  ui_splash();
  deviceLogf("Splash on screen at %lu ms (XIP %s)", millis(), xipActive() ? "on" : "off");
  logHeap("display");
  touch_init();
  ui_init();
  logHeap("ui");
  applyCachedState();
  // A hung loop (UI, HTTP or network) reboots instead of staying frozen. 30 s
  // covers the blocking pieces (GitHub check, OTA, page slides); OTA progress
  // feeds it too.
  esp_task_wdt_config_t wdt = {.timeout_ms = 30000, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  enableLoopWDT();
  if (otaNoteAtBoot) ui_toast("The last update didn't finish - see the web console log");
  knob_init();
  sound_init();
  logHeap("sound");

  makeHostname();
  WiFi.setHostname(HOSTNAME);
  // Join the saved network in the background; a failed join keeps retrying
  // and the UI offers the Wi-Fi page (never a hotspot).
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // netRetryTick() paces reconnects
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    lastDiscReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  char saved[33];
  if (net_saved_ssid(saved, sizeof(saved))) WiFi.begin();
  else ui_wifi_page(true);  // first boot: choose a network
  logHeap("wifi");

  server.on("/ping", HTTP_GET, []() { server.send(200, "text/plain", "OK"); });
  server.on("/v2/settings", HTTP_GET, handleSettings);
  server.on("/v2/state", HTTP_POST, handleState);
  server.on("/v2/event", HTTP_POST, handleEvent);
  server.on("/update", HTTP_GET, []() {
    if (!requireAuthorization()) return;
    redirectConsole("firmware");
  });
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/save", HTTP_POST, handleSaveSettings);
  server.on("/save-ota", HTTP_POST, handleSaveOta);
  server.on("/save-password", HTTP_POST, handleSavePassword);
  server.on("/save-host", HTTP_POST, handleSaveHost);
  server.on("/wifi-setup", HTTP_POST, []() {
    if (!requireAuthorization()) return;
    server.send(200, "text/plain", "Wi-Fi page opened on the panel");
    wifi_start_setup();
  });
  server.on("/poll-now", HTTP_POST, []() {
    if (!requireAuthorization()) return;
    poll_now_request();
    deviceLog("Manual MakerWorld poll requested");
    server.send(200, "text/plain", "Poll requested");
  });
  server.on("/github-update", HTTP_POST, handleGithubUpdate);
  server.on("/diagnostics/logs", HTTP_GET, []() {
    if (!requireAuthorization()) return;
    server.send(200, "text/plain; charset=utf-8", logSnapshot());
  });
  server.on("/diagnostics/logs", HTTP_DELETE, []() {
    if (!requireAuthorization()) return;
    logClear();
    deviceLog("Log cleared from web");
    server.send(200, "text/plain", "Cleared");
  });
  server.on("/diagnostics", HTTP_GET, handleDiagnostics);
  server.on("/screenshot.png", HTTP_GET, handleScreenshot);
  server.on("/v2/ui/show", HTTP_POST, []() {
    if (!requireAuthorization()) return;
    char shown[64];
    bool ok = ui_show(server.arg("page").c_str(), server.arg("tab").c_str(), server.arg("open").c_str(), shown,
                      sizeof(shown), server.arg("slide") == "1");
    if (!ok) shown[0] = 0;  // never echo an unknown name back
    String out = String("{\"ok\":") + (ok ? "true" : "false") + ",\"shown\":\"" + shown + "\"}";
    server.send(ok ? 200 : 400, "application/json", out);
  });
  server.on("/v2/ui/pages", HTTP_GET, []() {
    String out;
    ui_pages_json(out);
    server.send(200, "application/json", out);
  });
  server.on("/reset-wifi", HTTP_POST, []() {
    if (!requireAuthorization()) return;
    WiFi.disconnect(true, true);  // forget the saved network
    server.send(200, "text/plain", "Wi-Fi settings cleared; rebooting");
    delay(400);
    ESP.restart();
  });
  server.on("/", HTTP_GET, []() {
    if (apClient()) handleApPage();  // a phone on the setup hotspot
    else handleConsole();
  });
  server.on("/ap-connect", HTTP_POST, []() {
    if (!apClient()) return server.send(403, "text/plain", "setup hotspot only");
    String ssid = server.arg("ssid");
    ssid.trim();
    if (!ssid.length()) ssid = server.arg("pick");
    if (!ssid.length()) return server.send(400, "text/plain", "choose a network");
    apResult = 1;
    snprintf(apMsg, sizeof(apMsg), "Connecting to %s...", ssid.c_str());
    server.send(200, "text/plain", "connecting");
    net_connect(ssid.c_str(), server.arg("pass").c_str());
  });
  server.on("/ap-status", HTTP_GET, []() {
    char b[160];
    snprintf(b, sizeof(b), "{\"state\":%d,\"msg\":\"%s\"}", apResult, apMsg);
    server.send(200, "application/json", b);
  });
  // Captive portal: anything else asked over the hotspot goes to the setup page.
  server.onNotFound([]() {
    if (apClient()) {
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "not found");
    }
  });
  server.begin();
  deviceLog("Web console started");
  updateNet();
  logHeap("server");
}

#ifdef SERIAL_SHOTS
// Dev builds only (PLATFORMIO_BUILD_FLAGS=-DSERIAL_SHOTS): layout work over
// USB when the panel isn't on the network. "show <page> [tab] [open]" goes to
// a screen; "shot" sends the raw RGB565 framebuffer between SHOT and END.
static void serialShotTick() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n') {
      if (line.length() < 96) line += c;
      continue;
    }
    line.trim();
    if (line.startsWith("show ")) {
      String a[3];
      int n = 0, from = 5;
      while (n < 3 && from <= (int)line.length()) {
        int sp = line.indexOf(' ', from);
        if (sp < 0) sp = line.length();
        a[n++] = line.substring(from, sp);
        from = sp + 1;
      }
      char shown[64] = "";
      bool ok = ui_show(a[0].c_str(), a[1].c_str(), a[2].c_str(), shown, sizeof(shown), false);
      Serial.printf("SHOWN %s %s\n", ok ? "ok" : "fail", shown);
    } else if (line == "shot") {
      const uint16_t* fbp = display_framebuffer();
      if (!fbp) {
        Serial.println("SHOT none");
      } else {
        Serial.printf("SHOT %d %d\n", LCD_NATIVE_W, LCD_NATIVE_H);
        Serial.write((const uint8_t*)fbp, LCD_NATIVE_W * LCD_NATIVE_H * 2);
        Serial.println("END");
      }
    }
    line = "";
  }
}
#endif

void loop() {
  static uint32_t lastNet = 0;
  static bool idleLogged = false;
  if (!idleLogged && millis() > 60000) {  // steady state after the first poll
    idleLogged = true;
    logHeap("idle");
  }
  netConnTick();
  netRetryTick();
  apTick();
  server.handleClient();
  if (netStarted) ArduinoOTA.handle();

#ifdef SERIAL_SHOTS
  serialShotTick();
#endif
  if (!display_is_suspended()) {
    ui_input(knob_poll());
    ui_tick();
  }
  if ((display_is_suspended() || ui_updating()) && !otaActive && !otaDownloadActive &&
      millis() - otaLastActivity > 20000) {
    // The display is only switched off (or covered by the progress screen)
    // for a firmware update. One that stalled or ended without an error
    // callback would leave it that way with the panel still online;
    // restarting brings it back on the old firmware.
    deviceLog("Update screen up with no update running; restarting");
    otaRemember("update screen with no update running (stalled update?)");
    delay(200);
    ESP.restart();
  }

  if (millis() - lastNet > 1000) {
    lastNet = millis();
    updateNet();
    time_t now = time(nullptr);
    if (now > 1700000000) settings_stamp_install(FW_VERSION, (uint32_t)now);
    if (g_net.connected) {
      if (!wifiUpSince) wifiUpSince = millis();
      // Hourly; after failures (no network, private repo) the gap doubles up
      // to 16 h so a check that can't work doesn't run and log every hour.
      static uint8_t ghFails = 0;
      bool initialDue = !lastGithubCheck && millis() - wifiUpSince > 120000UL;
      bool hourlyDue = lastGithubCheck && millis() - lastGithubCheck > (3600000UL << min<int>(ghFails, 4));
      if (settings_auto_ota() && !settings_github_missing() && (initialDue || hourlyDue) &&
          !otaActive && !otaDownloadActive && !ui_overlay_open()) {
        char ver[32], url[256], err[96];
        if (ota_check_github_update(ver, sizeof(ver), url, sizeof(url), err, sizeof(err))) {
          ghFails = 0;
        } else {
          lastGithubCheck = millis(); // Avoid retrying a failed API call every second.
          if (ghFails < 255) ghFails++;
        }
      }
    } else {
      wifiUpSince = 0;
    }
  }
  delay(2);
}
