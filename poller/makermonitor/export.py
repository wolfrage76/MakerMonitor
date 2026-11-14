"""CSV export of the stored analytics, for the dashboard's Export tab.

Datasets (what the poller actually keeps):

- account: one row per day for the whole account, from data/daily.json
  (poller-tracked, about 30 days kept) plus today so far. Views and
  impressions are summed from MakerWorld's per-model analytics where those
  days have been fetched.
- hourly: activity per hour of day (data/daily.json hourly buckets, about
  35 days kept, plus today so far).
- models: one row per model per day. MakerWorld's own analytics
  (data/creator.json, about 35 days, complete days only) where fetched,
  filled in from the poller's per-model day baselines (about 31 days,
  includes today) for the days MakerWorld hasn't filled yet.
- lifetime: each model's current all-time totals from the last poll (no
  date range; includes comments, ratings and publish date).

Granularity "total" sums the chosen range into one row (account) or one row
per model (models).
"""

from __future__ import annotations

import csv
import io
from datetime import date, datetime, timedelta
from typing import Any, Callable

from fastapi import Cookie, FastAPI, Header, HTTPException, Query
from fastapi.responses import JSONResponse, Response

from .daily import STATS

RANGES = ("today", "7d", "30d", "90d", "this_month", "last_month", "all", "custom")
DATASETS = ("account", "hourly", "models", "lifetime")

# metric -> (column header, key in daily.json days, key in creator.json days)
METRICS: dict[str, tuple[str, str | None, str | None]] = {
    "impressions": ("impressions", None, "imp"),
    "views": ("views", None, "v"),
    "downloads": ("downloads", "downloads", "d"),
    "prints": ("prints", "prints", "p"),
    "likes": ("likes", "likes", "l"),
    "collects": ("collects", "collects", "c"),
    "boosts": ("boosts", "boosts", "b"),
    "points": ("points", "points", "pt"),
    "followers": ("followers", "followers", "f"),
    "ratings": ("ratings", "ratings", None),
    "comments": ("comments", None, None),
}

# Metrics each dataset can give, in column order.
DATASET_METRICS: dict[str, tuple[str, ...]] = {
    "account": ("impressions", "views", "downloads", "prints", "likes", "collects", "boosts",
                "points", "followers", "ratings"),
    "hourly": STATS,
    "models": ("impressions", "views", "downloads", "prints", "likes", "collects", "boosts",
               "points", "followers"),
    "lifetime": ("downloads", "prints", "likes", "collects", "boosts", "comments", "ratings"),
}
GRANULARITY: dict[str, tuple[str, ...]] = {
    "account": ("daily", "total"),
    "hourly": ("hourly",),
    "models": ("daily", "total"),
    "lifetime": ("total",),
}


def resolve_range(rng: str, start: str | None, end: str | None,
                  today: date | None = None) -> tuple[date | None, date | None]:
    """(start, end), inclusive, local days. None means open-ended ("all")."""
    t = today or date.today()
    if rng == "today":
        return t, t
    if rng in ("7d", "30d", "90d"):
        return t - timedelta(days=int(rng[:-1]) - 1), t
    if rng == "this_month":
        return t.replace(day=1), t
    if rng == "last_month":
        last = t.replace(day=1) - timedelta(days=1)
        return last.replace(day=1), last
    if rng == "all":
        return None, None
    if rng == "custom":
        try:
            a = date.fromisoformat(start) if start else None
            b = date.fromisoformat(end) if end else None
        except ValueError:
            raise HTTPException(400, "start and end must be YYYY-MM-DD") from None
        if a and b and a > b:
            a, b = b, a
        return a, b
    raise HTTPException(400, f"range must be one of {', '.join(RANGES)}")


def _in(d: str, a: date | None, b: date | None) -> bool:
    return (a is None or d >= a.isoformat()) and (b is None or d <= b.isoformat())


def _num(v: Any, metric: str) -> float | int:
    if metric == "points":
        return round(float(v or 0), 2)
    return int(round(float(v or 0)))


def _text(s: Any) -> str:
    """Model names are free text: keep spreadsheets from reading them as formulas."""
    s = str(s or "")
    return "'" + s if s[:1] in ("=", "+", "-", "@", "\t", "\r") else s


def _hour12(h: int) -> str:
    return f"{h % 12 or 12}:00 {'AM' if h < 12 else 'PM'}"


