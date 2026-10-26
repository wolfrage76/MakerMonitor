#pragma once
#include <stddef.h>
#include <stdint.h>

// The five daily stats, in display order. Comments are kind 5 in events,
// star ratings kind 6.
enum Stat { S_BOOSTS, S_LIKES, S_COLLECTS, S_DOWNLOADS, S_PRINTS, NSTAT };
static const int K_COMMENT = 5;
static const int K_RATING = 6;
static const int NKIND = 7;

extern const char* const STAT_KEYS[NSTAT];   // JSON keys: "boosts", ...
extern const char* const KIND_NAME[NKIND];   // "Boost", ..., "Comment"
extern const char* const KIND_PLURAL[NKIND];
extern const char* const KIND_UPPER[NKIND];  // "BOOSTS", ...
int kind_from_key(const char* k);            // -1 if unknown

struct DayRow {
  int v[NSTAT];
  float points;
  char d[6];
};

struct TopRow {
  char id[16];     // MakerWorld design id (for the model detail page)
  char name[64];
  int v[NSTAT];
  int score;
};

struct FeedRow {
  int8_t kind;
  int n;
  float stars;     // ratings: average stars of the new ones, 0 = unknown
  char model[64];
  uint32_t t;
};

struct CommentRow {
  char user[33];
  char text[161];
  char model[64];
  char when[12];
};

// Follower count and gains; series: gains per day, oldest first, ending today.
struct Followers {
  bool has;
  int total, today, d7, d30, n;
  int16_t series[30];
};

struct Month {
  bool has;
  long impressions, views, ydayImpressions, ydayViews;
  int followers, likes, saves, downloads, prints, boosts;
  float points, pointsModels, pointsProfiles;
  char start[12], end[12];
};

// Poller health: last good fetch per source and the cookie state.
enum HealthSrc { H_MODELS, H_REACH, H_POINTS, NHSRC };
struct Health {
  bool has;
  bool stale;          // models older than 3x the poll interval
  bool expired;        // cookie rejected by MakerWorld
  uint32_t since;      // last good models fetch (UTC)
  uint32_t srcT[NHSRC];
  uint8_t srcFails[NHSRC];
};

// Best times over the last 30 days, per metric (downloads, prints, boosts).
static const int NBEST = 3;
struct Insights {
  bool has;            // at least 7 days collected
  uint8_t days;
  uint16_t hour[NBEST][24];
  uint16_t wday[NBEST][7];  // Mon = 0
  int8_t bestHour[NBEST], bestDay[NBEST];
};

// Creator stats (poller "creator" block): top-4 lists plus account numbers.
enum CList { CL_CONV, CL_ENG, CL_RISE, CL_STALL, CL_PTS, CL_FOL, CL_UNANS, CL_RATED, NCLIST };
struct CRow {
  char id[16];
  char name[61];
  float a, b;      // list-specific values (see parse_state)
};
struct Creator {
  bool has;
  int have, of;    // models with MakerWorld analytics so far / all
  int n[NCLIST];
  CRow rows[NCLIST][4];
  int unans;
  int tail[3][2];  // today, 7d, 30d: downloads from models older / newer than 30 days
  float dpv7, dpv30, ppd, e100, pts7, pts30, pa7;  // -1 = no data
  float acct7;     // account points gained over the last 7 days (balance change)
  int fol7, fol30;
  int rt;          // ratings on all models
  float ra;        // their average stars, -1 = none
  int8_t ptTrend;  // 1 up, 0 flat, -1 down
  bool hasLaunch;
  char lname[61];
  int lday, lpct, lpast;  // lpct -1 = no earlier launches
  int lcur[7];
  float lmed[7];
  int nmed;
};

struct State {
  bool valid;
  uint32_t t;      // poller time, UTC epoch
  int32_t tz;      // poller UTC offset, seconds
  char account[32];
  int today[NSTAT];
  float todayPoints;
  bool hasYday;
  int yday[NSTAT];
  float ydayPoints;
  uint32_t since;
  bool midnight;   // baseline came from before midnight (full day)
  int total[NSTAT];
  float totalPoints;
  int followers, commentCount, models;
  int todayRatings, ydayRatings, totalRatings;
  bool hasYdayRatings;
  float ratingAvg;   // average stars over all ratings, 0 = none
  uint16_t hours[NSTAT][24];
  int ndays;
  DayRow days[7];
  int ntop[3];     // 0 today, 1 week, 2 lifetime
  TopRow top[3][4];
  int nfeed;
  FeedRow feed[8];
  Month month;
  Followers fol;
  int ncomments;
  CommentRow comments[9];
  char auth[10];
  char msg[64];
  int32_t cookieAge;
  uint32_t lastPoll;
  bool pollOk;
  int pollSec;
  Health health;
  Insights insights;
  char poller[48];   // http://host:port, for lazy fetches (model detail)
  Creator creator;
};

struct Event {
  int8_t kind;
  int n;
  float stars;     // ratings: average stars of the new ones
  char model[64];
  char user[33];
  char text[121];
};

bool parse_state(const char* json, size_t len, State& out);
bool parse_event(const char* json, size_t len, Event& out);
