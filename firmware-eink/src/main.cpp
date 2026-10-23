#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <string.h>
#include <time.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <esp_mac.h>
#include "soc/gpio_reg.h"
#include "EPD.h"
#include "spi.h"
#include "ota_secret.h"

static const int PIN_EPD_PWR = 7;
static const int PIN_PWR_LED = 19;

#define FW_VERSION "0.3.0"
#define BOARD_NAME "CrowPanel 2.13 e-ink"

static char HOSTNAME[24] = "makermonitor";

static void makeHostname() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(HOSTNAME, sizeof(HOSTNAME), "makermonitor-%01x%02x%02x", mac[3] & 0x0f, mac[4], mac[5]);
}

extern uint8_t ImageBW[ALLSCREEN_BYTES];

WebServer server(80);
Preferences prefs;

struct Stats {
  int totalDownloads = 0;
  int totalPrints = 0;
  int totalBoosts = 0;
  float totalPoints = 0.0f;
  char accountName[32] = "";
  bool initialized = false;
} stats;

struct Notification {
  String type;
  String modelName;
  float value = 0;
  float total = 0;
  float points = 0;
  String user;
  String text;
  unsigned long timestamp = 0;
  bool active = false;
} currentNotif;

struct StatusLine {
  String auth = "needed";
  String message = "waiting cookie";
  unsigned long updatedAt = 0;
} statusLine;

unsigned long refreshCount = 0;
unsigned long lastSyncMs = 0;  // last data from the poller (pages/sync/notify)
long tzOffset = 0;             // poller host's UTC offset, sent with /pages

// ---- Settings (menu-editable, kept in NVS "mmset") ----
enum SetId { S_CYCLE, S_POLL, S_ALERTS, S_ALERT_TIME, S_SOUND, S_QUIET, S_DEEP, NSET };
struct SetDef {
  const char *key;
  const char *label;
  uint8_t count;
  const char *const *names;
  const int *values;
  uint8_t def;
};
static const char *const CYCLE_N[] = {"AUTO", "OFF", "15 S", "30 S", "60 S", "120 S"};
static const int CYCLE_V[] = {-1, 0, 15, 30, 60, 120};  // -1 = poller's cycle
static const char *const POLL_N[] = {"5 MIN", "10 MIN", "15 MIN", "30 MIN", "60 MIN"};
static const int POLL_V[] = {300, 600, 900, 1800, 3600};
static const char *const ONOFF_N[] = {"OFF", "ON"};
static const int ONOFF_V[] = {0, 1};
static const char *const ALERT_T_N[] = {"10 S", "20 S", "40 S"};
static const int ALERT_T_V[] = {10, 20, 40};
// Quiet start*100 + end hour.
static const char *const QUIET_N[] = {"OFF", "22-07", "23-07", "00-07"};
static const int QUIET_V[] = {0, 2207, 2307, 2407};
static const char *const DEEP_N[] = {"EVERY 4", "EVERY 8", "EVERY 16"};
static const int DEEP_V[] = {4, 8, 16};
static const SetDef SETS[NSET] = {
  {"cyc", "Page cycle", 6, CYCLE_N, CYCLE_V, 0},
  {"poll", "Poll every", 5, POLL_N, POLL_V, 1},
  {"alrt", "Alerts", 2, ONOFF_N, ONOFF_V, 1},
  {"alt", "Alert time", 3, ALERT_T_N, ALERT_T_V, 1},
  {"snd", "Sound", 2, ONOFF_N, ONOFF_V, 0},
  {"quiet", "Quiet hours", 4, QUIET_N, QUIET_V, 0},
  {"deep", "Deep refresh", 3, DEEP_N, DEEP_V, 1},
};
uint8_t setIdx[NSET];
uint32_t pollSeq = 0;  // bumped by PRESS on Status; the poller watches it

int setVal(SetId i) { return SETS[i].values[setIdx[i]]; }

void loadSettings() {
  // Sound used to live in "makermon" (/sound?on=); carry it over once.
  prefs.begin("makermon", true);
  bool legacySound = prefs.getBool("snd", false);
  prefs.end();
  prefs.begin("mmset", true);
  for (int i = 0; i < NSET; i++) {
    uint8_t d = (i == S_SOUND && legacySound) ? 1 : SETS[i].def;
    uint8_t v = prefs.getUChar(SETS[i].key, d);
    setIdx[i] = v < SETS[i].count ? v : SETS[i].def;
  }
  pollSeq = prefs.getUInt("pnow", 0);
  prefs.end();
}

void saveSetting(int i) {
  prefs.begin("mmset", false);
  prefs.putUChar(SETS[i].key, setIdx[i]);
  prefs.end();
}

void requestPoll() {
  pollSeq++;
  prefs.begin("mmset", false);
  prefs.putUInt("pnow", pollSeq);
  prefs.end();
}

// Needs NTP time and the poller's UTC offset; unknown time = not quiet.
bool quietNow() {
  int q = setVal(S_QUIET);
  time_t t = time(nullptr);
  if (!q || t < 1700000000) return false;
  int h = (int)(((t + tzOffset) / 3600) % 24);
  int start = q / 100 % 24, end = q % 100;
  return start <= end ? (h >= start && h < end) : (h >= start || h < end);
}

// Factory CrowPanel 2.13" keys (active LOW). Wheel up/down = PRV/NEXT, press = OK.
static const int KEY_HOME = 2;
static const int KEY_EXIT = 1;
static const int KEY_PRV = 6;
static const int KEY_NEXT = 4;
static const int KEY_OK = 5;
static const int PIN_BUZZER = 40;  // spare GPIO_D header pin

enum Key { K_NONE, K_HOME, K_EXIT, K_PRV, K_NEXT, K_OK, K_HOME_HOLD };
static const int keyPins[5] = {KEY_HOME, KEY_EXIT, KEY_PRV, KEY_NEXT, KEY_OK};
static const Key keyIds[5] = {K_HOME, K_EXIT, K_PRV, K_NEXT, K_OK};