class Exporter:
    """Builds rows from the poller's live state (DayTracker, CreatorStats, Store)."""

    def __init__(self, ctx: Any) -> None:
        self.ctx = ctx

    # ---- sources -------------------------------------------------------------

    def _daily(self) -> dict[str, Any]:
        day = self.ctx.day
        with day._lock:
            d = day.d
            return {
                "days": [dict(x) for x in d.get("days", [])],
                "day": d.get("day"),
                "today": day.today(),
                "hour_days": [dict(x) for x in d.get("hour_days", [])],
                "hours": dict(d.get("hours") or {}),
                "model_bases": {k: dict(v) for k, v in d.get("model_bases", {}).items()},
                "last_models": dict(d.get("last_models") or {}),
            }

    def _creator_days(self) -> dict[str, dict[str, dict[str, float]]]:
        cr = self.ctx.creator
        with cr._lock:
            return {mid: dict(m.get("days") or {}) for mid, m in cr.d.get("models", {}).items()}

    def _names(self) -> dict[str, Any]:
        snap = self.ctx.store.status.last_snapshot
        return dict(snap.models) if snap else {}

    def _poller_model_days(self, dly: dict[str, Any]) -> dict[str, dict[str, dict[str, int]]]:
        """model id -> date -> {stat: n}, from the day baselines (same as /v2/model)."""
        mb, last = dly["model_bases"], dly["last_models"]
        dates = sorted(mb)
        out: dict[str, dict[str, dict[str, int]]] = {}
        for i, dt in enumerate(dates):
            nxt = mb[dates[i + 1]] if i + 1 < len(dates) else last
            for mid, a in mb[dt].items():
                b = nxt.get(mid)
                if a and b:
                    out.setdefault(mid, {})[dt] = {k: max(0, int(b.get(k, 0)) - int(a.get(k, 0))) for k in STATS}
        return out

    # ---- meta ----------------------------------------------------------------

    def meta(self) -> dict[str, Any]:
        dly = self._daily()
        acct_dates = [x["date"] for x in dly["days"]] + ([dly["day"]] if dly["day"] else [])
        hour_dates = [x["date"] for x in dly["hour_days"]] + ([dly["day"]] if dly["day"] else [])
        cdays = self._creator_days()
        pdays = self._poller_model_days(dly)
        mdates = {dt for m in cdays.values() for dt in m} | {dt for m in pdays.values() for dt in m}
        names = self._names()
        ids = sorted(set(names) | set(cdays) | set(pdays),
                     key=lambda i: (names[i].name.lower() if i in names else "~", i))

        def span(ds):
            ds = sorted(ds)
            return {"from": ds[0], "to": ds[-1], "days": len(ds)} if ds else None

        return {
            "today": date.today().isoformat(),
            "ranges": list(RANGES),
            "datasets": {
                "account": {"span": span(acct_dates), "metrics": list(DATASET_METRICS["account"]),
                            "granularity": list(GRANULARITY["account"])},
                "hourly": {"span": span(hour_dates), "metrics": list(DATASET_METRICS["hourly"]),
                           "granularity": list(GRANULARITY["hourly"])},
                "models": {"span": span(mdates), "metrics": list(DATASET_METRICS["models"]),
                           "granularity": list(GRANULARITY["models"])},
                "lifetime": {"span": None, "metrics": list(DATASET_METRICS["lifetime"]),
                             "granularity": list(GRANULARITY["lifetime"])},
            },
            "models": [{"id": i, "name": names[i].name if i in names else ""} for i in ids],
        }

    # ---- rows ----------------------------------------------------------------

    def rows(self, dataset: str, a: date | None, b: date | None, metrics: list[str],
             models: set[str] | None, gran: str) -> tuple[list[str], list[list[Any]]]:
        if dataset == "account":
            return self._account(a, b, metrics, gran)
        if dataset == "hourly":
            return self._hourly(a, b, metrics)
        if dataset == "models":
            return self._models(a, b, metrics, models, gran)
        return self._lifetime(metrics, models)

    def _account(self, a, b, metrics, gran):
        dly = self._daily()
        days = {x["date"]: x for x in dly["days"]}
        if dly["day"]:
            days[dly["day"]] = {**dly["today"], "date": dly["day"], "_partial": True}
        # Views / impressions: sum of MakerWorld's per-model days.
        mw: dict[str, dict[str, float]] = {}
        if {"views", "impressions"} & set(metrics):
            for m in self._creator_days().values():
                for dt, r in m.items():
                    acc = mw.setdefault(dt, {"v": 0.0, "imp": 0.0})
                    acc["v"] += float(r.get("v", 0))
                    acc["imp"] += float(r.get("imp", 0))

        def val(dt: str, row: dict[str, Any], m: str):
            _, dk, ck = METRICS[m]
            if dk is None:
                return _num(mw[dt][ck], m) if dt in mw else ""
            return _num(row.get(dk, 0), m)

        dates = sorted(dt for dt in days if _in(dt, a, b))
        heads = [METRICS[m][0] for m in metrics]
        if gran == "total":
            out = []
            for m in metrics:
                vals = [val(dt, days[dt], m) for dt in dates]
                nums = [v for v in vals if v != ""]
                out.append(_num(sum(nums), m) if nums else "")
            return (["start", "end", "days", *heads],
                    [[dates[0] if dates else "", dates[-1] if dates else "", len(dates), *out]])
        rows = [[dt, "yes" if days[dt].get("_partial") else "", *(val(dt, days[dt], m) for m in metrics)]
                for dt in dates]
        return ["date", "partial", *heads], rows

    def _hourly(self, a, b, metrics):
        dly = self._daily()
        days = {x["date"]: (x.get("hours") or {}) for x in dly["hour_days"]}
        if dly["day"]:
            days[dly["day"]] = dly["hours"]
        rows = []
        for dt in sorted(d for d in days if _in(d, a, b)):
            h = days[dt]
            for i in range(24):
                rows.append([dt, _hour12(i), "yes" if dt == dly["day"] else "",
                             *(int(((h.get(m) or [0] * 24) + [0] * 24)[i]) for m in metrics)])
        return ["date", "hour", "partial", *metrics], rows

    def _models(self, a, b, metrics, models, gran):
        dly = self._daily()
        cdays, pdays, names = self._creator_days(), self._poller_model_days(dly), self._names()
        ids = sorted(set(cdays) | set(pdays))
        if models:
            ids = [i for i in ids if i in models]
        recs: list[tuple[str, str, str, dict[str, Any]]] = []  # (id, date, source, values)
        for mid in ids:
            c, p = cdays.get(mid, {}), pdays.get(mid, {})
            for dt in sorted(set(c) | set(p)):
                if not _in(dt, a, b):
                    continue
                if dt in c:
                    vals = {m: _num(c[dt].get(METRICS[m][2], 0), m) for m in metrics}
                    recs.append((mid, dt, "makerworld", vals))
                else:
                    vals = {m: (_num(p[dt].get(METRICS[m][1], 0), m) if METRICS[m][1] in STATS else "")
                            for m in metrics}
                    recs.append((mid, dt, "poller", vals))

        def nm(mid):
            return _text(names[mid].name if mid in names else "")

        heads = [METRICS[m][0] for m in metrics]
        if gran == "total":
            agg: dict[str, dict[str, Any]] = {}
            for mid, dt, _, vals in recs:
                g = agg.setdefault(mid, {"start": dt, "end": dt, "n": 0, "v": {m: [] for m in metrics}})
                g["start"], g["end"], g["n"] = min(g["start"], dt), max(g["end"], dt), g["n"] + 1
                for m in metrics:
                    if vals[m] != "":
                        g["v"][m].append(vals[m])
            rows = []
            for mid, g in agg.items():
                rows.append([mid, nm(mid), g["start"], g["end"], g["n"],
                             *((_num(sum(g["v"][m]), m) if g["v"][m] else "") for m in metrics)])
            rows.sort(key=lambda r: (-(r[5 + metrics.index("downloads")] or 0) if "downloads" in metrics else 0,
                                     str(r[1]).lower()))
            return ["model_id", "model_name", "start", "end", "days", *heads], rows
        today = dly["day"]
        rows = [[dt, mid, nm(mid), src, "yes" if dt == today else "", *(vals[m] for m in metrics)]
                for mid, dt, src, vals in recs]
        rows.sort(key=lambda r: (r[0], str(r[2]).lower()))
        return ["date", "model_id", "model_name", "source", "partial", *heads], rows

    def _lifetime(self, metrics, models):
        snap = self.ctx.store.status.last_snapshot
        rows = []
        for mid, m in (snap.models.items() if snap else []):
            if models and mid not in models:
                continue
            vals = [int(getattr(m, k, 0)) for k in metrics]
            avg = round(m.rating_sum / m.ratings, 2) if m.ratings else ""
            rows.append([mid, _text(m.name), (m.published or "")[:10], *vals, avg])
        rows.sort(key=lambda r: str(r[1]).lower())
        asof = ""
        if snap and snap.fetched_at:
            t = datetime.fromtimestamp(snap.fetched_at)
            asof = f"{t:%Y-%m-%d} {t.hour % 12 or 12}:{t:%M} {'AM' if t.hour < 12 else 'PM'}"
        for r in rows:
            r.append(asof)
        return ["model_id", "model_name", "published", *metrics, "avg_rating", "as_of"], rows


