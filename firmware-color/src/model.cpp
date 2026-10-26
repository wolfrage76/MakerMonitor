#include "model.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <string.h>

// The state document is ~20 KB of nodes; keep it out of internal RAM.
struct PsramAllocator : ArduinoJson::Allocator {
  void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void* p) override { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};
static PsramAllocator psram;

const char* const STAT_KEYS[NSTAT] = {"boosts", "likes", "collects", "downloads", "prints"};
const char* const KIND_NAME[NKIND] = {"Boost", "Like", "Collect", "Download", "Print", "Comment", "Rating"};
const char* const KIND_PLURAL[NKIND] = {"Boosts", "Likes", "Collects", "Downloads", "Prints", "Comments", "Ratings"};
const char* const KIND_UPPER[NKIND] = {"BOOSTS", "LIKES", "COLLECTS", "DOWNLOADS", "PRINTS", "COMMENTS", "RATINGS"};

int kind_from_key(const char* k) {
  if (!k) return -1;
  for (int i = 0; i < NSTAT; i++)
    if (!strcmp(k, STAT_KEYS[i])) return i;
  if (!strcmp(k, "comment")) return K_COMMENT;
  if (!strcmp(k, "ratings")) return K_RATING;
  return -1;
}

static void copyStr(char* dst, size_t cap, const char* src) {
  strlcpy(dst, src ? src : "", cap);
}

static void readStats(JsonVariantConst o, int* v) {
  for (int i = 0; i < NSTAT; i++) v[i] = o[STAT_KEYS[i]] | 0;
}

static void readTop(JsonArrayConst arr, TopRow* rows, int& n) {
  n = 0;
  for (JsonObjectConst r : arr) {
    if (n >= 4) break;
    TopRow& t = rows[n++];
    copyStr(t.id, sizeof(t.id), r["id"]);
    copyStr(t.name, sizeof(t.name), r["n"]);
    t.v[S_BOOSTS] = r["b"] | 0;
    t.v[S_LIKES] = r["l"] | 0;
    t.v[S_COLLECTS] = r["c"] | 0;
    t.v[S_DOWNLOADS] = r["d"] | 0;
    t.v[S_PRINTS] = r["p"] | 0;
    t.score = r["s"] | 0;
  }
}