// Sections/pages pushed by the poller. The account-totals dashboard is
// appended to every section (first in section 0, last in the others).
JsonDocument pagesDoc;
String pagesRaw;
const int MAX_SECTIONS = 8;
int secMap[MAX_SECTIONS];
int secCount = 0;
int curSection = 0;
int curPage = 0;
bool cyclePaused = false;
unsigned long cycleMs = 30000;  // poller's cycle; the Page cycle setting can override
// Screens outside the section/page flow, opened from the HOME-hold menu.
enum Mode { M_PAGES, M_MENU, M_SETTINGS, M_STATUS };
Mode mode = M_PAGES;
int menuSel = 0, setSel = 0, setTop = 0;
unsigned long lastKeyMs = 0;
unsigned long pollReqAt = 0;  // shows "POLL REQUESTED" on Status for a while
const unsigned long IDLE_HOME_MS = 120000;
unsigned long lastFlip = 0;
unsigned long redrawAt = 0;  // coalesces fast wheel spins into one refresh
bool otaActive = false;
int otaResult = 0;  // HTTP status for the /update request in progress

void saveStats() {
  prefs.begin("makermon", false);
  prefs.putInt("dl", stats.totalDownloads);
  prefs.putInt("pr", stats.totalPrints);
  prefs.putInt("bst", stats.totalBoosts);
  prefs.putFloat("pts", stats.totalPoints);
  prefs.putString("acct", String(stats.accountName));
  prefs.putBool("init", stats.initialized);
  prefs.end();
}

void loadStats() {
  prefs.begin("makermon", true);
  stats.totalDownloads = prefs.getInt("dl", 0);
  stats.totalPrints = prefs.getInt("pr", 0);
  stats.totalBoosts = prefs.getInt("bst", 0);
  stats.totalPoints = prefs.getFloat("pts", 0.0f);
  String acct = prefs.getString("acct", "");
  strncpy(stats.accountName, acct.c_str(), sizeof(stats.accountName) - 1);
  stats.accountName[sizeof(stats.accountName) - 1] = '\0';
  stats.initialized = prefs.getBool("init", false);
  prefs.end();
}

String truncateStr(const String &s, size_t maxLen) {
  if (s.length() <= maxLen) return s;
  return s.substring(0, maxLen - 1) + ".";
}

void epdBeginFrame() {
  memset(ImageBW, 0xFF, ALLSCREEN_BYTES);
}

// JD79661 controller: no SSD1680-style partial update, so every frame is a
// full refresh (PON must precede DRF). The library's EPD_PartUpdate() is SSD1680
// code and does nothing useful here.
void epdEndFrame(bool fullRefresh) {
  (void)fullRefresh;
  // Every N refreshes (Deep refresh setting) flash white first to clear ghosting.
  if (++refreshCount >= (unsigned long)setVal(S_DEEP)) {
    refreshCount = 0;
    static uint8_t white[ALLSCREEN_BYTES];
    memset(white, 0xFF, sizeof(white));
    EPD_Init();
    EPD_PowerOn();
    EPD_DisplayImage(white);
    EPD_PowerOn();
    EPD_Update();
    EPD_Sleep();
  }
  EPD_Init();
  EPD_PowerOn();
  EPD_DisplayImage(ImageBW);
  EPD_PowerOn();
  EPD_Update();
  EPD_Sleep();
}
// ---- Drawing ----
// Same coordinates as EPD_ShowString (small x is the left edge). A cleared
// framebuffer bit is black ink; EPD_DrawPoint(WHITE) clears it.
static const int SCR_W = 250;
static const int SCR_H = 122;

inline void gfxDot(int x, int y, bool ink) {
  if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
  EPD_DrawPoint(x, y, ink ? WHITE : BLACK);
}

void gfxFill(int x, int y, int w, int h, bool ink = true) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) gfxDot(xx, yy, ink);
}

// Layer-line texture: every other row inked, reads as grey on e-paper.
void gfxHatch(int x, int y, int w, int h) {
  for (int yy = y; yy < y + h; yy += 2)
    for (int xx = x; xx < x + w; xx++) gfxDot(xx, yy, true);
}

void gfxRect(int x, int y, int w, int h, bool ink = true) {
  for (int xx = x; xx < x + w; xx++) {
    gfxDot(xx, y, ink);
    gfxDot(xx, y + h - 1, ink);
  }
  for (int yy = y; yy < y + h; yy++) {
    gfxDot(x, yy, ink);
    gfxDot(x + w - 1, yy, ink);
  }
}

void gfxHLine(int x0, int x1, int y, bool dotted = false, bool ink = true) {
  for (int x = x0; x <= x1; x++)
    if (!dotted || ((x - x0) % 3 == 0)) gfxDot(x, y, ink);
}

void gfxVLine(int x, int y0, int y1, bool dotted = false, int w = 1) {
  for (int y = y0; y <= y1; y++)
    if (!dotted || ((y - y0) % 3 == 0))
      for (int i = 0; i < w; i++) gfxDot(x + i, y, true);
}

int textLen(const char *s) {
  int n = 0;
  for (; *s; s++)
    if (*s >= ' ' && *s <= '~') n++;
  return n;
}

int textWidth(const char *s, int size, int track = 0, bool bold = false) {
  int n = textLen(s);
  return n ? n * (size / 2) + (n - 1) * track + (bold ? 1 : 0) : 0;
}

// align: 'l' x is left edge, 'c' centre, 'r' right edge.
// Glyphs go through EPD_ShowChar, which walks the font the same way the
// factory string routine does (its own x is mirrored by the panel driver).
void gfxText(int x, int y, const char *s, int size, char align = 'l', bool ink = true,
             bool bold = false, int track = 0) {
  if (size != 12 && size != 16 && size != 24 && size != 32 && size != 48) size = 12;
  int cw = size / 2;
  int width = textWidth(s, size, track, bold);
  int origin = x;
  if (align == 'c') origin = x - width / 2;
  else if (align == 'r') origin = x - width;
  int dx = origin + width - 1;
  uint8_t color = ink ? BLACK : WHITE;
  for (const char *p = s; *p; p++) {
    if (*p < ' ' || *p > '~') continue;
    if (dx >= 0 && dx < EPD_W && y >= 0 && y < EPD_H) {
      EPD_ShowChar((uint16_t)dx, (uint16_t)y, *p, color, (uint8_t)size);
      if (bold && dx + 1 < EPD_W)
        EPD_ShowChar((uint16_t)(dx + 1), (uint16_t)y, *p, color, (uint8_t)size);
    }
    dx -= cw + track;
  }
}

