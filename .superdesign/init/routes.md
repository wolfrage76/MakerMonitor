# Routes

Hash routes inside one page served at `/` and `/dashboard` (token query or mm_token cookie):
- #overview: Today peg board, Needs you, Top models, Funnel, Best times, 7 and 30 days, Followers, Creator at a glance
- #models: sortable all-models table, grouped insights
- #activity: today's feed, needs a reply, recent comments
- #export: CSV export builder (/api/export.csv)
- #health: data sources, request budget, poller, panels
- #settings/<section>: settings form
- /kiosk: separate full-screen page (kiosk.py)
Data: /api/dashboard, /v2/creator, /v2/model, /api/settings, /api/export/meta.