def to_csv(head: list[str], rows: list[list[Any]]) -> str:
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\r\n")
    w.writerow(head)
    w.writerows(rows)
    return buf.getvalue()


def register(app: FastAPI, ctx: Any, dash_ok: Callable[[str | None, str | None], bool]) -> None:
    exp = Exporter(ctx)

    def auth(token, mm_token, authorization, x_api_token) -> None:
        if authorization and authorization.lower().startswith("bearer "):
            token = authorization.split(" ", 1)[1].strip()
        elif x_api_token:
            token = x_api_token.strip()
        if not dash_ok(token, mm_token):
            raise HTTPException(401, "Unauthorized")

    @app.get("/api/export/meta")
    async def export_meta(token: str | None = None, mm_token: str | None = Cookie(default=None),
                          authorization: str | None = Header(default=None),
                          x_api_token: str | None = Header(default=None, alias="X-Api-Token")) -> JSONResponse:
        auth(token, mm_token, authorization, x_api_token)
        return JSONResponse(exp.meta())

    @app.get("/api/export.csv")
    async def export_csv(
        dataset: str = "account",
        range: str = Query("30d"),  # noqa: A002
        start: str | None = None,
        end: str | None = None,
        metrics: str | None = None,
        models: str | None = None,
        granularity: str | None = None,
        token: str | None = None,
        mm_token: str | None = Cookie(default=None),
        authorization: str | None = Header(default=None),
        x_api_token: str | None = Header(default=None, alias="X-Api-Token"),
    ) -> Response:
        auth(token, mm_token, authorization, x_api_token)
        if dataset not in DATASETS:
            raise HTTPException(400, f"dataset must be one of {', '.join(DATASETS)}")
        avail = DATASET_METRICS[dataset]
        if metrics:
            want = [m.strip().lower() for m in metrics.split(",") if m.strip()]
            bad = [m for m in want if m not in avail]
            if bad:
                raise HTTPException(400, f"{dataset} has no {', '.join(bad)}; choose from {', '.join(avail)}")
            mets = [m for m in avail if m in want]  # keep a stable column order
        else:
            mets = list(avail)
        gran = granularity or GRANULARITY[dataset][0]
        if gran not in GRANULARITY[dataset]:
            raise HTTPException(400, f"{dataset} granularity must be one of {', '.join(GRANULARITY[dataset])}")
        a, b = (None, None) if dataset == "lifetime" else resolve_range(range, start, end)
        ids = {m.strip() for m in models.split(",") if m.strip()} if models else None
        head, rows = exp.rows(dataset, a, b, mets, ids, gran)
        if dataset == "lifetime":
            span = date.today().isoformat()
        else:
            if gran != "total":
                dates = sorted({r[0] for r in rows})
            else:  # total rows carry their own start / end columns
                i = 2 if dataset == "models" else 0
                dates = sorted({x for r in rows for x in r[i:i + 2] if x})
            lo = a.isoformat() if a else (dates[0] if dates else "start")
            hi = b.isoformat() if b else (dates[-1] if dates else "end")
            span = lo if lo == hi else f"{lo}_to_{hi}"
        suffix = "-total" if gran == "total" and dataset != "lifetime" else ""
        fname = f"makermonitor-{dataset}{suffix}-{span}.csv"
        # BOM so Excel opens model names (UTF-8) correctly.
        body = "﻿" + to_csv(head, rows)
        return Response(body, media_type="text/csv; charset=utf-8",
                        headers={"Content-Disposition": f'attachment; filename="{fname}"',
                                 "Cache-Control": "no-store"})