String fitText(const String &s, int maxWidth, int size, int track = 0, bool bold = false) {
  if (textWidth(s.c_str(), size, track, bold) <= maxWidth) return s;
  String t = s;
  while (t.length() > 1 && textWidth((t + ".").c_str(), size, track, bold) > maxWidth)
    t.remove(t.length() - 1);
  return t + ".";
}

// Sections on screen: the poller's, with the device's ALL TIME totals page
// inserted as its own section right after TODAY. With no poller data yet,
// ALL TIME is the only section.
int vCount() { return secCount ? secCount + 1 : 1; }
bool isAllTime(int v) { return secCount == 0 || v == 1; }
int pollerSec(int v) { return v == 0 ? 0 : v - 1; }

JsonArray sectionPages(int v) {
  return pagesDoc["sections"][secMap[pollerSec(v)]]["pages"].as<JsonArray>();
}
String sectionName(int v) {
  if (isAllTime(v)) return "ALL TIME";
  return pagesDoc["sections"][secMap[pollerSec(v)]]["name"] | "";
}
int pageCount(int v) {
  if (isAllTime(v)) return 1;
  return max(1, (int)sectionPages(v).size());
}
bool onTotalsPage() { return isAllTime(curSection); }
void rebuildSections() {
  secCount = 0;
  int i = 0;
  for (JsonObject sec : pagesDoc["sections"].as<JsonArray>()) {
    bool hasPages = sec["pages"].as<JsonArray>().size() > 0;
    if (secCount < MAX_SECTIONS && (secCount == 0 || hasPages)) {
      secMap[secCount++] = i;
    }
    i++;
  }
  cycleMs = (unsigned long)(pagesDoc["cycle"] | 30) * 1000UL;
  if (cycleMs < 10000) cycleMs = 10000;
  tzOffset = pagesDoc["tz"] | 0;
  if (curSection >= vCount()) {
    curSection = 0;
    curPage = 0;
  }
  if (curPage >= pageCount(curSection)) curPage = 0;
}

static const int HEADER_H = 18;

// No bold below 24 px: the double-strike fills in M and W.
// Black band: white 16 px title on the left, one square per page on the
// right (filled = current), pause bars when cycling is paused, and the
// section index left of those. Menu screens pass pagesUi = false.
void drawHeader(const String &title, bool pagesUi = true) {
  gfxFill(0, 0, SCR_W, HEADER_H, true);
  int n = pagesUi && secCount ? pageCount(curSection) : 1;
  int squaresW = n > 1 ? n * 8 - 3 : 0;
  int x = SCR_W - 6 - squaresW;
  if (n > 1) {
    for (int i = 0; i < n; i++, x += 8) {
      if (i == curPage) gfxFill(x, 6, 5, 6, false);
      else gfxRect(x, 6, 5, 6, false);
    }
    x = SCR_W - 6 - squaresW;
  }
  if (pagesUi && cyclePaused) {
    x -= 9;
    gfxFill(x, 6, 2, 6, false);
    gfxFill(x + 3, 6, 2, 6, false);
  }
  // Section index, e.g. "2/6".
  if (pagesUi && vCount() > 1) {
    char idx[8];
    snprintf(idx, sizeof(idx), "%d/%d", curSection + 1, vCount());
    x -= 6;
    gfxText(x, 3, idx, 12, 'r', false);
    x -= textWidth(idx, 12);
  }
  String t = title;
  t.toUpperCase();
  t = fitText(t, x - 12, 16);
  gfxText(6, 1, t.c_str(), 16, 'l', false);
}
// Element vocabulary (all optional keys default sensibly):
//   text  {"t","x","y","s", "a":"l|c|r", "b":bold, "k":tracking, "inv":white}
//   rule  {"hl":y, "x0","x1", "d":dotted}   {"vl":x, "y0","y1", "d", "w"}
//   box   {"rect":[x,y,w,h], "f":"s|h|o"}   solid / hatch / outline
void drawElement(JsonObject e) {
  if (!e["hl"].isNull()) {
    gfxHLine(e["x0"] | 0, e["x1"] | (SCR_W - 1), e["hl"], e["d"] | false, !(e["inv"] | false));
  } else if (!e["vl"].isNull()) {
    gfxVLine(e["vl"], e["y0"] | 0, e["y1"] | (SCR_H - 1), e["d"] | false, e["w"] | 1);
  } else if (!e["rect"].isNull()) {
    JsonArray r = e["rect"];
    const char *f = e["f"] | "s";
    bool ink = !(e["inv"] | false);
    if (f[0] == 'h') gfxHatch(r[0], r[1], r[2], r[3]);
    else if (f[0] == 'o') gfxRect(r[0], r[1], r[2], r[3], ink);
    else gfxFill(r[0], r[1], r[2], r[3], ink);
  } else if (!e["t"].isNull()) {
    const char *a = e["a"] | "l";
    gfxText(e["x"] | 0, e["y"] | 0, e["t"] | "", e["s"] | 12, a[0],
            !(e["inv"] | false), e["b"] | false, e["k"] | 0);
  }
}

void drawSectionPage() {
  epdBeginFrame();
  int idx = curPage;
  // A page is either an element array or {"title": ..., "items": [...]}.
  JsonVariant page = sectionPages(curSection)[idx];
  JsonArray items = page.is<JsonArray>() ? page.as<JsonArray>() : page["items"].as<JsonArray>();
  String title = page["title"] | sectionName(curSection);

  drawHeader(title);
  for (JsonObject e : items) drawElement(e);
  epdEndFrame(true);
}

void fmtAge(char *out, size_t cap, unsigned long ms) {
  unsigned long s = ms / 1000;
  if (s < 60) snprintf(out, cap, "%lus", s);
  else if (s < 3600) snprintf(out, cap, "%lum", s / 60);
  else if (s < 86400) snprintf(out, cap, "%luh", s / 3600);
  else snprintf(out, cap, "%lud", s / 86400);
}

