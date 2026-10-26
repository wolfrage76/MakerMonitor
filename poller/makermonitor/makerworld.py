from __future__ import annotations

import json
import logging
import re
import time
from datetime import date, timedelta
from typing import Any

from curl_cffi.requests import AsyncSession

from .ratelimit import LIMITER, RateLimited
from .state import ModelStats, Snapshot

log = logging.getLogger(__name__)

BASE = "https://makerworld.com"
LIST_PAGE = f"{BASE}/en/my/data-overview/model"


class AuthError(Exception):
    pass


def _session(timeout: float) -> AsyncSession:
    # Cloudflare blocks Python's default TLS fingerprint; impersonate Chrome.
    return AsyncSession(impersonate="chrome", timeout=timeout, allow_redirects=True)


class MakerWorldClient:
    def __init__(self, handle: str, user_agent: str, cookie: str = "") -> None:
        self.handle = handle.lstrip("@")
        self.user_agent = user_agent
        self.cookie = cookie.strip()
        # Per-source outcome of the last fetch: "ok", "auth" (401/403) or an error.
        self.results: dict[str, str] = {}

    def with_cookie(self, cookie: str) -> MakerWorldClient:
        return MakerWorldClient(self.handle, self.user_agent, cookie)

    def _headers(self, accept: str = "application/json", extra: dict[str, str] | None = None) -> dict[str, str]:
        h = {
            "User-Agent": self.user_agent,
            "Accept": accept,
            "Accept-Language": "en-US,en;q=0.9",
            "Referer": f"{BASE}/en/@{self.handle}",
        }
        if self.cookie:
            h["Cookie"] = self.cookie
        if extra:
            h.update(extra)
        return h

    async def fetch_snapshot(self, prev: Snapshot | None = None) -> Snapshot:
        """Best-effort snapshot. Raises AuthError when cookie is required and invalid."""
        errors: list[str] = []

        # 0) Published-designs API: per-model counters summed into creator totals
        try:
            snap = await self._from_design_api(prev)
            snap.fetched_at = time.time()
            self.results["models"] = "ok"
            return snap
        except AuthError:
            self.results["models"] = "auth"
            raise
        except Exception as exc:  # noqa: BLE001
            self.results["models"] = str(exc)[:80] or "error"
            errors.append(f"design_api:{exc}")
            log.warning("Design API fetch failed: %s", exc)
            # With full data already, the profile page can't stand in for it (no
            # per-model numbers, totals that don't match): fail this poll instead
            # of spending more requests on a page that would be thrown away.
            if prev is not None and prev.models:
                raise RuntimeError(f"design API: {exc}")

        # 1) Public (or cookie-enhanced) profile page via __NEXT_DATA__
        try:
            snap = await self._from_profile_page()
            if snap.downloads or snap.prints or snap.boosts or snap.models:
                # Enrich points if cookie present
                if self.cookie:
                    try:
                        pts = await self._points_from_creator_center()
                        if pts is not None:
                            snap.points = pts
                            snap.source = f"{snap.source}+creator_points"
                    except AuthError:
                        raise
                    except Exception as exc:  # noqa: BLE001
                        log.info("Creator Center points skipped: %s", exc)
                snap.fetched_at = time.time()
                return snap
        except AuthError:
            raise
        except Exception as exc:  # noqa: BLE001
            errors.append(f"profile:{exc}")
            log.warning("Profile scrape failed: %s", exc)

        # 2) Creator Center list (cookie required) — date-range analytics, not always lifetime
        if self.cookie:
            try:
                snap = await self._from_creator_center()
                snap.fetched_at = time.time()
                return snap
            except AuthError:
                raise
            except Exception as exc:  # noqa: BLE001
                errors.append(f"creator:{exc}")
                log.warning("Creator Center scrape failed: %s", exc)

        if self.cookie:
            # Cookie present but everything failed — often expired / CF
            raise AuthError("; ".join(errors) or "Authenticated fetch failed")
        raise RuntimeError("; ".join(errors) or "Could not fetch MakerWorld stats")

    async def _from_design_api(self, prev: Snapshot | None = None) -> Snapshot:
        url = f"{BASE}/en/@{self.handle}"
        async with _session(30.0) as client:
            r = await LIMITER.get(client, endpoint="profile", core=True, url=url, headers=self._headers("text/html"))
            if r.status_code in (401, 403):
                raise AuthError(f"Profile HTTP {r.status_code}")
            r.raise_for_status()
            nd = _extract_next_data(r.text)
            user = (((nd or {}).get("props") or {}).get("pageProps") or {}).get("userInfo") or {}
            uid = user.get("uid")
            if not uid:
                raise RuntimeError("uid not found on profile page")

            models: dict[str, ModelStats] = {}
            offset, limit = 0, 50
            while True:
                r = await LIMITER.get(client, endpoint="models", core=True, url=
                    f"{BASE}/api/v1/design-service/published/{uid}/design"
                    f"?handle=@{self.handle}&limit={limit}&offset={offset}",
                    headers=self._headers(),
                )
                if r.status_code in (401, 403):
                    raise AuthError(f"design list HTTP {r.status_code}")
                r.raise_for_status()
                data = r.json() or {}
                hits = data.get("hits") or []
                for h in hits:
                    mid = str(h.get("id") or "")
                    if not mid:
                        continue
                    models[mid] = ModelStats(
                        name=str(h.get("title") or mid),
                        downloads=int(_num(h.get("downloadCount"))),
                        prints=int(_num(h.get("printCount"))),
                        boosts=int(_num(h.get("boostCnt"))),
                        likes=int(_num(h.get("likeCount"))),
                        comments=int(_num(h.get("commentCount"))),
                        collects=int(_num(h.get("collectionCount"))),
                        published=str(h.get("publishTime") or h.get("createTime") or ""),
                        ratings=sum(int(_num(i.get("ratingCount"))) for i in h.get("instances") or []),
                        rating_sum=sum(int(_num(i.get("ratingScoreTotal"))) for i in h.get("instances") or []),
                    )
                offset += len(hits)
                if not hits or offset >= int(_num(data.get("total"))):
                    break

        snap = Snapshot(
            account_name=str(user.get("handle") or user.get("name") or self.handle),
            downloads=sum(m.downloads for m in models.values()),
            prints=sum(m.prints for m in models.values()),
            boosts=sum(m.boosts for m in models.values()),
            points=_num(user.get("point")),
            models=models,
            source="design_api",
        )
        snap.extras["account"] = {
            "followers": int(_num(user.get("fanCount"))),
            "likes": sum(m.likes for m in models.values()),
            "saves": sum(m.collects for m in models.values()),
            "comments": sum(m.comments for m in models.values()),
            "models": len(models),
        }
        try:
            snap.extras["comments"] = await self._recent_comments(models, prev)
        except Exception as exc:  # noqa: BLE001
            log.info("comments skipped: %s", exc)
            if prev:
                snap.extras["comments"] = prev.extras.get("comments") or []
        if self.cookie:
            try:
                bal = await self._points_balance()
                if bal is not None:
                    snap.points = bal
                    snap.source = "design_api+point_bill"
                self.results["points"] = "ok" if bal is not None else "no data"
            except AuthError as exc:
                log.warning("point balance rejected (%s); counts still update", exc)
                self.results["points"] = "auth"
            except Exception as exc:  # noqa: BLE001
                log.info("point balance skipped: %s", exc)
                self.results["points"] = str(exc)[:80] or "error"
            # MakerWorld's data-overview page fails now and then (403, 502, empty
            # summary). A rejected cookie there must not throw away the model
            # counts already fetched. Retry once, then keep the last good report.
            month = None
            for attempt in range(2):
                try:
                    month = await self._last_30_days()
                except AuthError as exc:
                    log.warning("30-day stats rejected (%s); counts still update", exc)
                    self.results["reach"] = "auth"
                    break
                except Exception as exc:  # noqa: BLE001
                    log.info("30-day stats attempt %d failed: %s", attempt + 1, exc)
                    self.results["reach"] = str(exc)[:80] or "error"
                if month:
                    break
            if month:
                self.results["reach"] = "ok"
                snap.extras["month"] = month
            elif prev and prev.extras.get("month"):
                snap.extras["month"] = prev.extras["month"]
                log.info("30-day stats unavailable; keeping the previous report")
        return snap

    async def _recent_comments(
        self, models: dict[str, ModelStats], prev: Snapshot | None, keep: int = 8
    ) -> list[dict[str, Any]]:
        """Newest comments across all models. Refetch only models whose count changed."""
        cached: dict[str, list[dict[str, Any]]] = {}
        for c in (prev.extras.get("comments") if prev else None) or []:
            cached.setdefault(str(c.get("designId")), []).append(c)
        out: list[dict[str, Any]] = []
        async with _session(30.0) as client:
            for mid, m in models.items():
                if not m.comments:
                    continue
                old = prev.models.get(mid) if prev else None
                if old and old.comments == m.comments and mid in cached:
                    out.extend(cached[mid])
                    continue
                r = await LIMITER.get(client, endpoint="comments", core=True, url=
                    f"{BASE}/api/v1/comment-service/comment?designId={mid}&offset=0&limit=3",
                    headers=self._headers(),
                )
                if not r.ok:
                    out.extend(cached.get(mid, []))
                    continue
                for c in (r.json() or {}).get("comments") or []:
                    user = c.get("user") or {}
                    if str(user.get("handle") or "").lstrip("@").lower() == self.handle.lower():
                        continue
                    out.append(
                        {
                            "id": c.get("id"),
                            "designId": mid,
                            "model": m.name,
                            "user": user.get("name") or user.get("handle") or "",
                            "text": c.get("content") or "",
                            "time": c.get("createTime") or "",
                        }
                    )
        out.sort(key=lambda c: c.get("time") or "", reverse=True)
        return out[:keep]

    async def _last_30_days(self) -> dict[str, Any] | None:
        build_id, _ = await self._get_build_id()
        end = date.today()
        start = end - timedelta(days=29)
        url = (
            f"{BASE}/_next/data/{build_id}/en/my/data-overview/model.json"
            f"?startDate={start.isoformat()}&endDate={end.isoformat()}"
        )
        async with _session(45.0) as client:
            r = await LIMITER.get(client, endpoint="reach", core=True, url=url, headers=self._headers(extra={"x-nextjs-data": "1"}))
            if r.status_code in (401, 403):
                raise AuthError(f"data-overview HTTP {r.status_code}")
            if r.status_code == 404:
                _clear_build()
            if not r.ok:
                raise RuntimeError(f"data-overview HTTP {r.status_code}")
            page = (r.json() or {}).get("pageProps") or {}
        stats = page.get("statisticalData") or {}
        s = stats.get("summary") or {}
        if not s:
            raise RuntimeError("data-overview returned no summary")
        # MakerWorld only reports whole days and today stays empty until it ends.
        yday_key = (end - timedelta(days=1)).strftime("%Y/%m/%d")
        yday = next((d for d in stats.get("dateList") or [] if d.get("intervalVal") == yday_key), {})
        # Followers gained per whole day, oldest first (today is never in it).
        daily = sorted((d for d in stats.get("dateList") or [] if d.get("intervalVal")),
                       key=lambda d: d["intervalVal"])
        return {
            "followers_daily": [int(_num(d.get("follower"))) for d in daily if d["intervalVal"] < end.strftime("%Y/%m/%d")],
            "yesterday": {
                "impressions": int(_num(yday.get("impression"))),
                "views": int(_num(yday.get("view"))),
            },
            "start": start.isoformat(),
            "end": end.isoformat(),
            "impressions": int(_num(s.get("impression"))),
            "views": int(_num(s.get("view"))),
            "followers": int(_num(s.get("follower"))),
            "likes": int(_num(s.get("like"))),
            "saves": int(_num(s.get("collect"))),
            "downloads": int(_num(s.get("download"))),
            "prints": int(_num(s.get("print"))),
            "boosts": int(_num(s.get("boost"))),
            "points": round(_num(s.get("pointRegular")) + _num(s.get("pointExclusive")), 2),
            "points_models": _num(s.get("pointFromModel")),
            "points_profiles": _num(s.get("pointFromInst")),
        }

    async def _points_balance(self) -> float | None:
        """Current spendable balance = lifetime income - lifetime expense."""
        url = f"{BASE}/api/v1/point-service/point-bill/my?offset=0&limit=1&filter=all"
        async with _session(30.0) as client:
            r = await LIMITER.get(client, endpoint="points", core=True, url=url, headers=self._headers())
            if r.status_code in (401, 403):
                raise AuthError(f"point-bill HTTP {r.status_code}")
            if not r.ok:
                return None
            d = r.json() or {}
        keys = ("totalRegularIncome", "totalRegularExpense", "totalExclusiveIncome", "totalExclusiveExpense")
        if not all(k in d for k in keys):
            return None
        return round(
            _num(d["totalRegularIncome"]) - _num(d["totalRegularExpense"])
            + _num(d["totalExclusiveIncome"]) - _num(d["totalExclusiveExpense"]),
            2,
        )

    async def _from_profile_page(self) -> Snapshot:
        url = f"{BASE}/en/@{self.handle}"
        async with _session(30.0) as client:
            r = await LIMITER.get(client, endpoint="profile", core=True, url=url, headers=self._headers("text/html"))
            if r.status_code in (401, 403):
                raise AuthError(f"Profile HTTP {r.status_code}")
            if "/sign" in str(r.url) or "/login" in str(r.url):
                raise AuthError("Redirected to login")
            r.raise_for_status()
            html = r.text

        nd = _extract_next_data(html)
        if not nd:
            raise RuntimeError("No __NEXT_DATA__ on profile page")

        snap = Snapshot(account_name=self.handle, source="profile_next_data")
        _walk_extract(nd, snap)

        # Fallback: regex crude totals from HTML if walk found nothing
        if not (snap.downloads or snap.prints or snap.boosts):
            _regex_totals(html, snap)
            if snap.downloads or snap.prints or snap.boosts:
                snap.source = "profile_regex"

        return snap

    async def _from_creator_center(self) -> Snapshot:
        build_id, html = await self._get_build_id()
        end = date.today()
        start = end - timedelta(days=3650)
        url = (
            f"{BASE}/_next/data/{build_id}/en/my/data-overview/model.json"
            f"?startDate={start.isoformat()}&endDate={end.isoformat()}"
        )
        async with _session(45.0) as client:
            r = await LIMITER.get(client, endpoint="reach", core=True, url=
                url,
                headers=self._headers(extra={"x-nextjs-data": "1"}),
            )
            if r.status_code in (401, 403):
                raise AuthError(f"Creator Center HTTP {r.status_code}")
            r.raise_for_status()
            payload = r.json()

        page = payload.get("pageProps") or {}
        models_raw = page.get("statisticalList") or []
        snap = Snapshot(account_name=self.handle, source="creator_center")
        models: dict[str, ModelStats] = {}
        dl = pr = bst = 0
        for item in models_raw:
            if not isinstance(item, dict):
                continue
            mid = str(item.get("designId") or item.get("id") or "")
            name = str(item.get("title") or item.get("name") or mid)
            # List endpoint often lacks per-metric lifetime; keep names for later detail.
            models[mid] = ModelStats(name=name)
        snap.models = models

        stats = page.get("statisticalData") or {}
        summary = stats.get("summary") if isinstance(stats, dict) else {}
        if isinstance(summary, dict):
            # These are period points/boosts, not lifetime DL — still useful for points.
            regular = _num(summary.get("pointRegular"))
            exclusive = _num(summary.get("pointExclusive"))
            snap.points = regular + exclusive
            snap.boosts = int(_num(summary.get("boostRegular")) + _num(summary.get("boostExclusive")))

        # Try to enrich lifetime totals from the HTML fallback next data
        nd = _extract_next_data(html)
        if nd:
            tmp = Snapshot()
            _walk_extract(nd, tmp)
            if tmp.downloads:
                snap.downloads = tmp.downloads
            if tmp.prints:
                snap.prints = tmp.prints
            if tmp.boosts:
                snap.boosts = tmp.boosts

        # Optional: point bill for a clearer points total
        try:
            pts = await self._points_from_bill()
            if pts is not None:
                snap.points = pts
        except Exception as exc:  # noqa: BLE001
            log.debug("point-bill skipped: %s", exc)

        # Fetch a few model details for per-model counters (capped)
        for mid in list(models.keys())[:10]:
            try:
                detail = await self._model_detail(build_id, int(mid), start, end)
                s = detail.get("summary") or {}
                m = models[mid]
                m.downloads = int(_num(s.get("download")))
                m.prints = int(_num(s.get("print")))
                m.boosts = int(_num(s.get("boost")))
                dl += m.downloads
                pr += m.prints
                bst += m.boosts
            except Exception as exc:  # noqa: BLE001
                log.debug("model %s detail failed: %s", mid, exc)

        if dl or pr or bst:
            snap.downloads = dl
            snap.prints = pr
            snap.boosts = bst
            snap.source = "creator_center_details"

        return snap

    async def _points_from_creator_center(self) -> float | None:
        build_id, _ = await self._get_build_id()
        end = date.today()
        start = end - timedelta(days=3650)
        url = (
            f"{BASE}/_next/data/{build_id}/en/my/data-overview/model.json"
            f"?startDate={start.isoformat()}&endDate={end.isoformat()}"
        )
        async with _session(45.0) as client:
            r = await LIMITER.get(client, endpoint="reach", core=True, url=url, headers=self._headers(extra={"x-nextjs-data": "1"}))
            if r.status_code in (401, 403):
                raise AuthError(f"points HTTP {r.status_code}")
            if not r.ok:
                return None
            page = (r.json() or {}).get("pageProps") or {}
            summary = ((page.get("statisticalData") or {}).get("summary") or {})
            return _num(summary.get("pointRegular")) + _num(summary.get("pointExclusive"))

    async def _points_from_bill(self) -> float | None:
        url = f"{BASE}/api/v1/point-service/point-bill/my?offset=0&limit=1&filter=all"
        async with _session(30.0) as client:
            r = await LIMITER.get(client, endpoint="points", core=True, url=url, headers=self._headers())
            if r.status_code in (401, 403):
                raise AuthError(f"point-bill HTTP {r.status_code}")
            if not r.ok:
                return None
            data = r.json()
            # Shapes vary; try common fields
            for key in ("total", "balance", "point", "points", "currentPoint"):
                if key in data:
                    return _num(data[key])
            if isinstance(data.get("data"), dict):
                for key in ("total", "balance", "point", "points"):
                    if key in data["data"]:
                        return _num(data["data"][key])
        return None

    async def _get_build_id(self, fresh: bool = False) -> tuple[str, str]:
        """Next.js build id for the _next/data routes. Cached for 6 h (one less
        request per poll); a 404 on a data route clears it."""
        global _BUILD
        if not fresh and _BUILD[0] and time.time() - _BUILD[2] < 6 * 3600:
            return _BUILD[0], _BUILD[1]
        async with _session(30.0) as client:
            r = await LIMITER.get(client, endpoint="build", core=True, url=LIST_PAGE, headers=self._headers("text/html"))
            if r.status_code in (401, 403):
                raise AuthError(f"data-overview HTTP {r.status_code}")
            if "/sign" in str(r.url) or "/login" in str(r.url):
                raise AuthError("Redirected to login")
            r.raise_for_status()
            html = r.text
        m = re.search(r'"buildId":"([^"]+)"', html)
        if not m:
            raise RuntimeError("buildId not found — MakerWorld layout may have changed")
        _BUILD = (m.group(1), html, time.time())
        return m.group(1), html

    async def _model_detail(
        self, build_id: str, design_id: int, start: date, end: date
    ) -> dict[str, Any]:
        url = (
            f"{BASE}/_next/data/{build_id}/en/my/data-overview/model/{design_id}.json"
            f"?startDate={start.isoformat()}&endDate={end.isoformat()}&designId={design_id}"
        )
        async with _session(30.0) as client:
            r = await LIMITER.get(client, endpoint="model-detail", core=True, url=url, headers=self._headers(extra={"x-nextjs-data": "1"}))
            r.raise_for_status()
            return ((r.json() or {}).get("pageProps") or {}).get("modelData") or {}


