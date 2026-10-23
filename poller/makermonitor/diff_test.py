"""Tiny sanity check: python -m makermonitor.diff_test"""

from .panel import build_events
from .state import ModelStats, Snapshot


def main() -> None:
    prev = Snapshot(
        downloads=10,
        prints=2,
        boosts=1,
        points=5.0,
        models={"1": ModelStats(name="Box", downloads=10, prints=2, boosts=1)},
        fetched_at=1,
    )
    cur = Snapshot(
        downloads=12,
        prints=3,
        boosts=2,
        points=8.5,
        models={"1": ModelStats(name="Box", downloads=12, prints=3, boosts=2)},
        fetched_at=2,
    )
    events = build_events(prev, cur)
    types = [e["type"] for e in events]
    assert "download" in types and "print" in types and "boost" in types and "points" in types
    print("diff_test ok", types)


if __name__ == "__main__":
    main()