void drawDashboard(bool fullRefresh) {
  epdBeginFrame();

  char line[40];
  String account = stats.accountName[0] ? String("@") + stats.accountName : String("MakerMonitor");
  drawHeader(secCount ? String("ALL TIME") : account);

  // Three big counters in columns, 16 px labels underneath.
  const int cx[3] = {42, 125, 208};
  const int values[3] = {stats.totalDownloads, stats.totalPrints, stats.totalBoosts};
  const char *labels[3] = {"DOWNLOADS", "PRINTS", "BOOSTS"};
  for (int i = 0; i < 3; i++) {
    snprintf(line, sizeof(line), "%d", values[i]);
    bool big = textLen(line) <= 4;
    gfxText(cx[i], big ? 21 : 25, line, big ? 32 : 24, 'c');
    gfxText(cx[i], 54, labels[i], 16, 'c');
  }
  gfxVLine(83, 24, 68, true);
  gfxVLine(166, 24, 68, true);
  gfxHatch(0, 74, SCR_W, 5);

  // Points on the left; account (or a problem chip) and sync age on the right.
  snprintf(line, sizeof(line), "%.2f", stats.totalPoints);
  gfxText(6, 86, line, 24, 'l', true, true);
  gfxText(6 + textWidth(line, 24, 0, true) + 5, 93, "PTS", 16, 'l', true, true);

  String chip;
  if (statusLine.auth == "needed") chip = "AUTH NEEDED";
  else if (statusLine.auth == "error") chip = "ERROR";
  if (chip.length()) {
    int w = textWidth(chip.c_str(), 16) + 8;
    gfxFill(SCR_W - 6 - w, 84, w, 18, true);
    gfxText(SCR_W - 10, 85, chip.c_str(), 16, 'r', false);
  } else {
    gfxText(SCR_W - 6, 85, fitText(account, 110, 16).c_str(), 16, 'r');
  }
  if (!stats.initialized || !lastSyncMs) {
    snprintf(line, sizeof(line), "waiting for sync");
  } else {
    char age[12];
    fmtAge(age, sizeof(age), millis() - lastSyncMs);
    snprintf(line, sizeof(line), "synced %s ago", age);
  }
  gfxText(SCR_W - 6, 104, line, 12, 'r');

  epdEndFrame(fullRefresh);
}

// Footer used by the menu screens: dotted rule at y=107, 12 px hint.
void drawHint(const char *hint) {
  gfxHLine(0, SCR_W - 1, 107, true);
  gfxText(SCR_W / 2, 109, hint, 12, 'c');
}

// One 21 px list row; the selected row is inverted.
void drawRow(int y, int w, const char *label, const char *value, bool sel) {
  if (sel) gfxFill(0, y, w, 21, true);
  gfxText(8, y + 2, label, 16, 'l', !sel);
  if (value) gfxText(w - 6, y + 2, value, 16, 'r', !sel);
}

static const char *const MENU_ITEMS[] = {"Settings", "Status"};
static const int NMENU = 2;

void drawMenu() {
  epdBeginFrame();
  drawHeader("MENU", false);
  for (int i = 0; i < NMENU; i++) drawRow(22 + i * 21, SCR_W, MENU_ITEMS[i], nullptr, i == menuSel);
  drawHint("WHEEL: MOVE  PRESS: OPEN  EXIT: BACK");
  epdEndFrame(false);
}

static const int SET_ROWS = 4;

void drawSettings() {
  epdBeginFrame();
  drawHeader("SETTINGS", false);
  const int w = SCR_W - 6;  // room for the scroll bar
  for (int r = 0; r < SET_ROWS && setTop + r < NSET; r++) {
    int i = setTop + r;
    const char *v = SETS[i].names[setIdx[i]];
    char buf[16];
    if (i == S_CYCLE && setVal(S_CYCLE) < 0) {
      snprintf(buf, sizeof(buf), "AUTO %luS", cycleMs / 1000);
      v = buf;
    }
    drawRow(21 + r * 21, w, SETS[i].label, v, i == setSel);
  }
  // Scroll bar: track on the right, thumb for the visible rows.
  const int ty = 21, th = SET_ROWS * 21;
  gfxVLine(SCR_W - 3, ty, ty + th - 1, true);
  int thumbH = th * SET_ROWS / NSET, thumbY = ty + (th - thumbH) * setTop / (NSET - SET_ROWS);
  gfxFill(SCR_W - 4, thumbY, 3, thumbH, true);
  drawHint("WHEEL: MOVE  PRESS: CHANGE  EXIT: BACK");
  epdEndFrame(false);
}

void drawStatus() {
  epdBeginFrame();
  drawHeader("STATUS", false);
  char vals[6][32];
  snprintf(vals[0], 32, "%s", HOSTNAME);
  snprintf(vals[1], 32, "%s", WiFi.localIP().toString().c_str());
  if (lastSyncMs) {
    char age[12];
    fmtAge(age, sizeof(age), millis() - lastSyncMs);
    snprintf(vals[2], 32, "%s ago", age);
  } else {
    snprintf(vals[2], 32, "never");
  }
  String a = statusLine.auth;
  a.toUpperCase();
  snprintf(vals[3], 32, "%s", a == "OK" ? "OK" : a.c_str());
  snprintf(vals[4], 32, "%s", SETS[S_POLL].names[setIdx[S_POLL]]);
  snprintf(vals[5], 32, "v%s", FW_VERSION);
  static const char *const names[6] = {"HOST", "IP", "LAST SYNC", "COOKIE", "POLL", "FIRMWARE"};
  for (int i = 0; i < 6; i++) {
    int x = (i % 2) ? 131 : 6, y = 21 + (i / 2) * 28;
    int size = i == 0 ? 12 : 16;
    gfxText(x, y, names[i], 12);
    gfxText(x, y + 11, fitText(vals[i], 113, size).c_str(), size);
  }
  gfxVLine(124, 22, 103, true);
  bool req = pollReqAt && millis() - pollReqAt < 15000;
  drawHint(req ? "POLL REQUESTED   EXIT: BACK" : "PRESS: POLL NOW   EXIT: BACK");
  epdEndFrame(false);
}
// Greedy word wrap by pixel width; last line gets "." if text was cut.
int wrapText(const String &src, int maxW, int size, String *out, int maxLines) {
  String rest = src;
  rest.trim();
  int n = 0;
  while (rest.length() && n < maxLines) {
    int cut = rest.length();
    while (cut > 0 && textWidth(rest.substring(0, cut).c_str(), size) > maxW) {
      int sp = rest.lastIndexOf(' ', cut - 1);
      cut = sp > 0 ? sp : cut - 1;
    }
    if (cut <= 0) cut = 1;
    out[n] = rest.substring(0, cut);
    rest = rest.substring(cut);
    rest.trim();
    if (n == maxLines - 1 && rest.length()) out[n] = fitText(out[n] + " " + rest, maxW, size);
    n++;
  }
  return n;
}