_BUILD: tuple[str, str, float] = ("", "", 0.0)


def _clear_build() -> None:
    global _BUILD
    _BUILD = ("", "", 0.0)


def _num(v: Any) -> float:
    if isinstance(v, (int, float)):
        return float(v)
    if isinstance(v, str):
        s = v.strip().lower().replace(",", "")
        if s.endswith("k"):
            try:
                return float(s[:-1]) * 1000
            except ValueError:
                return 0.0
        try:
            return float(s)
        except ValueError:
            return 0.0
    return 0.0


def _extract_next_data(html: str) -> dict[str, Any] | None:
    m = re.search(
        r'<script id="__NEXT_DATA__"[^>]*>(.*?)</script>',
        html,
        re.DOTALL,
    )
    if not m:
        return None
    try:
        return json.loads(m.group(1))
    except json.JSONDecodeError:
        return None


def _regex_totals(html: str, snap: Snapshot) -> None:
    # Very loose fallbacks — prefer __NEXT_DATA__ walk.
    patterns = {
        "downloads": r'"downloadCount"\s*:\s*(\d+)',
        "prints": r'"printCount"\s*:\s*(\d+)',
        "boosts": r'"boostCount"\s*:\s*(\d+)',
        "points": r'"point(?:s|Count)?"\s*:\s*([0-9.]+)',
    }
    for attr, pat in patterns.items():
        m = re.search(pat, html)
        if m:
            setattr(snap, attr, float(m.group(1)) if attr == "points" else int(m.group(1)))


