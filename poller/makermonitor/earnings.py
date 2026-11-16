"""MakerWorld earnings estimate (dashboard).

Follows the PrintNexus MakerWorld earnings calculator:
  weighted uses = downloads + 2 x successful prints
  model milestones   1-50: 15 per 10   51-500: 12 per 25   501-1000: 20 per 50   1001+: 30 per 100
  profile milestones 1-50:  3 per 10   51-500:  3 per 25   501-1000:  5 per 50   1001+:  8 per 100
  only complete milestones count; exclusive models x1.25 on milestone points
  boosts: 12 points (15 on an exclusive model), never multiplied
  model reward adjustment (default 1.37x, seen 1.0-1.5x) applies to model milestone points only

Profile milestones are per print profile. Where MakerWorld's design list gives
per-profile counts they are used; otherwise each model counts as one profile
with the model's totals. Likes and comments earn nothing here. It is an
estimate: MakerWorld can delay or reject rewards.

Settings (adjustment, $ per point, models marked exclusive) live in
data/earnings.json and are edited from the dashboard.
"""
from __future__ import annotations

import json
import logging
import os
from pathlib import Path
from typing import Any

log = logging.getLogger(__name__)

# (upper bound of the tier in weighted uses, uses per milestone, points per milestone)
MODEL_TIERS = ((50, 10, 15), (500, 25, 12), (1000, 50, 20), (None, 100, 30))
PROFILE_TIERS = ((50, 10, 3), (500, 25, 3), (1000, 50, 5), (None, 100, 8))
EXCLUSIVE_X = 1.25
BOOST_PTS, BOOST_PTS_EXCL = 12, 15
LOW_X, HIGH_X = 1.0, 1.5
DEFAULTS = {"adjust": 1.37, "usd_per_point": round(40 / 490, 4), "exclusive": []}


def uses(downloads: int, prints: int) -> int:
    return max(0, int(downloads or 0)) + 2 * max(0, int(prints or 0))


def milestone_points(u: int, tiers=MODEL_TIERS) -> int:
    """Points from complete milestones only."""
    total, lo = 0, 0
    for hi, step, pts in tiers:
        if u <= lo:
            break
        top = u if hi is None else min(u, hi)
        total += ((top - lo) // step) * pts
        if hi is None:
            break
        lo = hi
    return total


def next_milestone(u: int, tiers=MODEL_TIERS) -> tuple[int, int]:
    """(weighted uses still needed, points it pays) for the next milestone."""
    lo = 0
    for hi, step, pts in tiers:
        if hi is None or u < hi:
            nxt = lo + ((max(u, lo) - lo) // step + 1) * step
            return nxt - u, pts
        lo = hi
    return 0, 0  # unreachable


class EarningsSettings:
    def __init__(self, data_path: Path) -> None:
        self.path = Path(data_path) / "earnings.json"
        self.values = dict(DEFAULTS)
        try:
            raw = json.loads(self.path.read_text())
            self.values.update(self._clean(raw))
        except FileNotFoundError:
            pass
        except Exception as exc:  # noqa: BLE001
            log.warning("earnings.json unreadable, using defaults: %s", exc)

    @staticmethod
    def _clean(body: dict[str, Any]) -> dict[str, Any]:
        out: dict[str, Any] = {}
        if "adjust" in body:
            a = float(body["adjust"])
            if not 0 <= a <= 3:
                raise ValueError("Reward adjustment must be between 0 and 3")
            out["adjust"] = round(a, 3)
        if "usd_per_point" in body:
            v = float(body["usd_per_point"])
            if not 0 <= v <= 10:
                raise ValueError("Value per point must be between 0 and 10")
            out["usd_per_point"] = round(v, 5)
        if "exclusive" in body:
            ex = body["exclusive"]
            if not isinstance(ex, list):
                raise ValueError("exclusive must be a list of model ids")
            out["exclusive"] = sorted({str(x) for x in ex if str(x).strip()})[:500]
        return out

    def update(self, body: dict[str, Any]) -> dict[str, Any]:
        self.values.update(self._clean(body or {}))
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.values, indent=1))
        os.replace(tmp, self.path)
        return dict(self.values)


def estimate(snap: Any, cfg: dict[str, Any]) -> dict[str, Any]:
    """Per-model and total estimate from the last snapshot."""
    adj = float(cfg.get("adjust", DEFAULTS["adjust"]))
    usd = float(cfg.get("usd_per_point", DEFAULTS["usd_per_point"]))
    marked = set(cfg.get("exclusive") or [])
    info = ((snap.extras or {}).get("earn") or {}) if snap else {}
    models: dict[str, Any] = {}
    tot = {"uses": 0, "base": 0.0, "model": 0.0, "profile": 0.0, "boost": 0.0}
    for mid, m in (snap.models if snap else {}).items():
        u = uses(m.downloads, m.prints)
        ei = info.get(mid) or {}
        auto = bool(ei.get("exclusive"))
        excl = mid in marked or auto
        mx = EXCLUSIVE_X if excl else 1.0
        base = milestone_points(u) * mx            # model milestones before the adjustment
        profs = [p for p in (ei.get("profiles") or []) if p > 0] or ([u] if u else [])
        prof = sum(milestone_points(p, PROFILE_TIERS) for p in profs) * mx
        boost = (m.boosts or 0) * (BOOST_PTS_EXCL if excl else BOOST_PTS)
        need, pays = next_milestone(u)
        pts = base * adj + prof + boost
        models[mid] = {"u": u, "x": excl, "auto": auto, "base": round(base, 2), "prof": round(prof, 2), "boost": boost,
                       "nb": m.boosts or 0, "pts": round(pts, 1), "usd": round(pts * usd, 2), "need": need,
                       "pays": round(pays * mx * adj, 1)}
        tot["uses"] += u
        tot["base"] += base
        tot["profile"] += prof
        tot["boost"] += boost

    def total_at(x: float) -> float:
        return tot["base"] * x + tot["profile"] + tot["boost"]

    pts = total_at(adj)
    return {
        "adjust": adj, "usdPerPoint": usd, "exclusive": sorted(marked),
        "uses": tot["uses"],
        "points": round(pts, 1), "usd": round(pts * usd, 2),
        "low": {"points": round(total_at(LOW_X), 1), "usd": round(total_at(LOW_X) * usd, 2)},
        "high": {"points": round(total_at(HIGH_X), 1), "usd": round(total_at(HIGH_X) * usd, 2)},
        "parts": {"model": round(tot["base"] * adj, 1), "profile": round(tot["profile"], 1),
                  "boost": round(tot["boost"], 1)},
        "models": models,
    }
