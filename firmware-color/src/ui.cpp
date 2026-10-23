// All LVGL screens: 8 stat screens, settings, about (QR), notification banner, toast.
// Layout matches the canvas mockups (820x320 landscape). 800x480 ("tall", the
// 5" and 7" panels) uses that extra height; 480x272 ("compact") uses smaller
// type and fewer columns. Navigation comes from the knob or, on touch boards,
// from swipes and taps (see touchEvent()).
#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>

#include "board.h"
#include "display.h"
#include "settings.h"
#include "sound.h"
#include "fw_version.h"

extern "C" {
extern const lv_img_dsc_t icon_boost_20, icon_like_20, icon_collect_20, icon_download_20, icon_print_20, icon_comment_20;
extern const lv_img_dsc_t icon_boost_32, icon_like_32, icon_collect_32, icon_download_32, icon_print_32, icon_comment_32;
}
static const lv_img_dsc_t* const ICON20[NKIND] = {&icon_boost_20, &icon_like_20, &icon_collect_20,
                                                  &icon_download_20, &icon_print_20, &icon_comment_20};
static const lv_img_dsc_t* const ICON32[NKIND] = {&icon_boost_32, &icon_like_32, &icon_collect_32,
                                                  &icon_download_32, &icon_print_32, &icon_comment_32};

// ---- palette ----------------------------------------------------------------
// Dark navy base, hairline card edges, one cyan accent; stat colors are neon.
static const uint32_t C_BG = 0x06090E, C_BG2 = 0x0B1119, C_CARD = 0x0E141D, C_CARD2 = 0x0A0F16;
static const uint32_t C_CHIP = 0x121A25, C_RAISED = 0x132030, C_EDGE = 0x1E2A38, C_ACCENT = 0x22D3EE;
static const uint32_t C_TXT = 0xE6EDF3, C_SUB = 0xB3C1CF, C_MUTED = 0x7E8C9B, C_DIM = 0x2A3644, C_TRACK = 0x17212C;
static const uint32_t C_OK = 0x2EE59D, C_WARN = 0xFFB020, C_ERR = 0xFF5C6C, C_IDLE = 0x4E5A68, C_SEL = C_ACCENT;
// boosts, likes, collects, downloads, prints, comments
static const uint32_t COL[NKIND] = {0xFFB020, 0xFF4D7E, 0x3DA9FF, 0x2EE59D, 0xA78BFA, 0x2DE2E6};
// Period colors for the 7-day / 30-day rows on the Today tiles.
static const uint32_t C_7D = 0xF2F5F8, C_30D = 0x22D3EE;
static const uint32_t COLBG[NKIND] = {0x2E230A, 0x2E1019, 0x0D2236, 0x0A2A1D, 0x1E1834, 0x0A2A2D};

// Set by layout() from the display size.
static int W = 820, H = 320, PAD = 16, TOP = 48, BOT = 300;
static int CW = W - 2 * PAD;  // content width
static int TH = BOT - TOP;    // card height
static bool COMPACT = false;  // 480x272-class panels
static bool TALL = false;     // 800x480-class panels (5" and 7")
static bool DENSE = false;    // same pixels, smaller glass (5": 187 ppi vs 7": 133)
static int TOUCH_PX = 36;     // ~7 mm touch target at this panel's density

// Named by their size on the large panels; compact panels step each one down.
static const lv_font_t *F12, *F14, *F16, *F20, *F22, *F26, *F32, *F40, *F48;