def _walk_extract(node: Any, snap: Snapshot, depth: int = 0) -> None:
    """Recursively pull known keys from nested Next.js props."""
    if depth > 12 or node is None:
        return
    if isinstance(node, dict):
        # User / profile aggregate fields
        mapping = [
            ("downloadCount", "downloads"),
            ("totalDownloadCount", "downloads"),
            ("printCount", "prints"),
            ("totalPrintCount", "prints"),
            ("boostCount", "boosts"),
            ("totalBoostCount", "boosts"),
            ("likeCount", None),
            ("point", "points"),
            ("points", "points"),
            ("rewardPoint", "points"),
        ]
        for src, dst in mapping:
            if src in node and dst:
                val = _num(node[src])
                if dst == "points":
                    if val and val > snap.points:
                        snap.points = val
                else:
                    iv = int(val)
                    if iv and iv > getattr(snap, dst):
                        setattr(snap, dst, iv)

        # Model cards
        if "designId" in node and ("downloadCount" in node or "download" in node):
            mid = str(node.get("designId") or node.get("id") or "")
            if mid:
                name = str(node.get("title") or node.get("name") or mid)
                snap.models[mid] = ModelStats(
                    name=name,
                    downloads=int(_num(node.get("downloadCount", node.get("download")))),
                    prints=int(_num(node.get("printCount", node.get("print")))),
                    boosts=int(_num(node.get("boostCount", node.get("boost")))),
                )

        name = node.get("name") or node.get("userName") or node.get("handle")
        if isinstance(name, str) and name and not snap.account_name:
            snap.account_name = name.lstrip("@")

        for v in node.values():
            _walk_extract(v, snap, depth + 1)
    elif isinstance(node, list):
        for item in node[:200]:
            _walk_extract(item, snap, depth + 1)
