# Theme

## Token summary
- Light: hb #55402e (header band), hb-ink #fbf6ef, bg #e9ecee, panel #fff, panel2 #f3f5f6, line #d3d8dc, line2 #e5e9ec, ink #14191e, ink2 #424c56, ink3 #58626c, acc #c2410c (orange, reserved for needs-attention and now), acc-soft #fbe8de, ok #07714d, warn #8a5a00, err #c42b40
- Dark: hb #271e17, bg #0f1214, panel #171b1f, panel2 #1e2328, line #2d343b, ink #e8ecef, ink2 #b4bec8, ink3 #939eaa, acc #fb8c4a, ok #34d399, warn #f5b13d, err #ff6b7d
- Metric colours (shared with desk panels): downloads d #109e6a, prints p #7b5cf0, boosts b #d98a00, likes l #e0457b, collects c #2f80ed, comments f #0f8fb8, ratings r #c99a00 (lighter in dark)
- Type: system-ui sans only, tabular figures; 12.5 small, 13.5 table, 14 body, 15.5 panel title, 27 Today numbers. Radius 8px panels, 6-7px controls. Shadows: none on panels.
- Breakpoints: 1000 (grid to one column), 900 (settings nav), 760 (phone: bottom tab bar), 560.

## Raw
```css
_BASE_CSS = r"""
:root{color-scheme:light dark;
--hb:#55402e;--hb2:#6b5440;--hb-ink:#fbf6ef;--hb-ink2:#e4d6c5;
--bg:#e9ecee;--panel:#ffffff;--panel2:#f3f5f6;--line:#d3d8dc;--line2:#e5e9ec;--ink:#14191e;--ink2:#424c56;--ink3:#58626c;
--acc:#c2410c;--acc-ink:#ffffff;--acc-soft:#fbe8de;--acc2:#fb8c4a;--acc2-ink:#1c0d03;--ok:#07714d;--warn:#8a5a00;--err:#c42b40;
--b:#d98a00;--l:#e0457b;--c:#2f80ed;--d:#109e6a;--p:#7b5cf0;--f:#0f8fb8;--r:#c99a00;
--shadow:0 1px 2px rgba(20,25,30,.08);--rad:8px;
--mono:ui-monospace,"SF Mono","Cascadia Mono",Menlo,Consolas,monospace;
--sans:system-ui,-apple-system,"Segoe UI Variable Text","Segoe UI",Roboto,"Helvetica Neue",sans-serif;
--display:var(--sans)}
@media (prefers-color-scheme:dark){:root{
--hb:#271e17;--hb2:#3a2d22;--hb-ink:#f4ede4;--hb-ink2:#cdbca8;
--bg:#0f1214;--panel:#171b1f;--panel2:#1e2328;--line:#2d343b;--line2:#242a30;--ink:#e8ecef;--ink2:#b4bec8;--ink3:#939eaa;
--acc:#fb8c4a;--acc-ink:#1c0d03;--acc-soft:#3b2416;--acc2:#c2410c;--acc2-ink:#ffffff;--ok:#34d399;--warn:#f5b13d;--err:#ff6b7d;
--b:#ffb020;--l:#ff5c93;--c:#5aa2ff;--d:#34d399;--p:#a78bfa;--f:#38c6f0;--r:#ffd23f;
--shadow:0 1px 2px rgba(0,0,0,.4)}}
*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 var(--sans);font-variant-numeric:tabular-nums}
::selection{background:color-mix(in srgb,var(--acc) 26%,transparent)}
a{color:inherit;text-underline-offset:2px;text-decoration-thickness:1px}button,input,select,textarea{font:inherit;color:inherit}
input,textarea{caret-color:var(--acc)}
:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
html{scrollbar-color:color-mix(in srgb,var(--ink3) 45%,transparent) transparent}
.foot{display:flex;flex-wrap:wrap;justify-content:center;gap:6px 18px;padding:18px 16px 28px;color:var(--ink3);font-size:13px}
.foot a{color:var(--ink2);text-decoration:none;font-weight:600}.foot a:hover{color:var(--acc);text-decoration:underline}
"""
```
