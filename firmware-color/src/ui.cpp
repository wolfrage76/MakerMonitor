// All LVGL screens: 8 stat screens, settings, about (QR), notification banner, toast.
// Layout matches the canvas mockups (820x320 landscape). 800x480 ("tall", the
// 5" and 7" panels) uses that extra height; 480x272 ("compact") uses smaller
// type and fewer columns. Navigation comes from the knob or, on touch boards,
// from swipes and taps (see touchEvent()).
#include "ui.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>

#include "board.h"
#include "display.h"
#include "settings.h"
#include "sound.h"
#include "fw_version.h"

extern "C" {
extern const lv_img_dsc_t icon_boost_20, icon_like_20, icon_collect_20, icon_download_20, icon_print_20, icon_comment_20,
    icon_rating_20, icon_follower_20;
extern const lv_img_dsc_t icon_boost_32, icon_like_32, icon_collect_32, icon_download_32, icon_print_32, icon_comment_32,
    icon_rating_32, icon_follower_32;
extern const lv_img_dsc_t icon_boost_40, icon_like_40, icon_collect_40, icon_download_40, icon_print_40, icon_rating_40,
    icon_follower_40;
}
static const lv_img_dsc_t* const ICON20[NKIND] = {&icon_boost_20, &icon_like_20, &icon_collect_20,
                                                  &icon_download_20, &icon_print_20, &icon_comment_20, &icon_rating_20};
static const lv_img_dsc_t* const ICON32[NKIND] = {&icon_boost_32, &icon_like_32, &icon_collect_32,
                                                  &icon_download_32, &icon_print_32, &icon_comment_32, &icon_rating_32};

// ---- palette ----------------------------------------------------------------
// Dark navy base, hairline card edges, one cyan accent; stat colors are neon.
static const uint32_t C_BG = 0x06090E, C_BG2 = 0x0B1119, C_CARD = 0x0E141D, C_CARD2 = 0x0A0F16;
static const uint32_t C_CHIP = 0x121A25, C_RAISED = 0x132030, C_EDGE = 0x1E2A38, C_ACCENT = 0x22D3EE;
static const uint32_t C_TXT = 0xE6EDF3, C_SUB = 0xB3C1CF, C_MUTED = 0x7E8C9B, C_DIM = 0x2A3644, C_TRACK = 0x17212C;
static const uint32_t C_OK = 0x2EE59D, C_WARN = 0xFFB020, C_ERR = 0xFF5C6C, C_IDLE = 0x4E5A68, C_SEL = C_ACCENT;
// boosts, likes, collects, downloads, prints, comments, ratings
static const uint32_t COL[NKIND] = {0xFFB020, 0xFF4D7E, 0x3DA9FF, 0x2EE59D, 0xA78BFA, 0x2DE2E6, 0xFFD23F};
// Period colors for the 7-day / 30-day rows on the Today tiles.
static const uint32_t C_7D = 0xF2F5F8, C_30D = 0x22D3EE;
static const uint32_t COLBG[NKIND] = {0x2E230A, 0x2E1019, 0x0D2236, 0x0A2A1D, 0x1E1834, 0x0A2A2D, 0x2E2808};

// Set by layout() from the display size.
static int W = 820, H = 320, PAD = 16, TOP = 48, BOT = 300;
static int CW = W - 2 * PAD;  // content width
static int TH = BOT - TOP;    // card height
static bool COMPACT = false;  // 480x272-class panels
static bool TALL = false;     // 800x480-class panels (5" and 7")
static bool ROOMY = false;    // compact width with ~320 px of height (3.5")
static bool DENSE = false;    // same pixels, smaller glass (5": 187 ppi vs 7": 133)
static bool TINY = false;     // compact under 400 px wide (2": 320x240)
static bool STRIP = false;    // 820x320 at 278 ppi (3.16"): fewer, bigger figures
static int TOUCH_PX = 36;     // ~7 mm touch target at this panel's density

// Named by their size on the large panels; compact panels step each one down.
static const lv_font_t *F12, *F14, *F16, *F20, *F22, *F26, *F32, *F40, *F48;

static void layout() {
  W = lv_disp_get_hor_res(NULL);
  H = lv_disp_get_ver_res(NULL);
  COMPACT = W < 640;
  TALL = !COMPACT && H >= 400;
  ROOMY = COMPACT && H >= 300;
  TINY = W < 400;
  STRIP = !COMPACT && !TALL;
  DENSE = TALL && BOARD_PPI >= 170;
  TOUCH_PX = constrain((int)(7.0f * BOARD_PPI / 25.4f + 0.5f), 32, 52);
  PAD = COMPACT ? 8 : 16;
  TOP = COMPACT ? 34 : (TALL ? 56 : 48);
  BOT = H - (COMPACT ? 16 : (TALL ? 22 : 20));
  CW = W - 2 * PAD;
  TH = BOT - TOP;
  // The 3.5" has the 4.3"'s width but more height and density: it keeps the
  // full-size ladder (the names mean their real pixel size), not the step down.
  if (COMPACT && !ROOMY) {
    F12 = &lv_font_montserrat_12; F14 = &lv_font_montserrat_12; F16 = &lv_font_montserrat_14;
    F20 = &lv_font_montserrat_16; F22 = &lv_font_montserrat_16; F26 = &lv_font_montserrat_20;
    F32 = &lv_font_montserrat_22; F40 = &lv_font_montserrat_26; F48 = &lv_font_montserrat_32;
  } else if (DENSE || STRIP) {
    // 5" packs the 7"'s pixels into less glass: step body type up one size
    // so text and targets land at a similar physical size on both. The
    // 3.16" (278 ppi) gets the same step.
    F12 = &lv_font_montserrat_12; F14 = &lv_font_montserrat_16; F16 = &lv_font_montserrat_20;
    F20 = &lv_font_montserrat_22; F22 = &lv_font_montserrat_26; F26 = &lv_font_montserrat_26;
    F32 = &lv_font_montserrat_32; F40 = &lv_font_montserrat_40; F48 = &lv_font_montserrat_40;
  } else {
    F12 = &lv_font_montserrat_12; F14 = &lv_font_montserrat_14; F16 = &lv_font_montserrat_16;
    F20 = &lv_font_montserrat_20; F22 = &lv_font_montserrat_22; F26 = &lv_font_montserrat_26;
    F32 = &lv_font_montserrat_32; F40 = &lv_font_montserrat_40; F48 = &lv_font_montserrat_40;
  }
}

static int lineH(const lv_font_t* f) { return lv_font_get_line_height(f); }

static lv_color_t C(uint32_t hex) { return lv_color_hex(hex); }

// ---- state ------------------------------------------------------------------
static State st;
static uint32_t stateMs = 0;
static uint32_t lastInput = 0;
static bool hbIsOpen = false;

// ---- small helpers ----------------------------------------------------------
static lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t bg, int radius = 0) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                         LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, C(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  return o;
}

// Panel: hairline edge, slight top-to-bottom fade, tight corners.
static lv_obj_t* card(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* o = box(parent, x, y, w, h, C_CARD, COMPACT ? 4 : 6);
  lv_obj_set_style_bg_grad_color(o, C(C_CARD2), 0);
  lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_color(o, C(C_EDGE), 0);
  return o;
}

// An accent rule along the top and the card's foot glowing faintly in that
// colour, like the Today tiles (Points, 7-day trend).
static void accentCard(lv_obj_t* c, uint32_t col) {
  lv_obj_set_style_border_side(c, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(c, 2, 0);
  lv_obj_set_style_border_color(c, C(col), 0);
  lv_obj_set_style_bg_grad_color(c, lv_color_mix(C(col), C(C_CARD2), 28), 0);
}

static lv_obj_t* label(lv_obj_t* parent, const lv_font_t* f, uint32_t color, int x, int y, const char* txt = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, C(color), 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, txt);
  return l;
}

// One line, cut with "..." at width w.
static void dotLabel(lv_obj_t* l, int w) {
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_size(l, w, lv_font_get_line_height(lv_obj_get_style_text_font(l, 0)));
}

static void chipStyle(lv_obj_t* l, uint32_t bg, uint32_t fg) {
  lv_obj_set_style_bg_color(l, C(bg), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(l, C(fg), 0);
  lv_obj_set_style_radius(l, 3, 0);
  lv_obj_set_style_border_width(l, 1, 0);
  lv_obj_set_style_border_color(l, C(C_EDGE), 0);
  lv_obj_set_style_pad_hor(l, 10, 0);
  // 2": tappable chips (Top, Best, Trend) are 22 px so they stay inside the
  // 29 px header; the touch target around them is wider (see tappable()).
  lv_obj_set_style_pad_ver(l, TINY && lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE) ? 4 : 3, 0);
}

static lv_obj_t* icon(lv_obj_t* parent, int kind, bool big, int x, int y) {
  lv_obj_t* i = lv_img_create(parent);
  lv_img_set_src(i, big ? ICON32[kind] : ICON20[kind]);
  lv_obj_set_style_img_recolor(i, C(COL[kind]), 0);
  lv_obj_set_style_img_recolor_opa(i, LV_OPA_COVER, 0);
  lv_obj_set_pos(i, x, y);
  return i;
}

static void setIcon(lv_obj_t* i, int kind, bool big) {
  lv_img_set_src(i, big ? ICON32[kind] : ICON20[kind]);
  lv_obj_set_style_img_recolor(i, C(COL[kind]), 0);
}

static lv_obj_t* row(lv_obj_t* parent, int gap) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, gap, 0);
  return r;
}

