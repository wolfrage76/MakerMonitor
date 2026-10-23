#include "model.h"

#include <ArduinoJson.h>
#include <string.h>

const char* const STAT_KEYS[NSTAT] = {"boosts", "likes", "collects", "downloads", "prints"};
const char* const KIND_NAME[NKIND] = {"Boost", "Like", "Collect", "Download", "Print", "Comment"};
const char* const KIND_PLURAL[NKIND] = {"Boosts", "Likes", "Collects", "Downloads", "Prints", "Comments"};
const char* const KIND_UPPER[NKIND] = {"BOOSTS", "LIKES", "COLLECTS", "DOWNLOADS", "PRINTS", "COMMENTS"};

int kind_from_key(const char* k) {
  if (!k) return -1;
  for (int i = 0; i < NSTAT; i++)
    if (!strcmp(k, STAT_KEYS[i])) return i;
  if (!strcmp(k, "comment")) return K_COMMENT;
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
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) return false;
  if ((doc["v"] | 0) != 1) return false;

  memset(&s, 0, sizeof(s));
  s.t = doc["t"] | 0;
  s.tz = doc["tz"] | 0;
  copyStr(s.account, sizeof(s.account), doc["account"]);
  readStats(doc["today"].as<JsonVariantConst>(), s.today);
  s.todayPoints = doc["today"]["points"] | 0.0f;
  s.hasYday = doc["yday"].is<JsonObjectConst>();
  if (s.hasYday) readStats(doc["yday"].as<JsonVariantConst>(), s.yday);
  s.since = doc["since"] | 0;
  s.midnight = doc["midnight"] | false;

  JsonObjectConst tot = doc["total"].as<JsonObjectConst>();
  readStats(tot, s.total);
  s.totalPoints = tot["points"] | 0.0f;
  s.followers = tot["followers"] | 0;
  s.commentCount = tot["comments"] | 0;
  s.models = tot["models"] | 0;

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
    copyStr(f.model, sizeof(f.model), e["m"]);
    f.t = e["t"] | 0;
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
  copyStr(e.model, sizeof(e.model), doc["m"]);
  copyStr(e.user, sizeof(e.user), doc["u"]);
  copyStr(e.text, sizeof(e.text), doc["x"]);
  return true;
}