void drawNotification() {
  epdBeginFrame();

  String title = "NEW EVENT";
  if (currentNotif.type == "download") title = "NEW DOWNLOAD";
  else if (currentNotif.type == "print") title = "NEW PRINT";
  else if (currentNotif.type == "boost") title = "NEW BOOST";
  else if (currentNotif.type == "points") title = "POINTS";
  else if (currentNotif.type == "like") title = "NEW LIKE";
  else if (currentNotif.type == "comment") title = "NEW COMMENT";

  char line[48];
  gfxFill(0, 0, SCR_W, 34, true);
  gfxText(6, 5, title.c_str(), 24, 'l', false, true);
  if (currentNotif.type == "comment") {
    String by = fitText(currentNotif.user, SCR_W - 6 - (6 + textWidth(title.c_str(), 24, 0, true)) - 10, 16);
    gfxText(SCR_W - 6, 9, by.c_str(), 16, 'r', false);
    line[0] = '\0';
  } else if (currentNotif.type == "points") {
    snprintf(line, sizeof(line), "+%.2f", currentNotif.value);
  } else if (currentNotif.type != "comment" && currentNotif.value > 1) {
    snprintf(line, sizeof(line), "+%.0f", currentNotif.value);
  } else {
    line[0] = '\0';
  }
  if (line[0]) gfxText(SCR_W - 6, 5, line, 24, 'r', false, true);
  gfxHatch(0, 36, SCR_W, 5);

  String rows[3];
  if (currentNotif.type == "comment") {
    // Commenter is in the band; model, then up to three 16 px lines.
    String model = fitText(currentNotif.modelName, SCR_W - 12, 16);
    gfxText(6, 44, model.c_str(), 16);
    int n = wrapText(currentNotif.text, SCR_W - 14 - 6, 16, rows, 3);
    if (n) gfxVLine(6, 64, 64 + n * 18 - 4, false, 2);
    for (int i = 0; i < n; i++) gfxText(14, 64 + i * 18, rows[i].c_str(), 16);
  } else if (currentNotif.type == "points") {
    snprintf(line, sizeof(line), "%.2f", currentNotif.points);
    gfxText(6, 52, line, 32, 'l', true, true);
    gfxText(6 + textWidth(line, 32, 0, true) + 6, 66, "PTS", 16, 'l', true, true);
    gfxHLine(0, SCR_W - 1, 101, true);
    snprintf(line, sizeof(line), "was %.2f", currentNotif.total);
    gfxText(6, 105, line, 16);
  } else {
    int n = wrapText(currentNotif.modelName, SCR_W - 12, 16, rows, 2);
    for (int i = 0; i < n; i++) gfxText(6, 48 + i * 18, rows[i].c_str(), 16);
    gfxHLine(0, SCR_W - 1, 101, true);
    snprintf(line, sizeof(line), "TOTAL %.0f", currentNotif.total);
    gfxText(6, 105, line, 16);
    if (currentNotif.points > 0) {
      snprintf(line, sizeof(line), "+%.1f PTS", currentNotif.points);
      gfxText(SCR_W - 6, 105, line, 16, 'r', true, true);
    }
  }

  epdEndFrame(true);
}

void refreshDisplay(bool forceFull = false) {
  if (otaActive) return;
  if (currentNotif.active) drawNotification();
  else if (mode == M_MENU) drawMenu();
  else if (mode == M_SETTINGS) drawSettings();
  else if (mode == M_STATUS) drawStatus();
  else if (onTotalsPage()) drawDashboard(forceFull);
  else drawSectionPage();
}
struct Note {
  uint16_t freq;
  uint16_t ms;
};

void playNotes(const Note *notes, size_t n) {
  for (size_t i = 0; i < n; i++) tone(PIN_BUZZER, notes[i].freq, notes[i].ms);
}

// Queued on the core's tone task, so this never blocks the display/server.
void chirp(const String &type, bool force = false) {
  if ((!setVal(S_SOUND) || quietNow()) && !force) return;
  static const Note like[] = {{2400, 60}};
  static const Note boost[] = {{1500, 70}, {0, 30}, {2000, 70}, {0, 30}, {2600, 110}};
  static const Note comment[] = {{2600, 70}, {0, 40}, {2000, 120}};
  static const Note download[] = {{1800, 90}};
  static const Note print[] = {{1800, 70}, {0, 40}, {1800, 70}};
  static const Note points[] = {{2200, 60}, {2900, 100}};
  if (type == "like") playNotes(like, 1);
  else if (type == "boost") playNotes(boost, 5);
  else if (type == "comment") playNotes(comment, 3);
  else if (type == "download") playNotes(download, 1);
  else if (type == "print") playNotes(print, 3);
  else if (type == "points") playNotes(points, 2);
  else playNotes(download, 1);
}