static void show(lv_obj_t* o, bool on) {
  if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Make o tappable (touch boards): cb gets idx as user data, and o brightens
// with a cyan outline while pressed.
static void tappable(lv_obj_t* o, lv_event_cb_t cb, intptr_t idx) {
#if INPUT_TOUCH
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(o, TINY || ROOMY ? 9 : 6);  // small chips, finger-sized targets
  lv_obj_set_style_border_color(o, C(C_ACCENT), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(o, 1, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(o, C(C_RAISED), LV_STATE_PRESSED);
  lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, (void*)idx);
#endif
}

static int tapIdx(lv_event_t* e) { return (int)(intptr_t)lv_event_get_user_data(e); }

static void fmtInt(char* out, size_t cap, long v) {
  char tmp[24];
  snprintf(tmp, sizeof(tmp), "%ld", labs(v));
  int len = strlen(tmp), o = 0;
  if (v < 0 && o < (int)cap - 1) out[o++] = '-';
  for (int i = 0; i < len && o < (int)cap - 1; i++) {
    out[o++] = tmp[i];
    int left = len - 1 - i;
    if (left && left % 3 == 0 && o < (int)cap - 1) out[o++] = ',';
  }
  out[o] = 0;
}

static void fmtSigned(char* out, size_t cap, long v) {
  char tmp[24];
  fmtInt(tmp, sizeof(tmp), v);
  snprintf(out, cap, v > 0 ? "+%s" : "%s", tmp);
}

// Full number when it fits; otherwise 1.2k / 12k / 3M. Used on the 2", where a
// chip or weekday cell is only a few dozen pixels wide.
static void fmtFit(char* out, size_t cap, long v, bool sign, int maxW, const lv_font_t* f) {
  if (sign) fmtSigned(out, cap, v);
  else fmtInt(out, cap, v);
  if (lv_txt_get_width(out, strlen(out), f, 0, LV_TEXT_FLAG_NONE) <= maxW) return;
  long a = v < 0 ? -v : v;
  long div = a >= 1000000L ? 1000000L : 1000L;
  const char* suf = a >= 1000000L ? "M" : "k";
  if (a < 1000L) return;
  long whole = v / div;
  int tenth = (int)((a % div) * 10 / div);
  const char* plus = sign && v > 0 ? "+" : "";
  if (whole <= -10 || whole >= 10 || tenth == 0) snprintf(out, cap, "%s%ld%s", plus, whole, suf);
  else snprintf(out, cap, "%s%ld.%d%s", plus, whole, tenth, suf);
  if (lv_txt_get_width(out, strlen(out), f, 0, LV_TEXT_FLAG_NONE) > maxW)
    snprintf(out, cap, "%s%ld%s", plus, whole, suf);
}

static uint32_t nowUtc() {
  time_t t = time(nullptr);
  if (t > 1700000000) return (uint32_t)t;
  if (st.valid) return st.t + (millis() - stateMs) / 1000;
  return 0;
}

// Local time uses the poller host's UTC offset (sent with every state push;
// UTC until the first one arrives).
static uint32_t toLocal(uint32_t utc) { return utc + st.tz; }

// 12-hour clock everywhere: "9:05 PM", "12:00 AM".
static void fmt12(char* out, size_t cap, int hour, int min) {
  snprintf(out, cap, "%d:%02d %s", hour % 12 ? hour % 12 : 12, min, hour < 12 ? "AM" : "PM");
}

// Hour-only labels: "9 PM".
static void fmtHour12(char* out, size_t cap, int hour) {
  snprintf(out, cap, "%d %s", hour % 12 ? hour % 12 : 12, hour % 24 < 12 ? "AM" : "PM");
}

static void fmtHM(char* out, size_t cap, uint32_t utc) {
  uint32_t l = toLocal(utc);
  fmt12(out, cap, l / 3600 % 24, l / 60 % 60);
}

// "Fri 11:17 PM" in the panel's local time.
static void fmtWeekHM(char* out, size_t cap, uint32_t utc) {
  time_t lt = toLocal(utc);
  struct tm tmv;
  gmtime_r(&lt, &tmv);
  static const char* const WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  char t[12];
  fmt12(t, sizeof(t), tmv.tm_hour, tmv.tm_min);
  snprintf(out, cap, "%s %s", WD[tmv.tm_wday], t);
}

static void fmtAgo(char* out, size_t cap, long sec) {
  if (sec < 0) snprintf(out, cap, "-");
  else if (sec < 60) snprintf(out, cap, "%lds", sec);
  else if (sec < 3600) snprintf(out, cap, "%ldm", sec / 60);
  else if (sec < 86400) snprintf(out, cap, "%ldh %ldm", sec / 3600, sec / 60 % 60);
  else snprintf(out, cap, "%ldd %ldh", sec / 86400, sec / 3600 % 24);
}

// How many local midnights have passed since this payload was polled.
// 0 means its "today" figures are still today.
static int payloadLagDays() {
  uint32_t n = nowUtc();
  if (!st.valid || !st.t || !n) return 0;
  int lag = (int)(toLocal(n) / 86400) - (int)(toLocal(st.t) / 86400);
  return lag > 0 ? lag : 0;
}

static bool eventIsToday(uint32_t t) {
  uint32_t n = nowUtc();
  if (!t || !n) return payloadLagDays() == 0;
  return toLocal(t) / 86400 == toLocal(n) / 86400;
}

static int minuteOfDay() {
  uint32_t n = nowUtc();
  if (!n) return -1;
  return toLocal(n) / 60 % 1440;
}

static const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// "2026-10-13..." -> "Oct 13"
static void fmtIsoDate(char* out, size_t cap, const char* iso) {
  int y, m, d;
  if (iso && sscanf(iso, "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12)
    snprintf(out, cap, "%s %d", MONTHS[m - 1], d);
  else
    snprintf(out, cap, "%s", iso ? iso : "");
}

// ---- screens: shared header + dots ------------------------------------------
// Page order: Points right after Today, then the activity pages.
enum { SC_TODAY, SC_POINTS, SC_FEED, SC_TOP, SC_TREND, SC_REACH, SC_COMMENTS, SC_BEST, SC_CREATOR, SC_STATUS, NSCREEN };
static lv_obj_t* screens[NSCREEN];
static lv_obj_t* menuRoot;
static lv_obj_t* aboutRoot;
static lv_obj_t* updRoot;
// Status and About are their own pages opened from the hamburger menu (like
// Settings), so they sit outside the swipe / knob rotation and have no pager
// segment.
static const int NCAR = SC_STATUS;
static lv_obj_t* dots[NCAR];
static lv_obj_t* verLbl = nullptr;  // firmware version, bottom right beside the pager
static lv_obj_t* syncHit = nullptr; // sync dot, bottom left
// Hamburger button (top right) and its dropdown.
static const int HB_W = 40;
static int cur = SC_TODAY;
static int topSel = -1;  // knob select mode on Top: -1 off, 0-2 chips, 3+ rows
static bool menuOpen = false, statusOpen = false, aboutOpen = false, updOpen = false;

struct Header {
  lv_obj_t* title;
  lv_obj_t* sub;
  lv_obj_t* right;   // flex row, top right
};

static Header header(lv_obj_t* root, const char* title, const char* sub) {
  Header h;
  const lv_font_t* tf = TALL ? F20 : F16;
  const int gap = TINY ? 6 : 10;
  lv_obj_t* left = row(root, gap);
  lv_obj_set_pos(left, PAD, COMPACT ? 9 : (TALL ? 16 : 14));
  box(left, 0, 0, 3, lineH(tf) - 4, C_ACCENT, 1);
  h.title = label(left, tf, C_ACCENT, 0, 0, title);
  lv_obj_set_style_text_letter_space(h.title, TINY ? 0 : 3, 0);
  // Hairline under the header, fading out to the right.
  lv_obj_t* rule = box(root, PAD, TOP - (COMPACT ? 5 : 8), CW, 1, C_ACCENT);
  lv_obj_set_style_bg_grad_color(rule, C(C_BG), 0);
  lv_obj_set_style_bg_grad_dir(rule, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_70, 0);
  h.sub = label(left, F14, C_MUTED, 0, 0, sub);
  // Keep the subtitle from running under the right-hand cluster.
  if (TINY) {
    // Whatever the title leaves before the hamburger.
    const int tw = lv_txt_get_width(title, strlen(title), tf, 0, LV_TEXT_FLAG_NONE);
    dotLabel(h.sub, max(0, CW - 3 - gap - tw - gap - HB_W - 8));
  } else {
    dotLabel(h.sub, COMPACT ? 168 : (W * 2) / 5);
  }
  h.right = row(root, 14);
  lv_obj_align(h.right, LV_ALIGN_TOP_RIGHT, -PAD - HB_W - (COMPACT ? 14 : 24), TINY ? 4 : COMPACT ? 7 : (TALL ? 16 : 12));
  return h;
}

// Gesture/knob hints; the 2" header has no room, so they stay empty there.
static lv_obj_t* hint(Header& h, const char* txt) {
  lv_obj_t* l = label(h.right, F14, C_MUTED, 0, 0, txt);
  if (TINY) {
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(l, 0);
  }
  return l;
}

static lv_obj_t* newScreen() {
  lv_obj_t* s = box(lv_scr_act(), 0, 0, W, H, C_BG2);
  lv_obj_set_style_bg_grad_color(s, C(C_BG), 0);
  lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_VER, 0);
  show(s, false);
  return s;
}

static void updateDots() {
  for (int i = 0; i < NCAR; i++) {
    lv_obj_set_width(dots[i], i == cur ? 22 : 10);
    lv_obj_set_style_bg_color(dots[i], C(i == cur ? C_ACCENT : C_DIM), 0);
    show(dots[i], !menuOpen && !statusOpen && !aboutOpen && !updOpen);
  }
  bool footer = !menuOpen && !statusOpen && !aboutOpen && !updOpen;
  if (verLbl) show(verLbl, footer);
  if (syncHit) show(syncHit, footer);
}

static void commitScreen(int idx) {
  topSel = -1;  // leaving or re-entering a page ends the knob's Top select mode
  for (int i = 0; i < NSCREEN; i++) show(screens[i], i == idx);
  lv_obj_set_x(screens[idx], 0);
  cur = idx;
  updateDots();
}

// ---- page slides --------------------------------------------------------------
// On unrotated panels a page change is composed straight into the framebuffer
// (display_compose): the visible frame is captured and the neighbor comes from
// a pre-rendered snapshot in PSRAM, so a slide never re-renders widgets per
// frame (a full LVGL redraw of Today takes ~200 ms here). Snapshots of the
// previous and next page are refreshed in idle time after data changes.
static bool slideBusy = false;
static int cacheIdx[2] = {-1, -1};  // page in each snapshot buffer: 0 prev, 1 next
static bool cacheOk[2] = {false, false};
static lv_obj_t* slideKeep[4];      // overlays that stay put (hamburger, segments, version, sync dot)

static void markPagesDirty() { cacheOk[0] = cacheOk[1] = false; }

static bool pageSnap(int slot, int idx) {
  if (!display_can_compose()) return false;
  if (cacheOk[slot] && cacheIdx[slot] == idx) return true;
  lv_obj_set_x(screens[idx], 0);
  lv_img_dsc_t dsc;
  cacheIdx[slot] = idx;
  cacheOk[slot] = lv_snapshot_take_to_buf(screens[idx], LV_IMG_CF_TRUE_COLOR, &dsc, display_page_buf(slot),
                                          (uint32_t)W * H * sizeof(lv_color_t)) == LV_RES_OK;
  // Software-dimmed boards: the neighbour must be as dark as the screen.
  if (cacheOk[slot]) display_dim_pixels(display_page_buf(slot), (size_t)W * H);
  return cacheOk[slot];
}

static void composeAt(int slot, int dx) {
  lv_area_t keep[4];
  int n = 0;
  for (lv_obj_t* o : slideKeep)
    if (o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_get_coords(o, &keep[n++]);
  display_compose(display_page_buf(slot), dx, keep, n);
}

// Ease-in-out from one offset to another, composing as fast as memory allows.
void deviceLogf(const char* fmt, ...);  // main.cpp: serial + web console log

// Swipe tuning lines go to Serial only, and only with -DDEBUG_SWIPE=1.
#ifndef DEBUG_SWIPE
#define DEBUG_SWIPE 0
#endif
#define swipeLog(...)                         \
  do {                                        \
    if (DEBUG_SWIPE) deviceLogf(__VA_ARGS__);   \
  } while (0)

static void runSlide(int slot, int fromDx, int toDx, uint32_t ms) {
  uint32_t t0 = millis();
  int frames = 0;
  for (;;) {
    float p = min(1.0f, (millis() - t0) / (float)ms);
    float e = p < 0.5f ? 2 * p * p : 1 - (-2 * p + 2) * (-2 * p + 2) / 2;
    composeAt(slot, fromDx + (int)((toDx - fromDx) * e));
    frames++;
    if (p >= 1) break;
  }
  uint32_t took = millis() - t0;
  deviceLogf("slide %d frames in %lu ms (%.0f fps)", frames, (unsigned long)took, took ? frames * 1000.0f / took : 0);
}

// Hand the screen back to LVGL; it re-renders the same pixels (plus the
// updated segments) without a visible change.
static void finishSlide(int idx) {
  commitScreen(idx);
  display_compose_end();
  lv_obj_invalidate(lv_scr_act());
  lv_obj_invalidate(lv_layer_top());
  markPagesDirty();  // the neighbors moved
  slideBusy = false;
}

// Rotated panels (ws316): a short LVGL slide of the incoming page.
static void slideIn(lv_obj_t* o, int dir) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_values(&a, dir * 60, 0);
  lv_anim_set_time(&a, 160);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void goScreen(int idx, int dir) {
  if (menuOpen || statusOpen || aboutOpen || updOpen || slideBusy) return;
  idx = (idx % NCAR + NCAR) % NCAR;
  // Knob boards: nothing was swiped, so the page just changes in place.
  if (!INPUT_TOUCH) dir = 0;
  if (idx == cur && !dir) return;  // already showing it: nothing to redraw
  if (idx != cur && dir && display_can_compose()) {
    int slot = dir > 0 ? 1 : 0;  // dir > 0: the new page comes in from the right
    slideBusy = true;
    if (pageSnap(slot, idx)) {
      display_compose_begin();
      runSlide(slot, 0, -dir * W, 240);
      finishSlide(idx);
      return;
    }
    slideBusy = false;
  }
  bool slide = idx != cur && dir;
  commitScreen(idx);
  if (slide && !display_can_compose()) slideIn(screens[idx], dir);
}

// ---- 1. Today ---------------------------------------------------------------
// Midnight to midnight, up from a shared baseline: four 6-hour bars, or on
// tall panels twelve 2-hour bars in a short strip. Twenty-four hour ticks were
// a few pixels wide.
static const int NBAR_MAX = 12;
static int NBAR = 4, BAR_HOURS = 6;
static const char* const BAR_LBL[4] = {"12a", "6a", "12p", "6p"};

struct Tile {
  lv_obj_t* card;
  lv_obj_t* today;
  lv_obj_t* yday;
  lv_obj_t* total;
  lv_obj_t* d7;   // 7-day and 30-day rows (TALL panels only)
  lv_obj_t* d30;
  lv_obj_t* ref7;   // 7-day / 30-day average per bucket over the bars (TALL)
  lv_obj_t* ref30;
  lv_obj_t* bars[NBAR_MAX];
};
static Tile tiles[NSTAT];
static Header todayHdr;
static lv_obj_t *ptsChip, *syncDot, *syncLbl, *clockLbl;
static bool ptsWant = false;
static int TODAY_NUM_Y = 0;  // the 3.5" re-centres smaller numbers on it
static int TILE_W, TILE_STEP, BAR_BOTTOM, BAR_MAX, BAR_W, BAR_GAP, BAR_X;

// On tall panels the chart stays short and the room under the tiles is
// RECENT ACTIVITY (one column) beside a RATINGS / FOLLOWERS column.
static const int ACT_MAX = 8;
struct ActUi {
  lv_obj_t *row, *img, *n, *model, *time;
};
static ActUi actUi[ACT_MAX];
static int actRows = 0;  // rows that fit in the card
static lv_obj_t* actEmpty;
struct TodaySide {
  lv_obj_t *head, *title, *avg, *value, *today, *icon;
};
static TodaySide ratSide, folSide;

static void buildTodaySide(lv_obj_t* side, TodaySide& blk, int by, int halfH, int in, const char* name, bool star) {
  if (STRIP) {
    // 3.16": one line per half: icon, title, average, then the figures
    // (placed after the head by placeSideSubs()).
    blk.head = row(side, 8);
    if (star) {
      blk.icon = icon(blk.head, K_RATING, false, 0, 0);
    } else {
      blk.icon = lv_img_create(blk.head);
      lv_img_set_src(blk.icon, &icon_follower_20);
      lv_obj_set_style_img_recolor(blk.icon, C(0x2DE2E6), 0);  // the 2"'s follower teal
      lv_obj_set_style_img_recolor_opa(blk.icon, LV_OPA_COVER, 0);
    }
    blk.title = label(blk.head, &lv_font_montserrat_16, C_ACCENT, 0, 0, name);
    lv_obj_set_style_text_letter_space(blk.title, 1, 0);
    blk.avg = star ? label(blk.head, &lv_font_montserrat_16, COL[K_RATING], 0, 0, "") : nullptr;
    if (blk.avg) show(blk.avg, false);
    lv_obj_align(blk.head, LV_ALIGN_LEFT_MID, in, 0);
    blk.value = label(side, F26, C_TXT, 0, 0, "0");
    blk.today = label(side, &lv_font_montserrat_20, C_MUTED, 0, 0, "");
    return;
  }
  // 3.5": no Today graphs, so the strip is tall enough for big figures.
  const lv_font_t* numF = ROOMY ? &lv_font_montserrat_32 : DENSE ? F20 : F22;
  const lv_font_t* headF = ROOMY ? F14 : F12;
  const int blockH = lineH(headF) + 3 + lineH(numF);
  const int titleY = by + max(4, (halfH - blockH) / 2);
  blk.head = row(side, 6);
  if (star) blk.icon = icon(blk.head, K_RATING, false, 0, 0);
  else blk.icon = nullptr;
  blk.title = label(blk.head, headF, C_ACCENT, 0, 0, name);
  lv_obj_set_style_text_letter_space(blk.title, 2, 0);
  if (star) {
    blk.avg = label(blk.head, headF, COL[K_RATING], 0, 0, "");
    show(blk.avg, false);
  } else {
    blk.avg = nullptr;
  }
  lv_obj_update_layout(blk.head);
  lv_obj_align(blk.head, LV_ALIGN_TOP_MID, 0, titleY);
  const int numY = titleY + lv_obj_get_height(blk.head) + 4;
  blk.value = label(side, numF, C_TXT, in, numY, "0");
  blk.today = label(side, ROOMY ? F22 : DENSE ? F16 : F20, C_MUTED, in + 80, numY + (lineH(numF) - lineH(ROOMY ? F22 : DENSE ? F16 : F20)) / 2, "");
}

static void centerSideHead(TodaySide& s) {
  if (!s.head) return;
  lv_obj_update_layout(s.head);
  lv_obj_align(s.head, LV_ALIGN_TOP_MID, 0, lv_obj_get_y(s.head));
}

static void openStatus(bool open);
#if INPUT_TOUCH
static void syncDotCb(lv_event_t*) { openStatus(true); }
#endif

// 2" (320x240): seven equal tiles, four over three (the bottom row centered).
// Each one is a 32 px icon, a short name, and today's number on a shared
// baseline. "Downloads" does not fit a tile, so that one says DLs. Yesterday,
// totals and the hourly bars stay on the larger panels.
static const int TT_N = NSTAT + 2;  // + ratings, followers
static const char* const TT_NAME[TT_N] = {"Boosts", "Likes", "Collects", "DLs", "Prints", "Ratings", "Followers"};
static const uint32_t C_FOL = 0x2DE2E6, C_FOLBG = 0x0A2A2D;  // teal: Downloads has the green
static lv_obj_t *ttNum[TT_N], *ttCard[TT_N], *ttIcon[TT_N];
static int ttW[TT_N];
static uint32_t ttCol(int i) { return i < NSTAT ? COL[i] : i == NSTAT ? COL[K_RATING] : C_FOL; }
// The glow at the bottom of a tile: the stat's tint mixed halfway into the
// card colour, so it reads as a hint and the number stays the loud thing.
static lv_color_t ttGlow(int i) {
  uint32_t bg = i < NSTAT ? COLBG[i] : i == NSTAT ? COLBG[K_RATING] : C_FOLBG;
  return lv_color_mix(C(bg), C(C_CARD2), 128);
}

// Each tile: the stat's colour rule on top, its name as a small eyebrow, and
// today's count as the one big thing, with the icon ghosted behind the number
// so it labels the tile without taking the number's room.
static void buildTodayTiny(lv_obj_t* s) {
  static const lv_img_dsc_t* const IC[TT_N] = {&icon_boost_40, &icon_like_40,   &icon_collect_40, &icon_download_40,
                                               &icon_print_40, &icon_rating_40, &icon_follower_40};
  const int gap = 4, tileW = (CW - 3 * gap) / 4, rowH = (TH - gap) / 2;
  const int botX = PAD + (CW - (3 * tileW + 2 * gap)) / 2;
  for (int i = 0; i < TT_N; i++) {
    const bool top = i < 4;
    const int col = top ? i : i - 4;
    const int x = top ? PAD + col * (tileW + gap) : botX + col * (tileW + gap);
    lv_obj_t* c = card(s, x, TOP + (top ? 0 : rowH + gap), tileW, rowH);
    if (i < NSTAT) tiles[i].card = c;  // the new-event pulse lights it
    lv_obj_set_style_border_side(c, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_color(c, C(ttCol(i)), 0);
    lv_obj_set_style_bg_grad_color(c, ttGlow(i), 0);
    lv_obj_t* nm = label(c, F12, C_SUB, 0, 5, TT_NAME[i]);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(nm, tileW, lineH(F12));
    lv_obj_set_style_text_align(nm, LV_TEXT_ALIGN_CENTER, 0);
    const int numY = 5 + lineH(F12);  // the number's band: from under the name to the bottom
    lv_obj_t* ic = lv_img_create(c);
    lv_img_set_src(ic, IC[i]);
    lv_obj_set_style_img_recolor(ic, C(ttCol(i)), 0);
    lv_obj_set_style_img_recolor_opa(ic, LV_OPA_COVER, 0);
    lv_obj_set_style_img_opa(ic, LV_OPA_20, 0);  // a tint behind the number, not a second glyph
    ttCard[i] = c;
    ttIcon[i] = ic;
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, numY + (rowH - numY - 40) / 2);
    ttW[i] = tileW - 6;
    ttNum[i] = label(c, &lv_font_montserrat_40, C_MUTED, 0, 0, "0");
    lv_obj_align(ttNum[i], LV_ALIGN_TOP_MID, 0, numY + (rowH - numY - lineH(&lv_font_montserrat_40)) / 2);
  }
}

static void setTextIfChanged(lv_obj_t* l, const char* txt);

// Biggest of 40 / 32 / 26 px that fits the tile; wider counts read as 1.2k.
// Zero stays muted so the live stats stand out; a loss is red.
static void ttSet(int i, int v, bool has) {
  static const lv_font_t* const FONTS[3] = {&lv_font_montserrat_40, &lv_font_montserrat_32, &lv_font_montserrat_26};
  char a[16];
  const lv_font_t* f = FONTS[2];
  if (!has) {
    snprintf(a, sizeof(a), "-");
    f = FONTS[0];
  } else {
    for (const lv_font_t* g : FONTS) {
      fmtFit(a, sizeof(a), v, true, ttW[i], g);
      if (lv_txt_get_width(a, strlen(a), g, 0, LV_TEXT_FLAG_NONE) <= ttW[i]) {
        f = g;
        break;
      }
    }
  }
  lv_obj_set_style_text_font(ttNum[i], f, 0);
  setTextIfChanged(ttNum[i], a);
  // A tile with nothing today steps back: dim number, no glow, fainter icon.
  const bool quiet = !has || v == 0;
  lv_obj_set_style_text_color(ttNum[i], C(quiet ? C_IDLE : v < 0 ? C_ERR : ttCol(i)), 0);
  lv_obj_set_style_bg_grad_color(ttCard[i], quiet ? C(C_CARD2) : ttGlow(i), 0);
  lv_obj_set_style_img_opa(ttIcon[i], quiet ? LV_OPA_10 : LV_OPA_20, 0);
  const int numY = 5 + lineH(F12), rowH = lv_obj_get_height(lv_obj_get_parent(ttNum[i]));
  lv_obj_align(ttNum[i], LV_ALIGN_TOP_MID, 0, numY + (rowH - numY - lineH(f)) / 2);
}

static void refreshTodayTiny() {
  const int lag = payloadLagDays();
  for (int k = 0; k < NSTAT; k++) ttSet(k, lag ? 0 : st.today[k], st.valid);
  ttSet(NSTAT, lag ? 0 : st.todayRatings, st.valid);
  ttSet(NSTAT + 1, lag ? 0 : st.fol.today, st.fol.has);
}

static void buildToday() {
  lv_obj_t* s = screens[SC_TODAY] = newScreen();
  todayHdr = header(s, "TODAY", TINY ? "" : "waiting for poller");
  // Placed by layoutTodayChips(), not in the header row.
  ptsChip = label(s, F14, C_WARN, 0, 0, "");
  chipStyle(ptsChip, C_CHIP, C_WARN);
  // Tap: the Points page, sliding like a swipe. 48 px tall target.
  tappable(ptsChip, [](lv_event_t*) { goScreen(SC_POINTS, SC_POINTS > cur ? 1 : -1); }, 0);
  lv_obj_set_ext_click_area(ptsChip, max(6, (48 - lineH(F14) - 6) / 2));
  // Sync state is just the dot (green ok / amber cookie / red error), in the
  // footer at the bottom left on every page; tapping it opens Status. The text
  // label is kept hidden for the knob-free layout code.
  lv_obj_t* sync = syncHit = row(lv_layer_top(), 6);
  syncDot = box(sync, 0, 0, 10, 10, C_IDLE, LV_RADIUS_CIRCLE);
  syncLbl = label(sync, F14, C_MUTED, 0, 0, "");
  show(syncLbl, false);
  lv_obj_align(sync, LV_ALIGN_BOTTOM_LEFT, PAD + 4, -(H - BOT - 10) / 2);
  slideKeep[3] = sync;
#if INPUT_TOUCH
  lv_obj_add_flag(sync, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(sync, 19);  // 48 x 48 px target around the dot
  lv_obj_add_event_cb(sync, syncDotCb, LV_EVENT_CLICKED, nullptr);
#endif
  // Clock centred on the full width, independent of the left and right items.
  // Same centre line as the title (the header's right-hand row is empty
  // now; the chips are placed by layoutTodayChips()).
  clockLbl = label(s, F16, C_TXT, 0, 0, "--:--");
  lv_obj_update_layout(s);
  lv_area_t ttl;
  lv_obj_get_coords(todayHdr.title, &ttl);
  lv_obj_align(clockLbl, LV_ALIGN_TOP_MID, 0, (ttl.y1 + ttl.y2) / 2 - lineH(F16) / 2);
  // Keep the subtitle left of the clock ("waiting for poller - host" is long).
  // 2": a 6 px gap is enough for "Oct 29". Wider panels keep a looser margin.
  const int clockHalf = lv_txt_get_width("12:00 PM", 8, F16, 0, LV_TEXT_FLAG_NONE) / 2 + (TINY ? 6 : 14);
  lv_obj_update_layout(s);
  lv_area_t subA;
  lv_obj_get_coords(todayHdr.sub, &subA);
  lv_label_set_long_mode(todayHdr.sub, LV_LABEL_LONG_DOT);
  const int subW = max(0, W / 2 - clockHalf - subA.x1);
  lv_obj_set_style_max_width(todayHdr.sub, subW, 0);
  if (TINY) lv_obj_set_width(todayHdr.sub, subW);
  if (TINY) {
    buildTodayTiny(s);
    return;
  }

  const int gap = TINY ? 4 : COMPACT ? 6 : 9, in = TINY ? 5 : ROOMY ? 6 : COMPACT ? 8 : 12;
  TILE_STEP = (CW + gap) / NSTAT;
  TILE_W = TILE_STEP - gap;
  // Bucket count follows the tile width: aim for ~16 px bars and snap to a
  // divisor of 24 (4, 6, 8, or 12 bars), never thinner than that again.
  {
    int inner0 = TILE_W - 2 * in, want = inner0 / 16;
    NBAR = want >= 12 ? 12 : want >= 8 ? 8 : want >= 6 ? 6 : 4;
  }
  BAR_HOURS = 24 / NBAR;
  // 7-day / 30-day rows and average lines: tall panels and the 3.5".
  // 3.16": no hourly bars; 7D / 30D rows in their room, all in larger type.
  const bool periods = TALL || ROOMY || STRIP;
  // 3.5": no hourly graphs either; the room goes to larger yesterday and 7D / 30D type.
  const lv_font_t* ydayF = STRIP ? F20 : ROOMY ? F16 : F12;
  const int numY = TODAY_NUM_Y = ROOMY ? 28 : COMPACT ? 30 : (TALL ? 36 : STRIP ? 38 : 40);
  const int ydayY = numY + lineH(F48) + (TALL ? 2 : 4);
  // Room under the baseline for the clock labels and the lifetime total.
  const int labelH = COMPACT ? 0 : lineH(F12) + 4;
  // The 3.5" drops the lifetime total under the bars; its strip takes the room.
  const int belowBase = ROOMY ? 10 : COMPACT ? 30 : (TALL ? 6 + lineH(F14) + 10 : 44);
  // 7-day and 30-day sums sit under the number.
  const lv_font_t* rowF = F20;
  const int rule1Y = ydayY + lineH(ydayF) + (TALL ? 6 : ROOMY ? 6 : 10);
  const int rowStep = lineH(rowF) + (TALL ? 4 : ROOMY ? 6 : 8);
  const int row1Y = rule1Y + (TALL ? 6 : ROOMY ? 8 : 8);
  const int rule2Y = row1Y + 2 * rowStep;
  const int barTop = periods ? rule2Y + (ROOMY ? 8 : 10) : ydayY + 28;
  // Tall: a fixed short chart, and the tile ends under it (the activity card
  // takes the rest). 3.5": the tile stops above a RATINGS / FOLLOWERS strip.
  // Otherwise the chart gets what the full-height tile leaves.
  const int chartH = 56;
  // Title row (20 px star icon) over the number, as in buildTodaySide().
  // 3.16": a one-line strip (ratings, followers) instead of the lifetime totals.
  const int stripH = STRIP ? 52 : 2 * 9 + max(20, lineH(F12)) + 4 + lineH(F22), stripGap = 6;
  int tileH = TH;
  if (TALL) {
    BAR_BOTTOM = barTop + chartH;
    tileH = BAR_BOTTOM + labelH + belowBase;
  } else if (ROOMY) {
    tileH = row1Y + 2 * rowStep + 4;  // ends under the 30D row; the strip takes the rest
    BAR_BOTTOM = tileH;
  } else {
    if (ROOMY || STRIP) tileH = TH - stripGap - stripH;
    BAR_BOTTOM = tileH - belowBase - labelH;
  }
  BAR_GAP = COMPACT ? 5 : (TALL ? 3 : 8);
  const int inner = TILE_W - 2 * in;
  BAR_W = (inner - (NBAR - 1) * BAR_GAP) / NBAR;
  if (BAR_W < 4) BAR_W = 4;
  const int used = NBAR * BAR_W + (NBAR - 1) * BAR_GAP;
  BAR_X = in + max(0, (inner - used) / 2);
  const int avail = max(16, BAR_BOTTOM - barTop);
  BAR_MAX = TALL ? chartH : ROOMY ? avail : min(avail, max(28, BAR_W * 4));
  if (STRIP || ROOMY) NBAR = 0;  // after the bar maths: refreshToday() then skips the bars
  for (int k = 0; k < NSTAT; k++) {
    Tile& t = tiles[k];
    t.card = card(s, PAD + k * TILE_STEP, TOP, TILE_W, tileH);
    lv_obj_set_style_border_side(t.card, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(t.card, 2, 0);
    lv_obj_set_style_border_color(t.card, C(COL[k]), 0);
    if (TINY) {
      // Names don't fit 57 px tiles; the stat's icon does.
      icon(t.card, k, false, in, 8);
    } else {
      if (!COMPACT) icon(t.card, k, false, 12, 12);
      lv_obj_t* name = label(t.card, STRIP || ROOMY ? &lv_font_montserrat_14 : F12, C_SUB, COMPACT ? in : 38, COMPACT ? 9 : STRIP ? 13 : 15, COMPACT ? KIND_PLURAL[k] : KIND_UPPER[k]);
      // 3.5": "Downloads" doesn't fit 14 px in its tile; that name steps down to 12.
      if (ROOMY && lv_txt_get_width(KIND_PLURAL[k], strlen(KIND_PLURAL[k]), &lv_font_montserrat_14, 0, LV_TEXT_FLAG_NONE) > TILE_W - 2 * in)
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
      dotLabel(name, TILE_W - (COMPACT ? 2 * in : 44));
    }
    t.today = label(t.card, F48, COL[k], in, numY, "0");
    t.yday = label(t.card, ydayF, C_MUTED, in, ydayY, "today");
    dotLabel(t.yday, TILE_W - 2 * in);
    if (periods) {
      box(t.card, in, rule1Y, TILE_W - 2 * in, 1, C_EDGE);
      // 7D / 30D get their own colors (same on every tile), so they never
      // read as today's number or bars, which use the stat color. The 3.5"
      // tiles are too narrow for the dot and the letter spacing.
      const char* names[2] = {"7D", "30D"};
      const uint32_t rc[2] = {C_7D, C_30D};
      lv_obj_t** vals[2] = {&t.d7, &t.d30};
      const lv_font_t* capF = STRIP || ROOMY ? F14 : F12;
      for (int r = 0; r < 2; r++) {
        int y = row1Y + r * rowStep, ly = y + (lineH(rowF) - lineH(capF)) / 2;
        if (!ROOMY) box(t.card, in, ly + (lineH(capF) - 6) / 2, 6, 6, rc[r], 1);
        lv_obj_t* l = label(t.card, capF, rc[r], ROOMY ? in : in + 11, ly, names[r]);
        if (!ROOMY) lv_obj_set_style_text_letter_space(l, 1, 0);
        *vals[r] = label(t.card, rowF, rc[r], 0, 0, "-");
        lv_obj_align(*vals[r], LV_ALIGN_TOP_RIGHT, -in, y);
      }
      if (!STRIP && !ROOMY) box(t.card, in, rule2Y, TILE_W - 2 * in, 1, C_EDGE);
    }
    if (STRIP || ROOMY) {
      t.total = label(t.card, F12, C_MUTED, 0, 0, "");  // refreshToday() still sets it
      show(t.total, false);
      continue;
    }
    // One baseline. Every bar stands on it and grows up.
    box(t.card, BAR_X, BAR_BOTTOM, used, 1, C_DIM);
    const int foot = max(5, BAR_W / 5);
    for (int b = 0; b < NBAR; b++) {
      int x = BAR_X + b * (BAR_W + BAR_GAP);
      t.bars[b] = box(t.card, x, BAR_BOTTOM - foot, BAR_W, foot, C_TRACK, 2);
    }
    if (!COMPACT) {
      // Four 6-hour clock marks across the bar span; single-line so a narrow
      // bar width never wraps "12a" into two lines on top of the total below.
      int slotW = used / 4;
      for (int q = 0; q < 4; q++) {
        lv_obj_t* lab = label(t.card, F12, C_MUTED, BAR_X + q * slotW, BAR_BOTTOM + 3, BAR_LBL[q]);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
        lv_obj_set_size(lab, slotW, lineH(F12));
        lv_obj_set_style_text_align(lab, NBAR == 4 ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
      }
    }
    if (periods) {
      // 1px average lines in the period colors, on top of the bars.
      t.ref7 = box(t.card, BAR_X, BAR_BOTTOM, used, 1, C_7D);
      t.ref30 = box(t.card, BAR_X, BAR_BOTTOM, used, 1, C_30D);
      lv_obj_set_style_bg_opa(t.ref7, LV_OPA_80, 0);
      lv_obj_set_style_bg_opa(t.ref30, LV_OPA_90, 0);
    }
    t.total = label(t.card, F14, C_MUTED, in, BAR_BOTTOM + (COMPACT ? 8 : labelH + 6), "");
    dotLabel(t.total, TILE_W - 2 * in);
    if (ROOMY) show(t.total, false);
  }
  if (ROOMY || STRIP) {
    // RATINGS and FOLLOWERS side by side in one strip under the tiles.
    (void)stripH;
    const int y0 = TOP + tileH + stripGap, h = BOT - y0, half = CW / 2;
    lv_obj_t* strip = card(s, PAD, y0, CW, h);
    lv_obj_t* l = lv_obj_create(strip);
    lv_obj_t* r = lv_obj_create(strip);
    for (lv_obj_t* o : {l, r}) {
      lv_obj_remove_style_all(o);
      lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(o, half, h);
    }
    lv_obj_set_pos(r, half, 0);
    buildTodaySide(l, ratSide, 0, h, in, "RATINGS", true);
    box(strip, half, 6, 1, h - 12, C_EDGE);
    buildTodaySide(r, folSide, 0, h, in, "FOLLOWERS", false);
    return;
  }
  if (!TALL) return;
  const int y0 = TOP + tileH + 10, h = BOT - y0;
  const int split = 10;
  const int sideW = max(248, min(304, CW * 38 / 100));
  const int actW = CW - sideW - split;
  lv_obj_t* c = card(s, PAD, y0, actW, h);
  lv_obj_t* title = label(c, F12, C_ACCENT, 14, 8, "RECENT ACTIVITY");
  lv_obj_set_style_text_letter_space(title, 2, 0);
  const int rowsY = 8 + lineH(F12) + 6, rowH = lineH(F16) + 6;
  const int perCol = max(1, (h - rowsY - 6) / rowH), colW = actW - 28;
  actRows = min(ACT_MAX, perCol);
  for (int i = 0; i < actRows; i++) {
    int y = rowsY + i * rowH;
    ActUi& a = actUi[i];
    a.row = lv_obj_create(c);
    lv_obj_remove_style_all(a.row);
    lv_obj_clear_flag(a.row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(a.row, 14, y);
    lv_obj_set_size(a.row, colW, rowH);
    a.img = icon(a.row, 0, false, 0, (rowH - 20) / 2);
    a.n = label(a.row, F16, COL[0], 28, (rowH - lineH(F16)) / 2, "");
    a.model = label(a.row, F14, C_SUB, 84, (rowH - lineH(F14)) / 2, "");
    dotLabel(a.model, colW - 84 - 52);
    a.time = label(a.row, F14, C_MUTED, 0, 0, "");
    lv_obj_align(a.time, LV_ALIGN_RIGHT_MID, 0, 0);
    show(a.row, false);
  }
  actEmpty = label(c, F16, C_MUTED, 0, 0, "No activity yet today");
  lv_obj_align(actEmpty, LV_ALIGN_CENTER, 0, 8);

  lv_obj_t* side = card(s, PAD + actW + split, y0, sideW, h);
  const int halfH = h / 2;
  buildTodaySide(side, ratSide, 0, halfH, 12, "RATINGS", true);
  box(side, 12, halfH, sideW - 24, 1, C_EDGE);
  buildTodaySide(side, folSide, halfH, h - halfH, 12, "FOLLOWERS", false);
}

// "+3 Downloads"; ratings add their stars: "+1 Rating  -  4 stars", "+2 Ratings  -  4.5 avg".
static void fmtEventTitle(char* out, size_t cap, int kind, int n, float stars) {
  int len = snprintf(out, cap, "+%d %s", n, n == 1 ? KIND_NAME[kind] : KIND_PLURAL[kind]);
  if (kind != K_RATING || stars <= 0 || len < 0 || (size_t)len >= cap) return;
  char v[8];
  snprintf(v, sizeof(v), "%.1f", stars);
  if (v[strlen(v) - 1] == '0') v[strlen(v) - 2] = 0;  // "4.0" -> "4"
  snprintf(out + len, cap - len, n == 1 ? "  -  %s star%s" : "  -  %s avg", v, n == 1 && strcmp(v, "1") ? "s" : "");
}

// "3m", "2h", "1d" since t.
static void fmtShortAgo(char* out, size_t cap, long sec) {
  if (sec < 60) snprintf(out, cap, "now");
  else if (sec < 3600) snprintf(out, cap, "%ldm", sec / 60);
  else if (sec < 86400) snprintf(out, cap, "%ldh", sec / 3600);
  else snprintf(out, cap, "%ldd", sec / 86400);
}

// The poller sends a fresh feed with each state, newest first, before the
// banners for that poll. Also re-run every 30 s for the relative times.
static void setTextIfChanged(lv_obj_t* l, const char* txt);
static uint32_t gainColor(int v) { return v > 0 ? C_OK : v < 0 ? C_ERR : C_MUTED; }
static void placeSideSubs(TodaySide& s) {
  if (!s.value || !s.today) return;
  if (STRIP) {
    lv_obj_update_layout(s.head);
    lv_obj_align_to(s.value, s.head, LV_ALIGN_OUT_RIGHT_MID, 14, 0);
    lv_obj_update_layout(s.value);
    if (lv_label_get_text(s.today)[0]) lv_obj_align_to(s.today, s.value, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    return;
  }
  lv_obj_update_layout(s.value);
  if (ROOMY) {
    // 3.5" strip: number and today's change centred together under the title.
    lv_obj_update_layout(s.today);
    int w = lv_obj_get_width(s.value);
    if (lv_label_get_text(s.today)[0]) w += 12 + lv_obj_get_width(s.today);
    lv_obj_set_x(s.value, (lv_obj_get_width(lv_obj_get_parent(s.value)) - w) / 2);
    lv_obj_update_layout(s.value);
  }
  if (lv_label_get_text(s.today)[0]) lv_obj_align_to(s.today, s.value, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
  centerSideHead(s);
}
static void refreshActivity() {
  markPagesDirty();
  if (!ratSide.value) return;
  char a[24], b[32];
  if (st.totalRatings > 0) {
    fmtInt(a, sizeof(a), st.totalRatings);
    setTextIfChanged(ratSide.value, a);
    lv_obj_set_style_text_color(ratSide.value, C(COL[K_RATING]), 0);
    snprintf(b, sizeof(b), "(%.2f avg)", st.ratingAvg);
    setTextIfChanged(ratSide.avg, b);
    show(ratSide.avg, true);
    snprintf(b, sizeof(b), "%+d today", st.todayRatings);
    setTextIfChanged(ratSide.today, b);
    lv_obj_set_style_text_color(ratSide.today, C(gainColor(st.todayRatings)), 0);
  } else {
    setTextIfChanged(ratSide.value, "0");
    lv_obj_set_style_text_color(ratSide.value, C(C_MUTED), 0);
    setTextIfChanged(ratSide.avg, "");
    show(ratSide.avg, false);
    setTextIfChanged(ratSide.today, "");
  }
  if (ratSide.icon) show(ratSide.icon, true);
  placeSideSubs(ratSide);

  int folTotal = st.fol.has ? st.fol.total : st.followers;
  fmtInt(a, sizeof(a), folTotal);
  setTextIfChanged(folSide.value, a);
  lv_obj_set_style_text_color(folSide.value, C(folTotal ? C_TXT : C_MUTED), 0);
  if (st.fol.has) {
    snprintf(b, sizeof(b), "%+d today", st.fol.today);
    setTextIfChanged(folSide.today, b);
    lv_obj_set_style_text_color(folSide.today, C(gainColor(st.fol.today)), 0);
  } else {
    setTextIfChanged(folSide.today, "");
  }
  placeSideSubs(folSide);

  uint32_t now = nowUtc();
  int shown = 0;
  for (int i = 0; i < st.nfeed && shown < actRows; i++) {
    const FeedRow& r = st.feed[i];
    if (!eventIsToday(r.t)) continue;
    ActUi& f = actUi[shown++];
    show(f.row, true);
    setIcon(f.img, r.kind, false);
    if (r.kind == K_COMMENT) snprintf(a, sizeof(a), "new");
    else snprintf(a, sizeof(a), "+%d", r.n);
    lv_label_set_text(f.n, a);
    lv_obj_set_style_text_color(f.n, C(COL[r.kind]), 0);
    lv_label_set_text(f.model, r.model);
    if (r.t && now >= r.t) fmtShortAgo(a, sizeof(a), (long)(now - r.t));
    else a[0] = 0;
    lv_label_set_text(f.time, a);
  }
  for (int i = shown; i < actRows; i++) show(actUi[i].row, false);
  if (actEmpty) show(actEmpty, shown == 0);
}

// Today header: date left, clock centred, hamburger right; the points chip
// sits centred in the gap between the clock and the hamburger, measured from
// the real widths, on the title's centre line. If it doesn't fit, it hides.
static void layoutTodayChips() {
  if (!clockLbl) return;
  const int m = COMPACT ? 4 : 8;
  show(ptsChip, true);
  lv_obj_update_layout(screens[SC_TODAY]);
  lv_area_t clk, ttl;
  lv_obj_get_coords(clockLbl, &clk);
  lv_obj_get_coords(todayHdr.title, &ttl);
  const int cy = (ttl.y1 + ttl.y2) / 2;
  const int x1 = clk.x2 + 1, x2 = W - PAD - HB_W - 1, w = lv_obj_get_width(ptsChip);
  bool fits = ptsWant && w + 2 * m <= x2 - x1;
  show(ptsChip, fits);
  if (fits) lv_obj_set_pos(ptsChip, (x1 + x2 - w) / 2, cy - lv_obj_get_height(ptsChip) / 2);
}

static void refreshToday() {
  char a[32], b[48];
  int hourNow = -1;
  uint32_t n = nowUtc();
  int lag = payloadLagDays();
  if (n) hourNow = toLocal(n) / 3600 % 24;
  if (TINY) refreshTodayTiny();
  for (int k = 0; k < NSTAT && !TINY; k++) {
    Tile& t = tiles[k];
    int todayV = lag ? 0 : st.today[k];
    bool hasY = false;
    int ydayV = 0;
    if (lag == 0 && st.hasYday) {
      hasY = true;
      ydayV = st.yday[k];
    } else if (lag == 1) {
      // The payload's "today" closed when the date changed.
      hasY = true;
      ydayV = st.today[k];
    }
    fmtSigned(a, sizeof(a), todayV);
    if (ROOMY) {
      // 3.5": the biggest of 40 / 32 / 26 px that fits the tile; zero steps back.
      static const lv_font_t* const NF[3] = {&lv_font_montserrat_40, &lv_font_montserrat_32, &lv_font_montserrat_26};
      const int maxW = TILE_W - 12;
      const lv_font_t* f = NF[2];
      for (const lv_font_t* g : NF) {
        fmtFit(a, sizeof(a), todayV, true, maxW, g);
        if (lv_txt_get_width(a, strlen(a), g, 0, LV_TEXT_FLAG_NONE) <= maxW) {
          f = g;
          break;
        }
      }
      lv_obj_set_style_text_font(t.today, f, 0);
      lv_obj_set_y(t.today, TODAY_NUM_Y + (lineH(NF[0]) - lineH(f)) / 2);
      lv_obj_set_style_text_color(t.today, C(todayV == 0 ? C_IDLE : todayV < 0 ? C_ERR : COL[k]), 0);
    }
    lv_label_set_text(t.today, todayV == 0 ? "0" : a);
    if (hasY) {
      fmtSigned(a, sizeof(a), ydayV);
      snprintf(b, sizeof(b), TINY ? "y %s" : COMPACT || STRIP ? "%s yday" : "today  /  %s yday", a);
    } else {
      snprintf(b, sizeof(b), "today");
    }
    lv_label_set_text(t.yday, b);
    if (TALL || ROOMY || STRIP) {
      long sum7 = 0;
      for (int d = 0; d < st.ndays; d++) sum7 += st.days[d].v[k];
      // 3.5": 20 px figures beside the caption; one that would touch the
      // caption steps down to 16 px, then reads as 1.2k.
      const int perW = TILE_W - 12 - 34;
      auto fitPer = [&](lv_obj_t* l, long v) {
        const lv_font_t* f = F20;
        fmtSigned(a, sizeof(a), v);
        if (lv_txt_get_width(a, strlen(a), f, 0, LV_TEXT_FLAG_NONE) > perW) f = F16;
        fmtFit(a, sizeof(a), v, true, perW, f);
        lv_obj_set_style_text_font(l, f, 0);
        lv_obj_set_style_translate_y(l, (lineH(F20) - lineH(f)) / 2, 0);  // same centre line
      };
      if (ROOMY) fitPer(t.d7, sum7);
      else fmtSigned(a, sizeof(a), sum7);
      lv_label_set_text(t.d7, st.ndays ? a : "-");
      // 30-day sums come from the account's month report (saves = collects).
      const int m30[NSTAT] = {st.month.boosts, st.month.likes, st.month.saves, st.month.downloads, st.month.prints};
      if (ROOMY) fitPer(t.d30, m30[k]);
      else fmtSigned(a, sizeof(a), m30[k]);
      lv_label_set_text(t.d30, st.month.has ? a : "-");
    }
    fmtInt(a, sizeof(a), st.total[k]);
    if (COMPACT) lv_label_set_text(t.total, a);
    else lv_label_set_text_fmt(t.total, "%s total", a);
    int bucket[NBAR_MAX] = {};
    if (!lag) {
      for (int h = 0; h < 24; h++) bucket[h / BAR_HOURS] += st.hours[k][h];
    }
    int mx = 1;
    for (int b = 0; b < NBAR; b++) mx = max(mx, bucket[b]);
    const int foot = max(5, BAR_W / 5);
    for (int b = 0; b < NBAR; b++) {
      int v = bucket[b];
      int hh = foot;
      if (v) hh = min(BAR_MAX, foot + 4 + (int)((long)(BAR_MAX - foot - 4) * v / mx));
      bool future = hourNow >= 0 && b * BAR_HOURS > hourNow;
      bool current = hourNow >= 0 && hourNow / BAR_HOURS == b;
      lv_obj_set_height(t.bars[b], hh);
      lv_obj_set_y(t.bars[b], BAR_BOTTOM - hh);
      lv_color_t col = C(!v || future ? C_TRACK : COL[k]);
      if (current && v && !future) col = lv_color_mix(C(C_TXT), C(COL[k]), 50);
      lv_obj_set_style_bg_color(t.bars[b], col, 0);
    }
    if (TALL) {
      // Average bucket over the last 7 / 30 days, on the bars' scale (clipped
      // to the top when today is quieter than usual).
      long sum7 = 0;
      for (int d = 0; d < st.ndays; d++) sum7 += st.days[d].v[k];
      const int m30[NSTAT] = {st.month.boosts, st.month.likes, st.month.saves, st.month.downloads, st.month.prints};
      float avg[2] = {st.ndays ? (float)sum7 * BAR_HOURS / (st.ndays * 24) : 0,
                      st.month.has ? m30[k] * BAR_HOURS / (30.0f * 24) : 0};
      lv_obj_t* ref[2] = {t.ref7, t.ref30};
      for (int r = 0; r < 2; r++) {
        int hh = foot + 4 + (int)((BAR_MAX - foot - 4) * avg[r] / mx);
        lv_obj_set_y(ref[r], BAR_BOTTOM - min(hh, BAR_MAX));
        show(ref[r], avg[r] > 0);
      }
    }
  }
  snprintf(a, sizeof(a), TINY ? "%+.1f" : COMPACT ? "%+.1f pts" : "%+.1f pts today", lag ? 0.0f : st.todayPoints);
  lv_label_set_text(ptsChip, a);
  ptsWant = st.valid && st.cookieAge >= 0;
  layoutTodayChips();
  refreshActivity();
}
static void setTextIfChanged(lv_obj_t* l, const char* txt) {
  const char* cur = lv_label_get_text(l);
  if (!cur || strcmp(cur, txt) != 0) lv_label_set_text(l, txt);
}

static void refreshTodayHeader() {
  char a[80], b[16];
  uint32_t n = nowUtc();
  if (n) fmtHM(b, sizeof(b), n);
  setTextIfChanged(clockLbl, n ? b : "--:--");
  if (g_net.portal) {
    setTextIfChanged(todayHdr.sub, TINY ? "" : "Wi-Fi setup: join " BOARD_AP);
  } else if (!st.valid) {
    if (TINY) setTextIfChanged(todayHdr.sub, "");
    else {
      snprintf(a, sizeof(a), "waiting for poller  -  %s.local", g_net.host[0] ? g_net.host : "offline");
      setTextIfChanged(todayHdr.sub, a);
    }
  } else {
    setTextIfChanged(todayHdr.sub, "");  // no date: the clock is enough
  }
  // Re-centre the chips when the date or clock text changed width.
  static char lastHdr[96] = "";
  char now[96];
  snprintf(now, sizeof(now), "%s|%s", lv_label_get_text(todayHdr.sub), lv_label_get_text(clockLbl));
  if (strcmp(now, lastHdr)) {
    strlcpy(lastHdr, now, sizeof(lastHdr));
    layoutTodayChips();
  }
  uint32_t dot = C_IDLE;
  if (st.valid) {
    dot = !strcmp(st.auth, "ok") ? C_OK : !strcmp(st.auth, "needed") ? C_WARN : C_ERR;
    if (st.health.has && st.health.expired) dot = C_ERR;
    else if (st.health.has && st.health.stale) dot = C_WARN;
    // Bucket seconds to <1m so the header doesn't redraw every second and
    // contend with PSRAM scanout.
    long sec = n && st.lastPoll ? (long)(n - st.lastPoll) : -1;
    if (sec >= 0 && sec < 60) snprintf(b, sizeof(b), "<1m");
    else fmtAgo(b, sizeof(b), sec);
    snprintf(a, sizeof(a), !strcmp(st.auth, "ok") ? "synced %s" : !strcmp(st.auth, "needed") ? "cookie needed" : "error", b);
    setTextIfChanged(syncLbl, a);
  } else {
    setTextIfChanged(syncLbl, g_net.connected ? "no data" : "offline");
  }
  static uint32_t lastDot = 0xFFFFFFFF;
  if (dot != lastDot) {
    lastDot = dot;
    lv_obj_set_style_bg_color(syncDot, C(dot), 0);
  }
}

static void bumpCb(void* var, int32_t v) {
  for (int k = 0; k < NSTAT; k++)
    if (tiles[k].card == var)
      lv_obj_set_style_bg_color(tiles[k].card, lv_color_mix(C(COLBG[k]), C(C_CARD), (uint8_t)v), 0);
}

static void bumpTile(int k) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, tiles[k].card);
  lv_anim_set_values(&a, 255, 0);
  lv_anim_set_time(&a, 1200);
  lv_anim_set_exec_cb(&a, bumpCb);
  lv_anim_start(&a);
}

// ---- 2. Live feed -----------------------------------------------------------
struct FeedUi {
  lv_obj_t *card, *iconBox, *img, *title, *model, *time;
};
static FeedUi feedUi[8];
static int feedCap = 8;  // rows that fit
static Header feedHdr;
static lv_obj_t* feedEmpty;
static int feedFilter = -1;  // -1 all, else kind

static void buildFeed() {
  lv_obj_t* s = screens[SC_FEED] = newScreen();
  feedHdr = header(s, "LIVE FEED", "");
  hint(feedHdr, INPUT_TOUCH ? "swipe up/down: filter" : "press: filter");
  // 3.16": three rows per column instead of four, in larger type.
  const int cols = COMPACT ? 1 : 2, gap = COMPACT ? 6 : 8, rowsPer = STRIP ? 3 : 4;
  feedCap = cols * rowsPer;
  const int colW = (CW - (cols - 1) * 12) / cols, rowH = (TH - (rowsPer - 1) * gap) / rowsPer;
  // 2" rows are ~43 px: a 36 px icon box leaves almost no air around the text.
  const bool bigIcon = TALL || STRIP;
  const int ib = TINY ? 28 : bigIcon ? 48 : 36, tx = TINY ? 44 : bigIcon ? 76 : 60;
  const lv_font_t* tf = TALL || STRIP ? F20 : TINY ? &lv_font_montserrat_16 : F16;
  const lv_font_t* mf = TALL || STRIP ? F16 : TINY ? &lv_font_montserrat_14 : F14;
  const lv_font_t* timeF = STRIP ? F16 : F14;
  const int textY = (rowH - lineH(tf) - lineH(mf) - 4) / 2;
  for (int i = 0; i < feedCap; i++) {
    FeedUi& f = feedUi[i];
    int col = i / rowsPer, r = i % rowsPer;
    f.card = card(s, PAD + col * (colW + 12), TOP + r * (rowH + gap), colW, rowH);
    f.iconBox = box(f.card, bigIcon ? 14 : 12, (rowH - ib) / 2, ib, ib, COLBG[0], 10);
    f.img = icon(f.iconBox, 0, bigIcon, (ib - (bigIcon ? 32 : 20)) / 2, (ib - (bigIcon ? 32 : 20)) / 2);
    // Time on the action's line, so the model name gets the full width.
    const int timeW = lv_txt_get_width("12:00 PM", 8, timeF, 0, LV_TEXT_FLAG_NONE);
    f.title = label(f.card, tf, COL[0], tx, textY, "");
    dotLabel(f.title, colW - tx - 14 - timeW - 8);
    f.model = label(f.card, mf, C_SUB, tx, textY + lineH(tf) + 4, "");
    dotLabel(f.model, colW - tx - 14);
    f.time = label(f.card, timeF, C_MUTED, 0, 0, "");
    lv_obj_align(f.time, LV_ALIGN_TOP_RIGHT, -14, textY + lineH(tf) - lineH(timeF));
  }
  feedEmpty = label(s, F20, C_MUTED, 0, 0, "No activity yet today");
  lv_obj_align(feedEmpty, LV_ALIGN_CENTER, 0, 10);
}

static void refreshFeed() {
  char a[48], b[16];
  int shown = 0, todayCount = 0;
  for (int i = 0; i < st.nfeed; i++) {
    if (eventIsToday(st.feed[i].t)) todayCount += st.feed[i].n;
  }
  for (int i = 0; i < st.nfeed && shown < feedCap; i++) {
    const FeedRow& r = st.feed[i];
    if (!eventIsToday(r.t)) continue;
    if (feedFilter >= 0 && r.kind != feedFilter) continue;
    FeedUi& f = feedUi[shown++];
    show(f.card, true);
    lv_obj_set_style_bg_color(f.iconBox, C(COLBG[r.kind]), 0);
    setIcon(f.img, r.kind, TALL || STRIP);
    if (r.kind == K_COMMENT) snprintf(a, sizeof(a), "New comment");
    else fmtEventTitle(a, sizeof(a), r.kind, r.n, r.stars);
    lv_label_set_text(f.title, a);
    lv_obj_set_style_text_color(f.title, C(COL[r.kind]), 0);
    lv_label_set_text(f.model, r.model);
    fmtHM(b, sizeof(b), r.t);
    lv_label_set_text(f.time, r.t ? b : "");
  }
  for (int i = shown; i < feedCap; i++) show(feedUi[i].card, false);
  show(feedEmpty, shown == 0);
  const char* filt = feedFilter < 0 ? "all events" : (TINY && feedFilter == S_DOWNLOADS ? "DLs" : KIND_PLURAL[feedFilter]);
  snprintf(a, sizeof(a), COMPACT ? "%s" : "%s  -  newest first", filt);
  lv_label_set_text(feedHdr.sub, a);
}

// ---- 3. Top models ----------------------------------------------------------
struct TopUi {
  lv_obj_t *card, *rank, *name, *fill, *chips[NSTAT];
};
static TopUi topUi[4];
static Header topHdr;
static lv_obj_t *topChips[3], *topEmpty;
static int topMode = 0;
static int TOP_BAR_W;
static int topChipTextW;  // room inside a stat chip for its number
static int topRows = 4;   // model rows on the page (3 on the 2")
// Compact panels narrow every stat chip so all five still fit beside the name.
static int topChipN = NSTAT;
static int topChipKind[NSTAT];

static void refreshTop();
static void topChipCb(lv_event_t* e) {
  topMode = tapIdx(e);
  refreshTop();
}

#if INPUT_TOUCH
static void topRowCb(lv_event_t* e);  // opens the model detail page (touch section)
#endif
static void modelPage(const TopRow* r);

static void buildTop() {
  lv_obj_t* s = screens[SC_TOP] = newScreen();
  topHdr = header(s, TINY ? "TOP" : "TOP MODELS", COMPACT ? "" : INPUT_TOUCH ? "tap a model for detail" : "ranked by activity");
  lv_obj_add_flag(s, LV_OBJ_FLAG_EVENT_BUBBLE);  // row taps/drags reach the screen handler
  lv_obj_set_style_pad_column(topHdr.right, 6, 0);
  const char* names[3] = {"Today", TINY ? "7d" : "7 days", TINY ? "All" : "Lifetime"};
  for (int i = 0; i < 3; i++) {
    topChips[i] = label(topHdr.right, F14, C_MUTED, 0, 0, names[i]);
    tappable(topChips[i], topChipCb, i);
  }
  // 2" and 3.5": three rows with real gaps between them (a fourth left no air).
  topRows = TINY || ROOMY || STRIP ? 3 : 4;
  const int gap = TINY || ROOMY ? 8 : COMPACT ? 6 : 8, rowH = (TH - (topRows - 1) * gap) / topRows;
  topChipN = NSTAT;
  for (int i = 0; i < NSTAT; i++) topChipKind[i] = i;
  // 2" and 3.5": the name takes the whole row and the five chips sit under
  // it, each a fifth of the width (room for "+1,234"). Elsewhere name and bar
  // on the left, chips on the right.
  const bool stack = TINY || ROOMY;
  const int chipR = TINY ? 6 : ROOMY ? 10 : 16;  // chip row's right margin
  const int chipGap = TINY ? 3 : 6;
  const int chipW = stack ? (CW - chipR - (TINY ? chipR : 32) - (topChipN - 1) * chipGap) / topChipN
                          : COMPACT ? 34 : (TALL ? 52 : STRIP ? 64 : 46);
  const int chipH = TALL || STRIP || ROOMY ? 34 : TINY ? 26 : 28;
  const int chipStep = chipW + (stack ? chipGap : COMPACT ? 4 : 6);
  const int nameX = TINY ? 20 : ROOMY ? 32 : COMPACT ? 32 : 48;
  topChipTextW = chipW - 8;
  // Small panels: the counts are the point of the page, so they get the bigger type.
  const lv_font_t* nf = TALL || STRIP ? F20 : ROOMY ? &lv_font_montserrat_22 : TINY ? &lv_font_montserrat_16 : F16;
  const lv_font_t* cf = TINY || ROOMY || STRIP ? &lv_font_montserrat_20 : F14;
  const int barH = TALL ? 8 : TINY ? 2 : STRIP ? 6 : 5;
  TOP_BAR_W = stack ? CW - 2 : CW - nameX - chipR - topChipN * chipStep - 10;
  const int nameY = TALL || STRIP ? (rowH - lineH(nf) - barH - 10) / 2
                    : stack ? (rowH - lineH(nf) - chipH - (ROOMY ? 6 : 2)) / 2
                            : (rowH - lineH(F16) - 13) / 2;
  for (int i = 0; i < topRows; i++) {
    TopUi& t = topUi[i];
    t.card = card(s, PAD, TOP + i * (rowH + gap), CW, rowH);
#if INPUT_TOUCH
    // Tap a row for that model's 7/30-day detail. Events bubble up so a drag
    // that starts on a row still pages.
    lv_obj_add_flag(t.card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_border_color(t.card, C(C_ACCENT), LV_STATE_PRESSED);
    lv_obj_add_event_cb(t.card, topRowCb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
#endif
    const lv_font_t* rf = TINY ? F14 : STRIP ? F26 : F22;
    t.rank = label(t.card, rf, C_IDLE, TINY ? 7 : COMPACT ? 10 : 16, stack ? nameY + (lineH(nf) - lineH(rf)) / 2 : (rowH - lineH(rf)) / 2, "");
    lv_label_set_text_fmt(t.rank, "%d", i + 1);
    t.name = label(t.card, nf, C_TXT, nameX, nameY, "");
    dotLabel(t.name, stack ? CW - nameX - 8 : TOP_BAR_W);
    lv_obj_t* track = box(t.card, nameX, nameY + lineH(nf) + 8, TOP_BAR_W, barH, C_TRACK, 3);
    t.fill = box(track, 0, 0, TOP_BAR_W, barH, C_TXT, 3);
    if (stack) show(track, false);  // no room for a score bar under the chips
    const int chipY = stack ? nameY + lineH(nf) + (ROOMY ? 6 : 2) : (rowH - chipH) / 2;
    for (int s = 0; s < topChipN; s++) {
      int k = topChipKind[s];
      lv_obj_t* c = box(t.card, CW - chipR - (topChipN - s) * chipStep + (stack ? chipGap : 6), chipY, chipW, chipH, COLBG[k], TINY ? 5 : 8);
      t.chips[s] = label(c, cf, COL[k], 0, 0, "");
      lv_obj_center(t.chips[s]);
    }
  }
  topEmpty = label(s, F20, C_MUTED, 0, 0, "No model activity yet");
  lv_obj_align(topEmpty, LV_ALIGN_CENTER, 0, 10);
}

static void refreshTop() {
  for (int i = 0; i < 3; i++) chipStyle(topChips[i], i == topMode ? C_TXT : C_CHIP, i == topMode ? C_BG : C_MUTED);
  // Top "Today" is the open day in the payload. After midnight that list is yesterday.
  int n = (topMode == 0 && payloadLagDays()) ? 0 : st.ntop[topMode];
  int mx = n ? max(1, st.top[topMode][0].score) : 1;
  char a[16];
  if (n > topRows) n = topRows;
  for (int i = 0; i < topRows; i++) {
    TopUi& t = topUi[i];
    show(t.card, i < n);
    if (i >= n) continue;
    const TopRow& r = st.top[topMode][i];
    lv_label_set_text(t.name, r.name);
    lv_obj_set_width(t.fill, max(4, TOP_BAR_W * r.score / mx));
    for (int s = 0; s < topChipN; s++) {
      int k = topChipKind[s];
      if (TINY || ROOMY || STRIP) fmtFit(a, sizeof(a), r.v[k], topMode != 2, topChipTextW, &lv_font_montserrat_20);
      else if (topMode == 2) fmtInt(a, sizeof(a), r.v[k]);
      else fmtSigned(a, sizeof(a), r.v[k]);
      lv_label_set_text(t.chips[s], a);
      lv_obj_center(t.chips[s]);
      // A zero chip steps back so the live counts stand out.
      lv_obj_set_style_bg_color(lv_obj_get_parent(t.chips[s]), C(r.v[k] ? COLBG[k] : C_CARD2), 0);
      lv_obj_set_style_text_color(t.chips[s], C(r.v[k] ? COL[k] : C_IDLE), 0);
    }
  }
  show(topEmpty, n == 0);
  // Knob select mode: cyan outline on the chosen chip or model row.
  for (int i = 0; i < 3; i++) {
    lv_obj_set_style_border_width(topChips[i], 1, 0);
    lv_obj_set_style_border_color(topChips[i], C(topSel == i ? C_ACCENT : C_EDGE), 0);
  }
  for (int i = 0; i < topRows; i++)
    lv_obj_set_style_border_color(topUi[i].card, C(topSel == 3 + i ? C_ACCENT : C_EDGE), 0);
}

// ---- 4. 7-day trend ---------------------------------------------------------
struct TrendCol {
  lv_obj_t *val, *bar, *day;
};
static TrendCol trend[7];
static Header trendHdr;
static lv_obj_t *trendChips[NSTAT], *trendTotal, *trendAvg, *trendBest;
static int trendStat = S_COLLECTS;
static int TR_X, TR_W, TR_COLW, TR_BARMAX, TR_BOTTOM;
static lv_obj_t *trSumCard, *trChartCard;  // accented in the chosen stat's colour

static void refreshTrend();
static void trendChipCb(lv_event_t* e) {
  trendStat = tapIdx(e);
  refreshTrend();
}

static void buildTrend() {
  lv_obj_t* s = screens[SC_TREND] = newScreen();
  trendHdr = header(s, "7-DAY TREND", COMPACT || INPUT_TOUCH || STRIP ? "" : "press to change stat");
  lv_obj_set_style_pad_column(trendHdr.right, COMPACT ? 3 : 6, 0);
  // Compact: five chips don't fit beside the title, so they get their own
  // row across the content width under the header.
  lv_obj_t* chipRow = trendHdr.right;
  const int chipRowH = TINY ? lineH(F14) + 10 : COMPACT ? lineH(F14) + 8 : 0;
  if (COMPACT) {
    chipRow = row(s, 0);
    lv_obj_set_size(chipRow, CW, chipRowH);
    lv_obj_set_pos(chipRow, PAD, TOP);
    lv_obj_set_flex_align(chipRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  }
  for (int k = 0; k < NSTAT; k++) {
    trendChips[k] = label(chipRow, F14, C_MUTED, 0, 0, TINY && k == S_DOWNLOADS ? "DLs" : KIND_PLURAL[k]);
    tappable(trendChips[k], trendChipCb, k);
  }
  lv_obj_t* right;
  if (COMPACT) {
    // Summary as a strip across the top, chart full width underneath: a
    // 118 px side column starved both the figures and the bars.
    const int stripH = TINY ? 48 : 56, colW3 = CW / 3, stripY = TOP + chipRowH + 6;
    lv_obj_t* strip = trSumCard = card(s, PAD, stripY, CW, stripH);
    trendTotal = label(strip, F26, COL[trendStat], 12, 6, "");
    label(strip, F12, C_MUTED, 12, 6 + lineH(F26) + 1, "last 7 days");
    trendAvg = label(strip, F22, C_TXT, colW3 + 8, 6, "");
    label(strip, F12, C_MUTED, colW3 + 8, 6 + lineH(F22) + 1, "avg per day");
    trendBest = label(strip, F22, C_TXT, 2 * colW3 + 8, 6, "");
    label(strip, F12, C_MUTED, 2 * colW3 + 8, 6 + lineH(F22) + 1, "best day");
    TR_X = PAD;
    TR_W = CW;
    const int chartH = BOT - (stripY + stripH + 6);
    // 2": 40 px reserved above the bars left a 23 px chart. The value is one
    // 15 px line, so the bars can take that room.
    TR_BOTTOM = chartH - (TINY ? 18 : 28);
    TR_BARMAX = TR_BOTTOM - (TINY ? 24 : 40);
    TR_COLW = min(66, (TR_W - 36 - 6 * 6) / 7);
    right = trChartCard = card(s, TR_X, stripY + stripH + 6, TR_W, chartH);
  } else {
    const int leftW = 190, sep = 14, lx = 16;
    TR_X = PAD + leftW + sep;
    TR_W = CW - leftW - sep;
    TR_BOTTOM = TH - (TALL ? 36 : 40);
    TR_BARMAX = TR_BOTTOM - (TALL ? 52 : 62);
    TR_COLW = min(TALL ? 96 : 66, (TR_W - 36 - 6 * 6) / 7);
    lv_obj_t* left = trSumCard = card(s, PAD, TOP, leftW, TH);
    // Three figures spread down the card, each with a caption under it.
    // On tall panels each block is centered in its third so the card doesn't
    // stack them at the top of a deep slot.
    const int slot = (TH - 2 * 14) / 3;
    const lv_font_t* midF = TALL ? F32 : STRIP ? F26 : F22;
    const lv_font_t* capF = STRIP ? F14 : F12;
    int y0, y1, y2;
    if (TALL) {
      auto yAt = [&](int i, const lv_font_t* f) {
        int block = lineH(f) + 2 + lineH(capF);
        return 14 + i * slot + max(0, (slot - block) / 2);
      };
      y0 = yAt(0, F40);
      y1 = yAt(1, midF);
      y2 = yAt(2, midF);
    } else {
      y0 = 14;
      y1 = 14 + slot + 8;
      y2 = 14 + 2 * slot + 8;
    }
    trendTotal = label(left, F40, COL[trendStat], lx, y0, "");
    label(left, capF, C_MUTED, lx, y0 + lineH(F40) + 2, "last 7 days");
    trendAvg = label(left, midF, C_TXT, lx, y1, "");
    label(left, capF, C_MUTED, lx, y1 + lineH(midF) + 2, "avg per day");
    trendBest = label(left, midF, C_TXT, lx, y2, "");
    label(left, capF, C_MUTED, lx, y2 + lineH(midF) + 2, "best day");
    right = trChartCard = card(s, TR_X, TOP, TR_W, TH);
  }
  int gap = (TR_W - 36 - 7 * TR_COLW) / 6;
  for (int i = 0; i < 7; i++) {
    int x = 18 + i * (TR_COLW + gap);
    trend[i].bar = box(right, x, TR_BOTTOM - 8, TR_COLW, 8, COL[trendStat], 6);
    trend[i].val = label(right, STRIP ? F20 : F14, C_TXT, x, 0, "");
    trend[i].day = label(right, STRIP ? F16 : F12, C_MUTED, x, TR_BOTTOM + (TINY ? 2 : COMPACT ? 6 : 8), "");
    lv_obj_set_width(trend[i].val, TR_COLW);
    lv_obj_set_width(trend[i].day, TR_COLW);
    lv_obj_set_style_text_align(trend[i].val, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(trend[i].day, LV_TEXT_ALIGN_CENTER, 0);
  }
}

static void refreshTrend() {
  int k = trendStat;
  for (int i = 0; i < NSTAT; i++) chipStyle(trendChips[i], i == k ? COL[i] : C_CHIP, i == k ? C_BG : C_MUTED);
  int n = st.ndays, total = 0, best = -1, mx = 1;
  for (int i = 0; i < n; i++) {
    int v = st.days[i].v[k];
    total += v;
    mx = max(mx, v);
    if (best < 0 || v > st.days[best].v[k]) best = i;
  }
  char a[24];
  fmtSigned(a, sizeof(a), total);
  lv_label_set_text(trendTotal, a);
  lv_obj_set_style_text_color(trendTotal, C(COL[k]), 0);
  if (trSumCard) accentCard(trSumCard, COL[k]);
  if (trChartCard) accentCard(trChartCard, COL[k]);
  lv_label_set_text_fmt(trendAvg, "%.1f", n ? (float)total / n : 0.0f);
  if (best >= 0) {
    fmtSigned(a, sizeof(a), st.days[best].v[k]);
    lv_label_set_text_fmt(trendBest, "%s %s", a, st.days[best].d);
  } else {
    lv_label_set_text(trendBest, "-");
  }
  // Right-align the days so today is always the last column.
  int off = 7 - n;
  for (int i = 0; i < 7; i++) {
    TrendCol& c = trend[i];
    int di = i - off;
    bool has = di >= 0;
    show(c.bar, has);
    show(c.val, has);
    show(c.day, has);
    if (!has) continue;
    int v = st.days[di].v[k];
    int h = 8 + v * (TR_BARMAX - 8) / mx;
    bool today = di == n - 1 && payloadLagDays() == 0;
    lv_obj_set_height(c.bar, h);
    lv_obj_set_y(c.bar, TR_BOTTOM - h);
    // Bars fade toward their foot; today's (still open) stays muted.
    lv_color_t bc = today ? lv_color_mix(C(COL[k]), C(C_CARD), 110) : C(COL[k]);
    lv_obj_set_style_bg_color(c.bar, bc, 0);
    lv_obj_set_style_bg_grad_color(c.bar, lv_color_mix(bc, C(C_CARD), 150), 0);
    lv_obj_set_style_bg_grad_dir(c.bar, LV_GRAD_DIR_VER, 0);
    if (TINY) fmtFit(a, sizeof(a), v, true, TR_COLW, F14);
    else fmtSigned(a, sizeof(a), v);
    lv_label_set_text(c.val, a);
    lv_obj_set_y(c.val, TR_BOTTOM - h - (TINY ? 16 : STRIP ? 26 : 22));
    lv_obj_set_style_text_color(c.val, C(today ? C_MUTED : C_TXT), 0);
    // "Today" is wider than a 2" column; the muted bar marks it there.
    lv_label_set_text(c.day, today && !TINY ? "Today" : st.days[di].d);
  }
}

// ---- 5. Points --------------------------------------------------------------
static lv_obj_t *ptBalance, *ptToday, *ptFollow, *ptRange, *ptMonth, *ptSplitA, *ptSplitB, *ptLegA, *ptLegB;
static lv_obj_t *ptAvg, *ptBoosts, *ptPrints, *ptMonthCard, *ptNoData, *ptHint;
static lv_obj_t *ptYday, *pt7d;  // 3.16" balance card rows
static int PT_SPLIT_W;

// 2": a balance strip across the top (balance left, today's gain right), and
// the 30-day card under it with the range and total on one line, the split
// bar, the legend on one line and the three figures in a row.
static void buildPointsTiny(lv_obj_t* s) {
  // Key figures use the real font sizes: the F* aliases step down on compact panels.
  const lv_font_t *big = &lv_font_montserrat_40, *mid = &lv_font_montserrat_26, *sub = &lv_font_montserrat_20;
  const int gap = 6, mx = 10, stripH = 66;
  lv_obj_t* c1 = card(s, PAD, TOP, CW, stripH);
  accentCard(c1, C_WARN);
  label(c1, F12, C_WARN, mx, 4, "BALANCE");
  ptBalance = label(c1, big, C_WARN, mx, 4 + lineH(F12) - 2, "-");
  ptToday = label(c1, sub, C_OK, 0, 0, "");
  lv_obj_align(ptToday, LV_ALIGN_RIGHT_MID, -mx, 6);
  ptFollow = nullptr;
  const int my = TOP + stripH + gap, mh = BOT - my;
  ptMonthCard = card(s, PAD, my, CW, mh);
  lv_obj_t* m = ptMonthCard;
  accentCard(m, 0x7A5B22);
  PT_SPLIT_W = CW - 2 * mx;
  ptRange = label(m, F12, C_SUB, mx, 9, "LAST 30 DAYS");
  ptMonth = label(m, mid, C_TXT, 0, 0, "");
  lv_obj_align(ptMonth, LV_ALIGN_TOP_RIGHT, -mx, 2);
  const int trackH = 12, trackY = 2 + lineH(mid) + 3;
  lv_obj_t* track = box(m, mx, trackY, PT_SPLIT_W, trackH, C_TRACK, trackH / 2);
  ptSplitA = box(track, 0, 0, PT_SPLIT_W / 2, trackH, C_WARN, trackH / 2);
  ptSplitB = box(track, PT_SPLIT_W / 2, 0, PT_SPLIT_W / 2, trackH, 0x7A5B22, trackH / 2);
  const int legY = trackY + trackH + 4;
  box(m, mx, legY + 3, 8, 8, C_WARN, 2);
  ptLegA = label(m, F12, C_TXT, mx + 14, legY, "");
  box(m, mx + PT_SPLIT_W / 2, legY + 3, 8, 8, 0x7A5B22, 2);
  ptLegB = label(m, F12, C_TXT, mx + PT_SPLIT_W / 2 + 14, legY, "");
  // Three figures fill the rest of the card.
  const int figY = legY + lineH(F12) + 6, capY = figY + lineH(mid), step = PT_SPLIT_W / 3;
  ptAvg = label(m, mid, C_WARN, mx, figY, "");
  label(m, F12, C_MUTED, mx, capY, "avg / day");
  ptBoosts = label(m, mid, COL[S_BOOSTS], mx + step, figY, "");
  label(m, F12, C_MUTED, mx + step, capY, "boosts");
  ptPrints = label(m, mid, COL[S_PRINTS], mx + 2 * step, figY, "");
  label(m, F12, C_MUTED, mx + 2 * step, capY, "prints");
  ptNoData = label(m, F16, C_MUTED, 0, 0, "Needs a fresh MakerWorld cookie");
  lv_obj_center(ptNoData);
}

static void buildPoints() {
  lv_obj_t* s = screens[SC_POINTS] = newScreen();
  Header h = header(s, "POINTS", "");
  ptHint = hint(h, COMPACT ? "needs cookie" : "30-day stats need a MakerWorld cookie");
  if (TINY) {
    buildPointsTiny(s);
    return;
  }
  // 3.5": a wider balance card, so the balance keeps 40 px type.
  const int w1 = TINY || ROOMY ? CW * 2 / 5 : (CW - 20) / 3, mx = TINY ? 8 : COMPACT ? 12 : (TALL ? 22 : 18);
  lv_obj_t* c1 = card(s, PAD, TOP, w1, TH);
  accentCard(c1, C_WARN);
  lv_obj_t* balCap = label(c1, STRIP ? F16 : ROOMY ? F14 : F12, C_WARN, mx, TALL ? 24 : 18, "BALANCE");
  lv_obj_set_style_text_letter_space(balCap, 2, 0);
  const lv_font_t* balF = TINY ? F40 : F48;
  ptBalance = label(c1, balF, C_WARN, mx, TALL ? TH / 4 : (TH - lineH(balF)) / 2, "-");
  if (TINY) {
    lv_label_set_long_mode(ptBalance, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ptBalance, w1 - 2 * mx);
  }
  if (TALL) {
    // Fill the tall card: followers and model count under the balance.
    ptFollow = label(c1, F32, C_TXT, mx, TH / 2 + 22, "-");
    label(c1, F12, C_MUTED, mx, TH / 2 + 22 + lineH(F32) + 2, "followers  /  models");
  }
  const lv_font_t* todayF = TALL || STRIP || ROOMY ? F20 : F16;
  ptToday = label(c1, todayF, C_OK, mx, TH - lineH(todayF) - (TALL ? 28 : 22), "");
  lv_label_set_long_mode(ptToday, LV_LABEL_LONG_DOT);
  lv_obj_set_width(ptToday, w1 - 2 * mx);
  if (STRIP || ROOMY) {
    // 3.16" and 3.5": balance at the top, then today / yesterday / 7 days as
    // rows, captions left and figures right-aligned.
    const int balY = 18 + lineH(F16) + 6;
    lv_obj_set_y(ptBalance, balY);
    const int y0 = balY + lineH(balF) + 10, rowStep = (TH - 14 - y0 - 10) / 3;
    box(c1, mx, y0, w1 - 2 * mx, 1, C_EDGE);
    const char* caps[3] = {"today", "yesterday", "last 7 days"};
    lv_obj_t** vals[3] = {&ptToday, &ptYday, &pt7d};
    for (int r = 0; r < 3; r++) {
      const int y = y0 + 10 + r * rowStep + (rowStep - lineH(F20)) / 2;
      label(c1, F16, C_MUTED, mx, y + (lineH(F20) - lineH(F16)) / 2, caps[r]);
      if (r) *vals[r] = label(c1, F20, C_TXT, 0, 0, "-");
      lv_obj_set_pos(*vals[r], mx, y);
      lv_obj_set_width(*vals[r], w1 - 2 * mx);
      lv_obj_set_style_text_align(*vals[r], LV_TEXT_ALIGN_RIGHT, 0);
    }
  }
  const int mw = CW - w1 - 10;
  PT_SPLIT_W = mw - 2 * mx;
  ptMonthCard = card(s, PAD + w1 + 10, TOP, mw, TH);
  lv_obj_t* m = ptMonthCard;
  accentCard(m, 0x7A5B22);
  const int trackH = TALL ? 26 : 18;
  int trackY, legY;
  if (TALL) {
    ptRange = label(m, F14, C_SUB, mx, 22, "LAST 30 DAYS");
    ptMonth = label(m, F40, C_TXT, 0, 0, "");
    lv_obj_align(ptMonth, LV_ALIGN_TOP_RIGHT, -mx, 14);
    trackY = 22 + lineH(F40) + 16;
    legY = trackY + trackH + 16;
  } else if (TINY) {
    // Range over the total: side by side they collide at this width.
    ptRange = label(m, F12, C_SUB, mx, 10, "LAST 30 DAYS");
    ptMonth = label(m, F32, C_TXT, mx, 10 + lineH(F12) + 2, "");
    trackY = 10 + lineH(F12) + 2 + lineH(F32) + 6;
    legY = trackY + trackH + 10;
  } else {
    ptRange = label(m, STRIP || ROOMY ? F14 : F12, C_SUB, mx, 22, "LAST 30 DAYS");
    ptMonth = label(m, F32, C_TXT, 0, 0, "");
    lv_obj_align(ptMonth, LV_ALIGN_TOP_RIGHT, -mx, 10);
    trackY = 22 + lineH(F32) + 6;
    legY = trackY + 18 + 16;
  }
  lv_obj_t* track = box(m, mx, trackY, PT_SPLIT_W, trackH, C_TRACK, trackH / 2);
  ptSplitA = box(track, 0, 0, PT_SPLIT_W / 2, trackH, C_WARN, trackH / 2);
  ptSplitB = box(track, PT_SPLIT_W / 2, 0, PT_SPLIT_W / 2, trackH, 0x7A5B22, trackH / 2);
  const lv_font_t* legF = STRIP ? F20 : ROOMY ? F16 : F14;
  const int legDot = (lineH(legF) - 10) / 2;
  box(m, mx, legY + legDot, 10, 10, C_WARN, 3);
  ptLegA = label(m, legF, C_TXT, mx + 18, legY, "");
  // 2": the legend stacks; two side by side overlap.
  const int legBX = TINY ? mx : mx + PT_SPLIT_W / 2, legBY = TINY ? legY + lineH(F14) + 2 : legY;
  box(m, legBX, legBY + legDot, 10, 10, 0x7A5B22, 3);
  ptLegB = label(m, legF, C_TXT, legBX + 18, legBY, "");
  if (TALL || STRIP || ROOMY) {
    // Three figure tiles under the split bar, tall enough to read as
    // metrics but not stretched over the whole lower card, each in its own
    // colour (points amber, boosts, prints).
    const int figTop = legY + lineH(legF) + (STRIP || ROOMY ? 16 : 26);
    const int figH = min(TH - figTop - (ROOMY ? 12 : 16), 168);
    const int figW = (PT_SPLIT_W - 16) / 3;
    const char* caps[3] = {"avg / day", "boosts", "prints"};
    const uint32_t figCol[3] = {C_WARN, COL[S_BOOSTS], COL[S_PRINTS]};
    const uint32_t figBg[3] = {0x2E230A, COLBG[S_BOOSTS], COLBG[S_PRINTS]};
    lv_obj_t** slots[3] = {&ptAvg, &ptBoosts, &ptPrints};
    for (int i = 0; i < 3; i++) {
      lv_obj_t* fc = card(m, mx + i * (figW + 8), figTop, figW, figH);
      lv_obj_set_style_bg_color(fc, C(figBg[i]), 0);
      lv_obj_set_style_bg_grad_color(fc, C(C_CARD2), 0);
      lv_obj_set_style_border_color(fc, lv_color_mix(C(figCol[i]), C(C_EDGE), 90), 0);
      *slots[i] = label(fc, STRIP || ROOMY ? F32 : F40, figCol[i], 0, 0, "");
      lv_obj_align(*slots[i], LV_ALIGN_CENTER, 0, STRIP ? -12 : ROOMY ? -10 : -14);
      lv_obj_t* cap = label(fc, STRIP ? F16 : F14, C_MUTED, 0, 0, caps[i]);
      lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, STRIP ? -12 : ROOMY ? -8 : -18);
    }
  } else {
    const lv_font_t* figF = STRIP || ROOMY ? F32 : F22;
    const lv_font_t* capF = STRIP ? F16 : ROOMY ? F14 : F12;
    const int figY = TH - lineH(figF) - lineH(capF) - 22, capY = figY + lineH(figF) + 2, step = PT_SPLIT_W / 3;
    ptAvg = label(m, figF, C_WARN, mx, figY, "");
    label(m, capF, C_MUTED, mx, capY, TINY ? "avg/day" : "avg / day");
    ptBoosts = label(m, figF, COL[S_BOOSTS], mx + step, figY, "");
    label(m, capF, C_MUTED, mx + step, capY, "boosts");
    ptPrints = label(m, figF, COL[S_PRINTS], mx + 2 * step, figY, "");
    label(m, capF, C_MUTED, mx + 2 * step, capY, "prints");
    if (TINY) {
      // Three columns of ~52 px. A wide points figure must not run into the next one.
      for (lv_obj_t* l : {ptAvg, ptBoosts, ptPrints}) {
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, step - 4);
      }
    }
  }
  ptNoData = label(m, F16, C_MUTED, 0, 0, COMPACT ? "Needs a fresh MakerWorld cookie" : "30-day stats need a fresh MakerWorld cookie");
  lv_obj_center(ptNoData);
}

static uint32_t ptTint(float v) { return v > 0.05f ? C_OK : v < -0.05f ? C_ERR : C_MUTED; }

static void refreshPoints() {
  char a[64], b[16], c[16];
  lv_label_set_text_fmt(ptBalance, "%.2f", st.totalPoints);
  if (ROOMY) {
    // 3.5": 40 px while the balance fits its card, else 32.
    const lv_font_t* f = &lv_font_montserrat_40;
    const char* bt = lv_label_get_text(ptBalance);
    if (lv_txt_get_width(bt, strlen(bt), f, 0, LV_TEXT_FLAG_NONE) > lv_obj_get_width(lv_obj_get_parent(ptBalance)) - 24)
      f = &lv_font_montserrat_32;
    lv_obj_set_style_text_font(ptBalance, f, 0);
  }
  snprintf(a, sizeof(a), STRIP || ROOMY ? "%+.1f" : "%+.1f today", payloadLagDays() ? 0.0f : st.todayPoints);
  lv_label_set_text(ptToday, a);
  lv_obj_set_style_text_color(ptToday, C(ptTint(payloadLagDays() ? 0.0f : st.todayPoints)), 0);
  if (STRIP || ROOMY) {
    if (st.hasYday) lv_label_set_text_fmt(ptYday, "%+.1f", st.ydayPoints);
    else lv_label_set_text(ptYday, "-");
    float w = 0;
    for (int d = 0; d < st.ndays; d++) w += st.days[d].points;
    if (st.ndays) lv_label_set_text_fmt(pt7d, "%+.1f", w);
    else lv_label_set_text(pt7d, "-");
    // Gains green, losses red, flat days muted.
    lv_obj_set_style_text_color(ptYday, C(st.hasYday ? ptTint(st.ydayPoints) : C_MUTED), 0);
    lv_obj_set_style_text_color(pt7d, C(st.ndays ? ptTint(w) : C_MUTED), 0);
  }
  if (TALL) lv_label_set_text_fmt(ptFollow, "%d  /  %d", st.followers, st.models);
  const Month& m = st.month;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(ptMonthCard); i++) {
    lv_obj_t* ch = lv_obj_get_child(ptMonthCard, i);
    if (ch != ptNoData) show(ch, m.has);
  }
  // Only blame the cookie when the poller says auth is missing/expired.
  bool authOk = !strcmp(st.auth, "ok");
  lv_label_set_text(ptNoData, authOk ? (COMPACT ? "30-day stats unavailable" : "30-day stats unavailable - retrying next poll")
                                     : (COMPACT ? "Needs a fresh MakerWorld cookie" : "30-day stats need a fresh MakerWorld cookie"));
  lv_label_set_text(ptHint, authOk ? (COMPACT ? "retrying" : "MakerWorld didn't return them this poll")
                                   : (COMPACT ? "needs cookie" : "30-day stats need a MakerWorld cookie"));
  show(ptNoData, !m.has);
  show(ptHint, !m.has);
  if (!m.has) return;
  fmtIsoDate(b, sizeof(b), m.start);
  fmtIsoDate(c, sizeof(c), m.end);
  snprintf(a, sizeof(a), COMPACT ? "%s - %s" : "LAST 30 DAYS  -  %s - %s", b, c);
  lv_label_set_text(ptRange, a);
  lv_label_set_text_fmt(ptMonth, "%+.1f", m.points);
  float sum = m.pointsModels + m.pointsProfiles;
  int wa = sum > 0 ? (int)(PT_SPLIT_W * m.pointsModels / sum) : PT_SPLIT_W;
  lv_obj_set_width(ptSplitA, max(wa - 2, 0));
  lv_obj_set_x(ptSplitB, wa + 2);
  lv_obj_set_width(ptSplitB, max(PT_SPLIT_W - wa - 2, 0));
  lv_label_set_text_fmt(ptLegA, COMPACT ? "%.0f models" : "%.0f from models", m.pointsModels);
  lv_label_set_text_fmt(ptLegB, COMPACT ? "%.0f profile" : "%.0f from profile", m.pointsProfiles);
  lv_label_set_text_fmt(ptAvg, "%.1f", m.points / 30.0f);
  lv_label_set_text_fmt(ptBoosts, "%+d", m.boosts);
  lv_label_set_text_fmt(ptPrints, "%d", m.prints);
}

// ---- 6. Reach ---------------------------------------------------------------
struct ReachRow {
  lv_obj_t *fill, *val, *rate;
};
static ReachRow reach[4];
static Header reachHdr;
static lv_obj_t *reachChips[3], *reachNoData, *reachYday;
static int RC_TRACK_X, RC_TRACK_W;

static void buildReach() {
  lv_obj_t* s = screens[SC_REACH] = newScreen();
  reachHdr = header(s, "REACH", "last 30 days");
  reachYday = hint(reachHdr, "");
  const char* names[4] = {TINY ? "IMPR." : "IMPRESSIONS", "VIEWS", TINY ? "DLs" : "DOWNLOADS", "PRINTS"};
  const uint32_t cols[4] = {C_DIM, 0x8A95A3, COL[S_DOWNLOADS], COL[S_PRINTS]};
  const int gap = COMPACT ? 6 : 8, chipsH = COMPACT ? 30 : (TALL ? 44 : 40);
  const int rowH = min(TALL ? 112 : 80, (TH - chipsH - 4 * gap) / 4);
  // 2": "IMPRESSIONS" took 96 px and left the bar 72 px wide.
  const int labW = TINY ? 58 : COMPACT ? 96 : STRIP ? 136 : 124, valW = TINY ? 62 : COMPACT ? 70 : STRIP ? 124 : 100,
            rateW = COMPACT ? 46 : STRIP ? 76 : 70;
  const lv_font_t* nameF = STRIP ? &lv_font_montserrat_16 : F12;
  const lv_font_t* valF = STRIP ? F26 : F22;
  const lv_font_t* rateF = STRIP ? F16 : F12;
  RC_TRACK_X = PAD + labW;
  RC_TRACK_W = CW - labW - valW - rateW - 20;
  for (int i = 0; i < 4; i++) {
    int y = TOP + i * (rowH + gap);
    lv_obj_t* l = label(s, nameF, C_SUB, PAD, y + (rowH - lineH(nameF)) / 2, names[i]);
    lv_obj_set_style_text_letter_space(l, COMPACT || STRIP ? 0 : 1, 0);
    if (TINY) {
      lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
      lv_obj_set_width(l, labW - 6);
    }
    lv_obj_t* track = card(s, RC_TRACK_X, y, RC_TRACK_W, rowH);
    reach[i].fill = box(track, 0, 0, RC_TRACK_W, rowH, cols[i], 10);
    reach[i].val = label(s, valF, C_TXT, RC_TRACK_X + RC_TRACK_W + 10, y + (rowH - lineH(valF)) / 2, "");
    lv_obj_set_width(reach[i].val, valW);
    lv_label_set_long_mode(reach[i].val, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(reach[i].val, LV_TEXT_ALIGN_RIGHT, 0);
    reach[i].rate = label(s, rateF, C_MUTED, W - PAD - rateW, y + (rowH - lineH(rateF)) / 2, "");
    lv_obj_set_width(reach[i].rate, rateW);
    lv_label_set_long_mode(reach[i].rate, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(reach[i].rate, LV_TEXT_ALIGN_RIGHT, 0);
  }
  lv_obj_t* chips = row(s, COMPACT ? 6 : 10);
  lv_obj_set_pos(chips, PAD, TOP + 4 * (rowH + gap) + 2);
  for (int i = 0; i < 3; i++) {
    reachChips[i] = label(chips, STRIP ? F16 : F14, C_TXT, 0, 0, "");
    chipStyle(reachChips[i], C_CHIP, C_TXT);
    if (TINY) lv_obj_set_style_pad_hor(reachChips[i], 6, 0);
    lv_label_set_recolor(reachChips[i], true);
  }
  reachNoData = label(s, F16, C_MUTED, 0, 0, COMPACT ? "Needs a fresh MakerWorld cookie" : "Reach stats need a fresh MakerWorld cookie");
  lv_obj_center(reachNoData);
}

static void refreshReach() {
  const Month& m = st.month;
  lv_label_set_text(reachNoData, !strcmp(st.auth, "ok")
                                    ? (COMPACT ? "Reach stats unavailable" : "Reach stats unavailable - retrying next poll")
                                    : (COMPACT ? "Needs a fresh MakerWorld cookie" : "Reach stats need a fresh MakerWorld cookie"));
  show(reachNoData, !m.has);
  for (int i = 0; i < 4; i++) {
    show(lv_obj_get_parent(reach[i].fill), m.has);
    show(reach[i].val, m.has);
    show(reach[i].rate, m.has);
  }
  show(lv_obj_get_parent(reachChips[0]), m.has);
  if (!m.has) {
    lv_label_set_text(reachYday, "");
    return;
  }
  char a[64], b[16], c[16];
  long vals[4] = {m.impressions, m.views, m.downloads, m.prints};
  double top = log10((double)max(1L, m.impressions) + 1);
  for (int i = 0; i < 4; i++) {
    // Log scale so small stages are still visible next to impressions.
    int w = (int)(RC_TRACK_W * log10((double)vals[i] + 1) / top);
    lv_obj_set_width(reach[i].fill, constrain(w, 6, RC_TRACK_W));
    fmtInt(a, sizeof(a), vals[i]);
    lv_label_set_text(reach[i].val, a);
    if (i == 0) lv_label_set_text(reach[i].rate, "");
    else if (vals[i - 1] > 0) lv_label_set_text_fmt(reach[i].rate, "%.1f%%", 100.0 * vals[i] / vals[i - 1]);
    else lv_label_set_text(reach[i].rate, "-");
  }
  fmtIsoDate(b, sizeof(b), m.start);
  fmtIsoDate(c, sizeof(c), m.end);
  snprintf(a, sizeof(a), COMPACT ? "%s - %s" : "last 30 days  -  %s - %s", b, c);
  lv_label_set_text(reachHdr.sub, a);
  fmtInt(b, sizeof(b), m.ydayImpressions);
  fmtInt(c, sizeof(c), m.ydayViews);
  snprintf(a, sizeof(a), COMPACT ? "yday %s / %s" : "yesterday: %s impressions  -  %s views", b, c);
  lv_label_set_text(reachYday, a);
  if (TINY) {
    lv_label_set_text_fmt(reachChips[0], "Fol %d #4FD18B (%+d)#", st.followers, m.followers);
    lv_label_set_text_fmt(reachChips[1], "Like #FF6B8A %+d#", m.likes);
    lv_label_set_text_fmt(reachChips[2], "Col #5AB0FF %+d#", m.saves);
  } else {
    lv_label_set_text_fmt(reachChips[0], COMPACT ? "Fol. %d #4FD18B (%+d)#" : "Followers %d #4FD18B (%+d)#", st.followers, m.followers);
    lv_label_set_text_fmt(reachChips[1], "Likes #FF6B8A %+d#", m.likes);
    lv_label_set_text_fmt(reachChips[2], "Collects #5AB0FF %+d#", m.saves);
  }
}

// ---- 7. Comments ------------------------------------------------------------
struct CommentUi {
  lv_obj_t *card, *user, *when, *text, *model;
};
static CommentUi commentUi[3];
static int perPage = 3;  // comment cards that fit
static Header commentHdr;
static lv_obj_t* commentEmpty;
static int commentPage = 0;

static void buildComments() {
  lv_obj_t* s = screens[SC_COMMENTS] = newScreen();
  commentHdr = header(s, "COMMENTS", "");
  // One wide card on compact (text stays readable), three on wide panels.
  perPage = COMPACT ? 1 : STRIP ? 2 : 3;
  // 3.5": the page count already fills the header; the hint ran into it.
  if (!ROOMY) hint(commentHdr, INPUT_TOUCH ? "swipe up/down: more" : "press: next");
  const int w = (CW - (perPage - 1) * 10) / perPage;
  for (int i = 0; i < perPage; i++) {
    CommentUi& c = commentUi[i];
    c.card = card(s, PAD + i * (w + 10), TOP, w, TH);
    c.user = label(c.card, STRIP || ROOMY ? F16 : F14, C_SEL, 16, 14, "");
    dotLabel(c.user, w - 32 - 60);
    c.when = label(c.card, STRIP || ROOMY ? F14 : F12, C_MUTED, 0, 0, "");
    lv_obj_align(c.when, LV_ALIGN_TOP_RIGHT, -16, 16);
    // 3.5": one card with room to spare, so the comment gets the bigger type.
    const bool bigText = TALL || STRIP || ROOMY;
    c.text = label(c.card, ROOMY ? &lv_font_montserrat_22 : bigText ? F20 : F16, C_TXT, 16, bigText ? 48 : 44, "");
    lv_label_set_long_mode(c.text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_line_space(c.text, TALL ? 6 : STRIP || ROOMY ? 2 : 0, 0);
    lv_obj_set_size(c.text, w - 32, TH - (bigText ? 48 : 44) - (TALL ? 48 : 40));
    c.model = label(c.card, STRIP || ROOMY ? F14 : F12, C_MUTED, 16, TH - (STRIP || ROOMY ? 30 : 28), "");
    dotLabel(c.model, w - 32);
  }
  commentEmpty = label(s, F20, C_MUTED, 0, 0, "No comments yet");
  lv_obj_align(commentEmpty, LV_ALIGN_CENTER, 0, 10);
}

static void refreshComments() {
  int pages = max(1, (st.ncomments + perPage - 1) / perPage);
  if (commentPage >= pages) commentPage = 0;
  if (commentPage < 0) commentPage = pages - 1;
  char a[200];
  for (int i = 0; i < perPage; i++) {
    int ci = commentPage * perPage + i;
    CommentUi& c = commentUi[i];
    show(c.card, ci < st.ncomments);
    if (ci >= st.ncomments) continue;
    const CommentRow& r = st.comments[ci];
    lv_label_set_text(c.user, r.user);
    fmtIsoDate(a, sizeof(a), r.when);
    lv_label_set_text(c.when, a);
    snprintf(a, sizeof(a), "\"%s\"", r.text);
    lv_label_set_text(c.text, a);
    snprintf(a, sizeof(a), "on %s", r.model);
    lv_label_set_text(c.model, a);
  }
  show(commentEmpty, st.ncomments == 0);
  char b[24];
  fmtInt(b, sizeof(b), st.commentCount);
  if (TINY) snprintf(a, sizeof(a), "%s  %d/%d", b, commentPage + 1, pages);
  else snprintf(a, sizeof(a), "%s total  -  page %d/%d", b, commentPage + 1, pages);
  lv_label_set_text(commentHdr.sub, a);
}

// ---- 8. Status --------------------------------------------------------------
// ---- 8. Best times ------------------------------------------------------------
// When activity happens, from the poller's last 30 days: 24 hour-of-day bars
// and a weekday row, best ones highlighted. Chips (or swipe up/down) pick the
// metric. Shows "collecting data" until a week of history exists.
static const int BEST_KIND[NBEST] = {S_DOWNLOADS, S_PRINTS, S_BOOSTS};
static const char* const BEST_NAME[NBEST] = {"Downloads", "Prints", "Boosts"};
static const char* const WDAY[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
static Header bestHdr;
static int bestMetric = 0;
static lv_obj_t *bestChips[NBEST], *bestHourCard, *bestDayCard, *bestEmpty, *bestNote;
static lv_obj_t *bestBars[24], *bestDayBox[7], *bestDayVal[7];
static int BEST_BOTTOM, BEST_MAX, bestDayW;

static void refreshBest();
static void bestChipCb(lv_event_t* e) {
  bestMetric = tapIdx(e);
  refreshBest();
}

static void buildBest() {
  lv_obj_t* s = screens[SC_BEST] = newScreen();
  bestHdr = header(s, TINY ? "BEST" : "BEST TIMES", COMPACT ? "" : "last 30 days");
  lv_obj_set_style_pad_column(bestHdr.right, 6, 0);
  for (int i = 0; i < NBEST; i++) {
    bestChips[i] = label(bestHdr.right, F14, C_MUTED, 0, 0, TINY && i == 0 ? "DLs" : BEST_NAME[i]);
    tappable(bestChips[i], bestChipCb, i);
  }
  const int gap = COMPACT ? 6 : 10, dayH = COMPACT ? 56 : (TALL ? 96 : 72), hourH = TH - dayH - gap;
  bestHourCard = card(s, PAD, TOP, CW, hourH);
  lv_obj_t* t = label(bestHourCard, F12, C_SUB, 14, 10, "BY HOUR OF DAY");
  lv_obj_set_style_text_letter_space(t, 1, 0);
  bestNote = label(bestHourCard, F14, C_ACCENT, 0, 0, "");
  lv_obj_align(bestNote, LV_ALIGN_TOP_RIGHT, -14, 8);
  const int inner = CW - 28, step = inner / 24, bw = max(3, step - 3);
  BEST_BOTTOM = hourH - lineH(F12) - 12;
  BEST_MAX = BEST_BOTTOM - (10 + lineH(F12) + 12);
  for (int h = 0; h < 24; h++) {
    int x = 14 + h * step;
    bestBars[h] = box(bestHourCard, x, BEST_BOTTOM - 3, bw, 3, C_TRACK, 2);
    if (h % 6 == 0) label(bestHourCard, F12, C_MUTED, x, BEST_BOTTOM + 4, h == 0 ? "12 AM" : h == 6 ? "6 AM" : h == 12 ? "12 PM" : "6 PM");
  }
  bestDayCard = card(s, PAD, TOP + hourH + gap, CW, dayH);
  const int cw = (CW - 28 - 6 * 6) / 7;
  bestDayW = cw;
  for (int i = 0; i < 7; i++) {
    lv_obj_t* b = bestDayBox[i] = box(bestDayCard, 14 + i * (cw + 6), 8, cw, dayH - 16, C_CHIP, 4);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, C(C_EDGE), 0);
    lv_obj_t* n = label(b, F12, C_SUB, 0, 0, WDAY[i]);
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, COMPACT ? 4 : 8);
    bestDayVal[i] = label(b, TALL ? F26 : TINY ? F14 : F20, C_TXT, 0, 0, "-");
    lv_obj_align(bestDayVal[i], LV_ALIGN_BOTTOM_MID, 0, COMPACT ? -4 : -8);
  }
  bestEmpty = label(s, F20, C_MUTED, 0, 0, "");
  lv_obj_align(bestEmpty, LV_ALIGN_CENTER, 0, 10);
}

static void refreshBest() {
  const Insights& in = st.insights;
  for (int i = 0; i < NBEST; i++)
    chipStyle(bestChips[i], i == bestMetric ? COL[BEST_KIND[i]] : C_CHIP, i == bestMetric ? C_BG : C_MUTED);
  bool has = st.valid && in.has;
  show(bestHourCard, has);
  show(bestDayCard, has);
  show(bestEmpty, !has);
  if (!has) {
    char a[64];
    snprintf(a, sizeof(a), st.valid ? "Collecting data (%d of 7 days)" : "Waiting for the poller", in.days);
    lv_label_set_text(bestEmpty, a);
    return;
  }
  const int m = bestMetric, col = COL[BEST_KIND[m]];
  int mx = 1, mw = 1;
  for (int h = 0; h < 24; h++) mx = max(mx, (int)in.hour[m][h]);
  for (int i = 0; i < 7; i++) mw = max(mw, (int)in.wday[m][i]);
  for (int h = 0; h < 24; h++) {
    int v = in.hour[m][h], hh = v ? 4 + v * (BEST_MAX - 4) / mx : 3;
    lv_obj_set_height(bestBars[h], hh);
    lv_obj_set_y(bestBars[h], BEST_BOTTOM - hh);
    lv_obj_set_style_bg_color(bestBars[h], C(!v ? C_TRACK : h == in.bestHour[m] ? C_ACCENT : col), 0);
  }
  for (int i = 0; i < 7; i++) {
    char a[12];
    if (TINY) fmtFit(a, sizeof(a), in.wday[m][i], false, bestDayW - 4, F14);
    else fmtInt(a, sizeof(a), in.wday[m][i]);
    lv_label_set_text(bestDayVal[i], a);
    bool best = i == in.bestDay[m];
    lv_obj_set_style_border_color(bestDayBox[i], C(best ? C_ACCENT : C_EDGE), 0);
    lv_obj_set_style_text_color(bestDayVal[i], C(best ? C_ACCENT : C_TXT), 0);
  }
  char a[64];
  int bh = in.bestHour[m];
  char hb[8];
  if (bh >= 0) fmtHour12(hb, sizeof(hb), bh);
  if (bh >= 0) snprintf(a, sizeof(a), "best %s  -  %s", hb, in.bestDay[m] >= 0 ? WDAY[in.bestDay[m]] : "-");
  else snprintf(a, sizeof(a), "no activity yet");
  lv_label_set_text(bestNote, a);
}


// ---- 9. Creator --------------------------------------------------------------
// The poller's creator stats in five views (header chips, a tap / vertical
// swipe / knob press steps through them): conversion and engagement leaders,
// momentum (rising and stalled), the latest launch against the typical one
// plus the long-tail share, points and followers per model, and unanswered
// comments. A row of summary tiles, then two cards side by side.
enum { CV_CONV, CV_TREND, CV_LAUNCH, CV_EARN, CV_FOL, CV_REPLY, NCVIEW };
static const char* const CV_NAME[NCVIEW] = {"Convert", "Trend", "Launch", "Earn", "Followers", "Feedback"};
static const int CR_ROWS = 4;
static int crRows = CR_ROWS;  // rows in use: the 3.5" shows three in larger type
static const int CR_TILES = 4;
struct CrCard {
  lv_obj_t *card, *title, *name[CR_ROWS], *val[CR_ROWS], *rule[CR_ROWS], *empty;
  int w;
};
struct CrTile {
  lv_obj_t *box, *val, *lbl;
};
static Header crHdr;
static int crView = 0;
static lv_obj_t *crSeg[NCVIEW], *crNote, *crChart, *crTail;
static lv_obj_t *crCur[7], *crMed[7], *crTailLbl[3], *crTailFill[3];
static lv_obj_t *crFolChart, *crFolBar[30], *crFolZero;
static int CR_FOL_MID, CR_FOL_HALF;
static CrCard crCards[2];
static CrTile crTiles[CR_TILES];
static int crTileN = 0, crTileY, crTileH, crCardW, crCardX1, crCardH;
static int CR_CH_BOTTOM, CR_CH_MAX, CR_TAIL_W;

static void refreshCreator();
static int crCardList[2] = {-1, -1};  // which CL_* list each card shows (-1: none)
static const char* crTileDef[CR_TILES];

// A list row opens that model's detail page.
static void crRowCb(lv_event_t* e) {
  int k = tapIdx(e), card = k / CR_ROWS, row = k % CR_ROWS, list = crCardList[card];
  if (list < 0 || row >= st.creator.n[list]) return;
  const CRow& r = st.creator.rows[list][row];
  static TopRow t;
  memset(&t, 0, sizeof(t));
  strlcpy(t.id, r.id, sizeof(t.id));
  strlcpy(t.name, r.name, sizeof(t.name));
  modelPage(&t);
}

// A tile says what its number means.
static void crTileCb(lv_event_t* e) {
  int i = tapIdx(e);
  if (i < CR_TILES && crTileDef[i]) ui_toast(crTileDef[i]);
}

static void crChipCb(lv_event_t* e) {
  crView = tapIdx(e);
  refreshCreator();
}

static int crTitleY() { return TINY ? 5 : COMPACT ? 8 : 12; }
static const lv_font_t* crTitleF() { return STRIP ? F14 : F12; }
static int crHeadH() { return crTitleY() + lineH(crTitleF()) + (TINY ? 2 : COMPACT ? 4 : 8); }
static int crEmptyDy() { return crHeadH() / 2; }

// Rows spread over the whole card, hairlines between them. Names keep at
// least 55% of the width; values sit right-aligned in the rest.
static void crMakeCard(CrCard& c, lv_obj_t* s, int x, int y, int w, int h) {
  c.card = card(s, x, y, w, h);
  c.w = w;
  const lv_font_t* rf = TALL || STRIP || ROOMY ? F16 : F14;
  const int px = COMPACT ? 10 : 16;
  crRows = ROOMY ? 3 : CR_ROWS;
  c.title = label(c.card, crTitleF(), C_SUB, px, crTitleY(), "");
  lv_obj_set_style_text_letter_space(c.title, TINY ? 0 : 1, 0);
  dotLabel(c.title, w - 2 * px);
  const int y0 = crHeadH();
  const int step = (h - y0 - 4) / crRows;
  for (int i = 0; i < CR_ROWS; i++) {
    int ry = y0 + min(i, crRows - 1) * step + (step - lineH(rf)) / 2;
    if (i) c.rule[i] = box(c.card, px, y0 + i * step, w - 2 * px, 1, C_EDGE);
    else c.rule[i] = nullptr;
    c.name[i] = label(c.card, rf, C_TXT, px, ry, "");
    c.val[i] = label(c.card, rf, C_ACCENT, 0, ry, "");
    lv_obj_set_style_text_align(c.val[i], LV_TEXT_ALIGN_RIGHT, 0);
    // The name is the tap target for the whole row (48 px tall).
    tappable(c.name[i], crRowCb, (&c == &crCards[0] ? 0 : CR_ROWS) + i);
    lv_obj_set_ext_click_area(c.name[i], max(4, (48 - lineH(rf)) / 2));
  }
  c.empty = label(c.card, F14, C_MUTED, 0, 0, "");
  lv_obj_set_width(c.empty, w - 2 * px);
  lv_label_set_long_mode(c.empty, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(c.empty, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(c.empty, LV_ALIGN_CENTER, 0, y0 / 2);
}

// Card width can change (Reply uses one full-width card).
static void crCardWidth(CrCard& c, int w) {
  c.w = w;
  lv_obj_set_width(c.card, w);
  const int px = COMPACT ? 10 : 16;
  lv_obj_set_width(c.title, w - 2 * px);
  for (int i = 0; i < CR_ROWS; i++) {
    if (c.rule[i]) lv_obj_set_width(c.rule[i], w - 2 * px);
    // 2" cards are ~130 px inside: values like "6.4%  19 dl" need the
    // larger share, and never wrap onto the row below.
    const int namePct = TINY ? 43 : STRIP ? 63 : 58, valPct = TINY ? 55 : STRIP ? 35 : 40;
    dotLabel(c.name[i], (w - 2 * px) * namePct / 100);
    dotLabel(c.val[i], (w - 2 * px) * valPct / 100);
    lv_obj_set_x(c.val[i], w - px - 2 - (w - 2 * px) * valPct / 100);
  }
  lv_obj_set_width(c.empty, w - 2 * px);
  lv_obj_align(c.empty, LV_ALIGN_CENTER, 0, crEmptyDy());
}

static void crMakeTile(CrTile& t, lv_obj_t* s) {
  t.box = card(s, PAD, crTileY, 100, crTileH);
  tappable(t.box, crTileCb, &t - crTiles);
  t.val = label(t.box, COMPACT ? F20 : STRIP ? F32 : F26, C_TXT, 0, 0, "");
  t.lbl = label(t.box, STRIP ? F14 : F12, C_MUTED, 0, 0, "");
}

// n equal tiles across the content width. Values are short ("1.7%", "+12")
// and get the full tile, so they never truncate; labels cut with "...".
static void crSetTiles(int n, const char* const* vals, const char* const* lbls, const uint32_t* cols) {
  const int gap = TINY ? 4 : COMPACT ? 6 : 12, w = (CW - (n - 1) * gap) / n;
  const int vh = lineH(COMPACT ? F20 : STRIP ? F32 : F26), lh = lineH(STRIP ? F14 : F12), top = (crTileH - vh - lh - 2) / 2;
  for (int i = 0; i < CR_TILES; i++) {
    CrTile& t = crTiles[i];
    show(t.box, i < n);
    if (i >= n) continue;
    if (n != crTileN) {
      lv_obj_set_pos(t.box, PAD + i * (w + gap), crTileY);
      lv_obj_set_width(t.box, w);
      dotLabel(t.lbl, w - 12);
    }
    setTextIfChanged(t.val, vals[i]);
    setTextIfChanged(t.lbl, lbls[i]);
    lv_obj_set_style_text_color(t.val, C(cols ? cols[i] : C_TXT), 0);
    lv_obj_align(t.val, LV_ALIGN_TOP_MID, 0, top);
    lv_obj_set_style_text_align(t.lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t.lbl, LV_ALIGN_TOP_MID, 0, top + vh + 2);
  }
  crTileN = n;
}

static void buildCreator() {
  lv_obj_t* s = screens[SC_CREATOR] = newScreen();
  crHdr = header(s, "CREATOR", "");
  // Segmented control: in the header on wide panels, its own row on compact.
  lv_obj_t* seg = lv_obj_create(COMPACT ? s : crHdr.right);
  lv_obj_remove_style_all(seg);
  lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(seg, 3, 0);
  lv_obj_set_style_pad_column(seg, 2, 0);
  lv_obj_set_style_bg_color(seg, C(C_CHIP), 0);
  lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(seg, 1, 0);
  lv_obj_set_style_border_color(seg, C(C_EDGE), 0);
  lv_obj_set_style_radius(seg, 6, 0);
  if (COMPACT) {
    lv_obj_set_pos(seg, PAD, TOP);
    lv_obj_set_size(seg, CW, LV_SIZE_CONTENT);
  } else {
    lv_obj_set_size(seg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    // Taller than the header text: lift it clear of the header rule.
    lv_obj_align(crHdr.right, LV_ALIGN_TOP_RIGHT, -PAD - HB_W - 24, TALL ? 8 : 6);
  }
  for (int i = 0; i < NCVIEW; i++) {
    static const char* const CV_SHORT[NCVIEW] = {"Conv", "Trend", "Launch", "Earn", "Fans", "Reply"};
    lv_obj_t* l = crSeg[i] = label(seg, F14, C_MUTED, 0, 0, TINY ? CV_SHORT[i] : CV_NAME[i]);
    lv_obj_set_style_radius(l, 4, 0);
    // 3.5": full names in 14 px only fit a sixth of the row with no side padding.
    lv_obj_set_style_pad_hor(l, TINY || ROOMY ? 1 : COMPACT ? 4 : 12, 0);
    lv_obj_set_style_pad_ver(l, TALL ? 6 : TINY ? 8 : ROOMY ? 6 : 4, 0);
    lv_obj_set_style_bg_color(l, C(C_ACCENT), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    if (COMPACT) lv_obj_set_flex_grow(l, 1);
    tappable(l, crChipCb, i);
    lv_obj_set_style_border_width(l, 0, LV_STATE_PRESSED);
  }
  lv_obj_update_layout(seg);

  const int gap = TINY ? 4 : COMPACT ? 6 : 12;
  crTileY = COMPACT ? TOP + lv_obj_get_height(seg) + gap : TOP;
  // 2": less padding in the summary tiles so the list cards below get a readable row.
  crTileH = (TINY ? 4 : COMPACT ? 8 : STRIP ? 12 : 16) + lineH(COMPACT ? F20 : STRIP ? F32 : F26) + 2 + lineH(STRIP ? F14 : F12);
  for (int i = 0; i < CR_TILES; i++) crMakeTile(crTiles[i], s);
  // Analytics coverage / what the view compares, muted along the bottom.
  crNote = label(s, F12, C_MUTED, 0, 0, "");
  dotLabel(crNote, CW / 2 - 90 - 24);  // between the footer's sync dot and the pager dots
  lv_obj_align(crNote, LV_ALIGN_BOTTOM_LEFT, PAD + 28, -(H - BOT - lineH(F12)) / 2);

  const int y = crTileY + crTileH + gap, h = BOT - y;
  crCardW = (CW - gap) / 2;
  crCardX1 = PAD + crCardW + gap;
  crCardH = h;
  crMakeCard(crCards[0], s, PAD, y, crCardW, h);
  crMakeCard(crCards[1], s, crCardX1, y, crCardW, h);
  crCardWidth(crCards[0], crCardW);
  crCardWidth(crCards[1], crCardW);
  const int cw = crCardW, ch = h;

  // Launch chart in the first card: this launch (accent) and the median one.
  crChart = lv_obj_create(crCards[0].card);
  lv_obj_remove_style_all(crChart);
  lv_obj_clear_flag(crChart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  const int top = TINY ? crTitleY() + lineH(F12) + 4 : (COMPACT ? 8 : 12) + lineH(crTitleF()) + 8;
  lv_obj_set_pos(crChart, 14, top);
  lv_obj_set_size(crChart, cw - 28, ch - top - 8);
  const int inner = cw - 28, step = inner / 7, bw = max(3, (step - (TINY ? 4 : 10)) / 2);
  CR_CH_BOTTOM = ch - top - 8 - lineH(F12) - 4;
  CR_CH_MAX = CR_CH_BOTTOM - 8 - lineH(F12);  // room for the legend
  for (int i = 0; i < 7; i++) {
    int x = i * step + 2;
    crCur[i] = box(crChart, x, CR_CH_BOTTOM - 3, bw, 3, C_ACCENT, 2);
    crMed[i] = box(crChart, x + bw + 2, CR_CH_BOTTOM - 3, bw, 3, C_MUTED, 2);
    char d[8];
    snprintf(d, sizeof(d), COMPACT ? "%d" : "day %d", i + 1);
    label(crChart, F12, C_MUTED, x, CR_CH_BOTTOM + 3, d);
  }
  // Legend, top right of the chart.
  lv_obj_t* lg = row(crChart, 6);
  box(lg, 0, 0, 8, 8, C_ACCENT, 2);
  label(lg, F12, C_MUTED, 0, 0, TINY ? "latest" : "this launch");
  box(lg, 0, 0, 8, 8, C_MUTED, 2);
  label(lg, F12, C_MUTED, 0, 0, "median");
  lv_obj_align(lg, LV_ALIGN_TOP_RIGHT, 0, 0);

  // Followers gained per day over 30 days in the first card; a lost
  // follower is a short red bar under the zero line.
  crFolChart = lv_obj_create(crCards[0].card);
  lv_obj_remove_style_all(crFolChart);
  lv_obj_clear_flag(crFolChart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(crFolChart, 14, top);
  lv_obj_set_size(crFolChart, cw - 28, ch - top - 8);
  {
    const int fh = ch - top - 8 - lineH(F12) - 6, fstep = (cw - 28) / 30, fbw = max(2, fstep - (TINY ? 1 : 2));
    CR_FOL_HALF = fh / 5;                // room under the line for losses
    CR_FOL_MID = fh - CR_FOL_HALF;       // zero line
    crFolZero = box(crFolChart, 0, CR_FOL_MID, fstep * 30, 1, C_EDGE);
    for (int i = 0; i < 30; i++) crFolBar[i] = box(crFolChart, i * fstep + 1, CR_FOL_MID - 2, fbw, 2, C_ACCENT, 1);
    lv_obj_t* a0 = label(crFolChart, F12, C_MUTED, 0, fh + 4, "30 days ago");
    (void)a0;
    lv_obj_t* a1 = label(crFolChart, F12, C_MUTED, 0, 0, "today");
    lv_obj_align(a1, LV_ALIGN_TOP_RIGHT, 0, fh + 4);
  }

  // Long tail in the second card: three bars (older models' share).
  crTail = lv_obj_create(crCards[1].card);
  lv_obj_remove_style_all(crTail);
  lv_obj_clear_flag(crTail, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(crTail, 14, top);
  lv_obj_set_size(crTail, cw - 28, ch - top - 8);
  CR_TAIL_W = cw - 28;
  const int th = (ch - top - 8) / 3;
  for (int i = 0; i < 3; i++) {
    crTailLbl[i] = label(crTail, COMPACT ? F12 : F14, C_TXT, 0, i * th, "");
    dotLabel(crTailLbl[i], CR_TAIL_W);
    int by = i * th + lineH(COMPACT ? F12 : F14) + 4, bh = COMPACT ? 6 : 10;
    box(crTail, 0, by, CR_TAIL_W, bh, C_TRACK, 3);
    crTailFill[i] = box(crTail, 0, by, 2, bh, COL[S_COLLECTS], 3);
  }
}

static void crPct(char* a, size_t cap, float v) {
  if (v < 0) snprintf(a, cap, "-");
  else snprintf(a, cap, v < 0.1f ? "%.1f%%" : "%.0f%%", v * 100);
}

static void crRowsShow(CrCard& c, bool on) {
  if (!on) crCardList[&c == &crCards[0] ? 0 : 1] = -1;
  for (int i = 0; i < CR_ROWS; i++) {
    show(c.name[i], on && i < crRows);
    show(c.val[i], on && i < crRows);
    if (c.rule[i]) show(c.rule[i], on && i < crRows);
  }
  if (!on) show(c.empty, false);
}

static void crList(CrCard& c, const char* title, int list) {
  const Creator& cr = st.creator;
  crCardList[&c == &crCards[0] ? 0 : 1] = list;
  lv_label_set_text(c.title, title);
  int n = cr.has ? min(cr.n[list], crRows) : 0;
  for (int i = 0; i < CR_ROWS; i++) {
    bool on = i < n;
    show(c.name[i], on);
    show(c.val[i], on);
    if (c.rule[i]) show(c.rule[i], on);
    if (!on) continue;
    const CRow& r = cr.rows[list][i];
    lv_label_set_text(c.name[i], r.name);
    char a[32], p[12];
    switch (list) {
      case CL_CONV: crPct(p, sizeof(p), r.a); snprintf(a, sizeof(a), "%s  %d dl", p, (int)r.b); break;
      case CL_ENG: snprintf(a, sizeof(a), "%.0f", r.a); break;
      case CL_RISE: snprintf(a, sizeof(a), "x%.1f  %d dl", r.a, (int)r.b); break;
      case CL_STALL: snprintf(a, sizeof(a), "was %.1f/day", r.a); break;
      case CL_PTS: snprintf(a, sizeof(a), "%.0f  /  %.0f", r.a, r.b); break;
      case CL_FOL: snprintf(a, sizeof(a), "+%d  /  +%d", (int)r.a, (int)r.b); break;
      case CL_RATED: snprintf(a, sizeof(a), "%.2f  (%d)", r.a, (int)r.b); break;
      default: snprintf(a, sizeof(a), "%d", (int)r.a); break;
    }
    lv_label_set_text(c.val[i], a);
    if (STRIP || ROOMY) {
      // 3.16" and 3.5": the name gets whatever the value leaves (short counts leave a lot).
      const int px = COMPACT ? 10 : 16, inner = c.w - 2 * px;
      const int vw = lv_txt_get_width(a, strlen(a), lv_obj_get_style_text_font(c.val[i], 0), 0, LV_TEXT_FLAG_NONE);
      dotLabel(c.name[i], max(inner / 2, inner - vw - 16));
      if (ROOMY) {
        // Wide values ("was 1.0/day") get their full width too, up to half the row.
        const int valW = min(vw + 2, inner / 2);
        dotLabel(c.val[i], valW);
        lv_obj_set_x(c.val[i], c.w - px - valW);
      }
    }
  }
  show(c.empty, !n);
  lv_label_set_text(c.empty, !st.valid ? "Waiting for the poller"
                             : !cr.has ? "Needs a poller with creator stats"
                             : cr.have ? "Nothing here yet" : "Collecting data...");
}

static void refreshCreator() {
  const Creator& cr = st.creator;
  for (int i = 0; i < NCVIEW; i++) {
    bool on = i == crView;
    lv_obj_set_style_bg_opa(crSeg[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(crSeg[i], C(on ? C_BG : C_MUTED), 0);
  }
  setTextIfChanged(crHdr.sub, "");
  bool one = false;
  show(crCards[1].card, !one);
  if ((crCards[0].w == crCardW) == one) crCardWidth(crCards[0], one ? CW : crCardW);
  show(crChart, crView == CV_LAUNCH);
  show(crTail, crView == CV_LAUNCH);
  show(crFolChart, crView == CV_FOL);

  char t[4][24], note[96];
  const char* v[4] = {t[0], t[1], t[2], t[3]};
  const char* l[4] = {"", "", "", ""};
  uint32_t col[4] = {C_TXT, C_TXT, C_TXT, C_TXT};
  int n = 4;
  note[0] = 0;
  if (cr.has && cr.of && cr.have < cr.of)
    snprintf(note, sizeof(note), TINY ? "%d of %d" : "Analytics for %d of %d models", cr.have, cr.of);
  if (!st.valid || !cr.has) {
    for (int i = 0; i < 4; i++) snprintf(t[i], sizeof(t[i]), "-");
    l[0] = TINY ? "DL 7d" : "DL per view 7d";
    l[1] = TINY ? "DL 30d" : "DL per view 30d";
    l[2] = TINY ? "Print/DL" : "Prints per DL";
    l[3] = TINY ? "Eng/100" : "Engagement";
    crSetTiles(4, v, l, nullptr);
    show(crFolChart, false);
    show(crChart, false);
    show(crTail, false);
    crList(crCards[0], TINY ? "DL PER VIEW, 30D" : "DOWNLOADS PER VIEW (30 DAYS)", CL_CONV);
    crList(crCards[1], TINY ? "PER 100 DOWNLOADS" : "ENGAGEMENT PER 100 DOWNLOADS", CL_ENG);
    setTextIfChanged(crNote, note);
    return;
  }

  switch (crView) {
    case CV_CONV:
      crPct(t[0], sizeof(t[0]), cr.dpv7);
      crPct(t[1], sizeof(t[1]), cr.dpv30);
      crPct(t[2], sizeof(t[2]), cr.ppd);
      if (cr.e100 < 0) snprintf(t[3], sizeof(t[3]), "-");
      else snprintf(t[3], sizeof(t[3]), "%.0f", cr.e100);
      l[0] = TINY ? "DL 7d" : "DL per view 7d";
      l[1] = TINY ? "DL 30d" : "DL per view 30d";
      l[2] = TINY ? "Print/DL" : "Prints per DL";
      l[3] = TINY ? "Eng/100" : STRIP ? "Eng. per 100 DL" : "Engagement / 100 DL";
      col[0] = C_ACCENT;
      crList(crCards[0], TINY ? "DL PER VIEW, 30D" : "DOWNLOADS PER VIEW (30 DAYS)", CL_CONV);
      crList(crCards[1], TINY ? "PER 100 DOWNLOADS" : "LIKES + COLLECTS + COMMENTS / 100 DL", CL_ENG);
      break;
    case CV_TREND:
      n = 3;
      snprintf(t[0], sizeof(t[0]), "%d", cr.n[CL_RISE]);
      snprintf(t[1], sizeof(t[1]), "%d", cr.n[CL_STALL]);
      if (cr.n[CL_RISE]) snprintf(t[2], sizeof(t[2]), "x%.1f", cr.rows[CL_RISE][0].a);
      else snprintf(t[2], sizeof(t[2]), "-");
      l[0] = "Rising"; l[1] = "Stalled"; l[2] = TINY ? "Jump" : "Biggest jump";
      col[0] = cr.n[CL_RISE] ? C_OK : C_TXT;
      col[1] = cr.n[CL_STALL] ? C_WARN : C_TXT;
      crList(crCards[0], TINY ? "RISING VS 7-DAY AVG" : "RISING  (YESTERDAY VS 7-DAY AVERAGE)", CL_RISE);
      crList(crCards[1], TINY ? "STALLED" : "STALLED  (WAS DOWNLOADING, NOW QUIET)", CL_STALL);
      break;
    case CV_LAUNCH: {
      crRowsShow(crCards[0], false);
      crRowsShow(crCards[1], false);
      bool has = cr.hasLaunch && cr.lday > 0;
      char ttl[64];
      snprintf(ttl, sizeof(ttl), has ? (TINY ? "LAUNCH: %s" : "LATEST LAUNCH: %s") : (TINY ? "LAUNCH VS MEDIAN" : "LATEST LAUNCH VS MEDIAN"), cr.lname);
      lv_label_set_text(crCards[0].title, ttl);
      lv_label_set_text(crCards[1].title, TINY ? "LONG TAIL, 30D+" : "LONG TAIL: MODELS OVER 30 DAYS OLD");
      if (has) {
        snprintf(t[0], sizeof(t[0]), "%d of 7", cr.lday);
        snprintf(t[1], sizeof(t[1]), "%d", cr.lcur[cr.lday - 1]);
        if (cr.lpct >= 0) snprintf(t[2], sizeof(t[2]), "%d%%", cr.lpct);
        else snprintf(t[2], sizeof(t[2]), "-");
      } else {
        for (int i = 0; i < 3; i++) snprintf(t[i], sizeof(t[i]), "-");
      }
      static char ahead[32];
      snprintf(ahead, sizeof(ahead), cr.lpast ? "Ahead of %d launches" : "Ahead of launches", cr.lpast);
      int old30 = cr.tail[2][0], sum30 = old30 + cr.tail[2][1];
      snprintf(t[3], sizeof(t[3]), "%d%%", sum30 ? 100 * old30 / sum30 : 0);
      l[0] = TINY ? "Day" : "Launch day";
      l[1] = TINY ? "DLs" : "Downloads";
      l[2] = TINY ? "Ahead" : ahead;
      l[3] = TINY ? "Tail" : "Long tail 30d";
      col[2] = has && cr.lpct >= 50 ? C_OK : C_TXT;
      float mx = 1;
      for (int i = 0; i < 7; i++) mx = max(mx, max((float)cr.lcur[i], cr.lmed[i]));
      for (int i = 0; i < 7; i++) {
        int v2 = cr.hasLaunch ? cr.lcur[i] : -1;
        int hc = v2 > 0 ? 4 + (int)(v2 * (CR_CH_MAX - 4) / mx) : 3;
        int hm = cr.hasLaunch && cr.nmed ? 4 + (int)(cr.lmed[i] * (CR_CH_MAX - 4) / mx) : 3;
        lv_obj_set_height(crCur[i], hc);
        lv_obj_set_y(crCur[i], CR_CH_BOTTOM - hc);
        lv_obj_set_style_bg_color(crCur[i], C(v2 >= 0 ? C_ACCENT : C_TRACK), 0);
        lv_obj_set_height(crMed[i], hm);
        lv_obj_set_y(crMed[i], CR_CH_BOTTOM - hm);
      }
      static const char* const TL[3] = {"Today", "7 days", "30 days"};
      for (int i = 0; i < 3; i++) {
        int old = cr.tail[i][0], sum = old + cr.tail[i][1], pc = sum ? 100 * old / sum : 0;
        char s2[64];
        if (TINY) snprintf(s2, sizeof(s2), "%s %d%%", TL[i], pc);
        else snprintf(s2, sizeof(s2), "%s: %d%%  (%d of %d downloads)", TL[i], pc, old, sum);
        lv_label_set_text(crTailLbl[i], s2);
        lv_obj_set_width(crTailFill[i], max(2, CR_TAIL_W * pc / 100));
      }
      break;
    }
    case CV_EARN: {
      // Same unit everywhere (points over 7 / 30 days). Account = the whole
      // balance change (models, profile, spending dips); Models = what
      // MakerWorld attributes to models, only for those with analytics so far.
      snprintf(t[0], sizeof(t[0]), "%.0f %s", cr.acct7, cr.ptTrend > 0 ? LV_SYMBOL_UP : cr.ptTrend < 0 ? LV_SYMBOL_DOWN : "");
      if (st.month.has) snprintf(t[1], sizeof(t[1]), "%.0f", st.month.points);
      else snprintf(t[1], sizeof(t[1]), "-");
      snprintf(t[2], sizeof(t[2]), "%.0f", cr.pts7 < 0 ? 0 : cr.pts7);
      snprintf(t[3], sizeof(t[3]), "%.0f", cr.pts30 < 0 ? 0 : cr.pts30);
      static char m7[32], m30[32];
      bool part = cr.of && cr.have < cr.of;
      snprintf(m7, sizeof(m7), part ? "Models 7d (%d of %d)" : "Models 7d", cr.have, cr.of);
      snprintf(m30, sizeof(m30), part ? "Models 30d (%d of %d)" : "Models 30d", cr.have, cr.of);
      l[0] = TINY ? "Acct 7d" : "Account 7d";
      l[1] = TINY ? "Acct 30d" : "Account 30d";
      l[2] = TINY ? "Mdl 7d" : m7;
      l[3] = TINY ? "Mdl 30d" : m30;
      col[0] = cr.ptTrend > 0 ? C_OK : cr.ptTrend < 0 ? C_ERR : C_TXT;
      crList(crCards[0], TINY ? "POINTS  7D / 30D" : "POINTS PER MODEL  7D / 30D", CL_PTS);
      crList(crCards[1], TINY ? "FOLLOWERS  7D / 30D" : "FOLLOWERS FROM  7D / 30D", CL_FOL);
      break;
    }
    case CV_FOL: {
      const Followers& f = st.fol;
      crRowsShow(crCards[0], false);
      lv_label_set_text(crCards[0].title, TINY ? "GAINED PER DAY" : "FOLLOWERS GAINED PER DAY (30 DAYS)");
      snprintf(t[0], sizeof(t[0]), "%d", st.followers);
      snprintf(t[1], sizeof(t[1]), "%+d", f.has ? f.today : 0);
      snprintf(t[2], sizeof(t[2]), "%+d", f.has ? f.d7 : 0);
      snprintf(t[3], sizeof(t[3]), "%+d", f.has ? f.d30 : st.month.followers);
      l[0] = TINY ? "Now" : "Followers";
      l[1] = "Today";
      l[2] = TINY ? "7d" : "7 days";
      l[3] = TINY ? "30d" : "30 days";
      for (int i = 1; i < 4; i++) col[i] = t[i][0] == '+' && strcmp(t[i], "+0") ? C_OK : t[i][0] == '-' ? C_ERR : C_TXT;
      int mx = 1, fstep = lv_obj_get_width(crFolChart) / 30;
      for (int i = 0; i < f.n; i++) mx = max(mx, (int)f.series[i]);
      for (int i = 0; i < 30; i++) {
        int j = i - (30 - f.n), v = f.has && j >= 0 ? f.series[j] : 0;
        int h = v > 0 ? max(3, v * (CR_FOL_MID - 4) / mx) : v < 0 ? min(CR_FOL_HALF, 3 + (-v) * 3) : 2;
        lv_obj_set_height(crFolBar[i], h);
        lv_obj_set_y(crFolBar[i], v >= 0 ? CR_FOL_MID - h : CR_FOL_MID + 1);
        lv_obj_set_x(crFolBar[i], i * fstep + 1);
        lv_obj_set_style_bg_color(crFolBar[i], C(v > 0 ? (i == 29 ? C_OK : C_ACCENT) : v < 0 ? C_ERR : C_TRACK), 0);
      }
      crList(crCards[1], "FROM MODELS  7D / 30D", CL_FOL);
      break;
    }
    default:
      // Feedback: comments waiting for a reply, and star ratings.
      snprintf(t[0], sizeof(t[0]), "%d", cr.unans);
      snprintf(t[1], sizeof(t[1]), "%d", st.commentCount);
      snprintf(t[2], sizeof(t[2]), "%d", st.totalRatings);
      if (st.totalRatings) snprintf(t[3], sizeof(t[3]), "%.2f", st.ratingAvg);
      else snprintf(t[3], sizeof(t[3]), "-");
      l[0] = TINY ? "Waiting" : "Unanswered";
      l[1] = TINY ? "Cmts" : "Comments, all time";
      l[2] = TINY ? "Stars" : "Ratings, all time";
      l[3] = TINY ? "Avg" : "Average stars";
      col[0] = cr.unans ? C_WARN : C_OK;
      col[3] = st.totalRatings ? COL[K_RATING] : C_TXT;
      crList(crCards[0], TINY ? "UNANSWERED" : "MOST UNANSWERED", CL_UNANS);
      crList(crCards[1], TINY ? "BEST RATED" : "BEST RATED  (3+ RATINGS)", CL_RATED);
      break;
  }
  static const char* const DEF[NCVIEW][CR_TILES] = {
      {"Downloads per view, last 7 days", "Downloads per view, last 30 days", "Prints per download, lifetime",
       "Likes + collects + comments per 100 downloads, 30 days"},
      {"Models with more downloads yesterday than their 7-day average", "Models that were downloading and went quiet",
       "Largest yesterday vs 7-day average ratio", nullptr},
      {"Days since the latest model was published (of 7)", "Downloads of the latest model so far",
       "Share of earlier launches it is ahead of at this day", "Downloads from models over 30 days old, last 30 days"},
      {"Account points gained in 7 days: models, profile and spending", "Account points in 30 days (MakerWorld report)",
       "Points MakerWorld credits to models, 7 days (models with analytics)", "Points credited to models, 30 days"},
      {"Followers now", "Followers gained today", "Followers gained in 7 days", "Followers gained in 30 days"},
      {"Comments from others you haven't replied to", "Comments on all your models", "Star ratings on all your models",
       "Average stars over all ratings (out of 5)"}};
  for (int i = 0; i < CR_TILES; i++) crTileDef[i] = DEF[crView][i];
  crSetTiles(n, v, l, col);
  setTextIfChanged(crNote, note);
}

enum { ST_POLLER, ST_COOKIE, ST_NEXT, ST_BASE, ST_NET, ST_DEVICE, NSTATUS };
struct StatusCell {
  lv_obj_t *dot, *value, *sub;
};
static StatusCell cells[NSTATUS];

static void makeStatusCell(lv_obj_t* parent, int i, int x, int y, int w, int h, const char* name) {
  lv_obj_t* c = card(parent, x, y, w, h);
  const lv_font_t* nameF = F12;
  const lv_font_t* valF = h >= 64 ? F20 : F16;
  const lv_font_t* subF = F12;
  const int pad = COMPACT ? 6 : 12;
  const int nameW = COMPACT ? 96 : (TALL ? 150 : 128);
  const bool two = h >= lineH(valF) + lineH(subF) + pad * 2 + 2;
  cells[i].dot = box(c, pad, pad + (lineH(nameF) - 8) / 2, 8, 8, C_IDLE, LV_RADIUS_CIRCLE);
  lv_obj_t* l = label(c, nameF, C_SUB, pad + 14, pad, name);
  lv_obj_set_style_text_letter_space(l, TINY ? 0 : 1, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(l, nameW - 14);
  const int textX = pad + nameW;
  const int textW = w - textX - pad;
  if (two) {
    cells[i].value = label(c, valF, C_TXT, textX, pad, "-");
    lv_label_set_long_mode(cells[i].value, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(cells[i].value, textW);
    cells[i].sub = label(c, subF, C_MUTED, textX, h - pad - lineH(subF), "");
    lv_label_set_long_mode(cells[i].sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(cells[i].sub, textW);
  } else {
    int vy = (h - lineH(valF)) / 2;
    cells[i].value = label(c, valF, C_TXT, textX, vy, "-");
    lv_label_set_long_mode(cells[i].value, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(cells[i].value, textW);
    cells[i].sub = label(c, subF, C_MUTED, 0, 0, "");
    show(cells[i].sub, false);
  }
}

static void statusBtnCb(lv_event_t* e);  // defined with openStatus()

// Header chip button (touch boards). Status and About both use it.
static void headerBtn(Header& h, const char* txt, uint32_t fg, lv_event_cb_t cb, intptr_t action) {
  lv_obj_t* b = label(h.right, F14, fg, 0, 0, txt);
  chipStyle(b, C_CHIP, fg);
  // Compact: short enough to stay above the page; ext_click_area keeps the target.
  lv_obj_set_style_pad_ver(b, COMPACT ? 5 : INPUT_TOUCH ? 9 : 6, 0);
  lv_obj_set_style_pad_hor(b, 14, 0);
  lv_obj_set_style_bg_color(b, C(C_RAISED), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b, C(fg), LV_STATE_PRESSED);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(b, 8);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)action);
}

static void buildStatus() {
  lv_obj_t* s = screens[SC_STATUS] = newScreen();
  Header h = header(s, "STATUS", "");
  // The hamburger is hidden here, so the buttons take the full right edge.
  lv_obj_align(h.right, LV_ALIGN_TOP_RIGHT, -PAD, COMPACT ? 4 : 8);
#if INPUT_TOUCH
  headerBtn(h, LV_SYMBOL_REFRESH "  Poll now", C_ACCENT, statusBtnCb, 0);
  headerBtn(h, LV_SYMBOL_CLOSE "  Back", C_TXT, statusBtnCb, 1);
#else
  hint(h, COMPACT ? "press: poll / hold: back" : "press: poll now  -  hold: back");
#endif
  const char* names[NSTATUS] = {"POLLER", "COOKIE", "NEXT POLL", "DAY BASE", "NETWORK", "DEVICE"};
  // One full-width row each, so an address or hostname is not squeezed into a
  // quarter of the screen. Short panels keep a single line; taller ones add
  // the detail underneath.
  const int gap = COMPACT ? 4 : 6;
  const int rowH = (TH - (NSTATUS - 1) * gap) / NSTATUS;
  for (int i = 0; i < NSTATUS; i++)
    makeStatusCell(s, i, PAD, TOP + i * (rowH + gap), CW, rowH, names[i]);
}

static void setCell(int i, uint32_t dot, const char* value, const char* sub) {
  lv_obj_set_style_bg_color(cells[i].dot, C(dot), 0);
  lv_label_set_text(cells[i].value, value);
  lv_label_set_text(cells[i].sub, sub);
}

static void refreshStatus() {
  char a[96], b[96];
  uint32_t n = nowUtc();
  if (!st.valid) {
    setCell(ST_POLLER, C_IDLE, "Waiting", "add it to the poller's panels list");
    setCell(ST_COOKIE, C_IDLE, "-", "");
    setCell(ST_NEXT, C_IDLE, "-", "");
    setCell(ST_BASE, C_IDLE, "-", "");
  } else {
    bool ok = !strcmp(st.auth, "ok");
    fmtAgo(b, sizeof(b), n && st.lastPoll ? (long)(n - st.lastPoll) : -1);
    snprintf(a, sizeof(a), "last poll %s ago", b);
    if (st.health.has && n) {
      // Last good fetch per MakerWorld source ("!" = failing now).
      static const char* const SRC[NHSRC] = {"models", "reach", "points"};
      size_t o = 0;
      for (int i = 0; i < NHSRC; i++) {
        char g[16];
        if (st.health.srcT[i]) fmtAgo(g, sizeof(g), (long)(n - st.health.srcT[i]));
        else snprintf(g, sizeof(g), "never");
        o += snprintf(a + o, sizeof(a) - o, "%s%s %s%s", i ? "  " : "", SRC[i], g, st.health.srcFails[i] ? "!" : "");
      }
    }
    setCell(ST_POLLER, st.pollOk ? (ok ? C_OK : C_WARN) : C_ERR,
            st.pollOk ? (ok ? "Synced" : "Public only") : "Error", st.pollOk ? a : st.msg);
    fmtAgo(a, sizeof(a), st.cookieAge);
    setCell(ST_COOKIE, st.cookieAge < 0 ? C_WARN : ok ? C_OK : C_WARN, st.cookieAge < 0 ? "None" : a,
            ok ? "auth OK" : st.msg);
    int pollSec = setting_value(SET_POLL);
    long left = n && st.lastPoll ? (long)(st.lastPoll + st.pollSec) - (long)n : -1;
    if (left < 0) snprintf(a, sizeof(a), "due");
    else snprintf(a, sizeof(a), "in %ldm %02lds", left / 60, left % 60);
    snprintf(b, sizeof(b), "every %d min", pollSec / 60);
    setCell(ST_NEXT, C_IDLE, a, b);
    if (st.midnight || payloadLagDays()) {
      // The sample may be 23:17 the night before. That is yesterday's clock,
      // so the baseline shown for today is midnight.
      char when[20];
      if (st.since) fmtWeekHM(when, sizeof(when), st.since);
      else when[0] = 0;
      if (COMPACT) snprintf(b, sizeof(b), when[0] ? "midnight %s" : "midnight", when);
      else snprintf(b, sizeof(b), when[0] ? "since midnight - sampled %s" : "since midnight", when);
      setCell(ST_BASE, C_OK, "12:00 AM", b);
    } else {
      fmtHM(a, sizeof(a), st.since);
      setCell(ST_BASE, C_WARN, a, "partial day (poller started)");
    }
  }
  if (g_net.connected) {
    snprintf(b, sizeof(b), "%s%s  -  %s  -  %d dBm", g_net.host, g_net.mdns ? ".local" : " (mDNS off)", g_net.ssid,
             g_net.rssi);
    setCell(ST_NET, g_net.rssi > -70 ? C_OK : C_WARN, g_net.ip, b);
  } else if (g_net.portal) {
    setCell(ST_NET, C_ERR, "Setup", "join " BOARD_AP);
  } else {
    snprintf(b, sizeof(b), "%s%s", g_net.host, settings_mdns() ? ".local" : " (mDNS off)");
    setCell(ST_NET, C_ERR, "Offline", b);
  }
  fmtAgo(a, sizeof(a), millis() / 1000);
#if BAT_ADC_PIN >= 0
  float v = analogReadMilliVolts(BAT_ADC_PIN) * BAT_DIVIDER / 1000.0f;
  if (v > 4.3f || v < 2.5f) snprintf(b, sizeof(b), "v" FW_VERSION "  -  " BOARD_NAME "  -  USB power");
  else snprintf(b, sizeof(b), "v" FW_VERSION "  -  " BOARD_NAME "  -  battery %.2f V", v);
#else
  unsigned psram = ESP.getFreePsram() / 1024;
  if (psram)
    snprintf(b, sizeof(b), "v" FW_VERSION "  -  " BOARD_NAME "  -  %u MB PSRAM free", (psram + 512) / 1024);
  else
    snprintf(b, sizeof(b), "v" FW_VERSION "  -  " BOARD_NAME "  -  %u KB free",
             (unsigned)(ESP.getFreeHeap() / 1024));
#endif
  setCell(ST_DEVICE, C_IDLE, a, b);
}

// ---- touch -------------------------------------------------------------------
// Touch gestures are handled in touchEvent() (see the model above it); menu
// cards, chips and buttons have their own tap handlers. Defined below ui_input().
#if INPUT_TOUCH
static void touchEvent(lv_event_t* e);
static void menuTapCb(lv_event_t* e);
#endif

static void applyBrightness();

// ---- settings menu ----------------------------------------------------------
// One scrolling list in sections, one setting per row: label left, value and
// a chevron right. Option settings open a choice sheet, on/off settings are
// switches, and Quiet from / until open an hours:minutes picker. Sheets are
// built when opened and deleted when closed. Knob: turn moves the highlight,
// press opens or flips, hold goes back.
enum MenuKind : uint8_t { MK_HEAD, MK_CHOICE, MK_TOGGLE, MK_TIME, MK_ACTION, MK_INFO };
enum { MA_WIFI, MA_UPDATE, MA_STATUS, MA_ABOUT, MA_RESET };
enum { MI_ADDR, MI_HOST };
struct MenuDef {
  uint8_t kind;
  int8_t id;          // SettingId, 0/1 for start/end, or MA_*
  const char* label;  // nullptr: the setting's own label
};
static const MenuDef MENU[] = {
  {MK_HEAD, 0, "DISPLAY"},
  {MK_CHOICE, SET_BRIGHTNESS, nullptr},
  {MK_CHOICE, SET_DIM, nullptr},
  {MK_CHOICE, SET_AUTOCYCLE, nullptr},
  {MK_CHOICE, SET_IDLE, nullptr},
  {MK_HEAD, 0, "QUIET HOURS"},
  {MK_TOGGLE, SET_QUIET, nullptr},
  {MK_CHOICE, SET_NIGHT, "During quiet hours"},
  {MK_TIME, 0, "Quiet from"},
  {MK_TIME, 1, "Quiet until"},
  {MK_HEAD, 0, "ALERTS"},
  {MK_CHOICE, SET_NOTIFY, nullptr},
  {MK_CHOICE, SET_BANNER, nullptr},
#if AUDIO_I2S
  {MK_TOGGLE, SET_SOUND, nullptr},
  {MK_CHOICE, SET_VOLUME, nullptr},
#endif
  {MK_HEAD, 0, "NETWORK"},
  {MK_ACTION, MA_WIFI, "Wi-Fi"},
  {MK_INFO, MI_ADDR, "Address"},
  {MK_INFO, MI_HOST, "Hostname"},
  {MK_CHOICE, SET_POLL, nullptr},
  {MK_HEAD, 0, "DEVICE"},
  {MK_ACTION, MA_UPDATE, "Firmware"},
  {MK_ACTION, MA_STATUS, "Status"},
  {MK_ACTION, MA_ABOUT, "About"},
  {MK_ACTION, MA_RESET, "Reset settings"},
};

// One-line explanation under a row's label (the two settings people ask about).
static const char* menuHint(int i) {
  if (MENU[i].kind != MK_CHOICE) return nullptr;
  if (MENU[i].id == SET_NOTIFY) return "Banner and chime for these events";
  if (MENU[i].id == SET_NIGHT) return "Once idle in quiet hours";
  return nullptr;
}
static const int NMENU = sizeof(MENU) / sizeof(MENU[0]);
struct MenuUi {
  lv_obj_t *row, *label, *value, *chev, *track, *thumb, *hint;
};
static MenuUi menuUi[NMENU];
static lv_obj_t* menuList;
static bool menuWide = false;  // 800 px and up: two settings per line
static int menuSel = 1;  // knob highlight; never a section header
static int menuRowH, menuSwW, menuSwH;

// Picker sheet over the list (nullptr when closed).
static lv_obj_t* sheet = nullptr;
static int sheetRow = -1;          // MENU row being edited
static int sheetSel = 0;           // choice: highlighted option; time: 0 hour, 1 minute
static lv_obj_t* sheetOpt[8];      // choice rows
static lv_obj_t* tpHour = nullptr;
static lv_obj_t* tpMin = nullptr;
static lv_obj_t* tpAmPm = nullptr;
static lv_obj_t *brSlider = nullptr, *brPct = nullptr;
enum { SH_CHOICE, SH_TIME, SH_BRIGHT, SH_RESET };
static int sheetMode = SH_CHOICE;

static void menuTapCb(lv_event_t* e);
static void menuBackCb(lv_event_t* e);
static void menuHoldCb(lv_event_t* e);
static void refreshMenu();

static void uiHeap(const char* where) {
  Serial.printf("heap @%s: internal free %u (largest %u)\n", where,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

// A bare container. Unlike box() it keeps scroll chaining, so a drag that
// starts on a row still scrolls the list.
static lv_obj_t* plainObj(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return o;
}

// Long values (a Wi-Fi name, "Update available") always get a whole line.
static bool menuFullRow(int i) {
  return (MENU[i].kind == MK_ACTION && (MENU[i].id == MA_WIFI || MENU[i].id == MA_UPDATE)) || MENU[i].kind == MK_INFO;
}

static bool menuFocusable(int i) {
  return MENU[i].kind != MK_HEAD && MENU[i].kind != MK_INFO && !lv_obj_has_flag(menuUi[i].row, LV_OBJ_FLAG_HIDDEN);
}

static void fmtMinutes(char* out, size_t cap, int m) { fmt12(out, cap, m / 60, m % 60); }

static void buildMenu() {
  menuRoot = newScreen();
  Header h = header(menuRoot, "SETTINGS", "");
  // The hamburger is hidden here, so the back control takes the right edge.
  lv_obj_align(h.right, LV_ALIGN_TOP_RIGHT, -PAD, COMPACT ? 4 : 8);
#if INPUT_TOUCH
  headerBtn(h, LV_SYMBOL_LEFT "  Back", C_TXT, menuBackCb, 0);
#else
  hint(h, COMPACT ? "turn / press / hold: back" : "turn: move  -  press: change  -  hold: back");
#endif
  menuRowH = max(48, TOUCH_PX + 12);
  menuWide = W >= 800;
  menuSwW = COMPACT ? 40 : 48;
  menuSwH = COMPACT ? 22 : 26;
  const int mx = COMPACT ? 12 : 16, radius = COMPACT ? 6 : 8;

  menuList = lv_obj_create(menuRoot);
  lv_obj_remove_style_all(menuList);
  lv_obj_set_pos(menuList, 0, TOP - (COMPACT ? 4 : 6));
  lv_obj_set_size(menuList, W, H - TOP + (COMPACT ? 4 : 6));
  lv_obj_set_flex_flow(menuList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_hor(menuList, PAD, 0);
  lv_obj_set_style_pad_bottom(menuList, COMPACT ? 16 : 24, 0);
  lv_obj_set_scroll_dir(menuList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(menuList, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_color(menuList, C(C_MUTED), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(menuList, LV_OPA_70, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(menuList, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(menuList, 2, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(menuList, 3, LV_PART_SCROLLBAR);

  lv_obj_t* group = nullptr;
  bool first = true;
  for (int i = 0; i < NMENU; i++) {
    const MenuDef& d = MENU[i];
    MenuUi& m = menuUi[i];
    m = MenuUi{};
    if (d.kind == MK_HEAD) {
      // Section title above a flat grouped card (no gradient: it scrolls).
      m.row = label(menuList, F12, C_MUTED, 0, 0, d.label);
      lv_obj_set_style_text_letter_space(m.row, 2, 0);
      lv_obj_set_style_pad_top(m.row, first ? (COMPACT ? 8 : 12) : (COMPACT ? 16 : 24), 0);
      lv_obj_set_style_pad_bottom(m.row, 8, 0);
      lv_obj_set_style_pad_left(m.row, mx, 0);
      first = false;
      group = plainObj(menuList);
      lv_obj_set_size(group, lv_pct(100), LV_SIZE_CONTENT);
      lv_obj_set_flex_flow(group, menuWide ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_bg_color(group, C(C_CARD), 0);
      lv_obj_set_style_bg_opa(group, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(group, 1, 0);
      lv_obj_set_style_border_color(group, C(C_EDGE), 0);
      lv_obj_set_style_radius(group, radius, 0);
      continue;
    }
    lv_obj_t* r = m.row = plainObj(group);
    lv_obj_set_size(r, lv_pct(100), menuRowH);
    lv_obj_set_style_radius(r, radius, 0);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(r, C(C_EDGE), 0);
    lv_obj_set_style_bg_color(r, C(C_RAISED), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
#if INPUT_TOUCH
    if (d.kind != MK_INFO) {
      lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(r, menuTapCb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
      lv_obj_add_event_cb(r, menuHoldCb, LV_EVENT_LONG_PRESSED, nullptr);
    }
#endif
    m.label = label(r, F16, d.kind == MK_INFO ? C_SUB : (d.kind == MK_ACTION && d.id == MA_RESET ? C_ERR : C_TXT), 0, 0,
                    d.label ? d.label : SETTINGS[d.id].label);
    lv_obj_align(m.label, LV_ALIGN_LEFT_MID, mx, 0);
    if (const char* h = menuHint(i)) {
      // Taller row: the label moves up and the hint sits under it.
      lv_obj_set_height(r, menuRowH + lineH(F12));
      lv_obj_align(m.label, LV_ALIGN_LEFT_MID, mx, -lineH(F12) / 2 - 1);
      m.hint = label(r, F12, C_MUTED, 0, 0, h);
      lv_obj_align(m.hint, LV_ALIGN_LEFT_MID, mx, lineH(F16) / 2 + 1);
    }
    if (d.kind == MK_TOGGLE) {
      m.track = box(r, 0, 0, menuSwW, menuSwH, C_DIM, LV_RADIUS_CIRCLE);
      lv_obj_align(m.track, LV_ALIGN_RIGHT_MID, -mx, 0);
      m.thumb = box(m.track, 3, 3, menuSwH - 6, menuSwH - 6, C_TXT, LV_RADIUS_CIRCLE);
    } else {
      m.chev = label(r, F14, C_MUTED, 0, 0, d.kind == MK_INFO ? "" : LV_SYMBOL_RIGHT);
      lv_obj_align(m.chev, LV_ALIGN_RIGHT_MID, -mx, 0);
      m.value = label(r, F16, C_SUB, 0, 0, "");
      lv_label_set_long_mode(m.value, LV_LABEL_LONG_DOT);
      const int chevW = d.kind == MK_INFO ? -10 : lv_txt_get_width(LV_SYMBOL_RIGHT, strlen(LV_SYMBOL_RIGHT), F14, 0, LV_TEXT_FLAG_NONE);
      lv_obj_align(m.value, LV_ALIGN_RIGHT_MID, -(mx + chevW + 10), 0);
      // Whatever the label leaves, so values like "Boosts, prints, comments" fit.
      int lw = lv_txt_get_width(lv_label_get_text(m.label), strlen(lv_label_get_text(m.label)), F16, 0,
                                LV_TEXT_FLAG_NONE);
      const int rowW = menuWide && !menuFullRow(i) ? CW / 2 : CW;
      lv_obj_set_style_max_width(m.value, max(60, rowW - 2 * mx - lw - chevW - 24), 0);
    }
  }
}

// Row widths and hairlines. Wide panels pair visible rows two to a line in
// list order (a row left alone, or a long-value row, takes the whole line);
// the left one of a pair gets a vertical hairline. No line under the last
// line of a section card.
static void menuLayout() {
  for (int i = 0; i < NMENU; i++) {
    if (MENU[i].kind == MK_HEAD) continue;
    int j = i;  // this section: rows i..j-1
    while (j < NMENU && MENU[j].kind != MK_HEAD) j++;
    int line[NMENU], nlines = 0, pending = -1;
    bool left[NMENU] = {};
    for (int k = i; k < j; k++) {
      if (lv_obj_has_flag(menuUi[k].row, LV_OBJ_FLAG_HIDDEN)) continue;
      bool half = menuWide && !menuFullRow(k);
      int hk = menuRowH + (menuHint(k) ? lineH(F12) : 0);
      if (half && pending >= 0) {
        left[pending] = true;
        line[k] = line[pending];
        lv_obj_set_width(menuUi[pending].row, lv_pct(50));
        lv_obj_set_width(menuUi[k].row, lv_pct(50));
        // Both halves of a line as tall as the taller one.
        int hp = menuRowH + (menuHint(pending) ? lineH(F12) : 0), h = max(hk, hp);
        lv_obj_set_height(menuUi[pending].row, h);
        lv_obj_set_height(menuUi[k].row, h);
        pending = -1;
        continue;
      }
      line[k] = nlines++;
      lv_obj_set_width(menuUi[k].row, lv_pct(100));
      lv_obj_set_height(menuUi[k].row, hk);
      pending = half ? k : -1;
    }
    for (int k = i; k < j; k++) {
      if (lv_obj_has_flag(menuUi[k].row, LV_OBJ_FLAG_HIDDEN)) continue;
      int side = (line[k] < nlines - 1 ? LV_BORDER_SIDE_BOTTOM : 0) | (left[k] ? LV_BORDER_SIDE_RIGHT : 0);
      lv_obj_set_style_border_side(menuUi[k].row, (lv_border_side_t)side, 0);
      lv_obj_set_style_border_width(menuUi[k].row, side ? 1 : 0, 0);
    }
    i = j - 1;
  }
}

static void refreshMenu() {
  if (!menuList) return;
  bool quietOn = setting_value(SET_QUIET);
  char buf[40];
  for (int i = 0; i < NMENU; i++) {
    const MenuDef& d = MENU[i];
    MenuUi& m = menuUi[i];
    if (d.kind == MK_HEAD) continue;
    // The quiet window rows only matter while quiet hours are on.
    if (d.kind == MK_TIME || (d.kind == MK_CHOICE && d.id == SET_NIGHT)) show(m.row, quietOn);
    bool sel = !INPUT_TOUCH && i == menuSel;
    lv_obj_set_style_bg_opa(m.row, sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    uint32_t base = d.kind == MK_INFO ? C_SUB : d.kind == MK_ACTION && d.id == MA_RESET ? C_ERR : C_TXT;
    lv_obj_set_style_text_color(m.label, C(sel ? C_ACCENT : base), 0);
    if (d.kind == MK_TOGGLE) {
      bool on = setting_value((SettingId)d.id);
      lv_obj_set_style_bg_color(m.track, C(on ? C_ACCENT : C_DIM), 0);
      lv_obj_set_x(m.thumb, on ? menuSwW - menuSwH + 3 : 3);
      continue;
    }
    const char* val = buf;
    if (d.kind == MK_CHOICE) val = setting_name((SettingId)d.id);
    else if (d.kind == MK_TIME) fmtMinutes(buf, sizeof(buf), d.id ? settings_quiet_end() : settings_quiet_start());
    else if (d.kind == MK_INFO && d.id == MI_ADDR) val = g_net.connected ? g_net.ip : "Offline";
    else if (d.kind == MK_INFO) {
      snprintf(buf, sizeof(buf), g_net.mdns ? "%s.local" : "%s", g_net.host);
      val = buf;
    } else if (d.id == MA_WIFI) val = g_net.connected && g_net.ssid[0] ? g_net.ssid : "Not connected";
    else if (d.id == MA_UPDATE) val = ota_update_available() ? "Update available" : "v" FW_VERSION;
    else val = "";
    lv_label_set_text(m.value, val);
    lv_obj_set_style_text_color(m.value, C(d.id == MA_UPDATE && d.kind == MK_ACTION && ota_update_available()
                                               ? C_OK : (sel ? C_ACCENT : C_SUB)), 0);
  }
  menuLayout();
}

// ---- picker sheets ----
static void sheetClose() {
  if (!sheet) return;
  bool preview = sheetMode == SH_BRIGHT;
  lv_obj_del(sheet);
  sheet = nullptr;
  tpHour = tpMin = tpAmPm = brSlider = brPct = nullptr;
  if (preview) applyBrightness();  // cancelled preview: back to the saved level
  sheetRow = -1;
  uiHeap("sheet closed");
}

// Dimmed backdrop (a tap on it cancels) and a centered card.
static lv_obj_t* sheetCard(int w, const char* title) {
  sheet = plainObj(menuRoot);
  lv_obj_set_size(sheet, W, H);
  lv_obj_set_style_bg_color(sheet, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(sheet, LV_OPA_60, 0);
  lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(sheet, [](lv_event_t* e) {
    if (lv_event_get_target(e) == sheet) sheetClose();
  }, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_t* c = plainObj(sheet);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);  // taps inside don't cancel
  lv_obj_set_width(c, w);
  lv_obj_set_height(c, LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(c, H - (COMPACT ? 16 : 48), 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(c, COMPACT ? 12 : 24, 0);
  lv_obj_set_style_pad_row(c, COMPACT ? 4 : 8, 0);
  lv_obj_set_style_bg_color(c, C(C_CARD), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, C(C_EDGE), 0);
  lv_obj_set_style_radius(c, COMPACT ? 8 : 12, 0);
  lv_obj_center(c);
  lv_obj_t* t = label(c, F20, C_TXT, 0, 0, title);
  lv_obj_set_style_pad_bottom(t, COMPACT ? 4 : 8, 0);
  return c;
}

static void choiceRefresh() {
  SettingId id = (SettingId)MENU[sheetRow].id;
  for (int j = 0; j < SETTINGS[id].count; j++) {
    bool cur = j == setting_index(id), hl = !INPUT_TOUCH && j == sheetSel;
    lv_obj_set_style_bg_opa(sheetOpt[j], hl ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_t* l = lv_obj_get_child(sheetOpt[j], 0);
    lv_obj_t* chk = lv_obj_get_child(sheetOpt[j], 1);
    lv_obj_set_style_text_color(l, C(cur || hl ? C_ACCENT : C_TXT), 0);
    show(chk, cur);
  }
  if (!INPUT_TOUCH) lv_obj_scroll_to_view(sheetOpt[sheetSel], LV_ANIM_ON);
}

static void applySetting(SettingId id) {
  if (id == SET_BRIGHTNESS) applyBrightness();
  if (id == SET_POLL) ui_toast("Poller picks this up within 15 s");
#if AUDIO_I2S
  if (id == SET_VOLUME || id == SET_SOUND) sound_test();
#endif
  refreshMenu();
  refreshStatus();
}

static void choicePick(int j) {
  SettingId id = (SettingId)MENU[sheetRow].id;
  setting_set_value(id, SETTINGS[id].values[j]);
  sheetClose();
  applySetting(id);
}

static void choiceTapCb(lv_event_t* e) {
  lastInput = millis();
  choicePick(tapIdx(e));
}

static void openChoice(int row) {
  SettingId id = (SettingId)MENU[row].id;
  sheetRow = row;
  sheetMode = SH_CHOICE;
  sheetSel = setting_index(id);
  lv_obj_t* c = sheetCard(min(CW, COMPACT ? 360 : 480), MENU[row].label ? MENU[row].label : SETTINGS[id].label);
  lv_obj_t* list = plainObj(c);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  // Fits the screen; longer lists scroll (5 options on a 272 px panel).
  const int room = H - (COMPACT ? 16 : 48) - 2 * (COMPACT ? 12 : 24) - lineH(F20) - 16 - (INPUT_TOUCH ? 0 : lineH(F12) + 8);
  lv_obj_set_size(list, lv_pct(100), min((int)SETTINGS[id].count * menuRowH, room));
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  for (int j = 0; j < SETTINGS[id].count && j < 8; j++) {
    lv_obj_t* r = sheetOpt[j] = plainObj(list);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(r, lv_pct(100), menuRowH);
    lv_obj_set_style_radius(r, 6, 0);
    lv_obj_set_style_bg_color(r, C(C_RAISED), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
#if INPUT_TOUCH
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, choiceTapCb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)j);
#endif
    lv_obj_t* l = label(r, F16, C_TXT, 0, 0, SETTINGS[id].names[j]);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t* chk = label(r, F16, C_ACCENT, 0, 0, LV_SYMBOL_OK);
    lv_obj_align(chk, LV_ALIGN_RIGHT_MID, -12, 0);
  }
  if (!INPUT_TOUCH) label(c, F12, C_MUTED, 0, 0, "turn: choose  -  press: set  -  hold: cancel");
  choiceRefresh();
  uiHeap("choice sheet");
}

static void timeRefresh() {
  lv_obj_t* r[3] = {tpHour, tpMin, tpAmPm};
  for (int k = 0; k < 3; k++) {
    bool hl = !INPUT_TOUCH && k == sheetSel;
    lv_obj_set_style_border_color(r[k], C(hl ? C_ACCENT : C_EDGE), 0);
    lv_obj_set_style_border_width(r[k], hl ? 2 : 1, 0);
  }
}

static void timeSave() {
  // Hour roller: 12, 1, ... 11 (index = hour % 12); AM/PM adds 12.
  int m = ((int)lv_roller_get_selected(tpAmPm) * 12 + (int)lv_roller_get_selected(tpHour)) * 60 +
          (int)lv_roller_get_selected(tpMin) * 15;
  if (MENU[sheetRow].id) settings_set_quiet(settings_quiet_start(), m);
  else settings_set_quiet(m, settings_quiet_end());
  sheetClose();
  refreshMenu();
  applyBrightness();  // the screen may be inside (or outside) the new window now
}

static void timeBtnCb(lv_event_t* e) {
  lastInput = millis();
  if (tapIdx(e)) timeSave();
  else sheetClose();
}

static lv_obj_t* tpRoller(lv_obj_t* parent, const char* opts, int sel) {
  lv_obj_t* r = lv_roller_create(parent);
  // Normal mode: an infinite roller's 7 copies make the label taller than
  // LVGL's text-hint limit and the selected row then draws the wrong line
  // (22 showed as 10). The knob wraps around in sheetInput() instead.
  lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(r, 3);
  lv_obj_set_width(r, COMPACT ? 72 : 112);
  lv_obj_set_style_text_font(r, F32, 0);
  lv_obj_set_style_text_font(r, F32, LV_PART_SELECTED);
  lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(r, C(C_MUTED), 0);
  lv_obj_set_style_bg_color(r, C(C_CARD2), 0);
  lv_obj_set_style_border_color(r, C(C_EDGE), 0);
  lv_obj_set_style_radius(r, 8, 0);
  lv_obj_set_style_text_color(r, C(C_TXT), LV_PART_SELECTED);
  lv_obj_set_style_bg_color(r, C(C_RAISED), LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
  lv_roller_set_selected(r, sel, LV_ANIM_OFF);
  return r;
}

static void openTime(int row) {
  static const char HOURS[] = "12\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11";
  sheetRow = row;
  sheetMode = SH_TIME;
  sheetSel = 0;
  int m = MENU[row].id ? settings_quiet_end() : settings_quiet_start();
  lv_obj_t* c = sheetCard(min(CW, COMPACT ? 340 : 480), MENU[row].label);
  lv_obj_t* r = plainObj(c);
  lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, COMPACT ? 8 : 12, 0);
  tpHour = tpRoller(r, HOURS, m / 60 % 12);
  label(r, F32, C_TXT, 0, 0, ":");
  tpMin = tpRoller(r, "00\n15\n30\n45", (m % 60) / 15);
  tpAmPm = tpRoller(r, "AM\nPM", m / 60 >= 12);
  if (INPUT_TOUCH) {
    lv_obj_t* b = plainObj(c);
    lv_obj_set_size(b, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 12, 0);
    lv_obj_set_style_pad_top(b, COMPACT ? 8 : 16, 0);
    for (int k = 0; k < 2; k++) {
      uint32_t fg = k ? C_ACCENT : C_TXT;
      lv_obj_t* btn = label(b, F16, fg, 0, 0, k ? "Save" : "Cancel");
      chipStyle(btn, C_CHIP, fg);
      lv_obj_set_style_min_height(btn, 48, 0);
      lv_obj_set_style_pad_hor(btn, 22, 0);
      lv_obj_set_style_pad_ver(btn, 14, 0);
      lv_obj_set_style_radius(btn, 8, 0);
      lv_obj_set_style_bg_color(btn, C(C_RAISED), LV_STATE_PRESSED);
      lv_obj_set_style_border_color(btn, C(fg), LV_STATE_PRESSED);
      lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(btn, timeBtnCb, LV_EVENT_CLICKED, (void*)(intptr_t)k);
    }
  } else {
    label(c, F12, C_MUTED, 0, 0, "turn: change  -  press: next field, then save  -  hold: cancel");
  }
  timeRefresh();
  uiHeap("time picker");
}

// Brightness: a slider that previews on the panel as it moves; Done saves,
// a tap outside (or hold) puts the old level back.
static void brShow(int idx) {
  char b[8];
  snprintf(b, sizeof(b), "%d%%", SETTINGS[SET_BRIGHTNESS].values[idx]);
  lv_label_set_text(brPct, b);
  display_set_brightness(SETTINGS[SET_BRIGHTNESS].values[idx]);
}

static void brSave() {
  int idx = lv_slider_get_value(brSlider);
  setting_set_value(SET_BRIGHTNESS, SETTINGS[SET_BRIGHTNESS].values[idx]);
  sheetMode = SH_CHOICE;  // saved: closing must not undo the preview
  sheetClose();
  applySetting(SET_BRIGHTNESS);
}

static void sheetButtons(lv_obj_t* c, const char* no, const char* yes, uint32_t yesCol, lv_event_cb_t cb) {
  lv_obj_t* b = plainObj(c);
  lv_obj_set_size(b, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b, 12, 0);
  lv_obj_set_style_pad_top(b, COMPACT ? 8 : 16, 0);
  for (int k = 0; k < 2; k++) {
    if (!k && !no) continue;
    uint32_t fg = k ? yesCol : C_TXT;
    lv_obj_t* btn = label(b, F16, fg, 0, 0, k ? yes : no);
    chipStyle(btn, C_CHIP, fg);
    lv_obj_set_style_min_height(btn, 48, 0);
    lv_obj_set_style_pad_hor(btn, 22, 0);
    lv_obj_set_style_pad_ver(btn, 14, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, C(C_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, C(fg), LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void*)(intptr_t)k);
  }
}

static void openBrightness(int row) {
  sheetRow = row;
  sheetMode = SH_BRIGHT;
  lv_obj_t* c = sheetCard(min(CW, COMPACT ? 360 : 480), "Brightness");
  brPct = label(c, F32, C_ACCENT, 0, 0, "");
  lv_obj_set_width(brPct, lv_pct(100));
  lv_obj_set_style_text_align(brPct, LV_TEXT_ALIGN_CENTER, 0);
  brSlider = lv_slider_create(c);
  lv_slider_set_range(brSlider, 0, SETTINGS[SET_BRIGHTNESS].count - 1);
  lv_slider_set_value(brSlider, setting_index(SET_BRIGHTNESS), LV_ANIM_OFF);
  lv_obj_set_width(brSlider, lv_pct(90));
  lv_obj_set_height(brSlider, 10);
  lv_obj_set_style_bg_color(brSlider, C(C_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(brSlider, C(C_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(brSlider, C(C_TXT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(brSlider, 10, LV_PART_KNOB);  // ~30 px knob, 48 px with the click area
  lv_obj_set_ext_click_area(brSlider, 20);
  lv_obj_add_event_cb(brSlider, [](lv_event_t*) {
    lastInput = millis();
    brShow(lv_slider_get_value(brSlider));
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  if (INPUT_TOUCH) {
    sheetButtons(c, nullptr, "Done", C_ACCENT, [](lv_event_t*) { brSave(); });
  } else {
    label(c, F12, C_MUTED, 0, 0, "turn: adjust  -  press: save  -  hold: cancel");
  }
  brShow(setting_index(SET_BRIGHTNESS));
  uiHeap("brightness sheet");
}

static void resetBtnCb(lv_event_t* e) {
  lastInput = millis();
  bool yes = tapIdx(e);
  sheetClose();
  if (!yes) return;
  settings_reset_defaults();
  applyBrightness();
  refreshMenu();
  refreshStatus();
  ui_toast("Settings reset to defaults");
}

static void openReset(int row) {
  sheetRow = row;
  sheetMode = SH_RESET;
  lv_obj_t* c = sheetCard(min(CW, COMPACT ? 360 : 480), "Reset settings?");
  lv_obj_t* l = label(c, F14, C_SUB, 0, 0,
                      "Display, quiet hours, alerts and polling go back to their defaults. "
                      "Wi-Fi, hostname, password and update preferences stay.");
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  if (INPUT_TOUCH) sheetButtons(c, "Cancel", "Reset", C_ERR, resetBtnCb);
  else label(c, F12, C_MUTED, 0, 0, "press: reset  -  hold: cancel");
}

// Knob input while a sheet is up.
static void sheetInput(const KnobInput& in) {
  if (in.longPress) {
    sheetClose();
    return;
  }
  if (sheetMode == SH_BRIGHT) {
    if (in.steps) {
      int n = SETTINGS[SET_BRIGHTNESS].count, v = constrain((int)lv_slider_get_value(brSlider) + in.steps, 0, n - 1);
      lv_slider_set_value(brSlider, v, LV_ANIM_ON);
      brShow(v);
    }
    if (in.click) brSave();
    return;
  }
  if (sheetMode == SH_RESET) {
    if (in.click) {
      sheetClose();
      settings_reset_defaults();
      applyBrightness();
      refreshMenu();
      ui_toast("Settings reset to defaults");
    }
    return;
  }
  if (sheetMode == SH_TIME) {
    if (in.steps) {
      lv_obj_t* r = sheetSel == 0 ? tpHour : sheetSel == 1 ? tpMin : tpAmPm;
      int n = sheetSel == 0 ? 12 : sheetSel == 1 ? 4 : 2;
      lv_roller_set_selected(r, ((int)lv_roller_get_selected(r) + in.steps % n + n) % n, LV_ANIM_ON);
    }
    if (in.click) {
      if (sheetSel < 2) {
        sheetSel++;
        timeRefresh();
      } else {
        timeSave();
      }
    }
    return;
  }
  int n = SETTINGS[MENU[sheetRow].id].count;
  if (in.steps) {
    sheetSel = ((sheetSel + in.steps) % n + n) % n;
    choiceRefresh();
  }
  if (in.click) choicePick(sheetSel);
}

static void openMenu(bool open);
static void openAbout(bool open);
static void openUpdate(bool open);
static void openStatus(bool open);

static void menuActivate(int i) {
  const MenuDef& d = MENU[i];
  switch (d.kind) {
    case MK_CHOICE:
      if (d.id == SET_BRIGHTNESS) openBrightness(i);
      else openChoice(i);
      break;
    case MK_TIME:
      openTime(i);
      break;
    case MK_TOGGLE:
      setting_next((SettingId)d.id);
      applySetting((SettingId)d.id);
      break;
    case MK_ACTION:
      if (d.id == MA_WIFI) {
        openMenu(false);
        wifi_start_setup();
      } else if (d.id == MA_UPDATE) {
        openUpdate(true);
      } else if (d.id == MA_STATUS) {
        openMenu(false);
        openStatus(true);
      } else if (d.id == MA_RESET) {
        openReset(i);
      } else {
        openAbout(true);
      }
      break;
  }
}

static void menuMove(int steps) {
  int dir = steps > 0 ? 1 : -1;
  for (int k = abs(steps); k > 0; k--) {
    int j = menuSel;
    do j = (j + dir + NMENU) % NMENU;
    while (!menuFocusable(j));
    menuSel = j;
  }
  refreshMenu();
  // Bring a section's title into view along with its first row.
  int target = menuSel > 0 && MENU[menuSel - 1].kind == MK_HEAD ? menuSel - 1 : menuSel;
  lv_obj_scroll_to_view_recursive(menuUi[target].row, LV_ANIM_ON);
  if (target != menuSel) lv_obj_scroll_to_view_recursive(menuUi[menuSel].row, LV_ANIM_ON);
}

static void refreshStatus();
static void hbUpdate();


static void requestPoll() {
  poll_now_request();
  ui_toast("Poll requested - the poller checks within 15 s");
}

// Today is home: closing Status, Settings, or About always lands there.
static void openStatus(bool open) {
  statusOpen = open;
  if (open) {
    aboutOpen = false;
    if (aboutRoot) show(aboutRoot, false);
    updOpen = false;
    if (updRoot) show(updRoot, false);
  } else {
    cur = SC_TODAY;
  }
  for (int i = 0; i < NSCREEN; i++) show(screens[i], open ? i == SC_STATUS : i == cur);
  if (open) refreshStatus();
  updateDots();
  hbUpdate();
}

static void statusBtnCb(lv_event_t* e) {
  if ((intptr_t)lv_event_get_user_data(e) == 0) requestPoll();
  else openStatus(false);
}

static void openMenu(bool open) {
  sheetClose();
  menuOpen = open;
  if (open) {
    aboutOpen = false;
    if (aboutRoot) show(aboutRoot, false);
    updOpen = false;
    if (updRoot) show(updRoot, false);
  } else {
    cur = SC_TODAY;
  }
  show(menuRoot, open);
  for (int i = 0; i < NSCREEN; i++) show(screens[i], !open && i == cur);
  // Selection and scroll position stay as they were (back from a sheet, the
  // Wi-Fi page or Status lands where you left off).
  if (open) refreshMenu();
  updateDots();
  hbUpdate();
}

// ---- about (@Wofl QR) -------------------------------------------------------
static const char PROFILE_URL[] = "https://makerworld.com/en/@Wofl";

static void openAbout(bool open) {
  aboutOpen = open;
  if (open) {
    menuOpen = false;
    statusOpen = false;
    updOpen = false;
    show(menuRoot, false);
    if (updRoot) show(updRoot, false);
  } else {
    cur = SC_TODAY;
  }
  show(aboutRoot, open);
  for (int i = 0; i < NSCREEN; i++) show(screens[i], !open && i == cur);
  updateDots();
  hbUpdate();
}

static void aboutBtnCb(lv_event_t* e) {
  (void)e;
  openAbout(false);
}

static void buildAbout() {
  lv_obj_t* s = aboutRoot = newScreen();
  Header h = header(s, "ABOUT", COMPACT ? "scan for @Wofl" : "scan to open @Wofl on MakerWorld");
  // The hamburger is hidden here, so the back control takes the right edge.
  lv_obj_align(h.right, LV_ALIGN_TOP_RIGHT, -PAD, COMPACT ? 4 : (TALL ? 12 : 8));
#if INPUT_TOUCH
  headerBtn(h, LV_SYMBOL_CLOSE "  Back", C_TXT, aboutBtnCb, 0);
#else
  hint(h, COMPACT ? "press: back" : "press or hold: back");
#endif
  // Compact shows "@Wofl on MakerWorld" under the plate; 240 px tall needs
  // its line kept out of the QR's share.
  const int urlH = TINY ? lineH(F14) + 8 : COMPACT ? 0 : lineH(TALL ? F16 : F14) + 16;
  const int quiet = TALL ? 22 : (COMPACT ? 10 : 14);
  int qr = TH - urlH - 8 - quiet * 2;
  const int cap = TALL ? 300 : (COMPACT ? 150 : 188);
  if (qr > cap) qr = cap;
  if (qr < 96) qr = 96;
  qr &= ~1;
  const int plate = qr + quiet * 2;
  const int y0 = TOP + max(0, (TH - plate - urlH) / 2);
  // White plate is the quiet zone. The code itself is black on white so a
  // phone can scan it off the glass.
  lv_obj_t* plateObj = box(s, (W - plate) / 2, y0, plate, plate, 0xFFFFFF, TALL ? 16 : 8);
  lv_obj_t* qrObj = lv_qrcode_create(plateObj, qr, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
  lv_qrcode_update(qrObj, PROFILE_URL, (uint32_t)strlen(PROFILE_URL));
  lv_obj_center(qrObj);
  lv_obj_clear_flag(qrObj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  if (!COMPACT) {
    lv_obj_t* url = label(s, TALL ? F16 : F14, C_SUB, 0, 0, PROFILE_URL);
    lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(url, LV_ALIGN_TOP_MID, 0, y0 + plate + 12);
  } else {
    lv_obj_t* who = label(s, F14, C_SUB, 0, 0, "@Wofl on MakerWorld");
    lv_obj_set_style_text_align(who, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(who, LV_ALIGN_TOP_MID, 0, y0 + plate + 8);
  }
}

// ---- firmware update (OTA) --------------------------------------------------
enum { UPD_CHECK, UPD_INSTALL, UPD_AUTO, UPD_BETA, NUPD };
static Header updHdr;
static lv_obj_t *updRemCell, *updRemEye, *updRemVer, *updRemSub;
static lv_obj_t *updBar, *updUrl;
static lv_obj_t *updCard[NUPD], *updLbl[NUPD], *updVal[NUPD];
static lv_obj_t *hbBadge, *hbUpdLbl;
static int updSel = 0;

static void setUpdStatus(const char* txt, uint32_t col) {
  if (!updHdr.sub) return;
  lv_label_set_text(updHdr.sub, txt);
  lv_obj_set_style_text_color(updHdr.sub, C(col), 0);
}

static void refreshUpdateBadge() {
  bool avail = settings_auto_ota() && ota_update_available();
  if (hbBadge) show(hbBadge, avail);
  if (hbUpdLbl) lv_label_set_text(hbUpdLbl, avail ? "Firmware update  (NEW)" : "Firmware update");
  // Footer version: muted normally; with a newer release, an accent chip that
  // opens the update page. Failed or skipped checks never get here.
  static int shown = -1;
  if (verLbl && shown != (int)avail) {
    shown = avail;
    if (avail) {
      char t[48];
      snprintf(t, sizeof(t), "v" FW_VERSION "  " LV_SYMBOL_RIGHT "  v%s", ota_cached_latest_version());
      lv_label_set_text(verLbl, t);
      chipStyle(verLbl, C_CHIP, C_ACCENT);
      lv_obj_set_style_border_color(verLbl, C(C_ACCENT), 0);
      lv_obj_add_flag(verLbl, LV_OBJ_FLAG_CLICKABLE);
    } else {
      lv_label_set_text(verLbl, "v" FW_VERSION);
      lv_obj_set_style_bg_opa(verLbl, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(verLbl, 0, 0);
      lv_obj_set_style_pad_all(verLbl, 0, 0);
      lv_obj_set_style_text_color(verLbl, C(C_IDLE), 0);
      lv_obj_clear_flag(verLbl, LV_OBJ_FLAG_CLICKABLE);
    }
  }
}

static void refreshUpdate() {
  refreshUpdateBadge();
  if (!updRoot) return;
  const char* lat = ota_cached_latest_version();
  bool has = lat && lat[0];
  bool newer = has && ota_compare_versions(lat, FW_VERSION) > 0;
  bool ahead = has && ota_compare_versions(lat, FW_VERSION) < 0;
  bool beta = has && ota_is_prerelease(lat);

  if (!has) {
    // Knob boards: short, since the control hint shares the header row.
    setUpdStatus(!INPUT_TOUCH ? (settings_auto_ota() ? "Hourly check on" : "Not checked yet")
                 : settings_auto_ota() ? "Hourly GitHub check enabled  -  tap Check to query now"
                                       : "Tap Check for updates to query GitHub Releases",
                 C_MUTED);
  } else if (newer) {
    char s[64];
    snprintf(s, sizeof(s), "Update available on GitHub: v%s", lat);
    setUpdStatus(s, C_OK);
  } else if (ahead) {
    setUpdStatus("Ahead of GitHub release (local build)", C_MUTED);
  } else {
    setUpdStatus("Up to date with latest GitHub release", C_MUTED);
  }

  lv_obj_set_style_bg_color(updRemCell, C(newer ? 0x0D2622 : C_BG), 0);
  lv_obj_set_style_border_color(updRemCell, C(newer ? C_OK : C_EDGE), 0);
  lv_label_set_text(updRemEye, beta ? (newer ? "BETA (NEW)" : "BETA") : (newer ? "GITHUB (NEW)" : "GITHUB"));
  lv_obj_set_style_text_color(updRemEye, C(newer ? C_OK : C_MUTED), 0);
  if (has) {
    char v[40];
    snprintf(v, sizeof(v), "v%s", lat);
    lv_label_set_text(updRemVer, v);
    lv_label_set_text(updRemSub, newer ? "ready to install" : (ahead ? "older than installed" : "current release"));
  } else {
    lv_label_set_text(updRemVer, TINY ? "None" : "Not checked");
    lv_label_set_text(updRemSub, PROJECT_GITHUB_REPO);
  }
  lv_obj_set_style_text_color(updRemVer, C(newer ? C_OK : C_TXT), 0);

  if (updUrl) {
    char a[64];
    if (g_net.host[0]) snprintf(a, sizeof(a), "http://%s.local/", g_net.host);
    else if (g_net.connected) snprintf(a, sizeof(a), "http://%s/", g_net.ip);
    else snprintf(a, sizeof(a), "connect Wi-Fi first");
    lv_label_set_text(updUrl, a);
  }

  char instVal[36];
  if (newer) snprintf(instVal, sizeof(instVal), "v%s", lat);
  else if (has) snprintf(instVal, sizeof(instVal), "Reinstall");
  else snprintf(instVal, sizeof(instVal), "Latest");
  const char* vals[NUPD] = {"Query", instVal, settings_auto_ota() ? "On" : "Off", settings_beta_ota() ? "On" : "Off"};
  for (int i = 0; i < NUPD; i++) {
    bool sel = !INPUT_TOUCH && i == updSel;
    bool hi = (i == UPD_INSTALL && newer);
    lv_obj_set_style_bg_color(updCard[i], C(sel ? C_RAISED : (hi ? 0x0D2622 : C_CARD)), 0);
    lv_obj_set_style_border_color(updCard[i], C(sel ? C_SEL : (hi ? C_OK : C_EDGE)), 0);
    lv_label_set_text(updVal[i], vals[i]);
    uint32_t vc = sel ? C_SEL : (hi ? C_OK : ((i >= UPD_AUTO && vals[i][0] == 'O' && vals[i][1] == 'n') ? C_ACCENT : C_MUTED));
    lv_obj_set_style_text_color(updVal[i], C(vc), 0);
    lv_obj_align(updVal[i], LV_ALIGN_RIGHT_MID, COMPACT ? -10 : -16, 0);
  }
}

static void runGithubCheckUi() {
  setUpdStatus("Checking GitHub for updates...", C_ACCENT);
  lv_refr_now(nullptr);
  char ver[32] = "", url[192] = "", err[96] = "";
  if (ota_check_github_update(ver, sizeof(ver), url, sizeof(url), err, sizeof(err))) {
    refreshUpdate();
  } else {
    char msg[128];
    snprintf(msg, sizeof(msg), "Update check failed: %s", err[0] ? err : "error");
    setUpdStatus(msg, C_ERR);
  }
}

static void onGithubProgressUi(int pct) {
  if (display_is_suspended()) return;
  if (updBar) {
    show(updBar, true);
    lv_bar_set_value(updBar, pct, LV_ANIM_OFF);
  }
  char msg[64];
  snprintf(msg, sizeof(msg), "Downloading & flashing... %d%%", pct);
  setUpdStatus(msg, C_ACCENT);
  lv_refr_now(nullptr);
}

static void runGithubInstallUi() {
  if (!ota_cached_latest_version()[0]) {
    setUpdStatus("Checking GitHub release first...", C_ACCENT);
    lv_refr_now(nullptr);
    char ver[32] = "", url[192] = "", err[96] = "";
    if (!ota_check_github_update(ver, sizeof(ver), url, sizeof(url), err, sizeof(err))) {
      char msg[128];
      snprintf(msg, sizeof(msg), "Update check failed: %s", err);
      setUpdStatus(msg, C_ERR);
      return;
    }
    refreshUpdate();
  }
  if (updBar) {
    show(updBar, true);
    lv_bar_set_value(updBar, 0, LV_ANIM_OFF);
  }
  setUpdStatus("Connecting to GitHub...", C_ACCENT);
  lv_refr_now(nullptr);
  char err[96] = "";
  if (ota_install_github_update(ota_cached_download_url(), onGithubProgressUi, err, sizeof(err))) {
    setUpdStatus("Update complete! Rebooting...", C_OK);
    lv_refr_now(nullptr);
    delay(700);
    ESP.restart();
  } else if (display_is_suspended()) {
    delay(300);
    ESP.restart();
  } else {
    if (updBar) show(updBar, false);
    char msg[128];
    snprintf(msg, sizeof(msg), "Update failed: %s", err[0] ? err : "error");
    setUpdStatus(msg, C_ERR);
    lv_refr_now(nullptr);
  }
}

static void updAction(int i) {
  lastInput = millis();
  if (i == UPD_CHECK) {
    runGithubCheckUi();
  } else if (i == UPD_INSTALL) {
    runGithubInstallUi();
  } else if (i == UPD_AUTO) {
    settings_set_auto_ota(!settings_auto_ota());
    refreshUpdate();
    if (settings_auto_ota()) runGithubCheckUi();
  } else if (i == UPD_BETA) {
    settings_set_beta_ota(!settings_beta_ota());
    ota_forget_latest();
    refreshUpdate();
    runGithubCheckUi();
  }
}

static void openUpdate(bool open) {
  updOpen = open;
  if (open) {
    menuOpen = false;
    statusOpen = false;
    aboutOpen = false;
    show(menuRoot, false);
    if (aboutRoot) show(aboutRoot, false);
    updSel = 0;
    if (updBar) show(updBar, false);
    refreshUpdate();
  } else {
    cur = SC_TODAY;
  }
  show(updRoot, open);
  for (int i = 0; i < NSCREEN; i++) show(screens[i], !open && i == cur);
  updateDots();
  hbUpdate();
}

static void updBtnCb(lv_event_t* e) {
  (void)e;
  openUpdate(false);
}

static void updCardCb(lv_event_t* e) {
  updAction((int)(intptr_t)lv_event_get_user_data(e));
}

static void buildUpdate() {
  lv_obj_t* s = updRoot = newScreen();
  updHdr = header(s, TINY || !INPUT_TOUCH ? "UPDATE" : "FIRMWARE UPDATE", TINY ? "Tap Check" : "Tap Check for updates to query GitHub");
  // Knob boards: the right-hand hint is ~330 px, so the status stops short of it.
  dotLabel(updHdr.sub, INPUT_TOUCH ? CW - (COMPACT ? 140 : 220) : CW - 330 - 160);
  lv_obj_align(updHdr.right, LV_ALIGN_TOP_RIGHT, -PAD, COMPACT ? 4 : (TALL ? 12 : 8));
#if INPUT_TOUCH
  headerBtn(updHdr, LV_SYMBOL_CLOSE "  Back", C_TXT, updBtnCb, 0);
#else
  hint(updHdr, COMPACT ? "turn / press / hold: back" : "turn: select  -  press: run  -  hold: back");
#endif

  const int gap = COMPACT ? 6 : 12;
  const int leftW = (CW - gap) * 52 / 100;
  const int rightW = CW - gap - leftW;
  const int mx = COMPACT ? 10 : 16;

  // Left card: Installed vs GitHub version boxes, progress bar, web console URL.
  lv_obj_t* lc = card(s, PAD, TOP, leftW, TH);
  lv_obj_t* eye = label(lc, F12, C_ACCENT, mx, COMPACT ? 8 : 14, "VERSIONS");
  lv_obj_set_style_text_letter_space(eye, 2, 0);

  const int cellY = (COMPACT ? 8 : 14) + lineH(F12) + (COMPACT ? 6 : 10);
  const int cellW = (leftW - 2 * mx - (COMPACT ? 6 : 10)) / 2;
  const int cellH = COMPACT ? 66 : (TALL ? 118 : 88);

  // Installed cell
  lv_obj_t* cInst = box(lc, mx, cellY, cellW, cellH, C_BG, COMPACT ? 4 : 6);
  lv_obj_set_style_border_width(cInst, 1, 0);
  lv_obj_set_style_border_color(cInst, C(C_EDGE), 0);
  lv_obj_t* instEye = label(cInst, F12, C_MUTED, 10, COMPACT ? 6 : 10, TINY ? "LOCAL" : "INSTALLED");
  if (TINY) dotLabel(instEye, cellW - 16);
  lv_obj_t* iv = label(cInst, TALL ? F26 : F20, C_TXT, 10, COMPACT ? 22 : (TALL ? 36 : 28), "v" FW_VERSION);
  dotLabel(iv, cellW - 20);
  lv_obj_t* ib = label(cInst, F12, C_MUTED, 10, cellH - lineH(F12) - (COMPACT ? 5 : 10), BOARD_NAME);
  dotLabel(ib, cellW - 20);

  // GitHub remote cell
  updRemCell = box(lc, mx + cellW + (COMPACT ? 6 : 10), cellY, cellW, cellH, C_BG, COMPACT ? 4 : 6);
  lv_obj_set_style_border_width(updRemCell, 1, 0);
  lv_obj_set_style_border_color(updRemCell, C(C_EDGE), 0);
  updRemEye = label(updRemCell, F12, C_MUTED, 10, COMPACT ? 6 : 10, "GITHUB");
  updRemVer = label(updRemCell, TALL ? F26 : F20, C_TXT, 10, COMPACT ? 22 : (TALL ? 36 : 28), TINY ? "None" : "Not checked");
  dotLabel(updRemVer, cellW - 20);
  updRemSub = label(updRemCell, F12, C_MUTED, 10, cellH - lineH(F12) - (COMPACT ? 5 : 10), PROJECT_GITHUB_REPO);
  dotLabel(updRemSub, cellW - 20);

  // Progress bar for GitHub OTA download/flash
  updBar = lv_bar_create(lc);
  lv_obj_set_pos(updBar, mx, cellY + cellH + (COMPACT ? 8 : 14));
  lv_obj_set_size(updBar, leftW - 2 * mx, COMPACT ? 8 : 12);
  lv_bar_set_range(updBar, 0, 100);
  lv_bar_set_value(updBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(updBar, C(C_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(updBar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(updBar, C(C_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(updBar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(updBar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(updBar, 4, LV_PART_INDICATOR);
  show(updBar, false);

  // Web console info at the bottom of the left card
  const int webY = TH - (COMPACT ? 44 : (TALL ? 78 : 58));
  box(lc, mx, webY - (COMPACT ? 6 : 10), leftW - 2 * mx, 1, C_EDGE);
  label(lc, F12, C_SUB, mx, webY, COMPACT ? "WEB CONSOLE" : "WEB CONSOLE & LOCAL .BIN UPLOAD");
  updUrl = label(lc, TALL ? F20 : F16, C_ACCENT, mx, webY + lineH(F12) + (COMPACT ? 2 : 6), "http://.../");
  dotLabel(updUrl, leftW - 2 * mx);

  // Right column: 4 action cards (Check, Install, Auto-check, Beta channel)
  static const char* const LABELS[NUPD] = {
    LV_SYMBOL_REFRESH "  Check for updates",
    LV_SYMBOL_DOWNLOAD "  Install from GitHub",
    "Auto-check GitHub",
    "Include beta releases",
  };
  static const char* const LABELS_TINY[NUPD] = {
    LV_SYMBOL_REFRESH "  Check",
    LV_SYMBOL_DOWNLOAD "  Install",
    "Auto-check",
    "Betas",
  };
  const int rowGap = COMPACT ? 6 : 8;
  const int rowH = (TH - (NUPD - 1) * rowGap) / NUPD;
  const int rx = PAD + leftW + gap;
  for (int i = 0; i < NUPD; i++) {
    lv_obj_t* c = updCard[i] = card(s, rx, TOP + i * (rowH + rowGap), rightW, rowH);
#if INPUT_TOUCH
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(c, C(C_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(c, C(C_ACCENT), LV_STATE_PRESSED);
    lv_obj_add_event_cb(c, updCardCb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
#endif
    updLbl[i] = label(c, F16, C_TXT, 0, 0, TINY ? LABELS_TINY[i] : LABELS[i]);
    dotLabel(updLbl[i], rightW - (TINY ? 62 : COMPACT ? 78 : 108));
    lv_obj_align(updLbl[i], LV_ALIGN_LEFT_MID, COMPACT ? 10 : 16, 0);
    updVal[i] = label(c, F16, C_MUTED, 0, 0, "");
    lv_obj_align(updVal[i], LV_ALIGN_RIGHT_MID, COMPACT ? -10 : -16, 0);
  }
}

// ---- banner + toast (top layer) ---------------------------------------------
static const int QMAX = 12;
static Event queue[QMAX];
static int qLen = 0;
static uint32_t bannerShownAt = 0;
static bool bannerUp = false;
static lv_obj_t *banner, *bnIconBox, *bnImg, *bnTitle, *bnModel, *bnCount, *bnHint;
static lv_obj_t* toastLbl;
static uint32_t toastUntil = 0;
static int BN_H = 96, BN_Y = 14;

static void goScreen(int idx, int dir);
static void bannerClear();
static void dotTapCb(lv_event_t* e) {
  int i = tapIdx(e);
  if (i != cur) goScreen(i, i > cur ? 1 : -1);
}
#if INPUT_TOUCH
static void bannerTapCb(lv_event_t* e) { bannerClear(); }
#endif

static void buildOverlay() {
  lv_obj_t* top = lv_layer_top();
  BN_H = COMPACT ? 72 : 96;
  BN_Y = COMPACT ? 8 : 14;
  const int ib = COMPACT ? 44 : 60, tx = COMPACT ? 68 : 100;
  banner = box(top, PAD, -BN_H - 24, CW, BN_H, C_RAISED, COMPACT ? 4 : 8);
  lv_obj_set_style_bg_grad_color(banner, C(C_CARD2), 0);
  lv_obj_set_style_bg_grad_dir(banner, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(banner, 1, 0);
  lv_obj_set_style_shadow_width(banner, 24, 0);
  lv_obj_set_style_shadow_color(banner, lv_color_black(), 0);
  lv_obj_set_style_shadow_opa(banner, LV_OPA_60, 0);
#if INPUT_TOUCH
  lv_obj_add_flag(banner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(banner, bannerTapCb, LV_EVENT_CLICKED, nullptr);
#endif
  bnIconBox = box(banner, COMPACT ? 12 : 20, (BN_H - ib) / 2 - 2, ib, ib, COLBG[0], COMPACT ? 4 : 6);
  bnImg = icon(bnIconBox, 0, !COMPACT, COMPACT ? 12 : 14, COMPACT ? 12 : 14);
  bnTitle = label(banner, F26, COL[0], tx, COMPACT ? 10 : 16, "");
  dotLabel(bnTitle, CW - tx - (COMPACT ? 90 : 200));
  bnModel = label(banner, F16, C_SUB, tx, COMPACT ? 40 : 54, "");
  dotLabel(bnModel, CW - tx - (COMPACT ? 16 : 200));
  bnCount = label(banner, F12, C_MUTED, 0, 0, "");
  lv_obj_align(bnCount, LV_ALIGN_TOP_RIGHT, -20, 20);
  bnHint = label(banner, F12, C_TXT, 0, 0, INPUT_TOUCH ? "tap or swipe up to dismiss" : "press to dismiss");
  chipStyle(bnHint, C_CHIP, C_TXT);
  lv_obj_align(bnHint, LV_ALIGN_BOTTOM_RIGHT, -20, -18);
  show(bnHint, !COMPACT);
  show(banner, false);

  // Page dots; updateDots() widens the current one.
  lv_obj_t* dotRow = row(top, 6);
  slideKeep[1] = dotRow;
  lv_obj_align(dotRow, LV_ALIGN_BOTTOM_MID, 0, COMPACT ? 3 : 3);
  lv_obj_set_style_pad_column(dotRow, 0, 0);
  for (int i = 0; i < NCAR; i++) {
    // ~24 px tall hit box per segment; the visible bar sits in the middle.
    lv_obj_t* hit = lv_obj_create(dotRow);
    lv_obj_remove_style_all(hit);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(hit, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(hit, COMPACT ? 8 : 10, 0);
    lv_obj_set_style_pad_hor(hit, 3, 0);
    lv_obj_set_style_radius(hit, 3, 0);
    tappable(hit, dotTapCb, i);
    dots[i] = box(hit, 0, 0, 10, 3, C_DIM, 1);
  }

  // Firmware version, small and muted, on the pager's line at the far right.
  verLbl = label(top, F12, C_IDLE, 0, 0, "v" FW_VERSION);
  lv_obj_align(verLbl, LV_ALIGN_BOTTOM_RIGHT, -PAD, -(H - BOT - lineH(F12)) / 2);
  lv_obj_set_ext_click_area(verLbl, 16);
  lv_obj_add_event_cb(verLbl, [](lv_event_t*) {
    lastInput = millis();
    bannerClear();
    openUpdate(true);
  }, LV_EVENT_CLICKED, nullptr);
  slideKeep[2] = verLbl;

  toastLbl = label(top, F14, C_TXT, 0, 0, "");
  chipStyle(toastLbl, C_RAISED, C_TXT);
  lv_obj_set_style_pad_ver(toastLbl, 8, 0);
  lv_obj_set_style_pad_hor(toastLbl, 16, 0);
  lv_obj_set_style_border_width(toastLbl, 1, 0);
  lv_obj_set_style_border_color(toastLbl, C(C_SEL), 0);
  lv_obj_set_style_shadow_width(toastLbl, 20, 0);
  lv_obj_set_style_shadow_opa(toastLbl, LV_OPA_70, 0);
  lv_obj_align(toastLbl, LV_ALIGN_CENTER, 0, 0);
  show(toastLbl, false);
}

static void renderBanner() {
  const Event& e = queue[0];
  int k = e.kind;
  lv_obj_set_style_border_color(banner, C(COL[k]), 0);
  lv_obj_set_style_bg_color(bnIconBox, C(COLBG[k]), 0);
  setIcon(bnImg, k, true);
  char a[160];
  if (k == K_COMMENT) {
    snprintf(a, sizeof(a), "New comment%s%s", e.user[0] ? " from " : "", e.user);
    lv_label_set_text(bnTitle, a);
    snprintf(a, sizeof(a), "\"%s\"  -  %s", e.text, e.model);
    lv_label_set_text(bnModel, a);
  } else {
    fmtEventTitle(a, sizeof(a), k, e.n, e.stars);
    lv_label_set_text(bnTitle, a);
    lv_label_set_text(bnModel, e.model);
  }
  lv_obj_set_style_text_color(bnTitle, C(COL[k]), 0);
  if (qLen > 1) lv_label_set_text_fmt(bnCount, COMPACT ? "1 of %d" : "just now  -  1 of %d", qLen);
  else lv_label_set_text(bnCount, "just now");
#if INPUT_TOUCH
  lv_label_set_text(bnHint, qLen > 1 ? "swipe: next  -  tap: clear" : "tap to dismiss");
#else
  lv_label_set_text(bnHint, qLen > 1 ? "turn: next  -  press: clear" : "press to dismiss");
#endif
  lv_obj_align(bnCount, LV_ALIGN_TOP_RIGHT, COMPACT ? -12 : -20, COMPACT ? 12 : 20);
  lv_obj_align(bnHint, LV_ALIGN_BOTTOM_RIGHT, -20, -18);
  bannerShownAt = millis();
}

static void slideBanner(bool in) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, banner);
  lv_anim_set_values(&a, in ? -BN_H - 24 : BN_Y, in ? BN_Y : -BN_H - 24);
  lv_anim_set_time(&a, in ? 320 : 220);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
  lv_anim_set_path_cb(&a, in ? lv_anim_path_overshoot : lv_anim_path_ease_in);
  lv_anim_start(&a);
}

static void bannerNext() {
  if (qLen <= 0) return;
  memmove(&queue[0], &queue[1], sizeof(Event) * (qLen - 1));
  qLen--;
  if (qLen) {
    renderBanner();
  } else {
    bannerUp = false;
    slideBanner(false);
  }
}

static void bannerClear() {
  qLen = 0;
  if (bannerUp) {
    bannerUp = false;
    slideBanner(false);
  }
}

// Full-screen notice drawn right before the display is switched off for a
// firmware update (the screen stays dark until the panel restarts).
void ui_update_notice(const char* title, const char* sub) {
  lv_obj_t* o = box(lv_layer_top(), 0, 0, W, H, C_BG);
  lv_obj_t* t = label(o, F26, C_ACCENT, 0, 0, title);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -lineH(F26) / 2 - 6);
  lv_obj_t* l = label(o, F16, C_SUB, 0, 0, sub);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, lineH(F16) / 2 + 10);
  lv_refr_now(NULL);
}

static lv_obj_t *updScr = nullptr, *updPctLbl, *updFill;
static int updTrackW = 0;

bool ui_updating() { return updScr != nullptr; }

void ui_update_progress(int pct) {
  if (!updScr) {
    updScr = box(lv_layer_top(), 0, 0, W, H, C_BG);
    lv_obj_add_flag(updScr, LV_OBJ_FLAG_CLICKABLE);  // nothing underneath reacts
    lv_obj_t* t = label(updScr, F26, C_ACCENT, 0, 0, "Updating firmware");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -lineH(F48) / 2 - lineH(F26) - 8);
    updPctLbl = label(updScr, F48, C_TXT, 0, 0, "0%");
    lv_obj_align(updPctLbl, LV_ALIGN_CENTER, 0, -lineH(F48) / 4);
    updTrackW = min(CW, 480);
    const int ty = H / 2 + lineH(F48) / 2;
    box(updScr, (W - updTrackW) / 2, ty, updTrackW, 8, C_TRACK, 4);
    updFill = box(updScr, (W - updTrackW) / 2, ty, 8, 8, C_ACCENT, 4);
    // The note is wider than a 320 px panel: wrap it inside the content width.
    lv_obj_t* sub = label(updScr, TINY ? F14 : F16, C_SUB, 0, 0, "Do not unplug. The panel restarts when it's done.");
    lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(sub, CW);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, ty + 8 + (COMPACT ? 10 : 18));
    show(toastLbl, false);
  }
  char b[16];
  if (pct < 0) snprintf(b, sizeof(b), "...");
  else snprintf(b, sizeof(b), "%d%%", min(pct, 100));
  lv_label_set_text(updPctLbl, b);
  lv_obj_align(updPctLbl, LV_ALIGN_CENTER, 0, -lineH(F48) / 4);
  lv_obj_set_width(updFill, max(8, updTrackW * constrain(pct, 0, 100) / 100));
  lv_refr_now(NULL);
}

static lv_obj_t* splashObj = nullptr;

void ui_splash() {
  layout();
  lv_obj_set_style_bg_color(lv_scr_act(), C(C_BG), 0);
  splashObj = box(lv_scr_act(), 0, 0, W, H, C_BG);
  lv_obj_t* t = label(splashObj, F32, C_ACCENT, 0, 0, "MakerMonitor");
  lv_obj_set_style_text_letter_space(t, 2, 0);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -lineH(F32) / 2);
  lv_obj_t* l = label(splashObj, F14, C_MUTED, 0, 0, "starting  -  v" FW_VERSION);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, lineH(F14) / 2 + 10);
  lv_refr_now(NULL);
  display_set_brightness(setting_value(SET_BRIGHTNESS));
}

void ui_toast(const char* msg) {
  lv_label_set_text(toastLbl, msg);
  show(toastLbl, true);
  toastUntil = millis() + 2500;
}

// ---- brightness / idle --------------------------------------------------------
static uint32_t lastCycle = 0;

static void hbOpen(bool open);
static uint32_t lastWake = 0;  // last banner on screen; restarts the dim timer
static bool dimmed = false;

// Full brightness unless idle past "Dim after" (~10% then) or in quiet hours
// (5%). A banner keeps the screen awake while it is up.
static void applyBrightness() {
  uint32_t now = millis();
  if (bannerUp) lastWake = now;
  int mod = minuteOfDay();
  int dim = setting_value(SET_DIM), bright = setting_value(SET_BRIGHTNESS);
  // Night mode: in Quiet hours, after the normal dim timeout (or 30 s), the
  // screen goes off or very low. Any touch wakes it (and only wakes it).
  uint32_t quietAfter = dim ? (uint32_t)dim * 1000 : 30000;
  bool quiet = mod >= 0 && in_quiet_hours(mod) && now - lastInput > quietAfter && !bannerUp;
  uint32_t idle = now - max(lastInput, lastWake);
  bool idleDim = dim && !bannerUp && idle > (uint32_t)dim * 1000;
  int level = bright;
  if (idleDim) level = max(3, bright / 10);
  if (quiet) level = setting_value(SET_NIGHT) ? min(level, 3) : 0;
  // Going dim (idle) also returns home, so waking always shows Today. Already
  // there with nothing open: leave the screen alone (no redraw, no dot flicker).
  if (idleDim && !dimmed) {
    hbOpen(false);
    if (menuOpen) openMenu(false);
    if (statusOpen) openStatus(false);
    if (aboutOpen) openAbout(false);
    if (updOpen) openUpdate(false);
    if (cur != SC_TODAY) goScreen(SC_TODAY, 0);
  }
  dimmed = level < bright;
  static int lastLevel = -1;
  if (level != lastLevel) {
    lastLevel = level;
    markPagesDirty();  // snapshots taken at the old level (software dimming)
  }
  display_set_brightness(level);
}

bool ui_wake() {
  if (!dimmed) return false;
  lastInput = millis();
  lastCycle = millis();
  applyBrightness();
  return true;
}

// ---- public API -------------------------------------------------------------
static void refreshAll() {
  markPagesDirty();
  refreshToday();
  refreshTodayHeader();
  refreshFeed();
  refreshTop();
  refreshTrend();
  refreshPoints();
  refreshReach();
  refreshComments();
  refreshBest();
  refreshCreator();
  refreshStatus();
}

static bool wifiOpen = false;



// ---- Wi-Fi page -----------------------------------------------------------------
// Settings > Change Wi-Fi, the "not connected" banner, or first boot. Scans in
// station mode, lists networks by signal, and joins one with an on-screen
// keyboard. Everything is built on open and deleted on close.
static lv_obj_t *wpRoot = nullptr, *wpCur, *wpList, *wpStatus, *wpDlg = nullptr;
static lv_obj_t *wpSsidTa, *wpPassTa, *wpKb, *wpShowLbl;
static lv_obj_t* whLbl = nullptr;  // knob character wheel (knob boards)
static lv_obj_t* apRoot = nullptr;  // "Set up from phone" screen
static lv_obj_t* wifiBanner;
static lv_timer_t* wpTimer = nullptr;
static WifiAp wpAps[20];
static int wpN = 0;
static bool wpScanning = false;
static uint32_t wpScanAt = 0;
static int wpConnLast = 0;
static char wpPick[33];
static bool wpPickSaved = false, wpHidden = false;
static int wpSel = 0;     // knob: highlighted list row
static int wpKbSel = 0;   // knob: highlighted keyboard key

static void wpRefreshCurrent() {
  char a[96], saved[33];
  if (g_net.connected) snprintf(a, sizeof(a), "Connected to %s  -  %s", g_net.ssid, g_net.ip);
  else if (net_saved_ssid(saved, sizeof(saved))) snprintf(a, sizeof(a), "Not connected  -  saved network: %s", saved);
  else snprintf(a, sizeof(a), "No network saved yet  -  choose one below");
  lv_label_set_text(wpCur, a);
}

static lv_obj_t* wpButton(lv_obj_t* parent, const char* txt, uint32_t fg, lv_event_cb_t cb) {
  lv_obj_t* b = label(parent, F14, fg, 0, 0, txt);
  chipStyle(b, C_CHIP, fg);
  lv_obj_set_style_pad_ver(b, COMPACT ? 5 : 8, 0);
  lv_obj_set_style_pad_hor(b, 16, 0);
  lv_obj_set_style_bg_color(b, C(C_RAISED), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b, C(fg), LV_STATE_PRESSED);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(b, 8);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

static void wpCloseDialog() {
  if (wpDlg) lv_obj_del_async(wpDlg);
  if (wpKb) lv_obj_del_async(wpKb);
  if (whLbl) lv_obj_del_async(whLbl);
  wpDlg = wpKb = whLbl = nullptr;
}

static void wpConnectNow() {
  const char* ssid = wpHidden ? lv_textarea_get_text(wpSsidTa) : wpPick;
  const char* pass = wpPassTa ? lv_textarea_get_text(wpPassTa) : "";
  if (!ssid[0]) return;
  char s[33], pw[65];
  strlcpy(s, ssid, sizeof(s));
  strlcpy(pw, pass, sizeof(pw));
  wpCloseDialog();
  // A saved network with the password left empty reuses the stored one.
  net_connect(s, wpPickSaved && !pw[0] ? nullptr : pw);
  wpConnLast = 1;
}

static void wpConnectCb(lv_event_t*) { wpConnectNow(); }
static void wpCancelCb(lv_event_t*) { wpCloseDialog(); }
static void wpKbCb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_READY) {
    // "Other network": OK on the name moves on to the password.
    if (wpHidden && lv_keyboard_get_textarea(wpKb) == wpSsidTa) {
      lv_obj_clear_state(wpSsidTa, LV_STATE_FOCUSED);
      lv_obj_add_state(wpPassTa, LV_STATE_FOCUSED);
      lv_keyboard_set_textarea(wpKb, wpPassTa);
      return;
    }
    wpConnectNow();
  } else if (lv_event_get_code(e) == LV_EVENT_CANCEL) {
    wpCloseDialog();
  }
}
static void wpTaFocusCb(lv_event_t* e) {
  if (wpKb) lv_keyboard_set_textarea(wpKb, lv_event_get_target(e));
}
static void wpShowCb(lv_event_t*) {
  bool hidden = lv_textarea_get_password_mode(wpPassTa);
  lv_textarea_set_password_mode(wpPassTa, !hidden);
  lv_label_set_text(wpShowLbl, hidden ? "Hide" : "Show");
}

static lv_obj_t* wpTextarea(lv_obj_t* parent, int y, int w, const char* placeholder) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_obj_set_pos(ta, 14, y);
  lv_obj_set_width(ta, w);
  lv_obj_set_style_text_font(ta, F16, 0);
  lv_obj_set_style_bg_color(ta, C(C_CHIP), 0);
  lv_obj_set_style_text_color(ta, C(C_TXT), 0);
  lv_obj_set_style_border_color(ta, C(C_EDGE), 0);
  lv_obj_set_style_border_color(ta, C(C_ACCENT), LV_STATE_FOCUSED);
  lv_obj_set_style_radius(ta, 4, 0);
  lv_obj_set_style_text_color(ta, C(C_MUTED), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_bg_color(ta, C(C_ACCENT), LV_PART_CURSOR);
  lv_obj_add_event_cb(ta, wpTaFocusCb, LV_EVENT_FOCUSED, nullptr);
  return ta;
}

// ---- knob character wheel ----
// Turn scrolls through one character group, press adds the highlighted
// character. The slots before the characters: delete, next group, show/hide
// and OK (connect, or "next" from the SSID field of "Other network").
static const char* const WH_GROUPS[] = {"abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789",
                                        " !@#$%^&*()-_=+[]{};:'\",.<>/?\\|`~"};
static const char* const WH_GROUP_NAME[] = {"abc", "ABC", "123", "#+="};
static const int WH_NGROUP = 4, WH_SLOTS = 4;  // DEL, group, show, OK
static lv_obj_t* whTa = nullptr;
static int whGroup = 0, whPos = WH_SLOTS;

static lv_obj_t* first_field(bool hidden) { return hidden ? wpSsidTa : wpPassTa; }

static int whCount() { return WH_SLOTS + (int)strlen(WH_GROUPS[whGroup]); }

static void whItem(int i, char* out, size_t cap) {
  if (i == 0) snprintf(out, cap, LV_SYMBOL_BACKSPACE);
  else if (i == 1) snprintf(out, cap, "%s", WH_GROUP_NAME[(whGroup + 1) % WH_NGROUP]);
  else if (i == 2) snprintf(out, cap, LV_SYMBOL_EYE_OPEN);
  else if (i == 3) snprintf(out, cap, LV_SYMBOL_OK);
  else snprintf(out, cap, "%c", WH_GROUPS[whGroup][i - WH_SLOTS] == ' ' ? '_' : WH_GROUPS[whGroup][i - WH_SLOTS]);
}

// Seven slots around the selection, selected one in cyan.
static void whRender() {
  char buf[200], it[8];
  size_t o = 0;
  int n = whCount();
  for (int d = -3; d <= 3; d++) {
    int i = ((whPos + d) % n + n) % n;
    whItem(i, it, sizeof(it));
    if (d == 0) o += snprintf(buf + o, sizeof(buf) - o, "#22D3EE [ %s ]#   ", it);
    else o += snprintf(buf + o, sizeof(buf) - o, "%s   ", it);
  }
  lv_label_set_text(whLbl, buf);
}

static void wpWheelBuild(lv_obj_t* field) {
  whTa = field;
  whGroup = 0;
  whPos = WH_SLOTS;
  lv_obj_t* hint = label(wpDlg, F12, C_MUTED, 0, 0, "turn: choose  -  press: add  -  hold: cancel");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 14, -14);
  whLbl = label(wpRoot, TALL ? F32 : F26, C_TXT, 0, 0, "");
  lv_label_set_recolor(whLbl, true);
  lv_obj_align(whLbl, LV_ALIGN_BOTTOM_MID, 0, -(H * 42 / 100) / 3);
  whRender();
}

static void wpConnectNow();
static void wpWheelKnob(const KnobInput& in) {
  if (in.steps) {
    int n = whCount();
    whPos = ((whPos + in.steps) % n + n) % n;
  }
  if (in.click) {
    if (whPos == 0) {
      lv_textarea_del_char(whTa);
    } else if (whPos == 1) {
      whGroup = (whGroup + 1) % WH_NGROUP;
      whPos = 1;
    } else if (whPos == 2) {
      lv_textarea_set_password_mode(wpPassTa, !lv_textarea_get_password_mode(wpPassTa));
    } else if (whPos == 3) {
      if (wpHidden && whTa == wpSsidTa) {  // name done, now the password
        lv_obj_clear_state(wpSsidTa, LV_STATE_FOCUSED);
        lv_obj_add_state(wpPassTa, LV_STATE_FOCUSED);
        whTa = wpPassTa;
      } else {
        wpConnectNow();
        return;
      }
    } else {
      lv_textarea_add_char(whTa, WH_GROUPS[whGroup][whPos - WH_SLOTS]);
    }
  }
  if (whLbl) whRender();
}

// ssid == nullptr: "Other network" (type the name too).
static void wpOpenDialog(const char* ssid, bool saved) {
  wpCloseDialog();
  wpHidden = !ssid;
  wpPickSaved = saved;
  strlcpy(wpPick, ssid ? ssid : "", sizeof(wpPick));
  const int kbH = H * 42 / 100, dw = min(CW, 620), dh = H - kbH - 16;
  wpDlg = card(wpRoot, (W - dw) / 2, 8, dw, dh);
  lv_obj_add_flag(wpDlg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_color(wpDlg, C(C_ACCENT), 0);
  char t[64];
  snprintf(t, sizeof(t), ssid ? "Join %s" : "Other network", ssid ? ssid : "");
  lv_obj_t* tl = label(wpDlg, F16, C_TXT, 14, 10, t);
  dotLabel(tl, dw - 28);
  int y = 10 + lineH(F16) + 8;
  const int taW = dw - 28 - 80;
  wpSsidTa = nullptr;
  if (wpHidden) {
    wpSsidTa = wpTextarea(wpDlg, y, dw - 28, "Network name (SSID)");
    lv_textarea_set_max_length(wpSsidTa, 32);
    y += lineH(F16) + 22;
  }
  wpPassTa = wpTextarea(wpDlg, y, taW, saved ? "Leave empty to use the saved password" : "Password");
  lv_textarea_set_password_mode(wpPassTa, true);
  lv_textarea_set_max_length(wpPassTa, 64);
  lv_obj_t* showB = wpButton(wpDlg, INPUT_TOUCH ? "Show" : "Hide", C_SUB, wpShowCb);
  wpShowLbl = showB;
  lv_obj_set_pos(showB, 14 + taW + 8, y + 4);
  show(showB, INPUT_TOUCH);
  lv_obj_t* btns = row(wpDlg, 10);
  lv_obj_align(btns, LV_ALIGN_BOTTOM_RIGHT, -14, -10);
  wpButton(btns, "Cancel", C_TXT, wpCancelCb);
  wpButton(btns, LV_SYMBOL_WIFI "  Connect", C_ACCENT, wpConnectCb);

  if (!INPUT_TOUCH) {  // knob boards: character wheel instead of a keyboard
    wpWheelBuild(first_field(wpHidden));
    return;
  }
  wpKb = lv_keyboard_create(wpRoot);
  lv_obj_set_size(wpKb, W, kbH);
  lv_obj_align(wpKb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(wpKb, C(C_CARD), 0);
  lv_obj_set_style_border_width(wpKb, 0, 0);
  lv_obj_set_style_bg_color(wpKb, C(C_CHIP), LV_PART_ITEMS);
  lv_obj_set_style_text_color(wpKb, C(C_TXT), LV_PART_ITEMS);
  lv_obj_set_style_border_color(wpKb, C(C_EDGE), LV_PART_ITEMS);
  lv_obj_set_style_border_width(wpKb, 1, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(wpKb, C(C_RAISED), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_add_event_cb(wpKb, wpKbCb, LV_EVENT_ALL, nullptr);
  lv_obj_t* first = wpHidden ? wpSsidTa : wpPassTa;
  lv_obj_add_state(first, LV_STATE_FOCUSED);
  lv_keyboard_set_textarea(wpKb, first);
  wpKbSel = 0;
  if (!INPUT_TOUCH) lv_btnmatrix_set_selected_btn(wpKb, 0);
}

static void wpStartScan();
static void apPage(bool open);

// Row actions, shared by taps and the knob: i >= 0 a scanned network,
// -1 "Other network", -2 "Rescan" (knob boards), -3 "Set up from phone".
static void wpActivate(int i) {
  if (i == -3) {
    apPage(true);
    return;
  }
  if (i == -2) {
    wpStartScan();
    return;
  }
  if (i < 0) {
    wpOpenDialog(nullptr, false);
    return;
  }
  char saved[33] = "";
  net_saved_ssid(saved, sizeof(saved));
  bool isSaved = !strcmp(saved, wpAps[i].ssid);
  if (wpAps[i].open) {  // open network: nothing to type
    strlcpy(wpPick, wpAps[i].ssid, sizeof(wpPick));
    net_connect(wpPick, "");
    wpConnLast = 1;
    return;
  }
  wpOpenDialog(wpAps[i].ssid, isSaved);
}

static void wpRowCb(lv_event_t* e) { wpActivate((int)(intptr_t)lv_event_get_user_data(e)); }

// Knob highlight: the selected row is raised.
static void wpHighlight() {
  uint32_t n = lv_obj_get_child_cnt(wpList);
  if (!n) return;
  wpSel = ((wpSel % (int)n) + n) % n;
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t* r = lv_obj_get_child(wpList, i);
    lv_obj_set_style_bg_opa(r, (int)i == wpSel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
  lv_obj_scroll_to_view(lv_obj_get_child(wpList, wpSel), LV_ANIM_ON);
}

static void wpAddRow(int i, const char* ssid, int rssi, const char* tag) {
  const int rowH = COMPACT ? 34 : 44;
  lv_obj_t* r = lv_obj_create(wpList);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), rowH);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(r, 4, 0);
  lv_obj_set_style_bg_color(r, C(C_RAISED), 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(r, wpRowCb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
  int x = 10;
  if (rssi) {  // four signal bars
    int lit = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    for (int b = 0; b < 4; b++) {
      int bh = 5 + b * 4;
      box(r, x + b * 6, (rowH + 18) / 2 - bh, 4, bh, b < lit ? C_ACCENT : C_DIM, 1);
    }
    x += 34;
  }
  lv_obj_t* l = label(r, F16, C_TXT, x, (rowH - lineH(F16)) / 2, ssid);
  dotLabel(l, lv_obj_get_width(wpList) - x - 100);
  if (tag) {
    lv_obj_t* t = label(r, F12, C_MUTED, 0, 0, tag);
    lv_obj_align(t, LV_ALIGN_RIGHT_MID, -12, 0);
  }
}

static void wpFillList() {
  lv_obj_clean(wpList);
  char saved[33] = "";
  net_saved_ssid(saved, sizeof(saved));
  if (wpScanning && !wpN) {
    lv_obj_t* l = label(wpList, F16, C_MUTED, 0, 0, "Scanning for networks...");
    lv_obj_set_style_pad_all(l, 12, 0);
  }
  for (int i = 0; i < wpN; i++) {
    bool cur = g_net.connected && !strcmp(g_net.ssid, wpAps[i].ssid);
    const char* tag = cur ? "connected" : !strcmp(saved, wpAps[i].ssid) ? "saved" : wpAps[i].open ? "open" : "secured";
    wpAddRow(i, wpAps[i].ssid, wpAps[i].rssi, tag);
  }
  if (!wpScanning || wpN) wpAddRow(-1, "Other network...", 0, "hidden SSID");
  wpAddRow(-3, LV_SYMBOL_UPLOAD "  Set up from phone", 0, "QR code");
  if (!INPUT_TOUCH) {
    wpAddRow(-2, LV_SYMBOL_REFRESH "  Rescan networks", 0, nullptr);
    wpHighlight();
  }
}

// Knob boards: turn moves, press picks, hold goes back. In the password
// dialog, turn walks the keyboard keys and press types the highlighted one.
static void wpKnob(const KnobInput& in) {
  if (apRoot) {
    if (in.longPress) apPage(false);
    return;
  }
  if (wpDlg && whLbl) {
    if (in.longPress) wpCloseDialog();
    else wpWheelKnob(in);
    return;
  }
  if (wpDlg && wpKb) {
    if (in.longPress) {
      wpCloseDialog();
      return;
    }
    uint16_t cnt = ((lv_btnmatrix_t*)wpKb)->btn_cnt;
    if (in.steps && cnt) {
      wpKbSel = ((wpKbSel + in.steps) % cnt + cnt) % cnt;
      lv_btnmatrix_set_selected_btn(wpKb, wpKbSel);
    }
    if (in.click) {
      lv_btnmatrix_set_selected_btn(wpKb, wpKbSel);
      lv_event_send(wpKb, LV_EVENT_VALUE_CHANGED, nullptr);  // the keyboard handles the key
      if (wpKb) {  // a mode key (abc/ABC/123) changes the layout size
        cnt = ((lv_btnmatrix_t*)wpKb)->btn_cnt;
        if (wpKbSel >= cnt) wpKbSel = 0;
        lv_btnmatrix_set_selected_btn(wpKb, wpKbSel);
      }
    }
    return;
  }
  if (in.longPress) {
    ui_wifi_page(false);
    return;
  }
  if (in.steps) {
    wpSel += in.steps;
    wpHighlight();
  }
  if (in.click) {
    lv_obj_t* r = lv_obj_get_child(wpList, wpSel);
    if (r) lv_event_send(r, LV_EVENT_SHORT_CLICKED, nullptr);
  }
}

static void wpStartScan() {
  wpScanning = true;
  wpScanAt = millis();
  wpN = 0;
  net_scan(wpAps, 20);  // kicks off an async scan
  wpFillList();
}

static void wpRescanCb(lv_event_t*) { wpStartScan(); }
static void wpBackCb(lv_event_t*) { ui_wifi_page(false); }

static void wpTick(lv_timer_t*) {
  if (wpScanning) {
    int n = net_scan(wpAps, 20);
    if (n >= 0) {
      wpN = n;
      wpScanning = false;
      wpFillList();
    } else if (millis() - wpScanAt > 15000) {
      wpScanning = false;
      wpFillList();
      lv_label_set_text(wpStatus, "Scan didn't finish - tap Rescan");
    }
  }
  char msg[96];
  int cs = net_connect_status(msg, sizeof(msg));
  if (cs && cs != wpConnLast) {
    lv_label_set_text(wpStatus, msg);
    lv_obj_set_style_text_color(wpStatus, C(cs == 1 ? C_ACCENT : cs == 2 ? C_OK : C_ERR), 0);
    if (cs != 1) {
      wpRefreshCurrent();
      wpFillList();
    }
    wpConnLast = cs;
  }
}

// ---- "Set up from phone" ---------------------------------------------------------
// Shows a Wi-Fi join QR for the panel's temporary hotspot (SSID and password
// beside it). Once a phone joins, the QR switches to the setup page URL in
// case the captive popup doesn't appear. Cancel (or hold) stops the hotspot.
static lv_obj_t *apQr, *apCaption, *apState;
static lv_timer_t* apTimer = nullptr;
static bool apShowUrl = false;

// WIFI: QR fields need \ ; , : " escaped with a backslash.
static void qrEscape(const char* in, char* out, size_t cap) {
  size_t o = 0;
  for (; *in && o + 2 < cap; in++) {
    if (strchr("\\;,:\"", *in)) out[o++] = '\\';
    out[o++] = *in;
  }
  out[o] = 0;
}

static void apCancelCb(lv_event_t*) { apPage(false); }

static void apTick(lv_timer_t*) {
  bool joined = false;
  int result = 0;
  char msg[96];
  int on = net_ap_status(&joined, &result, msg, sizeof(msg));
  if (joined && !apShowUrl) {
    apShowUrl = true;
    const char* url = "http://192.168.4.1";
    lv_qrcode_update(apQr, url, strlen(url));
    lv_label_set_text(apCaption, "Phone connected.\nScan to open the setup page\n(or open http://192.168.4.1).");
  }
  if (result) {
    lv_label_set_text(apState, msg);
    lv_obj_set_style_text_color(apState, C(result == 2 ? C_OK : result == 3 ? C_ERR : C_ACCENT), 0);
  }
  if (!on) {  // hotspot finished (joined, timed out or failed to start)
    apPage(false);
    if (wpRoot) {
      wpRefreshCurrent();
      wpStartScan();
    }
  }
}

static void apPage(bool open) {
  if (!open) {
    if (apTimer) lv_timer_del(apTimer);
    apTimer = nullptr;
    if (apRoot) lv_obj_del_async(apRoot);
    apRoot = nullptr;
    net_ap_stop();
    return;
  }
  if (apRoot) return;
  apRoot = box(lv_layer_top(), 0, 0, W, H, C_BG);
  lv_obj_add_flag(apRoot, LV_OBJ_FLAG_CLICKABLE);
  Header h = header(apRoot, "SET UP FROM PHONE", "");
  lv_obj_align(h.right, LV_ALIGN_TOP_RIGHT, -PAD, COMPACT ? 4 : 8);
  if (INPUT_TOUCH) wpButton(h.right, LV_SYMBOL_CLOSE "  Cancel", C_TXT, apCancelCb);
  else hint(h, "hold: cancel");
  lv_obj_t* wait = label(apRoot, F16, C_MUTED, PAD + 4, TOP, "Starting the setup hotspot...");
  lv_refr_now(NULL);  // the scan before the hotspot takes a few seconds
  char ssid[24], pass[12];
  if (!net_ap_start(ssid, sizeof(ssid), pass, sizeof(pass))) {
    lv_label_set_text(wait, "Couldn't start the hotspot");
    return;
  }
  lv_obj_del(wait);
  // QR on the left, instructions and credentials on the right.
  const int qs = min(TH - 24, COMPACT ? 150 : TALL ? 260 : 220);
  lv_obj_t* qc = box(apRoot, PAD, TOP + (TH - qs - 16) / 2, qs + 16, qs + 16, 0xFFFFFF, 6);
  apQr = lv_qrcode_create(qc, qs, lv_color_black(), lv_color_white());
  lv_obj_center(apQr);
  char es[64], ep[32], payload[128];
  qrEscape(ssid, es, sizeof(es));
  qrEscape(pass, ep, sizeof(ep));
  snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;;", es, ep);
  lv_qrcode_update(apQr, payload, strlen(payload));
  apShowUrl = false;
  const int tx = PAD + qs + 16 + (COMPACT ? 10 : 24), tw = W - tx - PAD;
  apCaption = label(apRoot, F16, C_SUB, tx, TOP + 8, "1. Scan with your phone's camera\n    to join the panel's hotspot.\n2. Pick your Wi-Fi on the page\n    that opens.");
  lv_obj_set_width(apCaption, tw);
  lv_label_set_long_mode(apCaption, LV_LABEL_LONG_WRAP);
  char a[80];
  int y = TOP + 8 + 4 * lineH(F16) + 16;
  label(apRoot, F12, C_MUTED, tx, y, "NETWORK");
  label(apRoot, TALL ? F22 : F16, C_TXT, tx, y + lineH(F12) + 2, ssid);
  y += lineH(F12) + lineH(TALL ? F22 : F16) + 10;
  label(apRoot, F12, C_MUTED, tx, y, "PASSWORD");
  label(apRoot, TALL ? F22 : F16, C_ACCENT, tx, y + lineH(F12) + 2, pass);
  y += lineH(F12) + lineH(TALL ? F22 : F16) + 14;
  snprintf(a, sizeof(a), "Stops by itself in 10 minutes.");
  apState = label(apRoot, F14, C_MUTED, tx, y, a);
  lv_obj_set_width(apState, tw);
  lv_label_set_long_mode(apState, LV_LABEL_LONG_WRAP);
  apTimer = lv_timer_create(apTick, 500, nullptr);
}

void ui_wifi_page(bool open) {
  if (open == (wpRoot != nullptr)) return;
  if (!open) {
    if (apRoot) apPage(false);
    wpCloseDialog();
    if (wpTimer) lv_timer_del(wpTimer);
    wpTimer = nullptr;
    lv_obj_del_async(wpRoot);
    wpRoot = nullptr;
    wifiOpen = false;
    hbUpdate();
    return;
  }
  if (menuOpen) openMenu(false);
  wifiOpen = true;
  hbUpdate();
  wpRoot = box(lv_layer_top(), 0, 0, W, H, C_BG);
  lv_obj_add_flag(wpRoot, LV_OBJ_FLAG_CLICKABLE);  // keep taps off the page below
  Header h = header(wpRoot, "WI-FI", "");
  lv_obj_align(h.right, LV_ALIGN_TOP_RIGHT, -PAD, COMPACT ? 4 : 8);
  wpButton(h.right, LV_SYMBOL_REFRESH "  Rescan", C_ACCENT, wpRescanCb);
  wpButton(h.right, LV_SYMBOL_CLOSE "  Back", C_TXT, wpBackCb);
  wpCur = label(wpRoot, F16, C_SUB, PAD + 4, TOP, "");
  dotLabel(wpCur, CW - 8);
  wpStatus = label(wpRoot, F14, C_ACCENT, PAD + 4, TOP + lineH(F16) + 4, "");
  dotLabel(wpStatus, CW - 8);
  const int listY = TOP + lineH(F16) + lineH(F14) + 14;
  lv_obj_t* c = card(wpRoot, PAD, listY, CW, BOT - listY);
  wpList = lv_obj_create(c);
  lv_obj_remove_style_all(wpList);
  lv_obj_set_pos(wpList, 6, 6);
  lv_obj_set_size(wpList, CW - 12, BOT - listY - 12);
  lv_obj_set_flex_flow(wpList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(wpList, 2, 0);
  lv_obj_set_scroll_dir(wpList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(wpList, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_color(wpList, C(C_DIM), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(wpList, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(wpList, 3, LV_PART_SCROLLBAR);
  lv_obj_update_layout(wpRoot);
  wpRefreshCurrent();
  wpConnLast = 0;
  wpStartScan();
  wpTimer = lv_timer_create(wpTick, 250, nullptr);
}

static int bannerKind = 0;  // status banner: 0 none, 1 Wi-Fi, 2 cookie, 3 stale
static void wifiBannerCb(lv_event_t*) {
  if (bannerKind == 1) ui_wifi_page(true);
  else openStatus(true);
}

// "Wi-Fi not connected" strip above the page segments; tap opens the page.
// One persistent banner, most important first: Wi-Fi down (opens the Wi-Fi
// page), cookie expired (red), MakerWorld data stale (amber; both open Status).
static void wifiBannerUpdate() {
  static uint32_t downSince = 0;
  if (g_net.connected) downSince = 0;
  else if (!downSince) downSince = millis();
  int kind = 0;
  if (downSince && millis() - downSince > 20000) kind = 1;
  else if (st.valid && st.health.has && st.health.expired) kind = 2;
  else if (st.valid && st.health.has && st.health.stale) kind = 3;
  if (wifiOpen || menuOpen || statusOpen) kind = 0;
  static uint32_t shownSince = 0;
  if (kind == bannerKind && (kind != 3 || shownSince == st.health.since)) return;
  bannerKind = kind;
  shownSince = st.health.since;
  if (kind) {
    char a[96], t[8];
    uint32_t col = kind == 2 ? C_ERR : C_WARN;
    if (kind == 1)
      snprintf(a, sizeof(a), TINY ? LV_SYMBOL_WARNING "  Wi-Fi off  -  tap to join"
                                  : LV_SYMBOL_WARNING "  Wi-Fi not connected  -  tap to choose network");
    else if (kind == 2)
      snprintf(a, sizeof(a), TINY ? LV_SYMBOL_WARNING "  Cookie expired"
                                  : LV_SYMBOL_WARNING "  Cookie expired  -  refresh it in the extension");
    else {
      fmtHM(t, sizeof(t), st.health.since);
      snprintf(a, sizeof(a), TINY ? LV_SYMBOL_WARNING "  Data stale since %s" : LV_SYMBOL_WARNING "  MakerWorld data stale since %s",
               st.health.since ? t : "--:--");
    }
    lv_label_set_text(wifiBanner, a);
    lv_obj_set_style_text_color(wifiBanner, C(col), 0);
    lv_obj_set_style_border_color(wifiBanner, C(col), 0);
  }
  show(wifiBanner, kind != 0);
}

static void buildWifiBanner() {
  wifiBanner = label(lv_layer_top(), F14, C_WARN, 0, 0, LV_SYMBOL_WARNING "  Wi-Fi not connected  -  tap to choose network");
  chipStyle(wifiBanner, C_CHIP, C_WARN);
  lv_obj_set_style_border_color(wifiBanner, C(C_WARN), 0);
  lv_obj_set_style_pad_ver(wifiBanner, TINY ? 4 : 8, 0);
  lv_obj_set_style_pad_hor(wifiBanner, TINY ? 8 : 16, 0);
  if (TINY) {
    lv_label_set_long_mode(wifiBanner, LV_LABEL_LONG_DOT);
    lv_obj_set_width(wifiBanner, CW);
    lv_obj_set_style_text_align(wifiBanner, LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_obj_add_flag(wifiBanner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(wifiBanner, wifiBannerCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_align(wifiBanner, LV_ALIGN_BOTTOM_MID, 0, -(COMPACT ? 22 : 30));
  show(wifiBanner, false);
}

static void buildHamburger();  // defined with the input handling below

void ui_init() {
  layout();
  lv_obj_set_style_bg_color(lv_scr_act(), C(C_BG), 0);
  memset(&st, 0, sizeof(st));
  buildToday();
  buildFeed();
  buildTop();
  buildTrend();
  buildPoints();
  buildReach();
  buildComments();
  buildBest();
  buildCreator();
  buildStatus();
  buildMenu();
  buildAbout();
  buildUpdate();
  buildOverlay();
  buildHamburger();
  buildWifiBanner();
  refreshAll();
  commitScreen(SC_TODAY);  // pages start hidden; goScreen skips a page already current
  if (splashObj) {
    lv_obj_del(splashObj);
    splashObj = nullptr;
  }
  lastInput = millis();
  applyBrightness();
#if INPUT_TOUCH
  // Taps on cards fall through to the screen (cards aren't clickable).
  lv_obj_t* scr = lv_scr_act();
  // Nothing on these panels scrolls. Elastic scroll was eating swipes and
  // springing the page back to where the finger started.
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM |
                             LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(scr, touchEvent, LV_EVENT_ALL, nullptr);
#endif
}

// ---- first-run guide ----------------------------------------------------------
// A new panel joins Wi-Fi but gets nothing until the poller knows where it
// is. Until the first state push this page says what to set, with a QR code
// for the panel's web console. "Skip" hides it until the next boot, "Don't
// show again" for good; the first push also ends it for good.
static lv_obj_t* guideRoot = nullptr;
static bool guideSkipped = false;
static uint32_t netUpAt = 0;

static void guideClose(bool forever) {
  if (forever) settings_set_guide_done();
  guideSkipped = true;
  if (!guideRoot) return;
  lv_obj_del_async(guideRoot);
  guideRoot = nullptr;
  uiHeap("guide closed");
}

static void guideBtnCb(lv_event_t* e) {
  lastInput = millis();
  guideClose(tapIdx(e));
}

static void guideOpen() {
  guideRoot = box(lv_layer_top(), 0, 0, W, H, C_BG);
  lv_obj_add_flag(guideRoot, LV_OBJ_FLAG_CLICKABLE);  // keep taps off the page below
  Header h = header(guideRoot, TINY ? "CONNECT" : "CONNECT THE POLLER", "");
  lv_obj_align(h.right, LV_ALIGN_TOP_RIGHT, -PAD, COMPACT ? 4 : 8);
#if INPUT_TOUCH
  headerBtn(h, COMPACT ? "Hide" : "Don't show again", C_MUTED, guideBtnCb, 1);
  headerBtn(h, LV_SYMBOL_CLOSE "  Skip", C_TXT, guideBtnCb, 0);
#else
  hint(h, COMPACT ? "press: skip" : "press: skip  -  hold: don't show again");
#endif
  char url[48], host[48], body[320];
  snprintf(url, sizeof(url), "http://%s/", g_net.ip);
  snprintf(host, sizeof(host), g_net.mdns ? "%s.local" : "%s", g_net.mdns ? g_net.host : g_net.ip);
  // 2": a side-by-side code leaves ~140 px for the steps and they run off the
  // bottom. Stack a smaller code over short instructions instead.
  if (TINY) {
    const int quiet = 4, qr = 72, plate = qr + 2 * quiet;
    const int y0 = TOP;
    lv_obj_t* pl = box(guideRoot, (W - plate) / 2, y0, plate, plate, 0xFFFFFF, 6);
    lv_obj_t* code = lv_qrcode_create(pl, qr, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
    lv_qrcode_update(code, url, strlen(url));
    lv_obj_center(code);
    snprintf(body, sizeof(body),
             "%s\n%s\n\nAdd to panels: [\"%s\"]\nCounts within a minute.",
             host, g_net.ip, host);
    lv_obj_t* l = label(guideRoot, F14, C_TXT, PAD, y0 + plate + 6, body);
    lv_obj_set_width(l, CW);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(l, 2, 0);
    uiHeap("guide");
    return;
  }
  const int quiet = COMPACT ? 8 : 14;
  int qr = min(TH - 2 * quiet - (COMPACT ? 4 : 16), COMPACT ? 132 : 220) & ~1;
  const int plate = qr + 2 * quiet, y0 = TOP + (TH - plate) / 2;
  lv_obj_t* pl = box(guideRoot, PAD, y0, plate, plate, 0xFFFFFF, COMPACT ? 6 : 12);
  lv_obj_t* code = lv_qrcode_create(pl, qr, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
  lv_qrcode_update(code, url, strlen(url));
  lv_obj_center(code);
  const int tx = PAD + plate + (COMPACT ? 12 : 28), tw = W - tx - PAD;
  snprintf(body, sizeof(body),
           "1.  This panel is online as %s  (%s).\n"
           "2.  On the PC that runs the MakerMonitor poller, open poller/config.yaml.\n"
           "3.  Add it to the panel list:  panels: [\"%s\"]  (or in the dashboard's Settings).",
           host, g_net.ip, host);
  lv_obj_t* l = label(guideRoot, COMPACT ? F14 : F16, C_TXT, tx, y0, body);
  lv_obj_set_width(l, tw);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(l, COMPACT ? 4 : 10, 0);
  lv_obj_update_layout(l);
  lv_obj_t* n = label(guideRoot, F14, C_MUTED, tx, y0 + lv_obj_get_height(l) + (COMPACT ? 8 : 20),
                      "Numbers show up here within a minute of the first poll. "
                      "The code opens this panel's web console.");
  lv_obj_set_width(n, tw);
  lv_label_set_long_mode(n, LV_LABEL_LONG_WRAP);
  uiHeap("guide");
}

static void guideTick() {
  if (g_net.connected) {
    if (!netUpAt) netUpAt = millis();
  } else {
    netUpAt = 0;
  }
  bool want = !st.valid && !settings_guide_done() && !guideSkipped && netUpAt && millis() - netUpAt > 15000 &&
              g_net.ip[0] && !wifiOpen && !apRoot && !bannerUp;
  if (want && !guideRoot) guideOpen();
}

// ---- remote page switching (/v2/ui/show, screenshot capture) ----------------
// The web server runs in loop(), the same task as LVGL, so this is called
// between LVGL timer runs and never concurrently with them.
static const char* const PAGE_NAME[NCAR] = {"today", "points", "feed", "models", "trend",
                                            "reach", "comments", "best", "creator"};
static const char* const CV_KEY[NCVIEW] = {"convert", "trend", "launch", "earn", "followers", "reply"};
static const char* const OTHER_PAGES[] = {"status", "settings", "about", "update", "wifi", "guide"};

void ui_pages_json(String& out) {
  out = F("{\"pages\":[");
  for (int i = 0; i < NCAR; i++) out += String(i ? ",\"" : "\"") + PAGE_NAME[i] + "\"";
  for (const char* p : OTHER_PAGES) out += String(",\"") + p + "\"";
  out += F("],\"tabs\":{\"creator\":[");
  for (int i = 0; i < NCVIEW; i++) out += String(i ? ",\"" : "\"") + CV_KEY[i] + "\"";
  out += F("]},\"open\":{\"settings\":[\"display\",\"quiet\",\"alerts\",\"network\",\"device\",\"reset\","
           "\"quiet_from\",\"quiet_until\"");
  for (int i = 0; i < NMENU; i++)
    if (MENU[i].kind == MK_CHOICE) out += String(",\"") + SETTINGS[MENU[i].id].key + "\"";
  out += F("],\"models\":[\"<design id>\"");
  for (int k = 0; k < 3; k++)
    for (int j = 0; j < st.ntop[k]; j++) out += String(",\"") + st.top[k][j].id + "\"";
  out += F("]}}");
}

bool ui_show(const char* page, const char* tab, const char* open, char* shown, size_t cap, bool slide) {
  snprintf(shown, cap, "%s", page);
  lastInput = millis();
  ui_wake();
  sheetClose();
  modelPage(nullptr);
  if (wifiOpen) ui_wifi_page(false);
  if (guideRoot && strcmp(page, "guide")) guideClose(false);
  if (hbIsOpen) hbOpen(false);
  if (menuOpen) openMenu(false);
  if (statusOpen) openStatus(false);
  if (aboutOpen) openAbout(false);
  if (updOpen) openUpdate(false);
  if (bannerUp) bannerClear();
  for (int i = 0; i < NCAR; i++) {
    if (strcmp(page, PAGE_NAME[i])) continue;
    if (i == SC_CREATOR && tab[0]) {
      int v = -1;
      for (int k = 0; k < NCVIEW; k++)
        if (!strcmp(tab, CV_KEY[k])) v = k;
      if (v < 0) return false;
      crView = v;
      refreshCreator();
      markPagesDirty();
    }
    goScreen(i, slide && i != cur ? (i > cur ? 1 : -1) : 0);
    if (i == SC_CREATOR) snprintf(shown, cap, "creator/%s", CV_KEY[crView]);
    if (i == SC_TOP && open[0]) {  // model detail by design id
      for (int k = 0; k < 3; k++)
        for (int j = 0; j < st.ntop[k]; j++)
          if (!strcmp(st.top[k][j].id, open)) {
            modelPage(&st.top[k][j]);
            snprintf(shown, cap, "models/%s", open);
            return true;
          }
      return false;
    }
    return true;
  }
  if (!strcmp(page, "settings")) {
    openMenu(true);
    if (!open[0]) return true;
    for (int i = 0; i < NMENU; i++) {
      const MenuDef& d = MENU[i];
      if (d.kind == MK_HEAD && !strncasecmp(open, d.label, strlen(open))) {  // section: scroll to it
        lv_obj_scroll_to_y(menuList, max(0, (int)lv_obj_get_y(menuUi[i].row) - (COMPACT ? 4 : 8)), LV_ANIM_OFF);
        // Near the end the list can't scroll that far; keep the title itself in view.
        lv_obj_scroll_to_view(menuUi[i].row, LV_ANIM_OFF);
        snprintf(shown, cap, "settings/%s", open);
        return true;
      }
      if (!strcmp(open, "reset") && d.kind == MK_ACTION && d.id == MA_RESET) {
        lv_obj_scroll_to_y(menuList, LV_COORD_MAX, LV_ANIM_OFF);
        menuActivate(i);
        snprintf(shown, cap, "settings/reset");
        return true;
      }
      if ((!strcmp(open, "quiet_from") && d.kind == MK_TIME && d.id == 0) ||
          (!strcmp(open, "quiet_until") && d.kind == MK_TIME && d.id == 1) ||
          (d.kind == MK_CHOICE && !strcmp(open, SETTINGS[d.id].key))) {
        // The quiet rows are hidden while quiet hours are off; show the
        // picker anyway (only Save changes anything).
        menuSel = i;
        menuActivate(i);
        snprintf(shown, cap, "settings/%s", open);
        return true;
      }
    }
    return false;
  }
  if (!strcmp(page, "status")) openStatus(true);
  else if (!strcmp(page, "about")) openAbout(true);
  else if (!strcmp(page, "update")) openUpdate(true);
  else if (!strcmp(page, "wifi")) ui_wifi_page(true);
  else if (!strcmp(page, "guide")) {
    if (!guideRoot) guideOpen();
  } else return false;
  return true;
}

void ui_set_state(const State& s) {
  st = s;
  stateMs = millis();
  if (st.valid) {
    settings_set_guide_done();
    guideClose(false);
  }
  refreshAll();
}

const State& ui_get_state() { return st; }

void ui_refresh_from_web() {
  applyBrightness();
  if (menuOpen) refreshMenu();
  if (statusOpen) refreshStatus();
  refreshUpdate();
}

static bool modelOpen = false;  // model detail page (touch)
bool ui_overlay_open() { return updScr || guideRoot || menuOpen || statusOpen || aboutOpen || updOpen || hbIsOpen || wifiOpen || modelOpen; }

// The poller sends the full state (counts, hours, feed) before the events of
// the same poll, so an event only announces itself: tile pulse, banner, chime.
// Adding it to the counts or feed here would count it twice.
void ui_event(const Event& e) {
  if (e.kind < NSTAT) bumpTile(e.kind);

  int mod = minuteOfDay();
  if (!notify_enabled(e.kind) || (mod >= 0 && in_quiet_hours(mod))) return;
  sound_event(e.kind);
  if (qLen >= QMAX) return;
  queue[qLen++] = e;
  if (!bannerUp) {
    bannerUp = true;
    show(banner, true);
    renderBanner();
    slideBanner(true);
  } else {
    // Update the "1 of N" count without restarting the timer.
    uint32_t keep = bannerShownAt;
    renderBanner();
    bannerShownAt = keep;
  }
}

// ---- hamburger menu ---------------------------------------------------------
// Tap the button (touch) or long-press (knob/touch) to open; items are
// tapped, or picked with turn + press.
static const int NHB = 4;
static lv_obj_t *hbBtn, *hbBackdrop, *hbDrop, *hbItem[NHB];
static bool hbShown = true;
static int hbSel = 0;

static void hbRefresh() {
  for (int i = 0; i < NHB; i++) {
    lv_obj_set_style_bg_opa(hbItem[i], !INPUT_TOUCH && i == hbSel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
}

static void hbOpen(bool open) {
  hbIsOpen = open;
  hbSel = 0;
  show(hbBackdrop, open);
  show(hbDrop, open);
  lv_obj_set_style_border_color(hbBtn, C(open ? C_ACCENT : C_EDGE), 0);
  hbRefresh();
}

static void hbActivate(int i) {
  hbOpen(false);
  bannerClear();
  if (i == 0) openMenu(true);
  else if (i == 1) openStatus(true);
  else if (i == 2) openAbout(true);
  else openUpdate(true);
}

static void hbBtnCb(lv_event_t* e) {
  lastInput = millis();
  hbOpen(lv_obj_has_flag(hbDrop, LV_OBJ_FLAG_HIDDEN));
}

static void hbBackdropCb(lv_event_t* e) { hbOpen(false); }

static void hbItemCb(lv_event_t* e) {
  lastInput = millis();
  hbActivate((int)(intptr_t)lv_event_get_user_data(e));
}

static void buildHamburger() {
  lv_obj_t* top = lv_layer_top();
  const int by = COMPACT ? 4 : (TALL ? 12 : 8);
  // Compact: never below the header into the page (the 3.5"'s density would
  // make it 39 px tall); the extended click area keeps the target big.
  const int bh = COMPACT ? min(max(26, TOUCH_PX - 6), TOP - by - 1) : (TALL ? max(36, TOUCH_PX - 8) : 32);
  hbBtn = box(top, W - PAD - HB_W, by, HB_W, bh, C_CHIP, 4);
  slideKeep[0] = hbBtn;
  lv_obj_set_style_border_width(hbBtn, 1, 0);
  lv_obj_set_style_border_color(hbBtn, C(C_EDGE), 0);
  lv_obj_add_flag(hbBtn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(hbBtn, 10);
  lv_obj_add_event_cb(hbBtn, hbBtnCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_set_style_bg_color(hbBtn, C(C_RAISED), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(hbBtn, C(C_ACCENT), LV_STATE_PRESSED);
  const int lw = 18, gap = COMPACT ? 5 : 6;
  for (int i = 0; i < 3; i++) {
    box(hbBtn, (HB_W - lw) / 2 - 1, bh / 2 - 1 - gap + i * gap - 1, i == 1 ? lw - 6 : lw, 2, i == 1 ? C_ACCENT : C_TXT, 1);
  }
  hbBadge = box(hbBtn, HB_W - 10, 4, 6, 6, C_OK, LV_RADIUS_CIRCLE);
  show(hbBadge, false);

  // Full-screen catcher so a tap anywhere else closes the dropdown.
  hbBackdrop = box(top, 0, 0, W, H, C_BG);
  lv_obj_set_style_bg_opa(hbBackdrop, LV_OPA_40, 0);
  lv_obj_add_flag(hbBackdrop, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(hbBackdrop, hbBackdropCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_move_foreground(hbBtn);

  int ih = COMPACT ? max(36, TOUCH_PX) : (TALL ? max(52, TOUCH_PX) : 48);
  // 2": four 52 px rows would run off the bottom; share what's below the button.
  if (TINY) ih = min(ih, (H - (by + bh + 6) - 4) / NHB - 1);
  const int dw = COMPACT ? 196 : (TALL ? 248 : 220);
  hbDrop = card(top, W - PAD - dw, by + bh + 6, dw, NHB * ih + NHB);
  lv_obj_set_style_border_color(hbDrop, C(C_ACCENT), 0);
  static const char* const ICONS[NHB] = {LV_SYMBOL_SETTINGS, LV_SYMBOL_LIST, LV_SYMBOL_HOME, LV_SYMBOL_REFRESH};
  static const char* const NAMES[NHB] = {"Settings", "Status", "About @WOFL", "Firmware update"};
  for (int i = 0; i < NHB; i++) {
    if (i) box(hbDrop, 10, i * (ih + 1) - 1, dw - 22, 1, C_EDGE);
    lv_obj_t* it = hbItem[i] = box(hbDrop, 0, i * (ih + 1), dw - 2, ih, C_RAISED);
    lv_obj_set_style_bg_opa(it, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(it, C(C_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(it, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(it, hbItemCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* ic = label(it, F16, C_ACCENT, 14, 0, ICONS[i]);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t* l = label(it, F16, C_TXT, 0, 0, NAMES[i]);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 44, 0);
    if (i == 3) hbUpdLbl = l;
  }
  hbOpen(false);
}

// Hidden while Settings, Status, About, or a notification banner owns the top edge.
static void hbUpdate() {
  if (!hbBtn) return;
  bool want = !menuOpen && !statusOpen && !aboutOpen && !updOpen && !bannerUp && !wifiOpen && !modelOpen;
  if (want == hbShown) return;
  hbShown = want;
  show(hbBtn, want);
  if (!want && hbIsOpen) hbOpen(false);
}

void ui_input(const KnobInput& in) {
  if (!in.any()) return;
  if (ui_wake()) return;  // the input that wakes the screen does nothing else
  lastInput = millis();
  lastCycle = millis();
  applyBrightness();

  if (guideRoot) {
    if (in.click || in.longPress) guideClose(in.longPress);
    return;
  }
  if (wifiOpen) {
    if (INPUT_TOUCH) {
      if (in.longPress) ui_wifi_page(false);
    } else {
      wpKnob(in);  // turn/press through the list and the keyboard
    }
    return;
  }
  if (menuOpen) {
    if (sheet) {
      sheetInput(in);
      return;
    }
    if (in.longPress) {
      openMenu(false);
      return;
    }
    if (in.steps) menuMove(in.steps);
    if (in.click) menuActivate(menuSel);
    return;
  }

  if (hbIsOpen) {
    if (in.longPress) hbOpen(false);
    if (in.steps) {
      hbSel = ((hbSel + in.steps) % NHB + NHB) % NHB;
      hbRefresh();
    }
    if (in.click) hbActivate(hbSel);
    return;
  }
  if (aboutOpen && !bannerUp) {
    if (in.longPress || (in.click && !INPUT_TOUCH)) openAbout(false);
    return;
  }
  if (updOpen && !bannerUp) {
    if (in.longPress) {
      openUpdate(false);
      return;
    }
    if (in.steps) {
      updSel = ((updSel + in.steps) % NUPD + NUPD) % NUPD;
      refreshUpdate();
    }
    if (in.click && !INPUT_TOUCH) updAction(updSel);
    return;
  }
  if (modelOpen) {  // model detail: press or hold returns
    if (in.longPress || in.click) modelPage(nullptr);
    return;
  }
  if (topSel >= 0 && cur == SC_TOP && !bannerUp) {
    // Knob select mode on Top: 0-2 the mode chips, 3+ the model rows.
    int n = 3 + min(st.ntop[topMode], topRows);
    if (in.longPress) topSel = -1;
    if (in.steps) topSel = ((topSel + in.steps) % n + n) % n;
    if (in.click) {
      if (topSel < 3) topMode = topSel;
      else modelPage(&st.top[topMode][topSel - 3]);
    }
    refreshTop();
    return;
  }
  if (statusOpen && !bannerUp) {
    // Its own page: no swiping away; hold (or the Back button) returns.
    if (in.longPress) openStatus(false);
    else if (in.click && !INPUT_TOUCH) requestPoll();
    return;
  }
  if (in.longPress) {
    bannerClear();
    hbOpen(true);
    return;
  }
  if (bannerUp) {
    if (in.steps) bannerNext();
    if (in.click) bannerClear();
    return;
  }
  if (in.steps) {
    goScreen(cur + in.steps, in.steps > 0 ? 1 : -1);
    return;
  }
  if (in.click) {
    switch (cur) {
      case SC_FEED:
        feedFilter = feedFilter >= NKIND - 1 ? -1 : feedFilter + 1;
        refreshFeed();
        break;
      case SC_TOP:  // knob: enter select mode (chips + models)
        topSel = st.ntop[topMode] ? 3 : 0;
        refreshTop();
        break;
      case SC_TODAY:  // the knob's way to the header dot: Status
        openStatus(true);
        break;
      case SC_BEST:
        bestMetric = (bestMetric + 1) % NBEST;
        refreshBest();
        break;
      case SC_CREATOR:
        crView = (crView + 1) % NCVIEW;
        refreshCreator();
        break;
      case SC_TREND:
        trendStat = (trendStat + 1) % NSTAT;
        refreshTrend();
        break;
      case SC_COMMENTS:
        commentPage++;
        refreshComments();
        break;
      default:
        goScreen(cur + 1, 1);
        break;
    }
  }
}

// ---- model detail page ---------------------------------------------------------
// One model's daily activity from the poller (GET <poller>/v2/model?id=),
// fetched when opened: 7- and 30-day totals per stat and daily download bars.
// Built on open, deleted on close.
static lv_obj_t* mdRoot = nullptr;
static void modelPage(const TopRow* r);
static void mdBackCb(lv_event_t*) { modelPage(nullptr); }

static void modelPage(const TopRow* r) {
  if (!r) {
    if (mdRoot) lv_obj_del_async(mdRoot);
    mdRoot = nullptr;
    modelOpen = false;
    hbUpdate();
    return;
  }
  if (mdRoot) return;
  modelOpen = true;
  hbUpdate();
  mdRoot = box(lv_layer_top(), 0, 0, W, H, C_BG);
  lv_obj_add_flag(mdRoot, LV_OBJ_FLAG_CLICKABLE);
  Header h = header(mdRoot, "MODEL", "");
  lv_obj_align(h.right, LV_ALIGN_TOP_RIGHT, -PAD, COMPACT ? 4 : 8);
  lv_obj_t* back = label(h.right, F14, C_TXT, 0, 0, LV_SYMBOL_CLOSE "  Back");
  chipStyle(back, C_CHIP, C_TXT);
  lv_obj_set_style_pad_ver(back, COMPACT ? 5 : 8, 0);
  lv_obj_set_style_pad_hor(back, 16, 0);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(back, 8);
  lv_obj_add_event_cb(back, mdBackCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* name = label(mdRoot, TALL ? F22 : F16, C_TXT, PAD + 4, TOP, r->name);
  dotLabel(name, CW - 8);
  lv_obj_t* note = label(mdRoot, F14, C_MUTED, PAD + 4, TOP + lineH(TALL ? F22 : F16) + 4, "Loading...");
  lv_refr_now(NULL);  // show the page before the (blocking) fetch

  const size_t cap = 6144;  // response buffer in PSRAM, freed after parsing
  char* body = (char*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
  char url[96];
  snprintf(url, sizeof(url), "%s/v2/model?id=%s", st.poller, r->id);
  JsonDocument doc;
  int n = body && st.poller[0] && r->id[0] ? net_http_get(url, body, cap) : -1;
  bool bad = n <= 0 || deserializeJson(doc, body, n);
  heap_caps_free(body);
  if (bad) {
    lv_label_set_text(note, st.poller[0] ? "Couldn't reach the poller" : "Poller address not known yet");
    return;
  }
  JsonArrayConst days = doc["days"].as<JsonArrayConst>();
  int nd = days.size();
  int sum7[NSTAT] = {}, sum30[NSTAT] = {}, dl[30] = {}, mx = 1;
  for (int i = 0; i < nd && i < 30; i++) {
    JsonObjectConst d = days[nd - 1 - i];  // newest first
    for (int k = 0; k < NSTAT; k++) {
      int v = d[STAT_KEYS[k]] | 0;
      sum30[k] += v;
      if (i < 7) sum7[k] += v;
    }
    dl[i] = d["downloads"] | 0;
    mx = max(mx, dl[i]);
  }
  char a[200];
  int at = snprintf(a, sizeof(a), nd < 30 ? "%d day%s of history so far" : "last 30 days", nd, nd == 1 ? "" : "s");
  // Creator stats for this model (when the poller has them).
  JsonObjectConst cs = doc["stats"].as<JsonObjectConst>();
  if (!cs.isNull() && (cs["has"] | false)) {
    char p1[12], p2[12];
    crPct(p1, sizeof(p1), cs["dpv30"] | -1.0f);
    crPct(p2, sizeof(p2), cs["ppd"] | -1.0f);
    at += snprintf(a + at, sizeof(a) - at, "  -  %s dl/view, %s prints/dl, %.0f eng/100 dl",
                   p1, p2, cs["e100"]["m"]["all"] | 0.0f);
    JsonObjectConst mo = cs["mom"].as<JsonObjectConst>();
    if (!mo.isNull() && at < (int)sizeof(a))
      at += snprintf(a + at, sizeof(a) - at, ", yday %d vs %.1f avg%s", mo["y"] | 0, mo["avg"] | 0.0f,
                     (mo["rising"] | false) ? " (rising)" : (mo["stalled"] | false) ? " (stalled)" : "");
    if (at < (int)sizeof(a))
      at += snprintf(a + at, sizeof(a) - at, ", %.0f/%.0f pts, +%d/+%d followers 7d/30d", cs["pts7"] | 0.0f,
                     cs["pts30"] | 0.0f, cs["fol7"] | 0, cs["fol30"] | 0);
  }
  if (!cs.isNull() && !cs["unans"].isNull() && at < (int)sizeof(a))
    snprintf(a + at, sizeof(a) - at, ", %d unanswered", cs["unans"] | 0);
  lv_label_set_text(note, a);
  if (TINY) {
    // Two lines, then the table. One line hid the per-model stats.
    lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
    lv_obj_set_size(note, CW - 8, lineH(F14) * 2);
  } else if (COMPACT) {
    dotLabel(note, CW - 8);
  } else {  // two lines
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(note, CW - 8);
  }

  const int noteLines = TINY ? 2 : COMPACT ? 1 : 2;
  const int y0 = TOP + lineH(TALL ? F22 : F16) + lineH(F14) * noteLines + 14, tw = COMPACT ? CW : CW * 45 / 100;
  lv_obj_t* tb = card(mdRoot, PAD, y0, tw, BOT - y0);
  const int rh = (BOT - y0 - 16) / (NSTAT + 1);
  // Column anchors were laid out for a wide card. On 320 px they sat in empty space.
  const int col7 = TINY ? tw - 148 : tw - 190, col30 = TINY ? tw - 70 : tw - 90;
  label(tb, F12, C_MUTED, col7, 8, "7 DAYS");
  label(tb, F12, C_MUTED, col30, 8, "30 DAYS");
  static const int ORDER[NSTAT] = {S_DOWNLOADS, S_PRINTS, S_LIKES, S_COLLECTS, S_BOOSTS};
  for (int i = 0; i < NSTAT; i++) {
    int k = ORDER[i], y = 8 + (i + 1) * rh;
    lv_obj_t* kn = label(tb, F16, COL[k], 14, y, TINY && k == S_DOWNLOADS ? "DLs" : KIND_PLURAL[k]);
    if (TINY) dotLabel(kn, col7 - 20);
    // 2" rows are ~19 px; the 16 px number used on other compact panels overlaps them.
    const lv_font_t* numF = TINY ? F14 : F20;
    fmtSigned(a, sizeof(a), sum7[k]);
    label(tb, numF, C_7D, col7, TINY ? y : y - 2, sum7[k] ? a : "0");
    fmtSigned(a, sizeof(a), sum30[k]);
    label(tb, numF, C_30D, col30, TINY ? y : y - 2, sum30[k] ? a : "0");
  }
  if (COMPACT) return;
  // Daily downloads, oldest on the left.
  const int cx = PAD + tw + 10, cw = CW - tw - 10;
  lv_obj_t* ch = card(mdRoot, cx, y0, cw, BOT - y0);
  label(ch, F12, C_SUB, 14, 10, "DAILY DOWNLOADS");
  const int bars = max(1, min(nd, 30)), step = (cw - 28) / 30, bw = max(3, step - 2);
  const int bottom = BOT - y0 - 14, maxH = bottom - (10 + lineH(F12) + 10);
  for (int i = 0; i < bars; i++) {
    int v = dl[bars - 1 - i], hh = v ? 4 + v * (maxH - 4) / mx : 3;
    box(ch, 14 + (30 - bars + i) * step, bottom - hh, bw, hh, v ? COL[S_DOWNLOADS] : C_TRACK, 2);
  }
}

#if INPUT_TOUCH
static bool swiped = false;

static void topRowCb(lv_event_t* e) {
  if (swiped) return;  // the release of a page drag, not a tap
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < st.ntop[topMode]) modelPage(&st.top[topMode][i]);
}
static void touchActivity();
// Horizontal drags move the page with the finger (compositor boards).
static bool dragDecided = false, dragging = false;
static lv_point_t dragStart;
static int dragDx = 0;
static uint32_t pressT = 0;  // when the finger went down
static uint32_t moveT = 0;   // when it had moved far enough to count as a drag
static uint32_t dragSeen = 0;  // last finger report during a drag
// Recent (time, x) samples: the release speed is measured over the last
// ~100 ms, which catches quick flicks even when frames are slow.
static uint32_t sampT[6];
static int sampX[6], sampN = 0;

static bool dragAllowed() { return display_can_compose() && !ui_overlay_open() && !slideBusy; }
bool ui_dragging() { return dragging; }

static void dragMove(const lv_point_t& p) {
  uint32_t now = millis();
  int dx = p.x - dragStart.x;
  if (!dragDecided) {
    int dy = p.y - dragStart.y;
    if (abs(dx) < 10 && abs(dy) < 10) return;
    dragDecided = true;
    // Horizontal unless clearly vertical (|dx| < 0.8 |dy|); those are left to gestures.
    if (abs(dx) * 10 < abs(dy) * 8 || !dragAllowed()) {
      swipeLog("swipe dx %d dy %d: vertical\n", dx, dy);
      return;
    }
    // Only the neighbor we're moving toward is needed (usually pre-rendered).
    int slot = dx < 0 ? 1 : 0, nb = dx < 0 ? (cur + 1) % NCAR : (cur + NCAR - 1) % NCAR;
    if (!pageSnap(slot, nb)) return;
    slideBusy = dragging = swiped = true;
    moveT = now;
    touchActivity();
    if (bannerUp) bannerClear();
    display_compose_begin();
    sampN = 0;
  }
  if (!dragging) return;
  dragSeen = now;
  dragDx = constrain(dx, -W, W);
  // Changing direction past the start needs the other neighbor too.
  int slot = dragDx < 0 ? 1 : 0, nb = dragDx < 0 ? (cur + 1) % NCAR : (cur + NCAR - 1) % NCAR;
  if (dragDx && !pageSnap(slot, nb)) return;
  memmove(&sampT[1], &sampT[0], sizeof(sampT) - sizeof(sampT[0]));
  memmove(&sampX[1], &sampX[0], sizeof(sampX) - sizeof(sampX[0]));
  sampT[0] = now;
  sampX[0] = p.x;
  if (sampN < 6) sampN++;
  composeAt(dragDx < 0 ? 1 : 0, dragDx);
}

// Goes to the neighbor when the drag passed 80 px (1/10 of the width on
// narrower panels), was a quick swipe (25+ px in under 400 ms), or was fast
// that way (>= 0.3 px/ms, 25+ px). Speed is the larger of the average over
// the movement and the peak over the last ~50 ms of samples; the speed
// right at lift-off alone reads far too low. Time counts from when the finger
// started moving, not from when it touched down: the pause before a short
// flick made it miss (the Waveshare 7" showed 330-410 ms from touch-down for
// 30-60 px flicks, and 27-29 px moves in ~330 ms). Otherwise it snaps back.
static void dragRelease(const lv_point_t* last) {
  if (!dragging) return;
  // While a slide is composed, touch is only read once per frame (~50 ms),
  // so the last position may not have been seen as a move yet.
  if (last) {
    int dx = constrain(last->x - dragStart.x, -W, W);
    if ((dx < 0) == (dragDx < 0) && abs(dx) > abs(dragDx)) dragDx = dx;
  }
  dragging = false;
  int dir = dragDx < 0 ? 1 : -1, slot = dragDx < 0 ? 1 : 0;
  uint32_t now = millis(), dur = now - moveT;
  float avg = dur ? dragDx / (float)dur : 0, peak = 0;
  for (int i = 1; i < sampN; i++) {
    if (sampT[0] - sampT[i - 1] > 50) break;
    if (sampT[i - 1] <= sampT[i]) continue;
    float v = (sampX[i - 1] - sampX[i]) / (float)(sampT[i - 1] - sampT[i]);
    if ((v < 0) == (dragDx < 0) && fabsf(v) > fabsf(peak)) peak = v;
  }
  float vel = fabsf(avg) >= fabsf(peak) ? avg : peak;
  int adx = abs(dragDx);
  bool far = adx >= min(80, W / 10);
  bool quick = adx >= 25 && dur < 400;
  bool flick = adx >= 25 && fabsf(vel) >= 0.3f && (vel < 0) == (dragDx < 0);
  bool commit = dragDx != 0 && (far || quick || flick);
  swipeLog("swipe dx %d %lums (touch %lums) avg %.2f peak %.2f: %s\n", dragDx, (unsigned long)dur,
           (unsigned long)(now - pressT), avg, peak,
           commit ? (far ? "commit" : quick ? "commit (quick)" : "commit (flick)") : "snap back");
  if (commit) {
    runSlide(slot, dragDx, -dir * W, 200);
    finishSlide((cur + dir + NCAR) % NCAR);
  } else {
    runSlide(slot, dragDx, 0, 180);
    finishSlide(cur);
  }
}
static void touchActivity();

// Touch model: horizontal swipe = page, vertical swipe = more within the page,
// tap = only what was tapped (chips, segments, buttons have their own
// handlers; empty background does nothing), long press = back on Settings /
// Status. A swipe never also counts as a tap.
static void touchActivity() {
  lastInput = millis();
  lastCycle = millis();
  applyBrightness();
}

// Vertical swipe on the current page: d = +1 for swipe up (next), -1 down.
static void pageStep(int d) {
  switch (cur) {
    case SC_FEED: {
      int i = ((feedFilter + 1 + d) % (NKIND + 1) + NKIND + 1) % (NKIND + 1);
      feedFilter = i - 1;
      refreshFeed();
      break;
    }
    case SC_TOP:
      topMode = ((topMode + d) % 3 + 3) % 3;
      refreshTop();
      break;
    case SC_TREND:
      trendStat = ((trendStat + d) % NSTAT + NSTAT) % NSTAT;
      refreshTrend();
      break;
    case SC_COMMENTS:
      commentPage += d;
      refreshComments();
      break;
    case SC_BEST:
      bestMetric = ((bestMetric + d) % NBEST + NBEST) % NBEST;
      refreshBest();
      break;
    case SC_CREATOR:
      crView = ((crView + d) % NCVIEW + NCVIEW) % NCVIEW;
      refreshCreator();
      break;
    default:
      break;
  }
}

static void touchEvent(lv_event_t* e) {
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      if (dragging) {  // the last drag's release never arrived
        swipeLog("drag had no release; settling it\n");
        dragRelease(nullptr);
      }
      swiped = false;
      dragDecided = dragging = false;
      dragDx = 0;
      pressT = millis();
      lv_indev_get_point(lv_indev_get_act(), &dragStart);
      return;
    case LV_EVENT_PRESSING: {
      lv_point_t p;
      lv_indev_get_point(lv_indev_get_act(), &p);
      dragMove(p);
      return;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST: {
      lv_point_t p;
      lv_indev_get_point(lv_indev_get_act(), &p);
      dragRelease(&p);
      return;
    }
    case LV_EVENT_GESTURE: {
      lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
      // Horizontal moves are finger-follow drags on compositor boards. One
      // that began on a button never became a drag (the button keeps the
      // press), so it pages here instead of doing nothing.
      if ((d == LV_DIR_LEFT || d == LV_DIR_RIGHT) && (dragging || modelOpen || guideRoot || updScr)) return;
      swiped = true;
      lv_indev_wait_release(lv_indev_get_act());
      touchActivity();
      if (hbIsOpen) hbOpen(false);
      if (wifiOpen || menuOpen || statusOpen || aboutOpen || updOpen) return;
      if (bannerUp && d == LV_DIR_TOP) {
        bannerClear();
        return;
      }
      if (d == LV_DIR_LEFT) goScreen(cur + 1, 1);
      else if (d == LV_DIR_RIGHT) goScreen(cur - 1, -1);
      else if (d == LV_DIR_TOP) pageStep(1);
      else if (d == LV_DIR_BOTTOM) pageStep(-1);
      return;
    }
    case LV_EVENT_SHORT_CLICKED:
      if (swiped) return;
      touchActivity();
      if (bannerUp) bannerClear();
      return;
    case LV_EVENT_LONG_PRESSED:
      if (swiped) return;
      touchActivity();
      if (wifiOpen) ui_wifi_page(false);
      else if (modelOpen) modelPage(nullptr);
      else if (menuOpen && sheet) sheetClose();
      else if (menuOpen) openMenu(false);
      else if (statusOpen) openStatus(false);
      else if (aboutOpen) openAbout(false);
      else if (updOpen) openUpdate(false);
      return;
    default:
      return;
  }
}

static void menuTapCb(lv_event_t* e) {
  if (ui_wake()) return;
  lastInput = millis();
  applyBrightness();
  menuSel = tapIdx(e);
  menuActivate(menuSel);
}

static void menuBackCb(lv_event_t*) { openMenu(false); }
static void menuHoldCb(lv_event_t*) { openMenu(false); }
#endif

void ui_tick() {
  static uint32_t lastSec = 0;
  uint32_t now = millis();
  lv_timer_handler();
  // A drag whose release never reached touchEvent leaves the compositor in
  // charge, and nothing is drawn again (the 2" froze this way). Once the
  // finger is up, settle it like a release.
#if INPUT_TOUCH
  if (dragging && now - dragSeen > 300) {
    lv_indev_t* in = lv_indev_get_next(nullptr);
    if (in && in->proc.state == LV_INDEV_STATE_RELEASED) {
      swipeLog("drag lost its release; settling it\n");
      dragRelease(nullptr);
    }
  }
#endif

  if (bannerUp && now - bannerShownAt > (uint32_t)setting_value(SET_BANNER) * 1000) bannerNext();
  if (toastUntil && now > toastUntil) {
    toastUntil = 0;
    show(toastLbl, false);
  }
  if (now - lastSec < 1000) return;
  lastSec = now;

  refreshTodayHeader();
  wifiBannerUpdate();
  guideTick();
  if (statusOpen) refreshStatus();
  // Keep the neighbor snapshots fresh while nobody is touching the panel;
  // paused while any overlay (Wi-Fi page, Settings, ...) is open.
  if (display_can_compose() && !slideBusy && now - lastInput > 700 && !ui_overlay_open()) {
    int prev = (cur + NCAR - 1) % NCAR, next = (cur + 1) % NCAR;
    if (!(cacheOk[0] && cacheIdx[0] == prev)) pageSnap(0, prev);
    else if (!(cacheOk[1] && cacheIdx[1] == next)) pageSnap(1, next);
  }
  static uint32_t lastAct = 0;
  if (now - lastAct >= 30000) {  // relative times in Recent Activity
    lastAct = now;
    refreshActivity();
  }
  applyBrightness();
  hbUpdate();

  if (menuOpen || statusOpen || aboutOpen || updOpen || bannerUp) return;
  int idle = setting_value(SET_IDLE), cycle = setting_value(SET_AUTOCYCLE);
  if (cycle && now - lastCycle > (uint32_t)cycle * 1000 && now - lastInput > 10000) {
    lastCycle = now;
    goScreen(cur + 1, 1);
  } else if (!cycle && idle && cur != SC_TODAY && now - lastInput > (uint32_t)idle * 1000) {
    goScreen(SC_TODAY, -1);
  }
  // Hourly bars shift as the clock moves. A new local day re-labels every
  // "today" figure even if the poller has not sent a fresh payload yet.
  static int lastHour = -1, lastDay = -1;
  uint32_t n = nowUtc();
  int h = n ? (int)(toLocal(n) / 3600 % 24) : -1;
  int day = n ? (int)(toLocal(n) / 86400) : -1;
  if (day != lastDay) {
    lastDay = day;
    lastHour = h;
    refreshAll();
  } else if (h != lastHour) {
    lastHour = h;
    refreshToday();
  }
}
