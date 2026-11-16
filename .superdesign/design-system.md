# MakerMonitor dashboard design system

## Product
A self-hosted LAN dashboard for one MakerWorld (3D-printable model site) creator. Jobs, in order: see today against yesterday at the same hour; act on the short list of things that need them (unanswered comments, a model spiking, a page converting badly); dig into per-model stats; export data; check the poller's health; change settings. Used on desktop and on a phone, several times a day. Times are always 12-hour (9:00 AM).

Views: Overview, Models, Activity, Export, Health, Settings, plus a Kiosk link to a separate full-screen page.

## World: workshop pegboard shadow board
- Shell: a hardboard-brown header band (#55402e light, #271e17 dark) with off-white text; navigation is folder tabs cut into the page ground (active tab takes the ground colour). On phones the tabs become a fixed bottom bar with icon over label.
- Ground: cool galvanized steel (#e9ecee light, #0f1214 dark). Panels: flat white (#ffffff / #171b1f), 1px hairline border (#d3d8dc / #2d343b), 8px radius, no drop shadow, a 48px header strip with a hairline under it.
- Signature move: the 24-peg day strip. Each hour is a small hole; yesterday's count is a dashed outline ring; today's count is a filled peg in the metric colour; the current hour has a short orange tick under it. Reused for best hours.
- Orange (#c2410c light, #fb8c4a dark) is reserved for what needs the creator (Needs you markers, unanswered counts), the current hour, selection and focus. Nothing decorative is orange.
- Metric colours are fixed (shared with the desk panels): downloads #109e6a, prints #7b5cf0, boosts #d98a00, likes #e0457b, collects #2f80ed, comments #0f8fb8, ratings #c99a00 (brighter variants in dark mode).
- State: ok #07714d, warn #8a5a00, err #c42b40. Up/down always carries a small triangle and a signed number or a word, never colour alone.

## Type
One system sans (system-ui, Segoe UI, Roboto) for everything, tabular figures for all numbers. Sizes: 12.5 small/meta, 13.5 tables, 14 body, 15.5 panel titles (650), 27 Today numbers (700). No display, serif or monospace faces (monospace only for config paths and error text).

## Components
Segmented control (pill group, 30px buttons), primary/ghost/danger buttons (8px radius), badges, chips, switches, sortable tables whose headers are buttons with a chevron, hairline-ruled lists. Icons are 18px line icons, 1.6 stroke.

## Motion
150 to 200 ms fades on view change only; respects reduced motion.