void applyTotalsFromDoc(JsonDocument &doc) {
  bool updated = false;
  if (!doc["accountName"].isNull()) {
    String name = doc["accountName"].as<String>();
    strncpy(stats.accountName, name.c_str(), sizeof(stats.accountName) - 1);
    stats.accountName[sizeof(stats.accountName) - 1] = '\0';
    updated = true;
  }
  if (!doc["totalDownloads"].isNull()) {
    stats.totalDownloads = doc["totalDownloads"].as<int>();
    updated = true;
  }
  if (!doc["totalPrints"].isNull()) {
    stats.totalPrints = doc["totalPrints"].as<int>();
    updated = true;
  }
  if (!doc["totalBoosts"].isNull()) {
    stats.totalBoosts = doc["totalBoosts"].as<int>();
    updated = true;
  }
  if (!doc["totalPoints"].isNull()) {
    stats.totalPoints = doc["totalPoints"].as<float>();
    updated = true;
  }
  if (!doc["currentTotalDownloads"].isNull()) {
    stats.totalDownloads = doc["currentTotalDownloads"].as<int>();
    updated = true;
  }
  if (!doc["currentTotalPrints"].isNull()) {
    stats.totalPrints = doc["currentTotalPrints"].as<int>();
    updated = true;
  }
  if (!doc["currentTotalBoosts"].isNull()) {
    stats.totalBoosts = doc["currentTotalBoosts"].as<int>();
    updated = true;
  }
  if (!doc["currentTotalPoints"].isNull()) {
    stats.totalPoints = doc["currentTotalPoints"].as<float>();
    updated = true;
  }
  if (updated) {
    stats.initialized = true;
    saveStats();
  }
}

void handlePing() { server.send(200, "text/plain", "OK"); }

void handleStats() {
  JsonDocument doc;
  doc["downloads"] = stats.totalDownloads;
  doc["prints"] = stats.totalPrints;
  doc["boosts"] = stats.totalBoosts;
  doc["points"] = stats.totalPoints;
  doc["accountName"] = stats.accountName;
  doc["initialized"] = stats.initialized;
  doc["auth"] = statusLine.auth;
  doc["statusMessage"] = statusLine.message;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleSync() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  applyTotalsFromDoc(doc);
  lastSyncMs = millis();
  currentNotif.active = false;
  refreshDisplay(true);
  server.send(200, "text/plain", "Synced");
}

void handleNotify() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  currentNotif.type = doc["type"] | "event";
  if (currentNotif.type == "points") {
    currentNotif.modelName = "";
    currentNotif.value = doc["increase"] | 0.0f;
    currentNotif.total = doc["before"] | 0.0f;
    currentNotif.points = doc["after"] | 0.0f;
  } else {
    currentNotif.modelName = doc["modelName"] | "Model";
    currentNotif.value = doc["value"] | 0;
    currentNotif.total = doc["total"] | 0;
    currentNotif.points = doc["points"] | 0.0f;
  }
  currentNotif.user = doc["user"] | "";
  currentNotif.text = doc["text"] | "";
  currentNotif.timestamp = millis();
  applyTotalsFromDoc(doc);
  lastSyncMs = millis();
  chirp(currentNotif.type);
  // Quiet hours or Alerts off: totals still update, but no alert screen.
  if (setVal(S_ALERTS) && !quietNow()) {
    currentNotif.active = true;
    refreshDisplay(true);
  }
  server.send(200, "text/plain", "OK");
}

void savePages() {
  prefs.begin("makermon", false);
  size_t n = prefs.putBytes("pages", pagesRaw.c_str(), pagesRaw.length());
  prefs.end();
  if (n != pagesRaw.length()) Serial.println("pages: NVS save failed");
}

void loadPages() {
  prefs.begin("makermon", true);
  size_t n = prefs.getBytesLength("pages");
  if (n > 0) {
    char *buf = (char *)malloc(n + 1);
    if (buf) {
      prefs.getBytes("pages", buf, n);
      buf[n] = '\0';
      pagesRaw = buf;
      free(buf);
    }
  }
  prefs.end();
  if (pagesRaw.length() && !deserializeJson(pagesDoc, pagesRaw)) {
    rebuildSections();
  } else {
    pagesDoc.clear();
    pagesRaw = "";
  }
}

void handlePages() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  const String &body = server.arg("plain");
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc["sections"].is<JsonArray>()) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  lastSyncMs = millis();
  // Pages are pushed every poll; only touch flash when they change.
  if (body != pagesRaw) {
    pagesRaw = body;
    pagesDoc = doc;
    savePages();
    rebuildSections();
  }
  server.send(200, "text/plain", "OK");
}

void showSection(int s) {
  int n = vCount();
  curSection = (s % n + n) % n;
  curPage = 0;
}

