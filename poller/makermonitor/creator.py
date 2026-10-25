"""Creator stats: per-model daily data from MakerWorld's own analytics.

Background work, all through the shared rate limiter (ratelimit.py) at low
priority, at most once every BG_EVERY seconds whatever the poll interval:

- model-stats: each model's data-overview page for the last 30 days (views,
  downloads, prints, likes, collects, boosts, followers gained, points).
  Per run: one model never fetched yet (the backfill) plus REFRESH_PER_RUN
  of the oldest fetched ones (when older than REFRESH_AFTER).
- launch: each older model's first 7 days after publishing, fetched once
  and kept for good (newer models are inside the 30-day window anyway).
- comment-scan: every comment of a model (paged) to count the ones the
  creator hasn't answered, once per model, then again only when the
  model's comment count changes.

Everything is cached in data/creator.json, so restarts don't refetch.
compute() turns the cache plus the current snapshot into the stats.
"""

from __future__ import annotations

import asyncio
import json
import logging
import random
import statistics
import threading
import time
from datetime import date, datetime, timedelta
from pathlib import Path
from typing import Any

from . import makerworld as mw
from .pages import ascii_text
from .ratelimit import LIMITER, RateLimited
from .state import Snapshot

log = logging.getLogger(__name__)

BG_EVERY = 600            # seconds between background runs
REFRESH_PER_RUN = 2       # model-stats refreshes per run (plus one backfill)
REFRESH_AFTER = 3600      # don't refetch a model sooner than this
COMMENT_SCANS_PER_RUN = 2
COMMENT_PAGE = 20
COMMENT_MAX_PAGES = 5
KEEP_DAYS = 35
STOP_AFTER_FAILS = 3      # pause backfill / launch / comment scans for an hour
OLD_DAYS = 30             # long-tail: models older than this

# dateList field -> short key in the cache
_DAY_KEYS = (("impression", "imp"), ("view", "v"), ("download", "d"), ("print", "p"), ("like", "l"),
             ("collect", "c"), ("boost", "b"), ("follower", "f"), ("pointFromModel", "pt"))


def _pub_date(m: Any) -> date | None:
    s = getattr(m, "published", "") or ""
    try:
        return datetime.fromisoformat(s.replace("Z", "+00:00")).astimezone().date()
    except ValueError:
        return None


def _days_from(model_data: dict[str, Any]) -> dict[str, dict[str, float]]:
    out: dict[str, dict[str, float]] = {}
    for row in model_data.get("dateList") or []:
        key = str(row.get("intervalVal") or "").replace("/", "-")
        if len(key) != 10:
            continue
        out[key] = {short: mw._num(row.get(src)) for src, short in _DAY_KEYS}
    return out


