// MakerMonitor Color: stats panel for the Waveshare ESP32-S3-LCD-3.16 and the
// Elecrow CrowPanel ESP32-S3 HMI 4.3" / 5" / 7" (board picked by the env).
// The poller pushes POST /v2/state (full stats) and /v2/event (one change);
// GET /v2/settings tells the poller the chosen poll interval / "poll now".
#include <Arduino.h>
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
#include <WiFiManager.h>

#include "display.h"
#include "knob.h"
#include "sound.h"
#include "fw_version.h"
#include "model.h"
#include "board.h"
#include "settings.h"
#include "touch.h"
#include "ui.h"

static char HOSTNAME[24] = "makermonitor";
static const char* AP_NAME = BOARD_AP;

// makermonitor- plus the last 5 hex digits of the STA MAC, e.g. makermonitor-e3b7d.
static void makeHostname() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(HOSTNAME, sizeof(HOSTNAME), "makermonitor-%01x%02x%02x", mac[3] & 0x0f, mac[4], mac[5]);
  strlcpy(g_net.host, HOSTNAME, sizeof(g_net.host));
}
static const char* STATE_FILE = "/state.json";

NetInfo g_net = {};
static WebServer server(80);
static WiFiManager wm;
static State incoming;
static bool netStarted = false;
static bool fsOk = false;
static int otaResult = 0;  // HTTP status for the /update upload in progress
static bool otaActive = false;
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

static void deviceLogf(const char* fmt, ...) {
  char b[LOG_W];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  deviceLog(b);
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
    "@media(max-width:720px){main{width:min(100% - 18px,1100px);padding-top:16px}header{align-items:flex-start}"
    ".card{grid-column:span 12}.metrics{grid-template-columns:repeat(2,1fr)}.row{align-items:flex-start;"
    "flex-direction:column}select,input{max-width:100%;width:100%}}"
    "</style></head><body><main><header><div><div class=brand>Maker<i>Monitor</i></div><div style='color:var(--muted)'>");
  p += BOARD_NAME;
  p += F("</div></div><div class=online>● ");
  p += g_net.connected ? htmlEscape(g_net.ssid) : F("Offline");
  p += F("</div></header><nav class=tabs><button class=on data-tab=status>Status</button>"
         "<button data-tab=firmware>Firmware</button><button data-tab=display>Display &amp; alerts</button>"
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
  p += F(" h</b></div></div><div class='card wide'><h2>Recent device log</h2><pre id=logs>");
  p += htmlEscape(logSnapshot());
  p += F("</pre><button class=secondary onclick=loadLogs()>Refresh log</button></div></div></section>"
         "<section id=firmware class=tab><div class=grid><div class=card><span class=tag>INSTALLED</span>"
         "<p class=version>v" FW_VERSION "</p><p>This device is running the " BOARD_OTA_ASSET " build.</p></div>"
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
         "<form method=post action=/update enctype=multipart/form-data><input type=file name=firmware accept=.bin required> "
         "<button>Upload &amp; install</button></form></div></div></section>"
         "<section id=display class=tab><form action=/save method=post><div class=grid><div class=card><h2>Display</h2>");
  const SettingId displayIds[] = {SET_BRIGHTNESS, SET_AUTOCYCLE, SET_IDLE, SET_DIM};
  for (SettingId id : displayIds) {
    p += F("<label class=row><span class=label>");
    p += SETTINGS[id].label;
    p += F("</span><select name=");
    p += SETTINGS[id].key;
    p += F(">");
    p += optionList(id);
    p += F("</select></label>");
  }
  p += F("</div><div class=card><h2>Alerts &amp; polling</h2>");
  const SettingId alertIds[] = {SET_NOTIFY, SET_QUIET, SET_POLL, SET_BANNER};
  for (SettingId id : alertIds) {
    p += F("<label class=row><span class=label>");
    p += SETTINGS[id].label;
    p += F("</span><select name=");
    p += SETTINGS[id].key;
    p += F(">");
    p += optionList(id);
    p += F("</select></label>");
  }
  p += F("</div><div class='card wide'><button>Save display settings</button></div></div></form></section>"
         "<section id=network class=tab><div class=grid><div class=card><h2>Wi-Fi</h2>"
         "<div class=row><span>Network</span><b>");
  p += g_net.connected ? htmlEscape(g_net.ssid) : F("Offline");
  p += F("</b></div><div class=row><span>Address</span><b>");
  p += g_net.connected ? WiFi.localIP().toString() : F("—");
  p += F("</b></div><div class=row><span>Hostname</span><b>");
  p += g_net.host;
  p += F(".local</b></div><div class=actions><button class=danger onclick=\"post('/reset-wifi',true)\">Reset Wi-Fi</button></div></div>"
         "<div class=card><h2>Console security</h2><p>Leave blank for local-network access without a password. "
         "Set a password to protect this console and OTA uploads (username: <b>admin</b>).</p>"
         "<form action=/save-password method=post><input type=password name=password maxlength=47 "
         "placeholder='New password'><div class=actions><button>Save password</button>"
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
         "if(j.progress)pr.value=j.progress;if(j.reboot)setTimeout(()=>location.reload(),12000)}"
         "async function loadLogs(){qs('#logs').textContent=await(await fetch('/diagnostics/logs')).text()}"
         "</script></main></body></html>");
  server.send(200, "text/html; charset=utf-8", p);
}