// TODAY page 1 is home: HOME, alert end, leaving the menus, and idle return here.
void goHome() {
  mode = M_PAGES;
  curSection = 0;
  curPage = 0;
}
void handleKey(Key k) {
  if (k == K_NONE) return;
  lastFlip = millis();
  lastKeyMs = millis();
  redrawAt = millis() + 250;
  if (currentNotif.active) {
    currentNotif.active = false;  // any key dismisses an alert first
    goHome();
    return;
  }
  if (k == K_HOME_HOLD) {
    mode = M_MENU;
    menuSel = 0;
    return;
  }
  if (k == K_HOME) {
    goHome();
    return;
  }
  switch (mode) {
    case M_MENU:
      if (k == K_PRV || k == K_NEXT) menuSel = (menuSel + (k == K_NEXT ? 1 : NMENU - 1)) % NMENU;
      else if (k == K_OK) {
        mode = menuSel == 0 ? M_SETTINGS : M_STATUS;
        setSel = setTop = 0;
      } else if (k == K_EXIT) goHome();
      return;
    case M_SETTINGS:
      if (k == K_PRV || k == K_NEXT) {
        setSel = (setSel + (k == K_NEXT ? 1 : NSET - 1)) % NSET;
        if (setSel < setTop) setTop = setSel;
        if (setSel >= setTop + SET_ROWS) setTop = setSel - SET_ROWS + 1;
      } else if (k == K_OK) {
        setIdx[setSel] = (setIdx[setSel] + 1) % SETS[setSel].count;
        saveSetting(setSel);
        if (setSel == S_SOUND && setVal(S_SOUND)) chirp("like", true);
      } else if (k == K_EXIT) {
        mode = M_MENU;
      }
      return;
    case M_STATUS:
      if (k == K_OK) {
        requestPoll();
        pollReqAt = millis();
      } else if (k == K_EXIT) {
        mode = M_MENU;
      }
      return;
    default:
      break;
  }
  if (k == K_EXIT) {
    cyclePaused = !cyclePaused;
    return;
  }
  if (k == K_PRV) showSection(curSection - 1);
  else if (k == K_NEXT) showSection(curSection + 1);
  else if (k == K_OK) curPage = (curPage + 1) % pageCount(curSection);
}
void handleNav() {
  String key = server.arg("key");
  Key k = K_NONE;
  if (key == "up" || key == "prev") k = K_PRV;
  else if (key == "down" || key == "next") k = K_NEXT;
  else if (key == "ok" || key == "page") k = K_OK;
  else if (key == "home") k = K_HOME;
  else if (key == "hold" || key == "menu") k = K_HOME_HOLD;
  else if (key == "exit" || key == "pause") k = K_EXIT;
  handleKey(k);
  JsonDocument doc;
  static const char *const MODES[] = {"pages", "menu", "settings", "status"};
  doc["mode"] = MODES[mode];
  doc["section"] = sectionName(curSection);
  doc["page"] = curPage + 1;
  doc["pages"] = pageCount(curSection);
  doc["sections"] = vCount();
  doc["paused"] = cyclePaused;
  String levels;
  for (int i = 0; i < 5; i++) levels += digitalRead(keyPins[i]) ? '1' : '0';
  doc["keyLevels"] = levels;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleSound() {
  if (server.hasArg("on")) {
    setIdx[S_SOUND] = server.arg("on") == "1" ? 1 : 0;
    saveSetting(S_SOUND);
  }
  if (server.hasArg("test")) chirp(server.arg("test"), true);
  server.send(200, "application/json", setVal(S_SOUND) ? "{\"on\":true}" : "{\"on\":false}");
}

// Same shape as the color panels: the poller follows pollSec and polls when
// pollNow changes.
void handleSettingsV2() {
  char buf[160];
  snprintf(buf, sizeof(buf), "{\"pollSec\":%d,\"pollNow\":%lu,\"fw\":\"%s\",\"board\":\"%s\"}",
           setVal(S_POLL), (unsigned long)pollSeq, FW_VERSION, BOARD_NAME);
  server.send(200, "application/json", buf);
}
void handleStatus() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  if (!doc["auth"].isNull()) statusLine.auth = doc["auth"].as<String>();
  if (!doc["message"].isNull()) statusLine.message = doc["message"].as<String>();
  statusLine.updatedAt = millis();
  if (!currentNotif.active) {
    refreshDisplay(false);
  }
  server.send(200, "text/plain", "OK");
}

void handleTest() {
  currentNotif.type = "download";
  currentNotif.modelName = "Test Model";
  currentNotif.value = 1;
  currentNotif.total = stats.totalDownloads + 1;
  currentNotif.points = 1.0f;
  currentNotif.timestamp = millis();
  currentNotif.active = true;
  chirp(currentNotif.type);
  refreshDisplay(true);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleReset() {
  prefs.begin("makermon", false);
  prefs.clear();
  prefs.end();
  stats = Stats{};
  currentNotif.active = false;
  refreshDisplay(true);
  server.send(200, "text/plain", "Stats reset");
}

void handleResync() {
  stats.initialized = false;
  saveStats();
  currentNotif.active = false;
  refreshDisplay(true);
  server.send(200, "text/plain", "Resync");
}

void handleResetWifi() {
  server.send(200, "text/plain", "WiFi reset");
  delay(500);
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

void setupServer() {
  server.on("/ping", HTTP_GET, handlePing);
  server.on("/stats", HTTP_GET, handleStats);
  server.on("/sync", HTTP_POST, handleSync);
  server.on("/notify", HTTP_POST, handleNotify);
  server.on("/status", HTTP_POST, handleStatus);
  server.on("/test", HTTP_GET, handleTest);
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/resync", HTTP_GET, handleResync);
  server.on("/reset-wifi", HTTP_GET, handleResetWifi);
  server.on("/pages", HTTP_POST, handlePages);
  server.on("/nav", HTTP_GET, handleNav);
  server.on("/sound", HTTP_GET, handleSound);
  server.on("/v2/settings", HTTP_GET, handleSettingsV2);
  server.on("/update", HTTP_GET, []() {
    server.send(200, "text/plain", "POST the firmware .bin as multipart, with ?token=");
  });
  server.on("/update", HTTP_POST,
    []() {
      int code = otaResult ? otaResult : 500;
      otaResult = 0;
      otaActive = false;
      server.sendHeader("Connection", "close");
      server.send(code, "text/plain", code == 200 ? "OK" : code == 401 ? "Unauthorized" : "FAIL");
      if (code == 200) {
        delay(250);
        ESP.restart();
      }
    },
    []() {
      HTTPUpload &up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        otaResult = 401;
        if (server.arg("token") != OTA_PASSWORD) {
          Serial.println("HTTP OTA rejected");
          return;
        }
        otaResult = 500;
        Serial.printf("HTTP OTA: %s\n", up.filename.c_str());
        if (Update.begin(UPDATE_SIZE_UNKNOWN)) otaActive = true;
        else Update.printError(Serial);
      } else if (!otaActive) {
        return;
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
      } else if (up.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
          otaResult = 200;
          Serial.printf("HTTP OTA wrote %u bytes\n", up.totalSize);
        } else {
          Update.printError(Serial);
        }
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
      }
    });
  server.begin();
}