bool parse_state(const char* json, size_t len, State& s) {
  JsonDocument doc(&psram);
  if (deserializeJson(doc, json, len)) return false;
  if ((doc["v"] | 0) != 1) return false;

  memset(&s, 0, sizeof(s));
  s.t = doc["t"] | 0;
  s.tz = doc["tz"] | 0;
  copyStr(s.account, sizeof(s.account), doc["account"]);
  readStats(doc["today"].as<JsonVariantConst>(), s.today);
  s.todayPoints = doc["today"]["points"] | 0.0f;
  s.todayRatings = doc["today"]["ratings"] | 0;
  s.hasYday = doc["yday"].is<JsonObjectConst>();
  if (s.hasYday) {
    readStats(doc["yday"].as<JsonVariantConst>(), s.yday);
    s.ydayPoints = doc["yday"]["points"] | 0.0f;
    // Days closed before ratings were tracked have no count.
    s.hasYdayRatings = doc["yday"]["ratings"].is<int>();
    s.ydayRatings = doc["yday"]["ratings"] | 0;
  }
  s.since = doc["since"] | 0;
  s.midnight = doc["midnight"] | false;

  JsonObjectConst tot = doc["total"].as<JsonObjectConst>();
  readStats(tot, s.total);
  s.totalPoints = tot["points"] | 0.0f;
  s.followers = tot["followers"] | 0;
  s.commentCount = tot["comments"] | 0;
  s.models = tot["models"] | 0;
  s.totalRatings = tot["ratings"] | 0;
  s.ratingAvg = tot["ratingAvg"] | 0.0f;

  for (int i = 0; i < NSTAT; i++) {
    JsonArrayConst h = doc["hours"][STAT_KEYS[i]].as<JsonArrayConst>();
    for (int j = 0; j < 24; j++) s.hours[i][j] = h[j] | 0;
  }

  for (JsonObjectConst d : doc["days"].as<JsonArrayConst>()) {
    if (s.ndays >= 7) break;
    DayRow& r = s.days[s.ndays++];
    readStats(d, r.v);
    r.points = d["points"] | 0.0f;
    copyStr(r.d, sizeof(r.d), d["d"]);
  }

  readTop(doc["top"]["today"].as<JsonArrayConst>(), s.top[0], s.ntop[0]);
  readTop(doc["top"]["week"].as<JsonArrayConst>(), s.top[1], s.ntop[1]);
  readTop(doc["top"]["life"].as<JsonArrayConst>(), s.top[2], s.ntop[2]);

  for (JsonObjectConst e : doc["feed"].as<JsonArrayConst>()) {
    if (s.nfeed >= 8) break;
    int k = kind_from_key(e["k"]);
    if (k < 0) continue;
    FeedRow& f = s.feed[s.nfeed++];
    f.kind = k;
    f.n = e["n"] | 1;
    f.stars = e["s"] | 0.0f;
    copyStr(f.model, sizeof(f.model), e["m"]);
    f.t = e["t"] | 0;
  }

  JsonObjectConst fo = doc["followers"].as<JsonObjectConst>();
  s.fol.has = !fo.isNull();
  if (s.fol.has) {
    s.fol.total = fo["total"] | 0;
    s.fol.today = fo["today"] | 0;
    s.fol.d7 = fo["d7"] | 0;
    s.fol.d30 = fo["d30"] | 0;
    s.fol.n = 0;
    for (JsonVariantConst v : fo["series30"].as<JsonArrayConst>())
      if (s.fol.n < 30) s.fol.series[s.fol.n++] = v | 0;
  }

  JsonObjectConst m = doc["month"].as<JsonObjectConst>();
  s.month.has = !m.isNull() && m["impressions"].is<long>();
  if (s.month.has) {
    s.month.impressions = m["impressions"] | 0L;
    s.month.views = m["views"] | 0L;
    s.month.ydayImpressions = m["yday_impressions"] | 0L;
    s.month.ydayViews = m["yday_views"] | 0L;
    s.month.followers = m["followers"] | 0;
    s.month.likes = m["likes"] | 0;
    s.month.saves = m["saves"] | 0;
    s.month.downloads = m["downloads"] | 0;
    s.month.prints = m["prints"] | 0;
    s.month.boosts = m["boosts"] | 0;
    s.month.points = m["points"] | 0.0f;
    s.month.pointsModels = m["points_models"] | 0.0f;
    s.month.pointsProfiles = m["points_profiles"] | 0.0f;
    copyStr(s.month.start, sizeof(s.month.start), m["start"]);
    copyStr(s.month.end, sizeof(s.month.end), m["end"]);
  }

  for (JsonObjectConst c : doc["comments"].as<JsonArrayConst>()) {
    if (s.ncomments >= 9) break;
    CommentRow& r = s.comments[s.ncomments++];
    copyStr(r.user, sizeof(r.user), c["u"]);
    copyStr(r.text, sizeof(r.text), c["x"]);
    copyStr(r.model, sizeof(r.model), c["m"]);
    copyStr(r.when, sizeof(r.when), c["t"]);  // ISO time, trimmed to the date
  }

  JsonObjectConst st = doc["status"].as<JsonObjectConst>();
  copyStr(s.auth, sizeof(s.auth), st["auth"] | "ok");
  copyStr(s.msg, sizeof(s.msg), st["msg"]);
  s.cookieAge = st["cookieAge"] | -1;
  s.lastPoll = st["lastPoll"] | 0;
  s.pollOk = st["ok"] | true;
  s.pollSec = st["pollSec"] | 600;

  JsonObjectConst h = doc["health"].as<JsonObjectConst>();
  s.health.has = !h.isNull();
  if (s.health.has) {
    s.health.stale = h["stale"] | false;
    s.health.expired = !strcmp(h["auth"] | "", "expired");
    s.health.since = h["since"] | 0;
    static const char* const SRC[NHSRC] = {"models", "reach", "points"};
    for (int i = 0; i < NHSRC; i++) {
      s.health.srcT[i] = h["src"][SRC[i]]["t"] | 0;
      s.health.srcFails[i] = h["src"][SRC[i]]["f"] | 0;
    }
  }
  JsonObjectConst ins = doc["insights"].as<JsonObjectConst>();
  s.insights.has = ins["has"] | false;
  s.insights.days = ins["days"] | 0;
  static const char* const BEST[NBEST] = {"downloads", "prints", "boosts"};
  for (int m = 0; m < NBEST; m++) {
    JsonObjectConst b = ins[BEST[m]].as<JsonObjectConst>();
    for (int i = 0; i < 24; i++) s.insights.hour[m][i] = b["hour"][i] | 0;
    for (int i = 0; i < 7; i++) s.insights.wday[m][i] = b["wday"][i] | 0;
    s.insights.bestHour[m] = b["bestHour"] | -1;
    s.insights.bestDay[m] = b["bestDay"] | -1;
  }
  copyStr(s.poller, sizeof(s.poller), doc["poller"]);

  JsonObjectConst cr = doc["creator"].as<JsonObjectConst>();
  Creator& c = s.creator;
  c.has = !cr.isNull();
  if (c.has) {
    c.have = cr["p"]["have"] | 0;
    c.of = cr["p"]["of"] | 0;
    // Per list: the two values each row shows.
    static const char* const LIST[NCLIST] = {"conv", "eng", "rise", "stall", "pts", "fol", "unansTop", "rated"};
    static const char* const KA[NCLIST] = {"v", "v", "r", "a", "w", "w", "u", "a"};
    static const char* const KB[NCLIST] = {"d", "d", "y", "a", "m", "m", "u", "c"};
    for (int l = 0; l < NCLIST; l++) {
      c.n[l] = 0;
      for (JsonObjectConst r : cr[LIST[l]].as<JsonArrayConst>()) {
        if (c.n[l] >= 4) break;
        CRow& row = c.rows[l][c.n[l]++];
        copyStr(row.id, sizeof(row.id), r["id"]);
        copyStr(row.name, sizeof(row.name), r["n"]);
        row.a = r[KA[l]] | 0.0f;
        row.b = r[KB[l]] | 0.0f;
      }
    }
    c.unans = cr["unans"] | 0;
    static const char* const TAIL[3] = {"today", "w", "m"};
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 2; j++) c.tail[i][j] = cr["tail"][TAIL[i]][j] | 0;
    JsonObjectConst a = cr["acct"].as<JsonObjectConst>();
    c.dpv7 = a["dpv7"] | -1.0f;
    c.dpv30 = a["dpv30"] | -1.0f;
    c.ppd = a["ppd"] | -1.0f;
    c.e100 = a["e100"] | -1.0f;
    c.pts7 = a["pts7"] | 0.0f;
    c.pts30 = a["pts30"] | 0.0f;
    c.pa7 = a["pa7"] | 0.0f;
    c.acct7 = a["p7"] | (c.pa7 * 7);
    c.fol7 = a["fol7"] | 0;
    c.fol30 = a["fol30"] | 0;
    c.rt = a["rt"] | 0;
    c.ra = a["ra"] | -1.0f;
    const char* pt = a["pt"] | "flat";
    c.ptTrend = !strcmp(pt, "up") ? 1 : !strcmp(pt, "down") ? -1 : 0;
    JsonObjectConst la = cr["launch"].as<JsonObjectConst>();
    c.hasLaunch = !la.isNull();
    if (c.hasLaunch) {
      copyStr(c.lname, sizeof(c.lname), la["n"]);
      c.lday = la["day"] | 0;
      c.lpct = la["pct"] | -1;
      c.lpast = la["past"] | 0;
      for (int i = 0; i < 7; i++) c.lcur[i] = la["cur"][i] | -1;
      c.nmed = la["med"].size();
      for (int i = 0; i < 7; i++) c.lmed[i] = la["med"][i] | 0.0f;
    }
  }
  s.valid = true;
  return true;
}

bool parse_event(const char* json, size_t len, Event& e) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  int k = kind_from_key(doc["k"]);
  if (k < 0) return false;
  memset(&e, 0, sizeof(e));
  e.kind = k;
  e.n = doc["n"] | 1;
  e.stars = doc["s"] | 0.0f;
  copyStr(e.model, sizeof(e.model), doc["m"]);
  copyStr(e.user, sizeof(e.user), doc["u"]);
  copyStr(e.text, sizeof(e.text), doc["x"]);
  return true;
}
