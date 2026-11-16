# Product

<!-- impeccable:product-schema 1 -->

## Platform

web (the LAN dashboard and kiosk); the desk panels are ESP32 firmware with their own LVGL UI.

## Users

A MakerWorld creator (3D-printable model designer) running MakerMonitor at home for their own account. They check the web dashboard on a desktop browser at their desk and on a phone around the house or workshop, several times a day, to see what happened since they last looked and what needs their attention (comments to answer, a model spiking, a page that converts badly). Inferred, unconfirmed: the dashboard is now the primary way people use the system, ahead of the desk panels.

## Product Purpose

Watch one MakerWorld creator account and show downloads, prints, boosts, likes, collects, ratings, comments, followers and points as they happen, with enough history and analysis (funnel, best times, per-model conversion, launches) to decide what to do next. Success: the creator learns today's state in seconds and knows the one or two things worth acting on.

## Positioning

A self-hosted, always-on monitor for a single creator: live events with yesterday-at-this-hour comparisons, per-model diagnostics the MakerWorld site does not show, and the same data on desk panels, a kiosk view and notification channels.

## Operating Context

- A Python poller (FastAPI/uvicorn) on the home network polls MakerWorld politely under a shared request budget; it needs a MakerWorld cookie (kept fresh by a browser extension) for impressions, views and analytics.
- The dashboard is served by the poller at `/` with token or cookie auth; `/kiosk` is a full-screen wall/TV view; `/api/export.csv` exports analytics.
- Times are shown in 12-hour format everywhere.

## Capabilities and Constraints

- Views: Overview, Models, Activity, Export, Health, Settings, plus a Kiosk link.
- One self-contained page with no external assets (no CDN, no web fonts from other hosts); light and dark themes follow the OS.
- Data refreshes every 60 s from `/api/dashboard` and `/v2/creator`; per-model history from `/v2/model`.
- The poller keeps only the last 40 events, so the live feed can be incomplete.

## Brand Commitments

- Name: MakerMonitor, by @Wofl. Footer links to the GitHub repo and the MakerWorld profile.
- Metric colours are shared with the desk panels: downloads, prints, boosts, likes, collects, ratings each have their own colour.
- No tool or generator attribution anywhere in the product, code, docs or commits.

## Evidence on Hand

Real live account data from the poller; screenshots of the panels in `docs/screens/`.

## Product Principles

1. Today first: what changed since the last look, against the same time yesterday.
2. Tell the creator what to do, with the reason in plain words and the numbers behind it.
3. Every number has its time window stated.
4. Honest about data health: stale data, an expired cookie or missing analytics are always visible.

## Accessibility & Inclusion

Keyboard operable, visible focus, WCAG AA contrast in both themes, reduced-motion respected, usable at 390 px wide.