void setupOta() {
  ArduinoOTA.setHostname(HOSTNAME);
  if (OTA_PASSWORD[0]) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setTimeout(8000);
  ArduinoOTA.onStart([]() {
    otaActive = true;
    Serial.println("OTA start");
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA done"); });
  ArduinoOTA.onError([](ota_error_t error) {
    otaActive = false;
    Serial.printf("OTA error %u\n", error);
  });
  ArduinoOTA.begin();
  Serial.printf("OTA ready as %s.local, ip ", HOSTNAME);
  Serial.println(WiFi.localIP());
}

// Keys are caught by interrupt so presses during a ~1.4 s e-paper refresh
// are not lost. Edges within 30 ms of the previous edge are contact bounce.
volatile uint8_t keyPresses[5];
volatile unsigned long keyLastEdge[5];
portMUX_TYPE keyMux = portMUX_INITIALIZER_UNLOCKED;

// HOME is different: short presses count on release, and a hold past
// HOLD_MS becomes K_HOME_HOLD (fired from loop() while held, or here on
// release if a refresh kept loop() busy).
static const int HOME_IDX = 0;
static const unsigned long HOLD_MS = 700;
volatile unsigned long homeDownAt = 0;
volatile bool homeHeld = false, homeHoldFired = false;
volatile uint8_t homeShort = 0, homeHold = 0;

void IRAM_ATTR keyIsr(void *arg) {
  int i = (int)(intptr_t)arg;
  unsigned long now = millis();
  bool pressed = ((REG_READ(GPIO_IN_REG) >> keyPins[i]) & 1) == 0;
  portENTER_CRITICAL_ISR(&keyMux);
  if (now - keyLastEdge[i] > 30) {
    if (i == HOME_IDX) {
      if (pressed && !homeHeld) {
        homeHeld = true;
        homeHoldFired = false;
        homeDownAt = now;
      } else if (!pressed && homeHeld) {
        homeHeld = false;
        if (!homeHoldFired) {
          if (now - homeDownAt >= HOLD_MS) homeHold++;
          else homeShort++;
        }
      }
    } else if (pressed && keyPresses[i] < 10) {
      keyPresses[i]++;
    }
  }
  keyLastEdge[i] = now;
  portEXIT_CRITICAL_ISR(&keyMux);
}
void setupKeys() {
  for (int i = 0; i < 5; i++) {
    pinMode(keyPins[i], INPUT);
    attachInterruptArg(keyPins[i], keyIsr, (void *)(intptr_t)i, CHANGE);
  }
}

void processKeys() {
  portENTER_CRITICAL(&keyMux);
  uint8_t hs = homeShort, hh = homeHold;
  homeShort = homeHold = 0;
  if (homeHeld && !homeHoldFired && millis() - homeDownAt >= HOLD_MS) {
    homeHoldFired = true;
    hh++;
  }
  portEXIT_CRITICAL(&keyMux);
  while (hh--) handleKey(K_HOME_HOLD);
  while (hs--) handleKey(K_HOME);
  for (int i = 0; i < 5; i++) {
    if (i == HOME_IDX) continue;
    portENTER_CRITICAL(&keyMux);
    uint8_t n = keyPresses[i];
    keyPresses[i] = 0;
    portEXIT_CRITICAL(&keyMux);
    if (n) Serial.printf("key gpio%d x%u level=%d\n", keyPins[i], n, digitalRead(keyPins[i]));
    while (n--) handleKey(keyIds[i]);
  }
}
void powerOnBoard() {
  pinMode(PIN_PWR_LED, OUTPUT);
  digitalWrite(PIN_PWR_LED, HIGH);

  pinMode(PIN_EPD_PWR, OUTPUT);
  digitalWrite(PIN_EPD_PWR, LOW);
  delay(200);
  digitalWrite(PIN_EPD_PWR, HIGH);
  delay(500);
}

void showBootSplash(const char *line1, const char *line2 = nullptr) {
  Serial.printf("splash: '%s'\n", line1);
  epdBeginFrame();
  gfxText(SCR_W / 2, 30, line1, 24, 'c', true, true);
  gfxHatch(40, 62, SCR_W - 80, 5);
  if (line2) {
    gfxText(SCR_W / 2, 76, line2, 12, 'c', true, false, 1);
  }
  uint32_t t0 = millis();
  epdEndFrame(true);
  Serial.printf("splash: done (%lu ms)\n", millis() - t0);
}

void setup() {
  powerOnBoard();
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("MakerMonitor e-ink boot (Elecrow EPD)");

  loadSettings();
  loadStats();
  loadPages();
  setupKeys();
  showBootSplash("MakerMonitor", "booting...");
  showBootSplash("WiFi setup...", "MakerMonitor-Setup");

  makeHostname();
  WiFi.setHostname(HOSTNAME);
  WiFiManager wm;
  wm.setHostname(HOSTNAME);
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("MakerMonitor-Setup")) {
    showBootSplash("WiFi failed", "Rebooting...");
    delay(3000);
    ESP.restart();
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");  // quiet hours
  setupServer();
  setupOta();
  statusLine.auth = "needed";
  statusLine.message = "waiting cookie";
  // Drop edges caught while the keys were set up and Wi-Fi connected, so
  // boot always lands on TODAY.
  portENTER_CRITICAL(&keyMux);
  for (int i = 0; i < 5; i++) keyPresses[i] = 0;
  homeShort = homeHold = 0;
  homeHeld = false;
  portEXIT_CRITICAL(&keyMux);
  goHome();
  refreshDisplay(true);
  Serial.print("Ready at http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  if (otaActive) return;
  processKeys();
  unsigned long now = millis();
  if (currentNotif.active && now - currentNotif.timestamp > (unsigned long)setVal(S_ALERT_TIME) * 1000UL) {
    currentNotif.active = false;
    goHome();
    lastFlip = now;
    refreshDisplay(true);
  }
  // Idle on anything but TODAY (or in the menus) returns home.
  if (!currentNotif.active && !redrawAt && (mode != M_PAGES || curSection != 0) &&
      now - lastKeyMs > IDLE_HOME_MS) {
    goHome();
    lastFlip = now;
    refreshDisplay(true);
  }
  if (redrawAt && (long)(now - redrawAt) >= 0) {
    redrawAt = 0;
    refreshDisplay(true);
  }
  int cyc = setVal(S_CYCLE);
  unsigned long cycle = cyc < 0 ? cycleMs : (unsigned long)cyc * 1000UL;
  if (cyc != 0 && mode == M_PAGES && !cyclePaused && !currentNotif.active && !redrawAt && !quietNow() &&
      pageCount(curSection) > 1 && now - lastFlip > cycle) {
    lastFlip = now;
    curPage = (curPage + 1) % pageCount(curSection);
    refreshDisplay(true);
  }
}