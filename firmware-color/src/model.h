#pragma once
#include <stddef.h>
#include <stdint.h>

// The five daily stats, in display order. Comments are kind 5 in events.
enum Stat { S_BOOSTS, S_LIKES, S_COLLECTS, S_DOWNLOADS, S_PRINTS, NSTAT };
static const int K_COMMENT = 5;
static const int NKIND = 6;

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
  char name[64];
  int v[NSTAT];
  int score;
};

struct FeedRow {
  int8_t kind;
  int n;
  char model[64];
  uint32_t t;
};

struct CommentRow {
  char user[33];
  char text[161];
  char model[64];
  char when[12];
};

struct Month {
  bool has;
  long impressions, views, ydayImpressions, ydayViews;
  int followers, likes, saves, downloads, prints, boosts;
  float points, pointsModels, pointsProfiles;
  char start[12], end[12];
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
  uint32_t since;
  bool midnight;   // baseline came from before midnight (full day)
  int total[NSTAT];
  float totalPoints;
  int followers, commentCount, models;
  uint16_t hours[NSTAT][24];
  int ndays;
  DayRow days[7];
  int ntop[3];     // 0 today, 1 week, 2 lifetime
  TopRow top[3][4];
  int nfeed;
  FeedRow feed[8];
  Month month;
  int ncomments;
  CommentRow comments[9];
  char auth[10];
  char msg[64];
  int32_t cookieAge;
  uint32_t lastPoll;
  bool pollOk;
  int pollSec;
};

struct Event {
  int8_t kind;
  int n;
  char model[64];
  char user[33];
  char text[121];
};

bool parse_state(const char* json, size_t len, State& out);
bool parse_event(const char* json, size_t len, Event& out);