class CreatorStats:
    def __init__(self, data_dir: Path) -> None:
        self.path = data_dir / "creator.json"
        self._lock = threading.RLock()
        try:
            self.d: dict[str, Any] = json.loads(self.path.read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            self.d = {}
        self.d.setdefault("models", {})
        self._task: asyncio.Task | None = None
        self.health: dict[str, Any] = {"t": float(self.d.get("ok_t", 0)), "fails": 0, "err": ""}
        self._bg_fails = 0
        self._bg_fail_t = 0.0

    def _save(self) -> None:
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.d, separators=(",", ":")))
        tmp.replace(self.path)

    # ---- background fetching ----------------------------------------------

    def kick(self, client: mw.MakerWorldClient, snap: Snapshot) -> None:
        """Called after each successful poll; starts a run when one is due."""
        if not client.cookie or not snap.models:
            return
        if self._task and not self._task.done():
            return
        if time.time() - float(self.d.get("run_t", 0)) < BG_EVERY:
            return
        self._task = asyncio.create_task(self._run(client, snap), name="creator-stats")

    async def _run(self, client: mw.MakerWorldClient, snap: Snapshot) -> None:
        self.d["run_t"] = time.time()
        await asyncio.sleep(random.uniform(5, 15))  # let the core poll finish its pushes
        cache = self.d["models"]
        now = time.time()
        today = date.today()
        mids = list(snap.models)
        for mid in mids:
            cache.setdefault(mid, {})
        for gone in [k for k in cache if k not in snap.models]:
            del cache[gone]
        cooling = self._bg_fails >= STOP_AFTER_FAILS and now - self._bg_fail_t < 3600
        jobs: list[tuple[str, str]] = []
        never = [m for m in mids if not cache[m].get("t")]
        if never and not cooling:
            jobs.append(("model-stats", never[0]))
        old = sorted((m for m in mids if cache[m].get("t") and now - cache[m]["t"] > REFRESH_AFTER),
                     key=lambda m: cache[m]["t"])
        jobs += [("model-stats", m) for m in old[:REFRESH_PER_RUN]]
        if not cooling:
            for m in mids:
                pub = _pub_date(snap.models[m])
                if (pub and "launch" not in cache[m] and (today - pub).days > OLD_DAYS - 2):
                    jobs.append(("launch", m))
                    break
            scans = [m for m in mids if cache[m].get("cm", {}).get("count") != snap.models[m].comments]
            for m in scans:
                if snap.models[m].comments == 0:
                    cache[m]["cm"] = {"count": 0, "unans": 0, "times": [], "t": now}
            jobs += [("comment-scan", m) for m in scans if snap.models[m].comments][:COMMENT_SCANS_PER_RUN]
        done = 0
        for kind, mid in jobs:
            if not LIMITER.background_ok(kind):
                log.info("creator stats: stopping (%s)", "budget/backoff/core poll")
                break
            try:
                if kind == "model-stats":
                    # Whole days only: a range ending today is refused more often.
                    await self._fetch_model(client, mid, today - timedelta(days=30), today - timedelta(days=1))
                elif kind == "launch":
                    pub = _pub_date(snap.models[mid])
                    await self._fetch_launch(client, mid, pub)
                else:
                    await self._scan_comments(client, mid, snap.models[mid].comments)
                done += 1
                self._bg_fails = 0
            except RateLimited as exc:
                log.info("creator stats: %s skipped (%s)", kind, exc)
                break
            except Exception as exc:  # noqa: BLE001
                self._bg_fails += 1
                self._bg_fail_t = time.time()
                log.warning("creator stats: %s %s failed: %s", kind, mid, exc)
                if kind == "model-stats":
                    self.health.update(fails=self.health["fails"] + 1, err=str(exc)[:60])
                if self._bg_fails >= STOP_AFTER_FAILS:
                    break
            await asyncio.sleep(random.uniform(2, 5))
        with self._lock:
            self._save()
        if done:
            have = sum(1 for m in mids if cache[m].get("t"))
            log.info("creator stats: %d fetches, %d/%d models backfilled, %d MakerWorld requests last hour",
                     done, have, len(mids), LIMITER.used_last_hour())

    async def _data_overview(self, client: mw.MakerWorldClient, mid: str, start: date, end: date,
                             endpoint: str) -> dict[str, Any]:
        build = mw._BUILD[0]
        if not build:
            raise RateLimited("no build id yet")
        url = (f"{mw.BASE}/_next/data/{build}/en/my/data-overview/model/{mid}.json"
               f"?startDate={start.isoformat()}&endDate={end.isoformat()}&designId={mid}")
        async with mw._session(30.0) as s:
            r = await LIMITER.get(s, endpoint=endpoint, core=False, url=url,
                                  headers=client._headers(extra={"x-nextjs-data": "1"}))
        if r.status_code == 404:
            mw._clear_build()
        if not r.ok:
            raise RuntimeError(f"HTTP {r.status_code}")
        md = ((r.json() or {}).get("pageProps") or {}).get("modelData") or {}
        if not md.get("dateList"):
            raise RuntimeError("no dateList")
        return md

    async def _fetch_model(self, client: mw.MakerWorldClient, mid: str, start: date, end: date) -> None:
        md = await self._data_overview(client, mid, start, end, "model-stats")
        days = _days_from(md)
        today = date.today().isoformat()
        days.pop(today, None)  # MakerWorld fills a day only once it is over
        with self._lock:
            c = self.d["models"].setdefault(mid, {})
            merged = {**c.get("days", {}), **days}
            for k in sorted(merged)[:-KEEP_DAYS]:
                del merged[k]
            c["days"] = merged
            c["t"] = time.time()
            ts = md.get("trafficSource") or {}
            if ts:
                c["src"] = {k: mw._num(v) for k, v in ts.items()}
            self.d["ok_t"] = c["t"]
        self.health.update(t=time.time(), fails=0, err="")

    async def _fetch_launch(self, client: mw.MakerWorldClient, mid: str, pub: date) -> None:
        md = await self._data_overview(client, mid, pub, pub + timedelta(days=6), "launch")
        days = _days_from(md)
        cum, run = [], 0
        for i in range(7):
            run += days.get((pub + timedelta(days=i)).isoformat(), {}).get("d", 0)
            cum.append(int(run))
        with self._lock:
            self.d["models"].setdefault(mid, {})["launch"] = cum

    async def _scan_comments(self, client: mw.MakerWorldClient, mid: str, count: int) -> None:
        unans, times = 0, []
        async with mw._session(30.0) as s:
            for page in range(COMMENT_MAX_PAGES):
                url = (f"{mw.BASE}/api/v1/comment-service/comment?designId={mid}"
                       f"&offset={page * COMMENT_PAGE}&limit={COMMENT_PAGE}")
                r = await LIMITER.get(s, endpoint="comment-scan", core=False, url=url, headers=client._headers())
                if not r.ok:
                    raise RuntimeError(f"HTTP {r.status_code}")
                rows = (r.json() or {}).get("comments") or []
                for c in rows:
                    handle = str((c.get("user") or {}).get("handle") or "").lstrip("@").lower()
                    if handle == client.handle.lower():
                        continue
                    times.append(str(c.get("createTime") or ""))
                    if not c.get("replyByDesignUser"):
                        unans += 1
                if len(rows) < COMMENT_PAGE:
                    break
                await asyncio.sleep(random.uniform(2, 5))
        with self._lock:
            self.d["models"].setdefault(mid, {})["cm"] = {"count": count, "unans": unans,
                                                          "times": times[:200], "t": time.time()}

    # ---- stats ---------------------------------------------------------------

    def progress(self, snap: Snapshot) -> dict[str, int]:
        cache = self.d["models"]
        return {"have": sum(1 for m in snap.models if cache.get(m, {}).get("t")), "of": len(snap.models)}

    def compute(self, snap: Snapshot, day_view: dict[str, Any], daily_days: list[dict[str, Any]]) -> dict[str, Any]:
        """Per-model rows plus account-wide numbers."""
        with self._lock:
            cache = json.loads(json.dumps(self.d["models"]))
        today = date.today()
        yday = (today - timedelta(days=1)).isoformat()
        last7 = {(today - timedelta(days=i)).isoformat() for i in range(1, 8)}
        last30 = {(today - timedelta(days=i)).isoformat() for i in range(1, 31)}
        prior7 = {(today - timedelta(days=i)).isoformat() for i in range(2, 9)}
        base = day_view.get("base_models") or {}
        now_c = {mid: {"d": m.downloads, "p": m.prints} for mid, m in snap.models.items()}
        cutoff7 = (datetime.now() - timedelta(days=7)).timestamp()
        cutoff30 = (datetime.now() - timedelta(days=30)).timestamp()

        def tsum(days: dict[str, Any], keys: set[str], f: str) -> float:
            return sum(v.get(f, 0) for k, v in days.items() if k in keys)

        rows: dict[str, dict[str, Any]] = {}
        for mid, m in snap.models.items():
            c = cache.get(mid, {})
            days = c.get("days") or {}
            pub = _pub_date(m)
            age = (today - pub).days if pub else None
            today_d = max(0, now_c[mid]["d"] - int((base.get(mid) or {}).get("downloads", now_c[mid]["d"])))
            r: dict[str, Any] = {"id": mid, "n": ascii_text(m.name or mid)[:60], "age": age,
                                 "has": bool(c.get("t")), "fetched": int(c.get("t", 0)), "todayD": today_d,
                                 "life": {"d": m.downloads, "p": m.prints, "l": m.likes, "c": m.collects,
                                          "m": m.comments, "b": m.boosts}}
            for tag, keys in (("w", last7), ("m", last30)):
                r[tag] = {f: round(tsum(days, keys, f), 2) for _, f in _DAY_KEYS}
            cm = c.get("cm") or {}
            ctimes = [_iso_ts(t) for t in cm.get("times", [])]
            r["w"]["m"] = sum(1 for t in ctimes if t >= cutoff7)
            r["m"]["m"] = sum(1 for t in ctimes if t >= cutoff30)
            r["unans"] = int(cm.get("unans", 0)) if cm else None
            # 1. conversion
            r["dpv7"] = _ratio(r["w"]["d"], r["w"]["v"])
            r["dpv30"] = _ratio(r["m"]["d"], r["m"]["v"])
            r["ppd"] = _ratio(m.prints, m.downloads)
            r["ppd30"] = _ratio(r["m"]["p"], r["m"]["d"])
            # 2. engagement per 100 downloads
            r["e100"] = {
                "life": _per100(m.likes, m.collects, m.comments, m.downloads),
                "w": _per100(r["w"]["l"], r["w"]["c"], r["w"]["m"], r["w"]["d"]),
                "m": _per100(r["m"]["l"], r["m"]["c"], r["m"]["m"], r["m"]["d"]),
            }
            # 3. momentum: yesterday vs the 7 days before it
            if days:
                y = days.get(yday, {}).get("d", 0)
                avg = tsum(days, prior7, "d") / 7
                ratio = round(y / avg, 2) if avg else (None if not y else 9.9)
                trend = "flat"
                if ratio is not None and ratio >= 1.25:
                    trend = "up"
                elif ratio is not None and ratio <= 0.75:
                    trend = "down"
                r["mom"] = {"y": int(y), "avg": round(avg, 2), "r": ratio, "dir": trend,
                            "rising": bool(y >= 3 and (ratio or 0) >= 2),
                            "stalled": bool(avg >= 1 and y == 0 and today_d == 0)}
            # 4. launch curve: cumulative downloads on days 1..7
            r["launch"] = c.get("launch") or _launch_from_days(days, pub, today, today_d)
            # 5. points, 6. followers
            r["pts7"], r["pts30"] = r["w"]["pt"], r["m"]["pt"]
            r["fol7"], r["fol30"] = int(r["w"]["f"]), int(r["m"]["f"])
            rows[mid] = r

        acct = self._account(rows, snap, daily_days, last7, last30)
        return {"t": int(time.time()), "progress": self.progress(snap), "acct": acct, "models": rows}

    def _account(self, rows: dict[str, dict[str, Any]], snap: Snapshot, daily_days: list[dict[str, Any]],
                 last7: set[str], last30: set[str]) -> dict[str, Any]:
        have = [r for r in rows.values() if r["has"]]
        w = {f: sum(r["w"][f] for r in have) for _, f in _DAY_KEYS}
        m = {f: sum(r["m"][f] for r in have) for _, f in _DAY_KEYS}
        wm = sum(r["w"]["m"] for r in rows.values())
        mm = sum(r["m"]["m"] for r in rows.values())
        life = {k: sum(r["life"][k] for r in rows.values()) for k in ("d", "p", "l", "c", "m")}
        # 4. latest launch vs the median of earlier launches
        launches = sorted((r for r in rows.values() if r["launch"] and r["age"] is not None), key=lambda r: r["age"])
        launch = None
        if launches:
            cur = launches[0]
            past = [r["launch"] for r in launches[1:] if len(r["launch"]) == 7 and (r["age"] or 0) >= 7]
            med = [statistics.median(c[i] for c in past) for i in range(7)] if past else []
            n = len(cur["launch"])
            pct = None
            if past and n:
                at = cur["launch"][n - 1]
                pct = round(100 * sum(1 for c in past if c[n - 1] < at) / len(past))
            launch = {"id": cur["id"], "n": cur["n"], "day": n, "cur": cur["launch"], "med": med,
                      "pct": pct, "past": len(past)}
        # 5. account points per day (balance change; spending shows up as a dip)
        pts_days = [round(float(d.get("points", 0)), 1) for d in daily_days][-30:]
        a7 = sum(pts_days[-7:]) / max(1, len(pts_days[-7:]))
        p7 = pts_days[-14:-7]
        a_prev = sum(p7) / len(p7) if p7 else 0
        # 7. long tail: activity from models older than OLD_DAYS
        def split(fn) -> list[int]:
            old = new = 0
            for r in rows.values():
                v = int(fn(r))
                if r["age"] is not None and r["age"] > OLD_DAYS:
                    old += v
                else:
                    new += v
            return [old, new]
        tail = {"today": split(lambda r: r["todayD"]), "w": split(lambda r: r["w"]["d"]),
                "m": split(lambda r: r["m"]["d"])}
        unans = [r for r in rows.values() if r["unans"]]
        return {
            "dpv7": _ratio(w["d"], w["v"]), "dpv30": _ratio(m["d"], m["v"]),
            "ppd": _ratio(life["p"], life["d"]), "ppd30": _ratio(m["p"], m["d"]),
            "views7": int(w["v"]), "views30": int(m["v"]),
            "e100": {"life": _per100(life["l"], life["c"], life["m"], life["d"]),
                     "w": _per100(w["l"], w["c"], wm, w["d"]), "m": _per100(m["l"], m["c"], mm, m["d"])},
            "launch": launch,
            "ptsDays": pts_days, "pts7avg": round(a7, 1), "ptsPrev7avg": round(a_prev, 1),
            "ptsTrend": _trend(a7, a_prev),
            "pts7": round(w["pt"], 1), "pts30": round(m["pt"], 1),
            "fol7": int(w["f"]), "fol30": int(m["f"]),
            "tail": tail,
            "unans": sum(r["unans"] for r in unans),
            "unansScanned": sum(1 for r in rows.values() if r["unans"] is not None),
        }

    def compact(self, full: dict[str, Any], n: int = 4) -> dict[str, Any]:
        """Small block for the panels' state: top-N lists only."""
        rows = list(full["models"].values())
        a = full["acct"]
        nm = lambda r: r["n"][:60]  # noqa: E731

        def top(key, rows_, fn, cond=lambda r: True) -> list[dict[str, Any]]:
            sel = sorted((r for r in rows_ if cond(r)), key=key, reverse=True)[:n]
            return [{"id": r["id"], "n": nm(r), **fn(r)} for r in sel]

        has = [r for r in rows if r["has"]]
        out = {
            "p": full["progress"],
            "conv": top(lambda r: r["dpv30"] or 0, has, lambda r: {"v": r["dpv30"], "d": int(r["m"]["d"])},
                        lambda r: r["m"]["v"] >= 30 and r["dpv30"] is not None),
            "eng": top(lambda r: r["e100"]["m"]["all"] or 0, has,
                       lambda r: {"v": r["e100"]["m"]["all"], "d": int(r["m"]["d"])},
                       lambda r: r["m"]["d"] >= 5 and r["e100"]["m"]["all"] is not None),
            "rise": top(lambda r: (r["mom"]["r"] or 0, r["mom"]["y"]), [r for r in has if r.get("mom")],
                        lambda r: {"y": r["mom"]["y"], "a": r["mom"]["avg"], "r": r["mom"]["r"]},
                        lambda r: r["mom"]["dir"] == "up"),
            "stall": top(lambda r: r["mom"]["avg"], [r for r in has if r.get("mom")],
                         lambda r: {"a": r["mom"]["avg"]}, lambda r: r["mom"]["stalled"]),
            "pts": top(lambda r: r["pts30"], has, lambda r: {"w": round(r["pts7"], 1), "m": round(r["pts30"], 1)},
                       lambda r: r["pts30"] > 0),
            "fol": top(lambda r: (r["fol30"], r["fol7"]), has, lambda r: {"w": r["fol7"], "m": r["fol30"]},
                       lambda r: r["fol30"] > 0),
            "unans": a["unans"],
            "unansTop": top(lambda r: r["unans"] or 0, rows, lambda r: {"u": r["unans"]}, lambda r: bool(r["unans"])),
            "tail": a["tail"],
            "acct": {"dpv7": a["dpv7"], "dpv30": a["dpv30"], "ppd": a["ppd"], "e100": a["e100"]["m"]["all"],
                     "pts7": a["pts7"], "pts30": a["pts30"], "fol7": a["fol7"], "fol30": a["fol30"],
                     "pa7": a["pts7avg"], "p7": round(sum(a["ptsDays"][-7:]), 1), "pt": a["ptsTrend"],
                     "pd": a["ptsDays"][-14:]},
        }
        la = a.get("launch")
        if la:
            out["launch"] = {"n": la["n"][:60], "id": la["id"], "day": la["day"], "cur": la["cur"],
                             "med": [round(x, 1) for x in la["med"]], "pct": la["pct"], "past": la["past"]}
        return out

    def model_stats(self, full: dict[str, Any], mid: str) -> dict[str, Any] | None:
        r = full["models"].get(mid)
        if not r:
            return None
        c = self.d["models"].get(mid, {})
        days = sorted((c.get("days") or {}).items())[-30:]
        return {**{k: r[k] for k in ("dpv7", "dpv30", "ppd", "ppd30", "e100", "launch", "pts7", "pts30",
                                     "fol7", "fol30", "unans", "age", "has", "fetched")},
                "mom": r.get("mom"), "src": c.get("src"),
                "mwDays": [{"d": k, "v": int(v.get("v", 0)), "dl": int(v.get("d", 0)), "f": int(v.get("f", 0)),
                            "pt": round(v.get("pt", 0), 1)} for k, v in days]}