static void layout() {
  W = lv_disp_get_hor_res(NULL);
  H = lv_disp_get_ver_res(NULL);
  COMPACT = W < 640;
  TALL = !COMPACT && H >= 400;
  DENSE = TALL && BOARD_PPI >= 170;
  TOUCH_PX = constrain((int)(7.0f * BOARD_PPI / 25.4f + 0.5f), 32, 52);
  PAD = COMPACT ? 8 : 16;
  TOP = COMPACT ? 34 : (TALL ? 56 : 48);
  BOT = H - (COMPACT ? 16 : (TALL ? 22 : 20));
  CW = W - 2 * PAD;
  TH = BOT - TOP;
  if (COMPACT) {
    F12 = &lv_font_montserrat_12; F14 = &lv_font_montserrat_12; F16 = &lv_font_montserrat_14;
    F20 = &lv_font_montserrat_16; F22 = &lv_font_montserrat_16; F26 = &lv_font_montserrat_20;
    F32 = &lv_font_montserrat_22; F40 = &lv_font_montserrat_26; F48 = &lv_font_montserrat_32;
  } else if (DENSE) {
    // 5" packs the 7"'s pixels into less glass: step body type up one size
    // so text and targets land at a similar physical size on both.
    F12 = &lv_font_montserrat_12; F14 = &lv_font_montserrat_16; F16 = &lv_font_montserrat_20;
    F20 = &lv_font_montserrat_22; F22 = &lv_font_montserrat_26; F26 = &lv_font_montserrat_26;
    F32 = &lv_font_montserrat_32; F40 = &lv_font_montserrat_40; F48 = &lv_font_montserrat_48;
  } else {
    F12 = &lv_font_montserrat_12; F14 = &lv_font_montserrat_14; F16 = &lv_font_montserrat_16;
    F20 = &lv_font_montserrat_20; F22 = &lv_font_montserrat_22; F26 = &lv_font_montserrat_26;
    F32 = &lv_font_montserrat_32; F40 = &lv_font_montserrat_40; F48 = &lv_font_montserrat_48;
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
  lv_obj_set_style_pad_ver(l, 3, 0);
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
  lv_obj_set_ext_click_area(o, 6);
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

static uint32_t nowUtc() {
  time_t t = time(nullptr);
  if (t > 1700000000) return (uint32_t)t;
  if (st.valid) return st.t + (millis() - stateMs) / 1000;
  return 0;
}

static uint32_t toLocal(uint32_t utc) { return utc + st.tz; }

static void fmtHM(char* out, size_t cap, uint32_t utc) {
  uint32_t l = toLocal(utc);
  snprintf(out, cap, "%02lu:%02lu", (unsigned long)(l / 3600 % 24), (unsigned long)(l / 60 % 60));
}

static void fmtAgo(char* out, size_t cap, long sec) {
  if (sec < 0) snprintf(out, cap, "-");
  else if (sec < 60) snprintf(out, cap, "%lds", sec);
  else if (sec < 3600) snprintf(out, cap, "%ldm", sec / 60);
  else if (sec < 86400) snprintf(out, cap, "%ldh %ldm", sec / 3600, sec / 60 % 60);
  else snprintf(out, cap, "%ldd %ldh", sec / 86400, sec / 3600 % 24);
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
enum { SC_TODAY, SC_FEED, SC_TOP, SC_TREND, SC_POINTS, SC_REACH, SC_COMMENTS, SC_STATUS, NSCREEN };
static lv_obj_t* screens[NSCREEN];
static lv_obj_t* menuRoot;
static lv_obj_t* aboutRoot;
static lv_obj_t* updRoot;
// Status and About are their own pages opened from the hamburger menu (like
// Settings), so they sit outside the swipe / knob rotation and have no pager
// segment.
static const int NCAR = SC_STATUS;
static lv_obj_t* dots[NCAR];
// Hamburger button (top right) and its dropdown.
static const int HB_W = 40;
static int cur = SC_TODAY;
static bool menuOpen = false, statusOpen = false, aboutOpen = false, updOpen = false;

struct Header {
  lv_obj_t* title;
  lv_obj_t* sub;
  lv_obj_t* right;   // flex row, top right
};

static Header header(lv_obj_t* root, const char* title, const char* sub) {
  Header h;
  const lv_font_t* tf = TALL ? F20 : F16;
  lv_obj_t* left = row(root, 10);
  lv_obj_set_pos(left, PAD, COMPACT ? 9 : (TALL ? 16 : 14));
  box(left, 0, 0, 3, lineH(tf) - 4, C_ACCENT, 1);
  h.title = label(left, tf, C_ACCENT, 0, 0, title);
  lv_obj_set_style_text_letter_space(h.title, 3, 0);
  // Hairline under the header, fading out to the right.
  lv_obj_t* rule = box(root, PAD, TOP - (COMPACT ? 5 : 8), CW, 1, C_ACCENT);
  lv_obj_set_style_bg_grad_color(rule, C(C_BG), 0);
  lv_obj_set_style_bg_grad_dir(rule, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_70, 0);
  h.sub = label(left, F14, C_MUTED, 0, 0, sub);
  // Keep the subtitle from running under the right-hand cluster.
  dotLabel(h.sub, COMPACT ? 168 : (W * 2) / 5);
  h.right = row(root, 14);
  lv_obj_align(h.right, LV_ALIGN_TOP_RIGHT, -PAD - HB_W - (COMPACT ? 14 : 24), COMPACT ? 7 : (TALL ? 16 : 12));
  return h;
}

static lv_obj_t* hint(Header& h, const char* txt) { return label(h.right, F14, C_MUTED, 0, 0, txt); }

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
}

static void commitScreen(int idx) {
  for (int i = 0; i < NSCREEN; i++) show(screens[i], i == idx);
  lv_obj_set_x(screens[idx], 0);
  cur = idx;
  updateDots();
}

static void goScreen(int idx, int dir) {
  if (menuOpen || statusOpen || aboutOpen || updOpen) return;
  (void)dir;
  idx = (idx % NCAR + NCAR) % NCAR;
  commitScreen(idx);
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
static int TILE_W, TILE_STEP, BAR_BOTTOM, BAR_MAX, BAR_W, BAR_GAP, BAR_X;

// On tall panels the chart stays short and the room under the tiles is a
// RECENT ACTIVITY card: the poller's feed, newest first, in two columns.
static const int ACT_MAX = 8;
struct ActUi {
  lv_obj_t *row, *img, *n, *model, *time;
};
static ActUi actUi[ACT_MAX];
static int actRows = 0;  // rows that fit in the card
static lv_obj_t* actEmpty;

static void buildToday() {
  lv_obj_t* s = screens[SC_TODAY] = newScreen();
  todayHdr = header(s, "TODAY", "waiting for poller");
  ptsChip = label(todayHdr.right, F14, C_WARN, 0, 0, "");
  chipStyle(ptsChip, C_CHIP, C_WARN);
  lv_obj_t* sync = row(todayHdr.right, 6);
  syncDot = box(sync, 0, 0, 8, 8, C_IDLE, LV_RADIUS_CIRCLE);
  syncLbl = label(sync, F14, C_MUTED, 0, 0, "no data");
  clockLbl = label(todayHdr.right, F16, C_TXT, 0, 0, "--:--");

  const int gap = COMPACT ? 6 : 9, in = COMPACT ? 8 : 12;
  TILE_STEP = (CW + gap) / NSTAT;
  TILE_W = TILE_STEP - gap;
  // Bucket count follows the tile width: aim for ~16 px bars and snap to a
  // divisor of 24 (4, 6, 8, or 12 bars), never thinner than that again.
  {
    int inner0 = TILE_W - 2 * in, want = inner0 / 16;
    NBAR = want >= 12 ? 12 : want >= 8 ? 8 : want >= 6 ? 6 : 4;
  }
  BAR_HOURS = 24 / NBAR;
  const int numY = COMPACT ? 30 : (TALL ? 36 : 40);
  const int ydayY = numY + lineH(F48) + (TALL ? 2 : 4);
  // Room under the baseline for the clock labels and the lifetime total.
  const int labelH = COMPACT ? 0 : lineH(F12) + 4;
  const int belowBase = COMPACT ? 30 : (TALL ? 6 + lineH(F14) + 10 : 44);
  // 7-day and 30-day sums sit under the number (tall panels).
  const int rule1Y = ydayY + lineH(F12) + (TALL ? 6 : 10);
  const int rowStep = lineH(F20) + (TALL ? 4 : 8);
  const int row1Y = rule1Y + (TALL ? 6 : 8);
  const int rule2Y = row1Y + 2 * rowStep;
  const int barTop = TALL ? rule2Y + 10 : ydayY + 28;
  // Tall: a fixed short chart, and the tile ends under it (the activity card
  // takes the rest). Otherwise the chart gets what the full-height tile leaves.
  const int chartH = 56;
  int tileH = TH;
  if (TALL) {
    BAR_BOTTOM = barTop + chartH;
    tileH = BAR_BOTTOM + labelH + belowBase;
  } else {
    BAR_BOTTOM = tileH - belowBase - labelH;
  }
  BAR_GAP = COMPACT ? 5 : (TALL ? 3 : 8);
  const int inner = TILE_W - 2 * in;
  BAR_W = (inner - (NBAR - 1) * BAR_GAP) / NBAR;
  if (BAR_W < 4) BAR_W = 4;
  const int used = NBAR * BAR_W + (NBAR - 1) * BAR_GAP;
  BAR_X = in + max(0, (inner - used) / 2);
  const int avail = max(16, BAR_BOTTOM - barTop);
  BAR_MAX = TALL ? chartH : min(avail, max(28, BAR_W * 4));
  for (int k = 0; k < NSTAT; k++) {
    Tile& t = tiles[k];
    t.card = card(s, PAD + k * TILE_STEP, TOP, TILE_W, tileH);
    lv_obj_set_style_border_side(t.card, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(t.card, 2, 0);
    lv_obj_set_style_border_color(t.card, C(COL[k]), 0);
    if (!COMPACT) icon(t.card, k, false, 12, 12);
    lv_obj_t* name = label(t.card, F12, C_SUB, COMPACT ? in : 38, COMPACT ? 9 : 15, COMPACT ? KIND_PLURAL[k] : KIND_UPPER[k]);
    dotLabel(name, TILE_W - (COMPACT ? 2 * in : 44));
    t.today = label(t.card, F48, COL[k], in, numY, "0");
    t.yday = label(t.card, F12, C_MUTED, in, ydayY, "today");
    dotLabel(t.yday, TILE_W - 2 * in);
    if (TALL) {
      box(t.card, in, rule1Y, TILE_W - 2 * in, 1, C_EDGE);
      // 7D / 30D get their own colors (same on every tile), so they never
      // read as today's number or bars, which use the stat color.
      const char* names[2] = {"7D", "30D"};
      const uint32_t rc[2] = {C_7D, C_30D};
      lv_obj_t** vals[2] = {&t.d7, &t.d30};
      for (int r = 0; r < 2; r++) {
        int y = row1Y + r * rowStep, ly = y + (lineH(F20) - lineH(F12)) / 2;
        box(t.card, in, ly + (lineH(F12) - 6) / 2, 6, 6, rc[r], 1);
        lv_obj_t* l = label(t.card, F12, rc[r], in + 11, ly, names[r]);
        lv_obj_set_style_text_letter_space(l, 1, 0);
        *vals[r] = label(t.card, F20, rc[r], 0, 0, "-");
        lv_obj_align(*vals[r], LV_ALIGN_TOP_RIGHT, -in, y);
      }
      box(t.card, in, rule2Y, TILE_W - 2 * in, 1, C_EDGE);
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
    if (TALL) {
      // 1px average lines in the period colors, on top of the bars.
      t.ref7 = box(t.card, BAR_X, BAR_BOTTOM, used, 1, C_7D);
      t.ref30 = box(t.card, BAR_X, BAR_BOTTOM, used, 1, C_30D);
      lv_obj_set_style_bg_opa(t.ref7, LV_OPA_80, 0);
      lv_obj_set_style_bg_opa(t.ref30, LV_OPA_90, 0);
    }
    t.total = label(t.card, F14, C_MUTED, in, BAR_BOTTOM + (COMPACT ? 8 : labelH + 6), "");
    dotLabel(t.total, TILE_W - 2 * in);
  }
  if (!TALL) return;
  const int y0 = TOP + tileH + 10, h = BOT - y0;
  lv_obj_t* c = card(s, PAD, y0, CW, h);
  lv_obj_t* title = label(c, F12, C_ACCENT, 14, 8, "RECENT ACTIVITY");
  lv_obj_set_style_text_letter_space(title, 2, 0);
  const int rowsY = 8 + lineH(F12) + 6, rowH = lineH(F16) + 6;
  const int perCol = max(1, (h - rowsY - 6) / rowH), colW = (CW - 28 - 24) / 2;
  actRows = min(ACT_MAX, 2 * perCol);
  for (int i = 0; i < actRows; i++) {
    // Newest first, down the left column, then the right.
    int x = 14 + (i / perCol) * (colW + 24), y = rowsY + (i % perCol) * rowH;
    ActUi& a = actUi[i];
    a.row = lv_obj_create(c);
    lv_obj_remove_style_all(a.row);
    lv_obj_clear_flag(a.row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(a.row, x, y);
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
static void refreshActivity() {
  if (!TALL) return;
  char a[24];
  uint32_t now = nowUtc();
  int shown = 0;
  for (int i = 0; i < st.nfeed && shown < actRows; i++) {
    const FeedRow& r = st.feed[i];
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
  show(actEmpty, shown == 0);
}

static void refreshToday() {
  char a[32], b[48];
  int hourNow = -1;
  uint32_t n = nowUtc();
  if (n) hourNow = toLocal(n) / 3600 % 24;
  for (int k = 0; k < NSTAT; k++) {
    Tile& t = tiles[k];
    fmtSigned(a, sizeof(a), st.today[k]);
    lv_label_set_text(t.today, st.today[k] == 0 ? "0" : a);
    if (st.hasYday) {
      fmtSigned(a, sizeof(a), st.yday[k]);
      snprintf(b, sizeof(b), COMPACT ? "%s yday" : "today  /  %s yday", a);
    } else {
      snprintf(b, sizeof(b), "today");
    }
    lv_label_set_text(t.yday, b);
    if (TALL) {
      long sum7 = 0;
      for (int d = 0; d < st.ndays; d++) sum7 += st.days[d].v[k];
      fmtSigned(a, sizeof(a), sum7);
      lv_label_set_text(t.d7, st.ndays ? a : "-");
      // 30-day sums come from the account's month report (saves = collects).
      const int m30[NSTAT] = {st.month.boosts, st.month.likes, st.month.saves, st.month.downloads, st.month.prints};
      fmtSigned(a, sizeof(a), m30[k]);
      lv_label_set_text(t.d30, st.month.has ? a : "-");
    }
    fmtInt(a, sizeof(a), st.total[k]);
    if (COMPACT) lv_label_set_text(t.total, a);
    else lv_label_set_text_fmt(t.total, "%s total", a);
    int bucket[NBAR_MAX] = {};
    for (int h = 0; h < 24; h++) bucket[h / BAR_HOURS] += st.hours[k][h];
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
  snprintf(a, sizeof(a), COMPACT ? "%+.1f pts" : "%+.1f pts today", st.todayPoints);
  lv_label_set_text(ptsChip, a);
  show(ptsChip, st.valid && st.cookieAge >= 0);
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
    setTextIfChanged(todayHdr.sub, "Wi-Fi setup: join " BOARD_AP);
  } else if (!st.valid) {
    snprintf(a, sizeof(a), "waiting for poller  -  %s.local", g_net.host[0] ? g_net.host : "offline");
    setTextIfChanged(todayHdr.sub, a);
  } else {
    time_t lt = toLocal(n ? n : st.t);
    struct tm tmv;
    gmtime_r(&lt, &tmv);
    static const char* const WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (COMPACT) {
      snprintf(a, sizeof(a), "%s %s %d", WD[tmv.tm_wday], MONTHS[tmv.tm_mon], tmv.tm_mday);
    } else if (st.midnight) {
      snprintf(a, sizeof(a), "%s %s %d  -  since midnight", WD[tmv.tm_wday], MONTHS[tmv.tm_mon], tmv.tm_mday);
    } else {
      fmtHM(b, sizeof(b), st.since);
      snprintf(a, sizeof(a), "%s %s %d  -  since %s", WD[tmv.tm_wday], MONTHS[tmv.tm_mon], tmv.tm_mday, b);
    }
    setTextIfChanged(todayHdr.sub, a);
  }
  uint32_t dot = C_IDLE;
  if (st.valid) {
    dot = !strcmp(st.auth, "ok") ? C_OK : !strcmp(st.auth, "needed") ? C_WARN : C_ERR;
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
  const int cols = COMPACT ? 1 : 2, gap = COMPACT ? 6 : 8;
  feedCap = cols * 4;
  const int colW = (CW - (cols - 1) * 12) / cols, rowH = (TH - 3 * gap) / 4;
  const int ib = TALL ? 48 : 36, tx = TALL ? 76 : 60;
  const lv_font_t* tf = TALL ? F20 : F16;
  const lv_font_t* mf = TALL ? F16 : F14;
  const int textY = (rowH - lineH(tf) - lineH(mf) - 4) / 2;
  for (int i = 0; i < feedCap; i++) {
    FeedUi& f = feedUi[i];
    int col = i / 4, r = i % 4;
    f.card = card(s, PAD + col * (colW + 12), TOP + r * (rowH + gap), colW, rowH);
    f.iconBox = box(f.card, TALL ? 14 : 12, (rowH - ib) / 2, ib, ib, COLBG[0], 10);
    f.img = icon(f.iconBox, 0, TALL, (ib - (TALL ? 32 : 20)) / 2, (ib - (TALL ? 32 : 20)) / 2);
    f.title = label(f.card, tf, COL[0], tx, textY, "");
    f.model = label(f.card, mf, C_SUB, tx, textY + lineH(tf) + 4, "");
    dotLabel(f.model, colW - tx - 70);
    f.time = label(f.card, F14, C_MUTED, 0, 0, "");
    lv_obj_align(f.time, LV_ALIGN_RIGHT_MID, -14, 0);
  }
  feedEmpty = label(s, F20, C_MUTED, 0, 0, "No activity yet today");
  lv_obj_align(feedEmpty, LV_ALIGN_CENTER, 0, 10);
}

static void refreshFeed() {
  char a[48], b[16];
  int shown = 0, todayCount = 0;
  for (int i = 0; i < st.nfeed; i++) todayCount += st.feed[i].n;
  for (int i = 0; i < st.nfeed && shown < feedCap; i++) {
    const FeedRow& r = st.feed[i];
    if (feedFilter >= 0 && r.kind != feedFilter) continue;
    FeedUi& f = feedUi[shown++];
    show(f.card, true);
    lv_obj_set_style_bg_color(f.iconBox, C(COLBG[r.kind]), 0);
    setIcon(f.img, r.kind, TALL);
    if (r.kind == K_COMMENT) snprintf(a, sizeof(a), "New comment");
    else snprintf(a, sizeof(a), "+%d %s", r.n, r.n == 1 ? KIND_NAME[r.kind] : KIND_PLURAL[r.kind]);
    lv_label_set_text(f.title, a);
    lv_obj_set_style_text_color(f.title, C(COL[r.kind]), 0);
    lv_label_set_text(f.model, r.model);
    fmtHM(b, sizeof(b), r.t);
    lv_label_set_text(f.time, r.t ? b : "");
  }
  for (int i = shown; i < feedCap; i++) show(feedUi[i].card, false);
  show(feedEmpty, shown == 0);
  snprintf(a, sizeof(a), COMPACT ? "%s" : "%s  -  newest first", feedFilter < 0 ? "all events" : KIND_PLURAL[feedFilter]);
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
// Compact tiles are too narrow for five stat chips plus a name: they show
// collects / downloads / prints and give the rest to the name and bar.
static int topChipN = NSTAT;
static int topChipKind[NSTAT];

static void refreshTop();
static void topChipCb(lv_event_t* e) {
  topMode = tapIdx(e);
  refreshTop();
}

static void buildTop() {
  lv_obj_t* s = screens[SC_TOP] = newScreen();
  topHdr = header(s, "TOP MODELS", COMPACT ? "" : "ranked by activity");
  lv_obj_set_style_pad_column(topHdr.right, 6, 0);
  const char* names[3] = {"Today", "7 days", "Lifetime"};
  for (int i = 0; i < 3; i++) {
    topChips[i] = label(topHdr.right, F14, C_MUTED, 0, 0, names[i]);
    tappable(topChips[i], topChipCb, i);
  }
  const int gap = COMPACT ? 6 : 8, rowH = (TH - 3 * gap) / 4;
  if (COMPACT) {
    const int kinds[3] = {S_COLLECTS, S_DOWNLOADS, S_PRINTS};
    topChipN = 3;
    for (int i = 0; i < 3; i++) topChipKind[i] = kinds[i];
  } else {
    topChipN = NSTAT;
    for (int i = 0; i < NSTAT; i++) topChipKind[i] = i;
  }
  const int chipW = COMPACT ? 40 : (TALL ? 52 : 46), chipH = TALL ? 36 : 28, chipStep = chipW + 6;
  const int nameX = COMPACT ? 32 : 48;
  const lv_font_t* nf = TALL ? F20 : F16;
  const int barH = TALL ? 8 : 5;
  TOP_BAR_W = CW - nameX - 16 - topChipN * chipStep - 10;
  const int nameY = TALL ? (rowH - lineH(nf) - barH - 10) / 2 : (rowH - lineH(F16) - 13) / 2;
  for (int i = 0; i < 4; i++) {
    TopUi& t = topUi[i];
    t.card = card(s, PAD, TOP + i * (rowH + gap), CW, rowH);
    t.rank = label(t.card, F22, C_IDLE, COMPACT ? 10 : 16, (rowH - lineH(F22)) / 2, "");
    lv_label_set_text_fmt(t.rank, "%d", i + 1);
    t.name = label(t.card, nf, C_TXT, nameX, nameY, "");
    dotLabel(t.name, TOP_BAR_W);
    lv_obj_t* track = box(t.card, nameX, nameY + lineH(nf) + 8, TOP_BAR_W, barH, C_TRACK, 3);
    t.fill = box(track, 0, 0, TOP_BAR_W, barH, C_TXT, 3);
    for (int s = 0; s < topChipN; s++) {
      int k = topChipKind[s];
      lv_obj_t* c = box(t.card, CW - 16 - (topChipN - s) * chipStep + 6, (rowH - chipH) / 2, chipW, chipH, COLBG[k], 8);
      t.chips[s] = label(c, F14, COL[k], 0, 0, "");
      lv_obj_center(t.chips[s]);
    }
  }
  topEmpty = label(s, F20, C_MUTED, 0, 0, "No model activity yet");
  lv_obj_align(topEmpty, LV_ALIGN_CENTER, 0, 10);
}

static void refreshTop() {
  for (int i = 0; i < 3; i++) chipStyle(topChips[i], i == topMode ? C_TXT : C_CHIP, i == topMode ? C_BG : C_MUTED);
  int n = st.ntop[topMode];
  int mx = n ? max(1, st.top[topMode][0].score) : 1;
  char a[16];
  for (int i = 0; i < 4; i++) {
    TopUi& t = topUi[i];
    show(t.card, i < n);
    if (i >= n) continue;
    const TopRow& r = st.top[topMode][i];
    lv_label_set_text(t.name, r.name);
    lv_obj_set_width(t.fill, max(4, TOP_BAR_W * r.score / mx));
    for (int s = 0; s < topChipN; s++) {
      int k = topChipKind[s];
      if (topMode == 2) fmtInt(a, sizeof(a), r.v[k]);
      else fmtSigned(a, sizeof(a), r.v[k]);
      lv_label_set_text(t.chips[s], a);
      lv_obj_center(t.chips[s]);
    }
  }
  show(topEmpty, n == 0);
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

static void refreshTrend();
static void trendChipCb(lv_event_t* e) {
  trendStat = tapIdx(e);
  refreshTrend();
}

static void buildTrend() {
  lv_obj_t* s = screens[SC_TREND] = newScreen();
  trendHdr = header(s, "7-DAY TREND", COMPACT || INPUT_TOUCH ? "" : "press to change stat");
  lv_obj_set_style_pad_column(trendHdr.right, COMPACT ? 3 : 6, 0);
  for (int k = 0; k < NSTAT; k++) {
    trendChips[k] = label(trendHdr.right, F14, C_MUTED, 0, 0, KIND_PLURAL[k]);
    tappable(trendChips[k], trendChipCb, k);
  }
  lv_obj_t* right;
  if (COMPACT) {
    // Summary as a strip across the top, chart full width underneath: a
    // 118 px side column starved both the figures and the bars.
    const int stripH = 56, colW3 = CW / 3;
    lv_obj_t* strip = card(s, PAD, TOP, CW, stripH);
    trendTotal = label(strip, F26, COL[trendStat], 12, 6, "");
    label(strip, F12, C_MUTED, 12, 6 + lineH(F26) + 1, "last 7 days");
    trendAvg = label(strip, F22, C_TXT, colW3 + 8, 6, "");
    label(strip, F12, C_MUTED, colW3 + 8, 6 + lineH(F22) + 1, "avg per day");
    trendBest = label(strip, F22, C_TXT, 2 * colW3 + 8, 6, "");
    label(strip, F12, C_MUTED, 2 * colW3 + 8, 6 + lineH(F22) + 1, "best day");
    TR_X = PAD;
    TR_W = CW;
    const int chartH = TH - stripH - 6;
    TR_BOTTOM = chartH - 28;
    TR_BARMAX = TR_BOTTOM - 40;
    TR_COLW = min(66, (TR_W - 36 - 6 * 6) / 7);
    right = card(s, TR_X, TOP + stripH + 6, TR_W, chartH);
  } else {
    const int leftW = 190, sep = 14, lx = 16;
    TR_X = PAD + leftW + sep;
    TR_W = CW - leftW - sep;
    TR_BOTTOM = TH - (TALL ? 36 : 40);
    TR_BARMAX = TR_BOTTOM - (TALL ? 52 : 62);
    TR_COLW = min(TALL ? 96 : 66, (TR_W - 36 - 6 * 6) / 7);
    lv_obj_t* left = card(s, PAD, TOP, leftW, TH);
    // Three figures spread down the card, each with a caption under it.
    // On tall panels each block is centered in its third so the card doesn't
    // stack them at the top of a deep slot.
    const int slot = (TH - 2 * 14) / 3;
    const lv_font_t* midF = TALL ? F32 : F22;
    int y0, y1, y2;
    if (TALL) {
      auto yAt = [&](int i, const lv_font_t* f) {
        int block = lineH(f) + 2 + lineH(F12);
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
    label(left, F12, C_MUTED, lx, y0 + lineH(F40) + 2, "last 7 days");
    trendAvg = label(left, midF, C_TXT, lx, y1, "");
    label(left, F12, C_MUTED, lx, y1 + lineH(midF) + 2, "avg per day");
    trendBest = label(left, midF, C_TXT, lx, y2, "");
    label(left, F12, C_MUTED, lx, y2 + lineH(midF) + 2, "best day");
    right = card(s, TR_X, TOP, TR_W, TH);
  }
  int gap = (TR_W - 36 - 7 * TR_COLW) / 6;
  for (int i = 0; i < 7; i++) {
    int x = 18 + i * (TR_COLW + gap);
    trend[i].bar = box(right, x, TR_BOTTOM - 8, TR_COLW, 8, COL[trendStat], 6);
    trend[i].val = label(right, F14, C_TXT, x, 0, "");
    trend[i].day = label(right, F12, C_MUTED, x, TR_BOTTOM + (COMPACT ? 6 : 8), "");
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
    bool today = di == n - 1;
    lv_obj_set_height(c.bar, h);
    lv_obj_set_y(c.bar, TR_BOTTOM - h);
    lv_obj_set_style_bg_color(c.bar, today ? lv_color_mix(C(COL[k]), C(C_CARD), 110) : C(COL[k]), 0);
    fmtSigned(a, sizeof(a), v);
    lv_label_set_text(c.val, a);
    lv_obj_set_y(c.val, TR_BOTTOM - h - 22);
    lv_obj_set_style_text_color(c.val, C(today ? C_MUTED : C_TXT), 0);
    lv_label_set_text(c.day, today ? "Today" : st.days[di].d);
  }
}

// ---- 5. Points --------------------------------------------------------------
static lv_obj_t *ptBalance, *ptToday, *ptFollow, *ptRange, *ptMonth, *ptSplitA, *ptSplitB, *ptLegA, *ptLegB;
static lv_obj_t *ptAvg, *ptBoosts, *ptPrints, *ptMonthCard, *ptNoData, *ptHint;
static int PT_SPLIT_W;

static void buildPoints() {
  lv_obj_t* s = screens[SC_POINTS] = newScreen();
  Header h = header(s, "POINTS", "");
  ptHint = hint(h, COMPACT ? "needs cookie" : "30-day stats need a MakerWorld cookie");
  const int w1 = (CW - 20) / 3, mx = COMPACT ? 12 : (TALL ? 22 : 18);
  lv_obj_t* c1 = card(s, PAD, TOP, w1, TH);
  label(c1, F12, C_SUB, mx, TALL ? 24 : 18, "BALANCE");
  ptBalance = label(c1, F48, C_WARN, mx, TALL ? TH / 4 : (TH - lineH(F48)) / 2, "-");
  if (TALL) {
    // Fill the tall card: followers and model count under the balance.
    ptFollow = label(c1, F32, C_TXT, mx, TH / 2 + 22, "-");
    label(c1, F12, C_MUTED, mx, TH / 2 + 22 + lineH(F32) + 2, "followers  /  models");
  }
  ptToday = label(c1, TALL ? F20 : F16, C_OK, mx, TH - lineH(TALL ? F20 : F16) - (TALL ? 28 : 22), "");
  const int mw = CW - w1 - 10;
  PT_SPLIT_W = mw - 2 * mx;
  ptMonthCard = card(s, PAD + w1 + 10, TOP, mw, TH);
  lv_obj_t* m = ptMonthCard;
  const int trackH = TALL ? 26 : 18;
  int trackY, legY;
  if (TALL) {
    ptRange = label(m, F14, C_SUB, mx, 22, "LAST 30 DAYS");
    ptMonth = label(m, F40, C_TXT, 0, 0, "");
    lv_obj_align(ptMonth, LV_ALIGN_TOP_RIGHT, -mx, 14);
    trackY = 22 + lineH(F40) + 16;
    legY = trackY + trackH + 16;
  } else {
    ptRange = label(m, F12, C_SUB, mx, 22, "LAST 30 DAYS");
    ptMonth = label(m, F32, C_TXT, 0, 0, "");
    lv_obj_align(ptMonth, LV_ALIGN_TOP_RIGHT, -mx, 10);
    trackY = 22 + lineH(F32) + 6;
    legY = trackY + 18 + 16;
  }
  lv_obj_t* track = box(m, mx, trackY, PT_SPLIT_W, trackH, C_TRACK, trackH / 2);
  ptSplitA = box(track, 0, 0, PT_SPLIT_W / 2, trackH, C_WARN, trackH / 2);
  ptSplitB = box(track, PT_SPLIT_W / 2, 0, PT_SPLIT_W / 2, trackH, 0x7A5B22, trackH / 2);
  box(m, mx, legY + 4, 10, 10, C_WARN, 3);
  ptLegA = label(m, F14, C_TXT, mx + 18, legY, "");
  box(m, mx + PT_SPLIT_W / 2, legY + 4, 10, 10, 0x7A5B22, 3);
  ptLegB = label(m, F14, C_TXT, mx + PT_SPLIT_W / 2 + 18, legY, "");
  if (TALL) {
    // Three figure tiles under the split bar, tall enough to read as
    // metrics but not stretched over the whole lower card.
    const int figTop = legY + lineH(F14) + 26;
    const int figH = min(TH - figTop - 16, 168);
    const int figW = (PT_SPLIT_W - 16) / 3;
    const char* caps[3] = {"avg / day", "boosts", "prints"};
    lv_obj_t** slots[3] = {&ptAvg, &ptBoosts, &ptPrints};
    for (int i = 0; i < 3; i++) {
      lv_obj_t* fc = card(m, mx + i * (figW + 8), figTop, figW, figH);
      *slots[i] = label(fc, F40, C_TXT, 0, 0, "");
      lv_obj_align(*slots[i], LV_ALIGN_CENTER, 0, -14);
      lv_obj_t* cap = label(fc, F14, C_MUTED, 0, 0, caps[i]);
      lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -18);
    }
  } else {
    const int figY = TH - lineH(F22) - lineH(F12) - 22, capY = figY + lineH(F22) + 2, step = PT_SPLIT_W / 3;
    ptAvg = label(m, F22, C_TXT, mx, figY, "");
    label(m, F12, C_MUTED, mx, capY, "avg / day");
    ptBoosts = label(m, F22, C_TXT, mx + step, figY, "");
    label(m, F12, C_MUTED, mx + step, capY, "boosts");
    ptPrints = label(m, F22, C_TXT, mx + 2 * step, figY, "");
    label(m, F12, C_MUTED, mx + 2 * step, capY, "prints");
  }
  ptNoData = label(m, F16, C_MUTED, 0, 0, COMPACT ? "Needs a fresh MakerWorld cookie" : "30-day stats need a fresh MakerWorld cookie");
  lv_obj_center(ptNoData);
}

static void refreshPoints() {
  char a[48], b[16], c[16];
  lv_label_set_text_fmt(ptBalance, "%.2f", st.totalPoints);
  snprintf(a, sizeof(a), "%+.1f today", st.todayPoints);
  lv_label_set_text(ptToday, a);
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
  const char* names[4] = {"IMPRESSIONS", "VIEWS", "DOWNLOADS", "PRINTS"};
  const uint32_t cols[4] = {C_DIM, 0x8A95A3, COL[S_DOWNLOADS], COL[S_PRINTS]};
  const int gap = COMPACT ? 6 : 8, chipsH = COMPACT ? 30 : (TALL ? 44 : 40);
  const int rowH = min(TALL ? 112 : 80, (TH - chipsH - 4 * gap) / 4);
  const int labW = COMPACT ? 96 : 124, valW = COMPACT ? 70 : 100, rateW = COMPACT ? 46 : 70;
  RC_TRACK_X = PAD + labW;
  RC_TRACK_W = CW - labW - valW - rateW - 20;
  for (int i = 0; i < 4; i++) {
    int y = TOP + i * (rowH + gap);
    lv_obj_t* l = label(s, F12, C_SUB, PAD, y + (rowH - lineH(F12)) / 2, names[i]);
    lv_obj_set_style_text_letter_space(l, COMPACT ? 0 : 1, 0);
    lv_obj_t* track = card(s, RC_TRACK_X, y, RC_TRACK_W, rowH);
    reach[i].fill = box(track, 0, 0, RC_TRACK_W, rowH, cols[i], 10);
    reach[i].val = label(s, F22, C_TXT, RC_TRACK_X + RC_TRACK_W + 10, y + (rowH - lineH(F22)) / 2, "");
    lv_obj_set_width(reach[i].val, valW);
    lv_obj_set_style_text_align(reach[i].val, LV_TEXT_ALIGN_RIGHT, 0);
    reach[i].rate = label(s, F12, C_MUTED, W - PAD - rateW, y + (rowH - lineH(F12)) / 2, "");
    lv_obj_set_width(reach[i].rate, rateW);
    lv_obj_set_style_text_align(reach[i].rate, LV_TEXT_ALIGN_RIGHT, 0);
  }
  lv_obj_t* chips = row(s, COMPACT ? 6 : 10);
  lv_obj_set_pos(chips, PAD, TOP + 4 * (rowH + gap) + 2);
  for (int i = 0; i < 3; i++) {
    reachChips[i] = label(chips, F14, C_TXT, 0, 0, "");
    chipStyle(reachChips[i], C_CHIP, C_TXT);
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
  lv_label_set_text_fmt(reachChips[0], COMPACT ? "Fol. %d #4FD18B (%+d)#" : "Followers %d #4FD18B (%+d)#", st.followers, m.followers);
  lv_label_set_text_fmt(reachChips[1], "Likes #FF6B8A %+d#", m.likes);
  lv_label_set_text_fmt(reachChips[2], "Collects #5AB0FF %+d#", m.saves);
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
  perPage = COMPACT ? 1 : 3;
  hint(commentHdr, INPUT_TOUCH ? "swipe up/down: more" : "press: next");
  const int w = (CW - (perPage - 1) * 10) / perPage;
  for (int i = 0; i < perPage; i++) {
    CommentUi& c = commentUi[i];
    c.card = card(s, PAD + i * (w + 10), TOP, w, TH);
    c.user = label(c.card, F14, C_SEL, 16, 14, "");
    dotLabel(c.user, w - 32 - 60);
    c.when = label(c.card, F12, C_MUTED, 0, 0, "");
    lv_obj_align(c.when, LV_ALIGN_TOP_RIGHT, -16, 16);
    c.text = label(c.card, TALL ? F20 : F16, C_TXT, 16, TALL ? 52 : 44, "");
    lv_label_set_long_mode(c.text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_line_space(c.text, TALL ? 6 : 0, 0);
    lv_obj_set_size(c.text, w - 32, TH - (TALL ? 52 : 44) - (TALL ? 48 : 40));
    c.model = label(c.card, F12, C_MUTED, 16, TH - 28, "");
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
  snprintf(a, sizeof(a), "%s total  -  page %d/%d", b, commentPage + 1, pages);
  lv_label_set_text(commentHdr.sub, a);
}

// ---- 8. Status --------------------------------------------------------------
enum { ST_POLLER, ST_COOKIE, ST_NEXT, ST_BASE, ST_NET, ST_DEVICE, NSTATUS };
struct StatusCell {
  lv_obj_t *dot, *value, *sub;
};
static StatusCell cells[NSTATUS];

static void makeStatusCell(lv_obj_t* parent, int i, int x, int y, int w, int h, const char* name, bool wide) {
  lv_obj_t* c = card(parent, x, y, w, h);
  const lv_font_t* nameF = COMPACT ? F12 : (TALL ? F14 : F12);
  const lv_font_t* valF = wide ? (TALL ? F32 : F22) : (COMPACT ? F20 : (TALL ? F22 : F20));
  const lv_font_t* subF = COMPACT ? F12 : (TALL ? F14 : F12);
  const int pad = COMPACT ? 8 : 16;
  const int nameY = COMPACT ? 6 : (TALL ? 18 : 12);
  const int dot = COMPACT ? 7 : 10;
  cells[i].dot = box(c, pad, nameY + (lineH(nameF) - dot) / 2, dot, dot, C_IDLE, LV_RADIUS_CIRCLE);
  lv_obj_t* l = label(c, nameF, C_SUB, pad + dot + 8, nameY, name);
  lv_obj_set_style_text_letter_space(l, 1, 0);
  const int subH = lineH(subF);
  const int subY = h - subH - (COMPACT ? 6 : (TALL ? 18 : 12));
  int valY = nameY + lineH(nameF) + 4;
  int room = subY - valY;
  if (room > lineH(valF)) valY += (room - lineH(valF)) / 2;
  cells[i].value = label(c, valF, C_TXT, pad, valY, "-");
  dotLabel(cells[i].value, w - 2 * pad);
  cells[i].sub = label(c, subF, C_MUTED, pad, subY, "");
  dotLabel(cells[i].sub, w - 2 * pad);
}

static void statusBtnCb(lv_event_t* e);  // defined with openStatus()

// Header chip button (touch boards). Status and About both use it.
static void headerBtn(Header& h, const char* txt, uint32_t fg, lv_event_cb_t cb, intptr_t action) {
  lv_obj_t* b = label(h.right, F14, fg, 0, 0, txt);
  chipStyle(b, C_CHIP, fg);
  lv_obj_set_style_pad_ver(b, INPUT_TOUCH ? 9 : 6, 0);
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
  const char* names[NSTATUS] = {"POLLER", "COOKIE", "NEXT POLL", "DAY BASELINE", "NETWORK", "DEVICE"};
  if (COMPACT) {
    const int gap = 6, cols = 2, rows = 3;
    const int w = (CW - (cols - 1) * gap) / cols;
    const int h = (TH - (rows - 1) * gap) / rows;
    for (int i = 0; i < NSTATUS; i++)
      makeStatusCell(s, i, PAD + (i % cols) * (w + gap), TOP + (i / cols) * (h + gap), w, h, names[i], i >= ST_NET);
    return;
  }
  // Four status metrics on top. Network and Device span two columns so the
  // IP address and hostname are not clipped.
  const int gap = TALL ? 12 : 8;
  const int colW = (CW - 3 * gap) / 4;
  const int rowH = (TH - gap) / 2;
  const int span = colW * 2 + gap;
  for (int i = 0; i < 4; i++)
    makeStatusCell(s, i, PAD + i * (colW + gap), TOP, colW, rowH, names[i], false);
  makeStatusCell(s, ST_NET, PAD, TOP + rowH + gap, span, rowH, names[ST_NET], true);
  makeStatusCell(s, ST_DEVICE, PAD + span + gap, TOP + rowH + gap, span, rowH, names[ST_DEVICE], true);
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
    setCell(ST_POLLER, C_IDLE, "Waiting", "set color_panel_host on the poller");
    setCell(ST_COOKIE, C_IDLE, "-", "");
    setCell(ST_NEXT, C_IDLE, "-", "");
    setCell(ST_BASE, C_IDLE, "-", "");
  } else {
    bool ok = !strcmp(st.auth, "ok");
    fmtAgo(b, sizeof(b), n && st.lastPoll ? (long)(n - st.lastPoll) : -1);
    snprintf(a, sizeof(a), "last poll %s ago", b);
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
    fmtHM(a, sizeof(a), st.since);
    setCell(ST_BASE, st.midnight ? C_OK : C_WARN, a, st.midnight ? "last poll before midnight" : "partial day (poller started)");
  }
  if (g_net.connected) {
    snprintf(b, sizeof(b), "%s.local  -  %s  -  %d dBm", g_net.host, g_net.ssid, g_net.rssi);
    setCell(ST_NET, g_net.rssi > -70 ? C_OK : C_WARN, g_net.ip, b);
  } else if (g_net.portal) {
    setCell(ST_NET, C_ERR, "Setup", "join " BOARD_AP);
  } else {
    snprintf(b, sizeof(b), "%s.local", g_net.host);
    setCell(ST_NET, C_ERR, "Offline", b);
  }
  fmtAgo(a, sizeof(a), millis() / 1000);
#if BAT_ADC_PIN >= 0
  float v = analogReadMilliVolts(BAT_ADC_PIN) * 2 / 1000.0f;
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

// ---- settings menu ----------------------------------------------------------
static const int NMENU = NSET + 1;  // + Exit
struct MenuUi {
  lv_obj_t *card, *value;
};
static MenuUi menuUi[NMENU];
static int menuSel = 0;

static void buildMenu() {
  menuRoot = newScreen();
  Header h = header(menuRoot, "SETTINGS", "");
#if INPUT_TOUCH
  hint(h, "tap a setting to change it  -  hold: back");
#else
  hint(h, COMPACT ? "turn / press / hold: exit" : "turn: move  -  press: change  -  hold: exit");
#endif
  // Two columns on every panel: even 11 items (audio boards) keep 60 px
  // rows on tall panels, and labels keep room next to their values.
  const int cols = 2;
  const int rows = (NMENU + cols - 1) / cols;
  const int gap = COMPACT ? 6 : 8, colW = (CW - (cols - 1) * 12) / cols, rowH = (TH - (rows - 1) * gap) / rows;
  for (int i = 0; i < NMENU; i++) {
    int col = i / rows, r = i % rows;
    MenuUi& m = menuUi[i];
    m.card = card(menuRoot, PAD + col * (colW + 12), TOP + r * (rowH + gap), colW, rowH);
#if INPUT_TOUCH
    lv_obj_add_flag(m.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(m.card, C(C_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(m.card, C(C_ACCENT), LV_STATE_PRESSED);
    lv_obj_add_event_cb(m.card, menuTapCb, LV_EVENT_SHORT_CLICKED, (void*)(intptr_t)i);
#endif
    lv_obj_set_style_border_width(m.card, 1, 0);
    lv_obj_t* l = label(m.card, F16, C_TXT, 18, 0, i < NSET ? SETTINGS[i].label : "Exit");
    dotLabel(l, colW - (COMPACT ? 112 : 136));
    lv_obj_align(l, LV_ALIGN_LEFT_MID, COMPACT ? 10 : 18, 0);
    m.value = label(m.card, F16, C_MUTED, 0, 0, "");
    dotLabel(m.value, COMPACT ? 92 : 100);
    lv_obj_set_style_text_align(m.value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(m.value, LV_ALIGN_RIGHT_MID, COMPACT ? -10 : -18, 0);
  }
}

static void refreshMenu() {
  for (int i = 0; i < NMENU; i++) {
    bool sel = i == menuSel;
    lv_obj_set_style_bg_color(menuUi[i].card, C(sel ? C_RAISED : C_CARD), 0);
    lv_obj_set_style_border_color(menuUi[i].card, C(sel ? C_SEL : C_EDGE), 0);
    lv_label_set_text(menuUi[i].value, i < NSET ? setting_name((SettingId)i) : "");
    lv_obj_set_style_text_color(menuUi[i].value, C(sel ? C_SEL : C_MUTED), 0);
    lv_obj_align(menuUi[i].value, LV_ALIGN_RIGHT_MID, COMPACT ? -10 : -18, 0);
  }
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
  if (open) {
    menuSel = 0;
    refreshMenu();
  }
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
  const int urlH = COMPACT ? 0 : lineH(TALL ? F16 : F14) + 16;
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
    setUpdStatus(settings_auto_ota() ? "Hourly GitHub check enabled  -  tap Check to query now"
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
    lv_label_set_text(updRemVer, "Not checked");
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
  updHdr = header(s, "FIRMWARE UPDATE", "Tap Check for updates to query GitHub");
  dotLabel(updHdr.sub, CW - (COMPACT ? 140 : 220));
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
  label(cInst, F12, C_MUTED, 10, COMPACT ? 6 : 10, "INSTALLED");
  lv_obj_t* iv = label(cInst, TALL ? F26 : F20, C_TXT, 10, COMPACT ? 22 : (TALL ? 36 : 28), "v" FW_VERSION);
  dotLabel(iv, cellW - 20);
  lv_obj_t* ib = label(cInst, F12, C_MUTED, 10, cellH - lineH(F12) - (COMPACT ? 5 : 10), BOARD_NAME);
  dotLabel(ib, cellW - 20);

  // GitHub remote cell
  updRemCell = box(lc, mx + cellW + (COMPACT ? 6 : 10), cellY, cellW, cellH, C_BG, COMPACT ? 4 : 6);
  lv_obj_set_style_border_width(updRemCell, 1, 0);
  lv_obj_set_style_border_color(updRemCell, C(C_EDGE), 0);
  updRemEye = label(updRemCell, F12, C_MUTED, 10, COMPACT ? 6 : 10, "GITHUB");
  updRemVer = label(updRemCell, TALL ? F26 : F20, C_TXT, 10, COMPACT ? 22 : (TALL ? 36 : 28), "Not checked");
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
    updLbl[i] = label(c, F16, C_TXT, 0, 0, LABELS[i]);
    dotLabel(updLbl[i], rightW - (COMPACT ? 78 : 108));
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
    snprintf(a, sizeof(a), "+%d %s", e.n, e.n == 1 ? KIND_NAME[k] : KIND_PLURAL[k]);
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
  bool quiet = mod >= 0 && in_quiet_hours(mod) && now - lastInput > 30000;
  int dim = setting_value(SET_DIM), bright = setting_value(SET_BRIGHTNESS);
  uint32_t idle = now - max(lastInput, lastWake);
  bool idleDim = dim && !bannerUp && idle > (uint32_t)dim * 1000;
  int level = bright;
  if (idleDim) level = max(3, bright / 10);
  if (quiet) level = min(level, 5);
  // Going dim (idle) also returns home, so waking always shows Today.
  if (idleDim && !dimmed) {
    hbOpen(false);
    if (menuOpen) openMenu(false);
    if (statusOpen) openStatus(false);
    if (aboutOpen) openAbout(false);
    if (updOpen) openUpdate(false);
    goScreen(SC_TODAY, 0);
  }
  dimmed = level < bright;
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
  refreshToday();
  refreshTodayHeader();
  refreshFeed();
  refreshTop();
  refreshTrend();
  refreshPoints();
  refreshReach();
  refreshComments();
  refreshStatus();
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
  buildStatus();
  buildMenu();
  buildAbout();
  buildUpdate();
  buildOverlay();
  buildHamburger();
  refreshAll();
  goScreen(SC_TODAY, 0);
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

void ui_set_state(const State& s) {
  st = s;
  stateMs = millis();
  refreshAll();
}

const State& ui_get_state() { return st; }

void ui_refresh_from_web() {
  applyBrightness();
  if (menuOpen) refreshMenu();
  if (statusOpen) refreshStatus();
  refreshUpdate();
}

bool ui_overlay_open() { return menuOpen || statusOpen || aboutOpen || updOpen || hbIsOpen; }

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
  const int bh = COMPACT ? max(26, TOUCH_PX - 6) : (TALL ? max(36, TOUCH_PX - 8) : 32);
  const int by = COMPACT ? 4 : (TALL ? 12 : 8);
  hbBtn = box(top, W - PAD - HB_W, by, HB_W, bh, C_CHIP, 4);
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

  const int dw = COMPACT ? 196 : (TALL ? 248 : 220), ih = COMPACT ? max(36, TOUCH_PX) : (TALL ? max(52, TOUCH_PX) : 48);
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
  bool want = !menuOpen && !statusOpen && !aboutOpen && !updOpen && !bannerUp;
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

  if (menuOpen) {
    if (in.longPress) {
      openMenu(false);
      return;
    }
    if (in.steps) {
      menuSel = ((menuSel + in.steps) % NMENU + NMENU) % NMENU;
      refreshMenu();
    }
    if (in.click) {
      if (menuSel == NSET) {
        openMenu(false);
        return;
      }
      setting_next((SettingId)menuSel);
      if (menuSel == SET_BRIGHTNESS) applyBrightness();
      if (menuSel == SET_POLL) ui_toast("Poller picks this up within 15 s");
#if AUDIO_I2S
      if (menuSel == SET_VOLUME || menuSel == SET_SOUND) sound_test();
#endif
      refreshMenu();
      refreshStatus();
    }
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
      case SC_TOP:
        topMode = (topMode + 1) % 3;
        refreshTop();
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

#if INPUT_TOUCH
static bool swiped = false;
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
    default:
      break;
  }
}

static void touchEvent(lv_event_t* e) {
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      swiped = false;
      return;
    case LV_EVENT_GESTURE: {
      lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
      swiped = true;
      lv_indev_wait_release(lv_indev_get_act());
      touchActivity();
      if (hbIsOpen) hbOpen(false);
      if (menuOpen || statusOpen || aboutOpen || updOpen) return;
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
      if (menuOpen) openMenu(false);
      else if (statusOpen) openStatus(false);
      else if (aboutOpen) openAbout(false);
      else if (updOpen) openUpdate(false);
      return;
    default:
      return;
  }
}

static void menuTapCb(lv_event_t* e) {
  menuSel = (int)(intptr_t)lv_event_get_user_data(e);
  KnobInput in;
  in.click = true;
  ui_input(in);
}
#endif

void ui_tick() {
  static uint32_t lastSec = 0;
  uint32_t now = millis();
  lv_timer_handler();

  if (bannerUp && now - bannerShownAt > (uint32_t)setting_value(SET_BANNER) * 1000) bannerNext();
  if (toastUntil && now > toastUntil) {
    toastUntil = 0;
    show(toastLbl, false);
  }
  if (now - lastSec < 1000) return;
  lastSec = now;

  refreshTodayHeader();
  if (statusOpen) refreshStatus();
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
  // Hourly bars shift as the clock moves.
  static int lastHour = -1;
  uint32_t n = nowUtc();
  int h = n ? (int)(toLocal(n) / 3600 % 24) : -1;
  if (h != lastHour) {
    lastHour = h;
    refreshToday();
  }
}