static void handleUpdateDone() {
  int code = otaResult ? otaResult : 500;
  otaResult = 0;
  otaActive = false;
  server.sendHeader("Connection", "close");
  server.send(code, "text/plain", code == 200 ? "OK - rebooting" : code == 401 ? "Authentication required" : "Update failed");
  if (code == 200) {
    delay(300);
    ESP.restart();
  }
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaAuth = requestAuthorized();
    if (!otaAuth) {
      otaResult = 401;
      deviceLog("HTTP OTA authentication failed");
      return;
    }
    otaResult = 500;
    deviceLogf("HTTP OTA started: %s", up.filename.c_str());
    if (Update.begin(UPDATE_SIZE_UNKNOWN)) {
      otaActive = true;
      ui_toast("Updating firmware...");
      ui_tick();
    } else {
      Update.printError(Serial);
    }
  } else if (!otaActive) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
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
  // Cached so a reboot shows numbers before the next poll. Throttle flash
  // writes to once every 15 minutes so SPI flash erases don't cause periodic
  // RGB scanout glitches.
  static uint32_t lastCacheMs = 0;
  uint32_t now = millis();
  if (fsOk && (!lastCacheMs || now - lastCacheMs >= 15UL * 60 * 1000)) {
    lastCacheMs = now ? now : 1;
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
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"pollSec\":%d,\"pollNow\":%lu,\"fw\":\"%s\",\"board\":\"%s\"}",
           setting_value(SET_POLL), (unsigned long)poll_now_seq(), FW_VERSION, BOARD_NAME);
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
  ui_refresh_from_web();
  deviceLog("Display and alert settings saved from web");
  redirectConsole("display");
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
    ui_toast("Update failed - still on v" FW_VERSION);
    return;
  }
  server.send(200, "application/json",
              String("{\"ok\":true,\"version\":\"") + latestVersion +
              "\",\"progress\":100,\"reboot\":true,\"message\":\"Installed; rebooting\"}");
  delay(500);
  ESP.restart();
}

static void loadCachedState() {
  if (!fsOk || !LittleFS.exists(STATE_FILE)) return;
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return;
  String body = f.readString();
  f.close();
  if (parse_state(body.c_str(), body.length(), incoming)) ui_set_state(incoming);
}

static void startNetServices() {
  netStarted = true;
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  ArduinoOTA.setHostname(HOSTNAME);
  if (settings_ota_password()[0]) ArduinoOTA.setPassword(settings_ota_password());
  ArduinoOTA.onStart([]() {
    otaPctShown = -10;
    ui_toast("Updating firmware...");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    int pct = total ? (int)(done * 100 / total) : 0;
    if (pct >= otaPctShown + 10 || pct == 100) {
      otaPctShown = pct;
      char buf[32];
      snprintf(buf, sizeof(buf), "Updating firmware... %d%%", pct);
      ui_toast(buf);
    }
  });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("ArduinoOTA error %u\n", (unsigned)err);
    ui_toast("Update failed - still on v" FW_VERSION);
  });
  ArduinoOTA.onEnd([]() { ui_toast("Update done - rebooting"); });
  ArduinoOTA.begin();
}

static void updateNet() {
  bool up = WiFi.status() == WL_CONNECTED;
  g_net.connected = up;
  if (up) {
    g_net.portal = false;
    strlcpy(g_net.ip, WiFi.localIP().toString().c_str(), sizeof(g_net.ip));
    strlcpy(g_net.ssid, WiFi.SSID().c_str(), sizeof(g_net.ssid));
    g_net.rssi = WiFi.RSSI();
    if (!netStarted) startNetServices();
  }
}

void setup() {
  Serial.begin(115200);
  settings_load();
  display_init();
  touch_init();
  ui_init();
  knob_init();
  sound_init();

  fsOk = LittleFS.begin(true);
  loadCachedState();

  makeHostname();
  WiFi.setHostname(HOSTNAME);
  wm.setHostname(HOSTNAME);
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(20);
  if (!wm.autoConnect(AP_NAME)) {
    g_net.portal = true;
    ui_toast("Wi-Fi setup: join " BOARD_AP);
  }

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
  server.on("/reset-wifi", HTTP_POST, []() {
    if (!requireAuthorization()) return;
    wm.resetSettings();
    server.send(200, "text/plain", "Wi-Fi settings cleared; rebooting");
    delay(400);
    ESP.restart();
  });
  server.on("/", HTTP_GET, handleConsole);
  server.begin();
  deviceLog("Web console started");
  updateNet();
}

void loop() {
  static uint32_t lastNet = 0;
  if (g_net.portal) wm.process();
  server.handleClient();
  if (netStarted) ArduinoOTA.handle();

  ui_input(knob_poll());
  ui_tick();

  if (millis() - lastNet > 1000) {
    lastNet = millis();
    updateNet();
    if (g_net.connected) {
      if (!wifiUpSince) wifiUpSince = millis();
      bool initialDue = !lastGithubCheck && millis() - wifiUpSince > 120000UL;
      bool hourlyDue = lastGithubCheck && millis() - lastGithubCheck > 3600000UL;
      if (settings_auto_ota() && (initialDue || hourlyDue) &&
          !otaActive && !otaDownloadActive && !ui_overlay_open()) {
        char ver[32], url[256], err[96];
        if (!ota_check_github_update(ver, sizeof(ver), url, sizeof(url), err, sizeof(err)))
          lastGithubCheck = millis(); // Avoid retrying a failed API call every second.
      }
    } else {
      wifiUpSince = 0;
    }
  }
  delay(2);
}