def _iso_ts(s: str) -> float:
    try:
        return datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp()
    except ValueError:
        return 0.0


def _ratio(a: float, b: float) -> float | None:
    return round(a / b, 4) if b else None


def _per100(likes: float, collects: float, comments: float, downloads: float) -> dict[str, float | None]:
    if not downloads:
        return {"l": None, "c": None, "m": None, "all": None}
    f = 100.0 / downloads
    return {"l": round(likes * f, 1), "c": round(collects * f, 1), "m": round(comments * f, 1),
            "all": round((likes + collects + comments) * f, 1)}


def _trend(a: float, b: float) -> str:
    if not b:
        return "up" if a > 0 else "flat"
    r = a / b
    return "up" if r >= 1.15 else "down" if r <= 0.85 else "flat"


def _launch_from_days(days: dict[str, Any], pub: date | None, today: date, today_d: int) -> list[int]:
    """Days 1..n of a launch still inside the 30-day window (n <= 7)."""
    if not pub or not days or (today - pub).days > 29:
        return []
    cum, run = [], 0
    for i in range(7):
        dt = pub + timedelta(days=i)
        if dt > today:
            break
        run += today_d if dt == today else days.get(dt.isoformat(), {}).get("d", 0)
        cum.append(int(run))
    return cum
