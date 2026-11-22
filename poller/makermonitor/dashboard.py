"""LAN dashboard served by the poller at / (and /dashboard).

One static page with five views (Overview, Models, Activity, Health,
Settings). It reads /api/dashboard (the state the color panels get, plus
health, best-time insights and the dashboard-only "dash" extras: yesterday's
hourly buckets, weekday counts and today's full feed) and /v2/creator every 60 s, /v2/model for
per-model sparklines, and /api/settings* for the Settings view. No external
assets, light and dark themes.
"""

GITHUB_URL = "https://github.com/wolfrage76/MakerMonitor"
MAKERWORLD_URL = "https://makerworld.com/en/@Wofl"

# Project links, at the foot of both pages.
_FOOTER = (f'<footer class=foot><span>MakerMonitor by @Wofl</span>'
           f'<a href="{GITHUB_URL}" target=_blank rel=noopener>GitHub</a>'
           f'<a href="{MAKERWORLD_URL}" target=_blank rel=noopener>MakerWorld</a></footer>')

_BASE_CSS = r"""
:root{color-scheme:light dark;
--hb:#55402e;--hb2:#6b5440;--hb-ink:#fbf6ef;--hb-ink2:#e4d6c5;
--bg:#e9ecee;--panel:#ffffff;--panel2:#f3f5f6;--line:#d3d8dc;--line2:#e5e9ec;--ink:#14191e;--ink2:#424c56;--ink3:#4d5761;
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
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 var(--sans);font-variant-numeric:tabular-nums}
::selection{background:color-mix(in srgb,var(--acc) 26%,transparent)}
a{color:inherit;text-underline-offset:2px;text-decoration-thickness:1px}button,input,select,textarea{font:inherit;color:inherit}
input,textarea{caret-color:var(--acc)}::placeholder{color:var(--ink3);opacity:1}p,.lab p,.help{text-wrap:pretty}
:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
html{scrollbar-color:color-mix(in srgb,var(--ink3) 45%,transparent) transparent}
.foot{display:flex;flex-wrap:wrap;justify-content:center;gap:6px 18px;padding:18px 16px 28px;color:var(--ink3);font-size:15px}
.foot a{color:var(--ink2);text-decoration:none;font-weight:600}.foot a:hover{color:var(--acc);text-decoration:underline}
"""

LOGIN_HTML = """<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>MakerMonitor</title>
<style>""" + _BASE_CSS + """
main{min-height:calc(100vh - 70px);display:grid;place-items:center;padding:16px}
.box{max-width:440px;background:var(--panel);border:1px solid var(--line);border-radius:var(--rad);padding:28px;box-shadow:var(--shadow)}
h1{font:700 24px var(--display);margin:0 0 12px}code{font-family:var(--mono);color:var(--acc);word-break:break-all}
p{color:var(--ink2)}</style></head><body><main><div class=box><h1>MakerMonitor</h1>
<p>Open this page once with your poller token:</p><p><code>http://&lt;this-pc&gt;:8787/?token=&lt;api_token&gt;</code></p>
<p>The token is <code>api_token</code> in <code>poller/config.yaml</code>. This browser remembers it after that.</p>
</div></main>""" + _FOOTER + """</body></html>"""

DASHBOARD_HTML = r"""<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>MakerMonitor</title>
<style>""" + _BASE_CSS + r"""
/* ---- shell: hardboard band, folder tabs cut into the steel ground ---- */
.top{position:sticky;top:0;z-index:20;background:var(--hb);color:var(--hb-ink)}
.bar{max-width:1280px;margin:0 auto;display:flex;align-items:center;gap:12px;padding:12px 20px 10px}
.brand{display:flex;align-items:center;gap:10px;font:700 18px var(--sans);letter-spacing:.1px}
.mark{display:block;flex:none}
.acct{color:var(--hb-ink2);font-weight:500}
.pill{margin-left:auto;display:inline-flex;align-items:center;gap:8px;padding:5px 12px;border-radius:999px;
 background:color-mix(in srgb,var(--hb-ink) 10%,transparent);color:var(--hb-ink);font-size:15px;white-space:nowrap}
.pill .dot{width:8px;height:8px;border-radius:50%;background:var(--hb-ink2)}
.pill.ok .dot{background:#4ade80}.pill.warn .dot{background:#fbbf24}.pill.err{background:color-mix(in srgb,#ff6b7d 22%,transparent)}.pill.err .dot{background:#ff8a98}
.kio{display:inline-flex;align-items:center;gap:7px;min-height:34px;padding:5px 12px;border-radius:7px;color:var(--hb-ink);text-decoration:none;font-weight:600;font-size:15px;
 border:1px solid color-mix(in srgb,var(--hb-ink) 28%,transparent)}
.kio:focus-visible,nav.tabs a:focus-visible{outline-color:var(--hb-ink)}
.kio:hover{background:color-mix(in srgb,var(--hb-ink) 12%,transparent)}
nav.tabs{max-width:1280px;margin:0 auto;display:flex;gap:2px;padding:0 14px;overflow-x:auto;scrollbar-width:none}
nav.tabs::-webkit-scrollbar{display:none}
nav.tabs a{display:flex;align-items:center;gap:7px;padding:9px 14px 10px;text-decoration:none;color:var(--hb-ink2);font-weight:600;border-radius:8px 8px 0 0;white-space:nowrap}
nav.tabs a svg{flex:none;opacity:.85}
nav.tabs a:hover{color:var(--hb-ink);background:color-mix(in srgb,var(--hb-ink) 9%,transparent)}
nav.tabs a.on{color:var(--ink);background:var(--bg)}nav.tabs a.on svg{color:var(--acc);opacity:1}
nav.tabs a:focus-visible{outline-color:var(--acc2);outline-offset:-2px}
nav.tabs a .ct{font:600 13px var(--sans);padding:1px 7px;border-radius:999px;background:color-mix(in srgb,var(--hb-ink) 16%,transparent);color:var(--hb-ink)}
nav.tabs a.on .ct{background:var(--panel2);color:var(--ink2);box-shadow:inset 0 0 0 1px var(--line)}
main{max-width:1280px;margin:0 auto;padding:20px}
section.view{display:none}section.view.on{display:block;animation:in .16s cubic-bezier(.2,.7,.2,1)}
@keyframes in{from{opacity:.5}to{opacity:1}}
@media (prefers-reduced-motion:reduce){section.view.on{animation:none}}
.grid{display:grid;gap:20px;grid-template-columns:repeat(12,minmax(0,1fr));align-items:start}
.s12{grid-column:span 12}.s8{grid-column:span 8}.s7{grid-column:span 7}.s6{grid-column:span 6}.s5{grid-column:span 5}.s4{grid-column:span 4}
@media (max-width:1000px){.s8,.s7,.s6,.s5,.s4{grid-column:span 12}}
/* panels: flat on hairlines, a header strip instead of a floating title */
.card{background:var(--panel);border:1px solid var(--line);border-radius:var(--rad);padding:0 20px 18px;min-width:0}
.card>h2,.h2{display:flex;align-items:center;gap:10px;margin:0 -20px 16px;padding:12px 20px;min-height:54px;border-bottom:1px solid var(--line2);font:650 19px/1.3 var(--sans)}
.h2 small,.card>h2 small{font:500 14px var(--sans);color:var(--ink3)}
.card>h2 .sp{flex:1}
.mut{color:var(--ink3)}.ink2{color:var(--ink2)}.num{font-variant-numeric:tabular-nums}
.up{color:var(--ok)}.down{color:var(--err)}
.banner{display:none;margin-bottom:16px;padding:12px 16px;border-radius:var(--rad);font-weight:600;border:1px solid}
.banner.err{display:block;color:var(--err);border-color:color-mix(in srgb,var(--err) 40%,transparent);background:color-mix(in srgb,var(--err) 8%,var(--panel))}
.banner.warn{display:block;color:var(--warn);border-color:color-mix(in srgb,var(--warn) 40%,transparent);background:color-mix(in srgb,var(--warn) 8%,var(--panel))}
.banner a{font-weight:700}
.seg{display:inline-flex;flex-wrap:wrap;min-width:0;background:var(--panel2);border:1px solid var(--line);border-radius:8px;padding:2px}
.seg button{border:0;background:none;min-height:36px;padding:4px 11px;border-radius:6px;color:var(--ink2);cursor:pointer;font-size:15px;font-weight:600}
.seg button:hover{color:var(--ink)}
.seg button.on{background:var(--panel);color:var(--ink);box-shadow:var(--shadow),inset 0 0 0 1px var(--line)}
/* ---- today: one tile per stat, a sparkline per tile (dashed: yesterday) ---- */
.hero{display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:12px}
@media (max-width:1100px){.hero{grid-template-columns:repeat(3,minmax(0,1fr))}}
@media (max-width:760px){.hero{grid-template-columns:repeat(2,minmax(0,1fr))}.stat .val{font-size:28px}}
.stat{position:relative;padding:14px 14px 10px;border-radius:12px;background:var(--panel2);overflow:hidden;min-width:0}
.stat .lab{font-size:14px;font-weight:600;color:var(--ink2);display:flex;justify-content:space-between;align-items:center;gap:6px}
.stat .lab .rowlink{color:inherit}
.stat .val{font:800 34px/1.1 var(--sans);margin:6px 0 2px;color:var(--ink);font-variant-numeric:tabular-nums}
.stat .val.zero{color:var(--ink3)}
.stat .cmp{font-size:13px;color:var(--ink3);display:flex;flex-wrap:wrap;gap:0 6px;min-height:19px}
.stat .cmp .up,.stat .cmp .down{font-weight:700}
.stat svg.dl{display:block;width:100%;height:30px;margin-top:8px}
.stat svg.stars{display:block;width:120px;height:24px;margin-top:8px}
/* ---- peg strips (Best times) ---- */
.board{display:flex;flex-direction:column}
.brow{display:grid;grid-template-columns:200px minmax(0,1fr);grid-template-areas:"lab pegs" "cmp pegs";gap:2px 20px;align-items:center;padding:13px 0;border-top:1px solid var(--line2)}
.hour-details{margin-top:12px;border-top:1px solid var(--line2);padding-top:4px}
.hour-details summary{cursor:pointer;width:fit-content;padding:10px 0;font-weight:650}
.hour-details p{margin:4px 0 12px}
.hour-details .tw{max-height:420px;margin-top:10px;overflow:auto}
.card>h2{flex-wrap:wrap}
.freshness{margin:0 0 12px;color:var(--ink2)}
.brow:first-child{border-top:0;padding-top:2px}
.brow .lab{display:flex;flex-direction:column;gap:1px;min-width:0}
.brow .lab span{font-size:15px;font-weight:600;color:var(--ink2)}
.brow .val{font:700 31px/1.1 var(--sans);letter-spacing:-.01em;color:var(--ink)}
.brow .val.zero{color:var(--ink3);font-weight:600}
.brow .lab{grid-area:lab}.brow .pegs,.brow .note{grid-area:pegs}.brow .cmp{grid-area:cmp;font-size:14.5px;color:var(--ink2);line-height:1.45;align-self:start}
.brow .cmp b{display:inline-flex;align-items:center;gap:3px;font-size:16px;font-weight:700;color:var(--ink2)}
.brow .cmp b.up{color:var(--ok)}.brow .cmp b.down{color:var(--err)}
.brow .note{font-size:14px;color:var(--ink3)}
.pegs{display:block;width:100%;height:auto;overflow:visible}
.brow.zero .pegs{opacity:.85}
.baxis>span:last-child{display:none}
.baxis{display:grid;grid-template-columns:200px minmax(0,1fr);gap:0 18px;padding-top:2px}
.hax{position:relative;height:20px;color:var(--ink2);font-size:13.5px}.hax span{position:absolute;top:0;transform:translateX(-50%);white-space:nowrap}
.hax span:first-child{transform:translateX(-4px)}.hax span:last-child{transform:translateX(calc(-100% + 4px))}
.blegend{display:flex;gap:16px;flex-wrap:wrap;justify-content:flex-end;color:var(--ink2);font-size:13.5px}
.blegend span{display:inline-flex;align-items:center;gap:6px}
@media (max-width:760px){.brow{grid-template-columns:minmax(0,1fr) auto;grid-template-areas:"lab cmp" "pegs pegs";padding:12px 0 10px}.brow .cmp{text-align:right}
 #top th:nth-child(n+5),#top td:nth-child(n+5){display:none}#top th:nth-child(2),#periods th:nth-child(2){display:none}.brow .val{font-size:26px}
 .baxis{grid-template-columns:minmax(0,1fr)}.baxis>span{display:none}}
.ov{display:grid;grid-template-columns:minmax(0,7fr) minmax(0,5fr);gap:20px;align-items:start}.ovl,.ovr{display:flex;flex-direction:column;gap:20px;min-width:0}.ovw{grid-column:1/-1}
@media (max-width:1000px){.ov{display:flex;flex-direction:column;align-items:stretch}.ovl,.ovr{display:contents}.o1{order:1}.o2{order:2}.o3{order:3}.o4{order:4}.o5{order:5}.o6{order:6}.o6b{order:6}.o7{order:7}.o8{order:8}.o9b{order:8}.o9{order:9}.o10{order:10}}
.herofoot{display:flex;flex-wrap:wrap;gap:6px 22px;margin-top:14px;color:var(--ink2);font-size:14.5px}
.herofoot>span{display:inline-flex;align-items:baseline;gap:6px}
.herofoot b{font:650 15px var(--sans);color:var(--ink);font-variant-numeric:tabular-nums}
.herofoot b small{font:500 14px var(--sans);color:var(--ink3);margin-left:4px}
/* ---- tables ---- */
.tw{overflow-x:auto;margin:0 -4px}
table{width:100%;border-collapse:collapse;font-size:15px}
th,td{padding:11px 9px;text-align:right;white-space:nowrap}
th{font-weight:600;color:var(--ink3);font-size:14px;border-bottom:1px solid var(--line);vertical-align:bottom}
td{border-bottom:1px solid var(--line2)}
tbody tr:hover td{background:color-mix(in srgb,var(--panel2) 70%,transparent)}
tr:last-child td{border-bottom:0}
th:first-child,td:first-child{text-align:left}
td.nm{max-width:320px;overflow:hidden;text-overflow:ellipsis;font-weight:550}
td.nm a{text-decoration:none}td.nm a:hover{text-decoration:underline;color:var(--acc)}
th .sb{border:0;background:none;padding:6px 0;min-height:36px;margin:0;font:inherit;color:inherit;cursor:pointer;display:inline-flex;align-items:center;gap:3px;white-space:nowrap}
th .sb:hover{color:var(--ink)}th[aria-sort] .sb{color:var(--ink)}
th .sb svg{flex:none;color:var(--acc)}
td svg.spark{width:90px;height:22px;vertical-align:middle}
td svg.wk7{width:64px;height:20px;vertical-align:middle}
.fit table td.nm,.fit table th:first-child{max-width:0;width:100%}
#top th:nth-child(2),#top td:nth-child(2){display:none}#top td.nm{white-space:normal}#top td.nm>a,#top td.nm>span{display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden;line-height:1.35}
@media (max-width:760px){.nophone{display:none}.fit table td.nm,.fit table th:first-child{min-width:100px}#periods td.nm{white-space:nowrap;overflow:visible;max-width:none;width:auto}
 #mtable td.nm{white-space:normal;max-width:none;min-width:118px}#mtable th,#mtable td{padding:8px 5px;font-size:15px}#mtable td.nm>a,#mtable td.nm>span{display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden;line-height:1.35}}
.priv{display:inline-block;margin-right:2px;padding:0 6px;border-radius:5px;border:1px solid var(--line);color:var(--ink2);font-size:12px;font-weight:600;line-height:18px;vertical-align:1px;white-space:nowrap}
.kdot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--k);margin-right:6px;vertical-align:1px}
.empty{padding:18px 4px;color:var(--ink3);text-align:center}
/* ---- charts ---- */
.bars{display:flex;align-items:flex-end;gap:2px;height:96px}
.bars i{flex:1;background:color-mix(in srgb,var(--ink3) 22%,var(--panel2));border-radius:2px 2px 0 0;min-height:2px}
.bars i.hi{background:color-mix(in srgb,var(--ok) 70%,var(--panel2))}.bars i.neg{background:var(--err)}
.axis{display:flex;justify-content:space-between;color:var(--ink2);font-size:13.5px;margin-top:6px}
.wk{display:grid;grid-template-columns:repeat(7,1fr);gap:6px;margin-top:14px}
.wk div{background:var(--panel2);border:1px solid var(--line2);border-radius:6px;padding:9px 0;text-align:center;font-size:14px;color:var(--ink2)}
.wk div.hi{background:var(--panel);border-color:var(--ink2);color:var(--ink);font-weight:650}
.wk b{display:block;font:650 17px var(--sans);color:var(--ink)}
.minis{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-bottom:14px}
.mini{border-left:1px solid var(--line2);padding:2px 0 2px 12px}.mini:first-child{border-left:0;padding-left:0}
.mini b{display:block;font:700 24px var(--sans)}.mini span{font-size:14px;color:var(--ink3)}
@media (max-width:760px){.minis{grid-template-columns:repeat(2,1fr)}.mini:nth-child(3){border-left:0;padding-left:0}}
.facts{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px 28px}
@media (max-width:900px){.facts{grid-template-columns:minmax(0,1fr)}}
.facts h3{margin:0 0 2px;font:600 14px var(--sans);color:var(--ink3);letter-spacing:.02em}
.facts .fr{display:flex;justify-content:space-between;align-items:baseline;gap:16px;padding:12px 0;border-bottom:1px solid var(--line2)}
.facts .fr:last-child{border-bottom:0}
.facts .fr>span{color:var(--ink2);min-width:0}.facts .fr>span small{display:block;color:var(--ink3);font-size:14px}
.facts .fr>b{text-align:right;font:650 17px var(--sans);white-space:nowrap}
.facts .fr>b small{font:500 14px var(--sans);color:var(--ink3);margin-left:6px}
.lists td.nm{white-space:normal}.lists td.nm>a,.lists td.nm>span{display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden;line-height:1.35}
.lists{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:16px;align-items:start}
@media (max-width:420px){.lists{grid-template-columns:minmax(0,1fr)}}
.ihead{display:flex;align-items:center;gap:12px;flex-wrap:wrap}.ihead h2{margin:0;font:650 19px var(--sans)}.ihead p{margin:0;color:var(--ink3);font-size:15px;flex:1;min-width:200px}
.lc{display:flex;align-items:flex-end;gap:8px;height:110px}.lc div{flex:1;display:flex;gap:3px;align-items:flex-end;height:100%}
.lc i{flex:1;border-radius:2px 2px 0 0;min-height:2px}
/* actions and explanations */
.do{display:flex;flex-wrap:wrap;gap:4px 14px;margin-top:6px}
.do a{display:inline-flex;align-items:center;gap:4px;font-size:15px;font-weight:600;color:var(--ink);text-decoration:underline;text-decoration-color:color-mix(in srgb,var(--ink3) 55%,transparent)}
.do a:hover{color:var(--acc);text-decoration-color:currentColor}
@media (max-width:760px){.do a{min-height:32px}}
.exi{flex:none;opacity:.7}a:hover>.exi{opacity:1}
.tip{cursor:help;text-decoration:underline dotted;text-decoration-color:color-mix(in srgb,var(--ink3) 70%,transparent);text-underline-offset:3px}
.tipbox{position:fixed;z-index:60;max-width:340px;padding:10px 13px;border-radius:7px;background:var(--ink);color:var(--bg);font-size:14px;line-height:1.45;font-weight:500;
 box-shadow:0 6px 20px rgba(0,0,0,.22);pointer-events:none}
.tipbox[hidden]{display:none}
a.rowlink{color:inherit;text-decoration:none}a.rowlink:hover{color:var(--acc);text-decoration:underline}
.fsum>button{display:inline-flex;align-items:center;gap:6px;padding:4px 12px;min-height:38px;border-radius:999px;background:var(--panel2);border:1px solid var(--line2);font-size:15px;color:var(--ink2);cursor:pointer}
.fsum>button:hover{border-color:var(--ink3);color:var(--ink)}.fsum>button[aria-pressed=true]{background:var(--panel);border-color:var(--ink2);color:var(--ink)}
.fsum>button b{font:700 15px var(--sans);color:var(--ink)}
.afilter{overflow-wrap:anywhere;gap:8px;margin:-2px 0 10px;font-size:15px;color:var(--ink2)}.afilter[hidden]{display:none}
.ucm{padding:11px 0;border-bottom:1px solid var(--line2)}.ucm:last-child{border:0}
.ucm .who{font-weight:650}.ucm .on{color:var(--ink3);font-size:14px}.ucm p{margin:4px 0 0;color:var(--ink2);overflow-wrap:anywhere}
.ucm .do{margin-top:4px}
/* needs you: the shadow-paint list */
.next-action{display:none}
#needs-reply,#model-focus-title,#needs-you{scroll-margin-top:120px}
.model-focus{margin:0 0 16px;padding:12px 0;border-bottom:1px solid var(--line2);overflow-wrap:anywhere}
.model-focus h3{margin:0 0 6px;font-size:18px}
.model-focus p{margin:0;color:var(--ink2)}
.more-actions{margin-top:8px}
.more-actions summary{cursor:pointer;width:fit-content;padding:10px 0;font-weight:650}
.more-actions .acts{margin-top:4px}
@media(max-width:760px){
 .next-action:not([hidden]){display:block;margin-bottom:16px;padding:12px 16px;border:1px solid var(--line);border-radius:var(--rad);background:var(--panel)}
 .next-action a{display:inline-flex;align-items:center;min-height:44px;font-weight:650;color:var(--acc)}
}
.acts{list-style:none;margin:0;padding:0}
.acts li{display:flex;gap:12px;padding:14px 0;border-top:1px solid var(--line2)}.acts li:first-child{border-top:0;padding-top:2px}
.acts li>i{flex:none;width:10px;height:10px;border-radius:50%;margin-top:5px;box-shadow:inset 0 0 0 2px var(--ink3)}
.acts li.now>i{background:var(--acc);box-shadow:none}.acts li.fix>i{box-shadow:inset 0 0 0 2px var(--acc)}
.acts li>div{min-width:0}.acts b{display:block;font-weight:650;overflow-wrap:anywhere}.acts b a{color:inherit}
.acts span{display:block;color:var(--ink2);font-size:15px;margin-top:2px}.acts span.tip{display:inline;margin:0;white-space:nowrap}.acts span.do{display:flex;margin-top:6px}
.fn .fs{display:flex;align-items:baseline;gap:10px;padding:6px 0}.fn .fs>span{flex:1;font-weight:600}
.fn .fs>span small{display:block;font-weight:400;color:var(--ink3);font-size:14px}
.fn .fs b{font-size:19px}.fn .fs>small{color:var(--ink3);font-size:14px;min-width:96px;text-align:right}
.fn .fc{display:flex;align-items:center;gap:8px;padding:3px 0 3px 2px;color:var(--ink2);font-size:15px}
.fn .fc .ar{color:var(--ink3);width:14px;display:grid;place-items:center}.fn .fc .rt{font-weight:700;color:var(--ink)}
.fn .fc .y{margin-left:auto;display:inline-flex;align-items:center;gap:4px}
.fn .take{margin:12px 0 0;padding-top:12px;border-top:1px solid var(--line2);color:var(--ink2);font-size:15px}
.tail{display:flex;height:10px;border-radius:999px;overflow:hidden;background:var(--panel2);margin:6px 0 12px}
.legend{display:flex;gap:14px;font-size:14px;color:var(--ink3);flex-wrap:wrap}.legend i{display:inline-block;width:10px;height:10px;border-radius:3px;margin-right:5px;vertical-align:-1px}
/* ---- feed ---- */
.feed{display:flex;flex-direction:column}
.fsum{display:flex;flex-wrap:wrap;gap:6px;margin:-2px 0 10px}
.fsum>span{display:inline-flex;align-items:center;gap:6px;padding:3px 10px;border-radius:999px;background:var(--panel2);border:1px solid var(--line2);font-size:15px;color:var(--ink2)}
.fsum b{font:700 15px var(--sans);color:var(--ink)}
.mtools{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:-2px 0 12px}
.mtools input[type=search]{flex:0 1 280px;min-height:36px;padding:6px 11px;border-radius:7px;border:1px solid var(--line);background:var(--panel2);outline:none}
.mtools input[type=search]:focus{border-color:var(--acc);box-shadow:0 0 0 3px color-mix(in srgb,var(--acc) 22%,transparent)}
.reply{display:flex;flex-direction:column}.reply>a{display:flex;justify-content:space-between;gap:12px;padding:8px 0;text-decoration:none;border-bottom:1px solid var(--line2)}
.reply>a:last-child{border:0}.reply>a span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.reply>a:hover span{color:var(--acc);text-decoration:underline}
.reply>a b{font:700 15px var(--sans);color:var(--acc)}
.ev{display:grid;grid-template-columns:56px 1fr auto;gap:12px;align-items:center;padding:12px 0;border-bottom:1px solid var(--line2)}
.ev:last-child{border:0}
.tag{font:700 14px var(--sans);padding:3px 0;border-radius:5px;text-align:center;color:color-mix(in srgb,var(--k) 58%,var(--ink));background:color-mix(in srgb,var(--k) 15%,transparent)}
.ev .m{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.tag .kico{display:block;margin:0 auto}.ev .t{color:var(--ink3);font-size:14px}
.cm{padding:12px 0;border-bottom:1px solid var(--line2)}.cm:last-child{border:0}
.cm .who{font-weight:650}.cm .on{color:var(--ink3);font-size:14px}.cm p{margin:4px 0 0;color:var(--ink2)}
/* ---- health ---- */
.src{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:0 24px}
.src>div{padding:12px 0;border-top:1px solid var(--line2);--st:var(--ok)}
.src>div.warn{--st:var(--warn)}.src>div.err{--st:var(--err)}.src>div.idle{--st:var(--ink3)}
.src b{display:flex;align-items:center;gap:8px;font-weight:650;margin-bottom:4px;min-width:0;overflow-wrap:anywhere}
.src b:before{content:"";flex:none;width:9px;height:9px;border-radius:50%;background:var(--st)}
.src>div.idle b:before{background:none;box-shadow:inset 0 0 0 2px var(--st)}
.hsum{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:16px}.src .e{margin-top:6px;font:13.5px var(--mono);color:var(--err);word-break:break-word}
.meter{height:8px;border-radius:999px;background:var(--panel2);overflow:hidden;margin:8px 0;box-shadow:inset 0 0 0 1px var(--line2)}.meter i{display:block;height:100%;background:var(--ink2)}
dl.kv{display:grid;grid-template-columns:max-content 1fr;gap:6px 16px;margin:0}dl.kv dt{color:var(--ink3)}dl.kv dd{margin:0;overflow-wrap:anywhere}
/* ---- controls (shared) ---- */
main:focus{outline:none}#ctset{color:var(--warn);background:color-mix(in srgb,var(--warn) 14%,var(--panel))}
.sr{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap;border:0}
.skip{position:absolute;left:12px;top:-60px;z-index:50;padding:8px 14px;border-radius:9px;background:var(--acc);color:var(--acc-ink);font-weight:700;text-decoration:none}
.skip:focus{top:10px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;min-height:36px;padding:7px 14px;border-radius:9px;border:1px solid var(--line);
 background:var(--panel);font-weight:600;cursor:pointer;white-space:nowrap;text-decoration:none}
.btn:hover{border-color:var(--ink3)}.btn:disabled{opacity:.55;cursor:default}
.btn.pri{background:var(--acc);border-color:var(--acc);color:var(--acc-ink)}.btn.pri:hover{filter:brightness(1.08)}
.btn.danger{background:var(--err);border-color:var(--err);color:var(--panel)}
.btn.ghost{background:none;border-color:transparent;color:var(--ink2)}.btn.ghost:hover{background:var(--panel2);color:var(--ink)}
.btn.dan{color:var(--err)}.btn.dan:hover{background:color-mix(in srgb,var(--err) 9%,transparent)}
.btn.sm{min-height:32px;padding:5px 11px;font-size:15px}
.btn svg{flex:none}
.lnk{border:0;background:none;padding:0;color:var(--ink);font-weight:600;text-decoration:underline;cursor:pointer}
.badge{display:inline-flex;align-items:center;gap:6px;padding:3px 10px;border-radius:999px;font-weight:700;font-size:14px;white-space:nowrap}
.badge .bd{width:7px;height:7px;border-radius:50%;background:currentColor}
.badge.ok{background:color-mix(in srgb,var(--ok) 13%,var(--panel));color:var(--ok)}
.badge.err{background:color-mix(in srgb,var(--err) 11%,var(--panel));color:var(--err)}
.badge.warn{background:color-mix(in srgb,var(--warn) 13%,var(--panel));color:var(--warn)}
.badge.mut{background:var(--panel2);color:var(--ink2);box-shadow:inset 0 0 0 1px var(--line)}
input[type=text],input[type=number],input[type=password],select,textarea{width:100%;min-height:40px;padding:8px 11px;border-radius:9px;
 border:1px solid var(--line);background:var(--panel2);min-width:0;outline:none}
select{cursor:pointer}
textarea{min-height:76px;resize:vertical;font-family:var(--mono);font-size:14px;line-height:1.5}
textarea.mask{-webkit-text-security:disc}
input:focus,select:focus,textarea:focus{border-color:var(--acc);box-shadow:0 0 0 3px color-mix(in srgb,var(--acc) 24%,transparent)}
[aria-invalid=true]{border-color:var(--err)!important}
[aria-invalid=true]:focus{box-shadow:0 0 0 3px color-mix(in srgb,var(--err) 22%,transparent)}
input[type=checkbox]{width:20px;height:20px;accent-color:var(--acc);margin:0;flex:none}
.sw{position:relative;width:44px;height:26px;flex:none;display:inline-block}.sw input{position:absolute;inset:0;opacity:0;width:100%;height:100%;margin:0;cursor:pointer;z-index:1}
.sw i{position:absolute;inset:0;border-radius:999px;background:color-mix(in srgb,var(--ink3) 45%,var(--line));transition:background .15s}
.sw i:after{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;border-radius:50%;background:#fff;box-shadow:0 1px 2px rgba(0,0,0,.25);transition:transform .15s}
.sw input:checked+i{background:var(--ok)}.sw input:checked+i:after{transform:translateX(18px)}
.sw input:focus-visible+i{outline:2px solid var(--acc);outline-offset:2px}
@media (prefers-reduced-motion:reduce){.sw i,.sw i:after{transition:none}}
.chips{display:flex;flex-wrap:wrap;gap:6px}
.chip{display:inline-flex;align-items:center;gap:5px;min-height:32px;padding:5px 12px;border-radius:999px;border:1px solid var(--line);background:var(--panel2);
 cursor:pointer;font-size:15px;font-weight:600;color:var(--ink2)}
.chip:hover{border-color:var(--ink3);color:var(--ink)}
.chip.on{background:color-mix(in srgb,var(--acc) 12%,var(--panel));border-color:var(--acc);color:var(--ink)}
.chip svg{color:var(--acc)}
.chips.bad .chip{border-color:color-mix(in srgb,var(--err) 50%,var(--line))}
.res{font-size:15px;font-weight:600}.res.ok{color:var(--ok)}.res.no{color:var(--err)}
.skel{height:12px;border-radius:6px;margin:14px 0;background:linear-gradient(90deg,var(--panel2) 25%,var(--line) 50%,var(--panel2) 75%);
 background-size:200% 100%;animation:sk 1.4s linear infinite}
@keyframes sk{to{background-position:-200% 0}}
@media (prefers-reduced-motion:reduce){.skel{animation:none}}
/* ---- settings ---- */
.set{display:grid;grid-template-columns:188px minmax(0,1fr);gap:28px;align-items:start;max-width:1080px;margin:0 auto}
.setnav{position:sticky;top:calc(var(--toph,104px) + 20px);display:flex;flex-direction:column;gap:2px}
.setnav a{display:flex;align-items:center;gap:8px;padding:9px 12px;border-radius:9px;text-decoration:none;color:var(--ink2);font-weight:600}
.setnav a:hover{background:var(--panel);color:var(--ink)}
.setnav a[aria-current]{background:var(--panel);color:var(--acc);box-shadow:var(--shadow)}
.setnav .dd{margin-left:auto;width:8px;height:8px;border-radius:50%;flex:none}
.setnav a.dirty .dd{background:var(--warn)}.setnav a.bad .dd{background:var(--err)}
.form{display:flex;flex-direction:column;gap:20px;padding-bottom:110px}
.sec{scroll-margin-top:calc(var(--toph,104px) + var(--navh,0px) + 16px);padding:22px 24px 6px}
.sec>header{display:flex;flex-wrap:wrap;gap:8px 12px;align-items:flex-start;padding-bottom:16px}.sec>header>div{flex:1 1 240px}
.sec>header h2{margin:0;font:700 20px var(--display);outline:none}
.sec>header p{margin:4px 0 0;color:var(--ink3);font-size:15px;max-width:64ch}
.sec>header .sp{flex:1}
.sec.state{padding:22px 24px}.sec.state h2{margin:0 0 6px;font:700 20px var(--display)}.sec.state p{color:var(--ink2);margin:0 0 14px}
.srow{display:grid;grid-template-columns:minmax(0,5fr) minmax(0,7fr);gap:10px 32px;padding:18px 0;border-top:1px solid var(--line)}
.srow.full{grid-template-columns:minmax(0,1fr)}
.lab>label,.lt{display:block;font-weight:650;color:var(--ink)}
.lab p{margin:4px 0 0;color:var(--ink2);font-size:14px;max-width:52ch}
.ctl{min-width:0;display:flex;flex-direction:column;gap:8px;align-items:stretch}
.help{color:var(--ink3);font-size:14px;margin:0}
.help:empty,.ferr:empty{display:none}
.ferr{color:var(--err);font-size:14px;font-weight:600;margin:0}
.narrow{max-width:260px}
.affix{display:flex;align-items:stretch;border:1px solid var(--line);border-radius:9px;background:var(--panel2);overflow:hidden}
.affix:focus-within{border-color:var(--acc);box-shadow:0 0 0 3px color-mix(in srgb,var(--acc) 24%,transparent)}
.affix:has([aria-invalid=true]){border-color:var(--err)}
.affix input{border:0!important;border-radius:0;background:none;box-shadow:none!important}
.affix span{display:grid;place-items:center;padding:0 11px;color:var(--ink2);font-size:15px;background:var(--panel)}
.affix .pre{border-right:1px solid var(--line)}.affix .suf{border-left:1px solid var(--line)}
.unitrow{display:flex;gap:12px;align-items:center;flex-wrap:wrap}.unitrow .affix{flex:0 1 220px}
.fields{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:16px}
.fld{display:flex;flex-direction:column;gap:6px;min-width:0}.fld.wide{grid-column:1/-1}
.fld>label,.fld>.lt{font-size:15px;font-weight:600;color:var(--ink)}
.opt{font-weight:500;color:var(--ink3)}
.chk{display:inline-flex;align-items:center;gap:8px;font-weight:500;cursor:pointer;min-height:32px}
.mini-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(104px,1fr));gap:12px}
.mini-grid label{font-size:14px;font-weight:600;color:var(--ink2)}
.qh{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.qh select{width:auto;min-width:128px}.qh label{font-size:15px;color:var(--ink2);font-weight:600}
.swrow{display:flex;align-items:center;gap:10px;min-height:32px}.swrow span{color:var(--ink2);font-weight:600}
.cookie{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
details.disc{border:1px solid var(--line2);border-radius:10px;background:transparent}
details.disc>summary{cursor:pointer;padding:9px 12px;font-weight:600;color:var(--ink2);list-style-position:inside}
details.disc>summary:hover{color:var(--ink)}
details.disc[open]>summary{border-bottom:1px solid var(--line)}
details.disc>div{padding:12px;display:flex;flex-direction:column;gap:8px}
details.disc textarea{background:var(--panel)}
.empty-st{border:1px dashed var(--line);border-radius:12px;padding:18px;text-align:center;color:var(--ink3)}
.empty-st b{display:block;color:var(--ink2);margin-bottom:2px}.empty-st p{margin:0;font-size:15px}
.cnt{font-size:14px;color:var(--ink3);white-space:nowrap;padding-top:4px}
.plist,.chans{display:flex;flex-direction:column;gap:10px}
.pnl{border:1px solid var(--line2);border-radius:12px;background:transparent;padding:12px}
.pnl .pt{display:flex;gap:8px;align-items:center}.pnl .pt input{flex:1;background:var(--panel)}
.pnl .pm{display:flex;gap:6px 12px;align-items:center;flex-wrap:wrap;margin-top:10px;font-size:15px;color:var(--ink2)}
.pnl .ferr{margin-top:8px}
.pnl .btn.dan:not(:hover):not(:focus-visible){color:var(--ink3)}
.cnt b{color:var(--ok);font-weight:600}.cnt em{color:var(--err);font-style:normal;font-weight:600}
@media (max-width:560px){.pnl .pt{flex-wrap:wrap}.pnl .pt input{flex:1 1 100%}}
.ch{border:1px solid var(--line2);border-radius:12px;background:transparent}
.ch.bad{border-color:color-mix(in srgb,var(--err) 55%,var(--line))}
.chh{display:flex;align-items:center;gap:10px;padding:8px 10px 8px 14px}
.chx{flex:1;min-width:0;display:flex;align-items:center;gap:6px 10px;flex-wrap:wrap;border:0;background:none;padding:8px 6px;text-align:left;cursor:pointer;border-radius:8px;color:var(--ink)}
.chx:hover{background:color-mix(in srgb,var(--ink) 4%,transparent)}
.chx .nm{font-weight:700;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.chx .ty{font:600 13.5px var(--sans);color:var(--ink2)}
.chx .sum{margin-left:auto;color:var(--ink3);font-size:14px}
.chx .chev{flex:none;color:var(--ink3);transition:transform .15s}.chx[aria-expanded=true] .chev{transform:rotate(180deg)}
.ch.off .nm{color:var(--ink3)}
.chb{padding:16px;border-top:1px solid var(--line)}
.chb input,.chb select,.chb textarea{background:var(--panel)}
.chb>.ferr{margin:0 0 12px}
.chf{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:18px;padding-top:14px;border-top:1px solid var(--line)}.chf .sp{flex:1}
.pw{display:flex;gap:6px;align-items:flex-start}.pw input,.pw textarea{flex:1}
.secret{display:flex;gap:8px;align-items:center;flex-wrap:wrap;min-height:40px}
.addch{margin-top:12px}.addch select{width:auto;min-width:200px}
.esum{border:1px solid color-mix(in srgb,var(--err) 45%,var(--line));background:color-mix(in srgb,var(--err) 6%,var(--panel));border-radius:var(--rad);padding:14px 18px;outline:none}
.esum:focus-visible{box-shadow:0 0 0 3px color-mix(in srgb,var(--err) 30%,transparent)}
.esum h2{margin:0 0 6px;font:700 17px var(--display);color:var(--err)}.esum ul{margin:0;padding-left:20px}.esum li{margin:3px 0}
@media (max-width:900px){.set{grid-template-columns:minmax(0,1fr);gap:12px}
 .setnav{top:var(--toph,96px);z-index:15;flex-direction:row;gap:6px;overflow-x:auto;scrollbar-width:none;margin:-8px -14px 0;padding:10px 14px;
  background:color-mix(in srgb,var(--bg) 90%,transparent);backdrop-filter:blur(10px)}
 .setnav::-webkit-scrollbar{display:none}
 .setnav a{flex:none;padding:6px 13px;border:1px solid var(--line);border-radius:999px;background:var(--panel);font-size:15px}
 .setnav a[aria-current]{background:var(--ink);border-color:var(--ink);color:var(--bg);box-shadow:none}
 .setnav .dd{margin-left:0}.setnav a:not(.dirty):not(.bad) .dd{display:none}}
@media (max-width:760px){.srow{grid-template-columns:minmax(0,1fr);gap:8px}.sec{padding:18px 16px 4px}.chx .sum{flex-basis:100%;margin-left:0;order:2}.chx .chev{margin-left:auto}}
.savebar{position:fixed;left:50%;bottom:16px;transform:translate(-50%,170%);visibility:hidden;z-index:30;display:flex;align-items:center;gap:10px;
 width:max-content;max-width:calc(100vw - 24px);padding:8px 8px 8px 16px;border-radius:14px;background:var(--ink);color:var(--bg);
 box-shadow:0 10px 30px rgba(0,0,0,.28);transition:transform .2s,visibility .2s}
body[data-view=settings].dirty .savebar{transform:translate(-50%,0);visibility:visible}
.savebar .msg{display:flex;align-items:center;gap:8px;font-weight:600;min-width:0}
.savebar .msg i{width:8px;height:8px;border-radius:50%;background:var(--warn);flex:none}
.savebar .msg span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.savebar .btn.ghost{color:var(--bg)}.savebar .btn.ghost:hover{background:rgba(127,127,127,.25)}
.savebar .btn.pri{background:var(--acc2);border-color:var(--acc2);color:var(--acc2-ink)}
@media (max-width:560px){.savebar{left:10px;right:10px;width:auto;transform:translateY(170%)}
 body[data-view=settings].dirty .savebar{transform:none}.savebar .msg{flex:1}.savebar .where{display:none}}
@media (prefers-reduced-motion:reduce){.savebar{transition:none}}
.toast{position:fixed;right:18px;bottom:18px;z-index:40;padding:12px 16px;border-radius:12px;background:var(--panel);border:1px solid var(--line);
 box-shadow:0 10px 30px rgba(0,0,0,.18);font-weight:600;display:none;max-width:min(440px,calc(100vw - 24px))}
.toast.show{display:flex;align-items:center;gap:10px}.toast:before{content:"";flex:none;width:8px;height:8px;border-radius:50%;background:var(--ok)}
.toast.err{color:var(--err);border-color:color-mix(in srgb,var(--err) 45%,var(--line))}.toast.err:before{background:var(--err)}
body[data-view=settings].dirty .toast{bottom:84px}
@media (max-width:560px){.toast{left:12px;right:12px;max-width:none}}
dialog.dlg{border:1px solid var(--line);border-radius:16px;background:var(--panel);color:var(--ink);padding:22px;width:min(420px,calc(100vw - 32px));
 box-shadow:0 20px 50px rgba(0,0,0,.3)}
dialog.dlg::backdrop{background:rgba(8,12,18,.5)}
dialog.dlg h2{margin:0 0 8px;font:700 19px var(--display)}dialog.dlg p{margin:0 0 20px;color:var(--ink2)}
dialog.dlg .row{justify-content:flex-end}
/* phone: the tabs move to a bottom bar within thumb reach */
@media (max-width:440px){.bar{gap:8px}.brand{font-size:16.5px;gap:8px}.pill{min-width:0;font-size:14px;padding:5px 10px}#pilltx{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.kio{flex:none}}
@media (max-width:760px){.seg button{min-height:44px;padding:4px 13px}.fsum>button,.chip,.btn.sm,.do a,.xmods label,.chk,.swrow{min-height:42px}
 input[type=checkbox]{width:22px;height:22px}#mtable th,#mtable td{padding:10px 6px;font-size:15px}#mtable td.nm{min-width:110px;max-width:170px}#mtable td:nth-child(11){white-space:normal;min-width:64px}#mcount{display:block}.setnav a{min-height:40px;display:inline-flex;align-items:center}}
@media (max-width:760px){.setnav{margin:-8px -12px 0!important;padding:10px 12px!important}}
@media (max-width:760px){main{padding:14px 12px}.bar{padding:10px 14px}.card{padding:0 14px 14px}.card>h2,.h2{margin:0 -14px 12px;padding:10px 14px}
 .acct{display:none}.kio span{display:none}#since:not(.show-date){display:none}.kio{padding:5px 9px}
 body{padding-bottom:calc(64px + env(safe-area-inset-bottom))}
 nav.tabs{position:fixed;left:0;right:0;bottom:0;z-index:25;max-width:none;display:grid;grid-template-columns:repeat(6,minmax(0,1fr));gap:0;padding:0 2px env(safe-area-inset-bottom);
  background:var(--hb);border-top:1px solid color-mix(in srgb,var(--hb-ink) 14%,transparent)}
 nav.tabs a{position:relative;flex-direction:column;gap:3px;min-height:60px;padding:9px 0 7px;border-radius:0;font-size:13.5px;justify-content:center}
 nav.tabs a.on{background:none;color:var(--hb-ink)}nav.tabs a.on svg{color:var(--acc2)}
 nav.tabs a.on:before{content:"";position:absolute;top:0;left:22%;right:22%;height:3px;border-radius:0 0 3px 3px;background:var(--acc2)}
 nav.tabs a .ct{position:absolute;top:4px;left:calc(50% + 6px);padding:0 6px;font-size:13px;line-height:18px}
 nav.tabs a.on .ct{background:color-mix(in srgb,var(--hb-ink) 16%,transparent);color:var(--hb-ink);box-shadow:none}
 nav.tabs a #ctset{font-size:0;width:10px;height:10px;padding:0;top:9px;background:var(--warn);box-shadow:0 0 0 2px var(--hb)}
 .savebar{bottom:calc(76px + env(safe-area-inset-bottom))!important}.toast{bottom:calc(76px + env(safe-area-inset-bottom))!important}
 body[data-view=settings].dirty .toast{bottom:calc(140px + env(safe-area-inset-bottom))!important}}
@media (max-width:360px){
 nav.tabs{grid-template-columns:repeat(3,minmax(0,1fr))}
 nav.tabs a{min-height:49px;padding:5px 0;gap:2px}
 body{padding-bottom:calc(104px + env(safe-area-inset-bottom))}
 .savebar,.toast{bottom:calc(112px + env(safe-area-inset-bottom))!important}
 body[data-view=settings].dirty .toast{bottom:calc(176px + env(safe-area-inset-bottom))!important}
 .wk{grid-template-columns:repeat(4,minmax(0,1fr))}
}
/* ---- export ---- */
.xp{display:grid;grid-template-columns:minmax(0,1fr) 300px;gap:16px;align-items:start}
@media (max-width:900px){.xp{grid-template-columns:1fr}}
.xp .fields{grid-template-columns:repeat(auto-fill,minmax(min(200px,100%),1fr))}
.xp input[type=date]{width:100%;min-height:40px;padding:8px 11px;border-radius:9px;border:1px solid var(--line);background:var(--panel2);color:var(--ink);outline:none;font:inherit}
.xp input[type=date]:focus{border-color:var(--acc);box-shadow:0 0 0 3px color-mix(in srgb,var(--acc) 24%,transparent)}
.xp .hint{font-size:14px;color:var(--ink3);margin:0;max-width:75ch}
.xchips{display:flex;flex-wrap:wrap;gap:6px 14px}
.xmods{max-height:260px;overflow:auto;border:1px solid var(--line);border-radius:10px;background:var(--panel2);padding:4px 10px}
.xmods label{display:flex;gap:8px;align-items:center;min-height:30px;font-size:15px;cursor:pointer}
.xmods label span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.xsum{display:flex;flex-direction:column;gap:12px;position:sticky;top:110px}
@media (max-width:900px){.xsum{position:static}}
.xsum dl{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;margin:0;font-size:15px}
.xsum dt{color:var(--ink3)}.xsum dd{margin:0;color:var(--ink);font-weight:600;overflow-wrap:anywhere}
.xsum .btn{width:100%}.xsum .btn.dis{opacity:.55;pointer-events:none}
.xp [hidden]{display:none!important}
.xprev td,.xprev th{white-space:nowrap;padding:6px 8px;text-align:left}
/* ---- earnings estimate ---- */
.earn{display:grid;grid-template-columns:minmax(0,4fr) minmax(0,4fr) minmax(0,5fr);gap:12px 28px;align-items:start}
.earn h3{margin:0 0 2px;font:600 14px var(--sans);color:var(--ink3);letter-spacing:.02em}
.earn .tot{font:700 40px/1.1 var(--sans);letter-spacing:-.01em;margin:6px 0 4px}
.earn .tot small{font:500 16px var(--sans);color:var(--ink3);margin-left:8px;letter-spacing:0}
.earn .rng{color:var(--ink2);margin:0}.earn .rng b{color:var(--ink);font-weight:650}
.earn .meter{position:relative;height:6px;border-radius:3px;background:var(--panel2);box-shadow:inset 0 0 0 1px var(--line);margin:12px 0 4px}
.earn .meter i{position:absolute;top:-4px;width:3px;height:14px;border-radius:2px;background:var(--acc)}
.earn .meter span{position:absolute;inset:0;border-radius:3px;background:color-mix(in srgb,var(--acc) 22%,transparent)}
.earn .ax{display:flex;justify-content:space-between;font-size:13px;color:var(--ink3)}
.msl .fr{display:flex;justify-content:space-between;align-items:baseline;gap:16px;padding:10px 0;border-bottom:1px solid var(--line2)}
.msl .fr:last-child{border-bottom:0}.msl .fr>span{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.msl .fr>span small{color:var(--ink3);font-size:14px}
.msl .fr>small{color:var(--ink3);font-size:14px;white-space:nowrap}.msl .fr a{text-decoration:none;color:inherit}.msl .fr a:hover{text-decoration:underline;color:var(--acc)}
.msl .f{color:var(--acc);margin-right:8px}.msl .rwp{color:var(--ink2);font-weight:600}
.earn .fr{display:flex;justify-content:space-between;align-items:baseline;gap:16px;padding:10px 0;border-bottom:1px solid var(--line2)}
.earn .fr:last-child{border-bottom:0}.earn .fr>span{color:var(--ink2);min-width:0}.earn .fr>span small{display:block;color:var(--ink3);font-size:14px}
.earn .fr>b{text-align:right;font:650 17px var(--sans);white-space:nowrap}
.earn .fr>span a{text-decoration:none}.earn .fr>span a:hover{text-decoration:underline;color:var(--acc)}
.earn .fr .nm{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:550;color:var(--ink)}
.earnfoot{display:flex;flex-wrap:wrap;align-items:center;gap:8px 16px;margin:14px -20px 0;padding:12px 20px 0;border-top:1px solid var(--line2);font-size:14px;color:var(--ink3)}
.earnfoot p{margin:0;flex:1 1 320px;max-width:80ch}
.earnset{flex:1 1 100%}.earnset summary{cursor:pointer;color:var(--ink2);font-weight:600;width:max-content}
.earnset .row{display:flex;flex-wrap:wrap;gap:12px 16px;align-items:end;margin-top:10px}
.earnset label{display:flex;flex-direction:column;gap:4px;color:var(--ink2);font-size:14px}
.earnset input[type=number]{width:130px}
#etable td.nm{max-width:360px}#etable td .x{font-size:12px;font-weight:600;color:var(--ink3);margin-left:6px}
#etable input[type=checkbox]{width:18px;height:18px;accent-color:var(--acc);cursor:pointer}
@media (max-width:1000px){.earn{grid-template-columns:minmax(0,1fr) minmax(0,1fr)}.earn>div:last-child{grid-column:1/-1}}
@media (max-width:640px){.earn{grid-template-columns:minmax(0,1fr)}.earn .tot{font-size:34px}}
</style></head><body>
<a class=skip href="#main">Skip to content</a>
<header class=top>
 <div class=bar><div class=brand><svg class=mark width=24 height=24 viewBox="0 0 24 24" aria-hidden=true><rect width=24 height=24 rx=6 fill="var(--hb2)"/><circle cx=7 cy=7 r=2.6 fill="var(--d)"/><circle cx=17 cy=7 r=2.6 fill="var(--p)"/><circle cx=7 cy=17 r=2.6 fill="var(--l)"/><circle cx=17 cy=17 r=1.5 fill="none" stroke="var(--hb-ink2)" stroke-width=1.2/></svg>MakerMonitor</div><span class=acct id=acct></span>
  <span class=pill id=pill><span class=dot></span><span id=pilltx>Loading</span></span>
  <a class=kio href="/kiosk" title="Full-screen view for a wall screen or TV"><svg width=16 height=16 viewBox="0 0 16 16" aria-hidden=true><rect x="1.5" y="2.5" width="13" height="8.5" rx="1.5" fill=none stroke=currentColor stroke-width="1.5"/><path d="M5.5 14h5M8 11v3" stroke=currentColor stroke-width="1.5" stroke-linecap=round /></svg><span>Kiosk</span></a></div>
 <nav class=tabs id=tabs aria-label="Dashboard">
  <a href="#overview" data-v=overview><svg width=18 height=18 viewBox="0 0 18 18" aria-hidden=true><g fill=none stroke=currentColor stroke-width="1.6"><rect x="2" y="2" width="6" height="7" rx="1.5"/><rect x="10" y="2" width="6" height="4" rx="1.5"/><rect x="2" y="11" width="6" height="5" rx="1.5"/><rect x="10" y="8" width="6" height="8" rx="1.5"/></g></svg>Overview</a>
  <a href="#models" data-v=models><svg width=18 height=18 viewBox="0 0 18 18" aria-hidden=true><g fill=none stroke=currentColor stroke-width="1.6" stroke-linejoin=round><path d="M9 1.8l6.2 3.5v7.4L9 16.2 2.8 12.7V5.3z"/><path d="M2.8 5.3L9 8.8l6.2-3.5M9 8.8v7.4"/></g></svg>Models</a>
  <a href="#activity" data-v=activity><svg width=18 height=18 viewBox="0 0 18 18" aria-hidden=true><path d="M1.5 9.5h3.2l2-5.5 3.6 10 2-4.5h4.2" fill=none stroke=currentColor stroke-width="1.6" stroke-linecap=round stroke-linejoin=round /></svg>Activity<span class=ct id=ctact hidden></span></a>
  <a href="#export" data-v=export><svg width=18 height=18 viewBox="0 0 18 18" aria-hidden=true><g fill=none stroke=currentColor stroke-width="1.6" stroke-linecap=round stroke-linejoin=round><path d="M9 2.5v8.5M5.5 7.5L9 11l3.5-3.5"/><path d="M2.5 11.5v2.5a1.5 1.5 0 0 0 1.5 1.5h10a1.5 1.5 0 0 0 1.5-1.5v-2.5"/></g></svg>Export</a>
  <a href="#health" data-v=health><svg width=18 height=18 viewBox="0 0 18 18" aria-hidden=true><g fill=none stroke=currentColor stroke-width="1.6" stroke-linecap=round stroke-linejoin=round><path d="M9 15.5S2 11.3 2 6.4A3.6 3.6 0 0 1 9 5a3.6 3.6 0 0 1 7 1.4c0 4.9-7 9.1-7 9.1z"/><path d="M5 8.8h2l1-1.8 1.6 3.4 1-1.6H13"/></g></svg>Health<span class=ct id=cthl hidden></span></a>
  <a href="#settings" data-v=settings id=tab-settings><svg width=18 height=18 viewBox="0 0 18 18" aria-hidden=true><g fill=none stroke=currentColor stroke-width="1.6" stroke-linecap=round><path d="M2.5 5h7M13.5 5h2M2.5 13h2M8.5 13h7"/><circle cx="11.5" cy="5" r="2"/><circle cx="6.5" cy="13" r="2"/></g></svg>Settings<span class=ct id=ctset hidden title="Unsaved changes">Unsaved</span></a></nav>
</header>
<main id=main tabindex=-1>
<div class=banner id=banner role=status></div>
<div class=banner id=dashboard-error hidden><span role=status id=dashboard-error-text></span> <button type=button class="btn sm" id=retry-dashboard>Retry</button></div>

<section class=view id=v-overview aria-label=Overview><div class=next-action id=next-action hidden></div><div class=ov>
 <div class="card ovw o1"><h2><span id=today-title>Today</span><small id=since></small><span class=sp></span><small id=ydaynote></small></h2>
  <div class=hero id=hero></div>
  <details class=hour-details id=hour-details><summary>View hourly counts</summary>
   <p class=mut><span id=hour-zone>Hours use the poller’s time zone.</span> Counts are recorded increases and can differ from daily net totals.</p>
   <label for=hour-metric>Metric</label> <select id=hour-metric></select>
   <div class=tw id=hour-table></div>
  </details><div class=herofoot id=herofoot></div></div>
 <div class=ovl>
 <div class="card o2"><h2 id=needs-you tabindex=-1>To do<span class=sp></span><small id=actn></small></h2><p class=freshness id=action-freshness hidden></p><ul class=acts id=acts></ul></div>
 <div class="card o4"><h2>Top models<span class=sp></span><span class=seg id=topseg role=group aria-label="Top models period"></span></h2><div class="tw fit" id=top></div></div>
 <div class="card o6"><h2>7 and 30 days<span class=sp></span><small>bars: last 7 days</small></h2><div class="tw fit" id=periods></div></div>
 <div class="card o6b"><h2>Followers<span class=sp></span><small>last 30 days</small></h2><div class=minis id=fol></div>
  <div class=bars id=folbars style="height:70px"></div><div class=axis><span>30 days ago</span><span>today</span></div></div>
 </div><div class=ovr>
 <div class="card o3"><h2>Funnel<span class=sp></span><small>30 days, and yesterday</small></h2><div class=fn id=fun></div></div>
 <div class="card o8"><h2><span class=tip tabindex=0 data-tip="★ MakerWorld reward milestones per model, from the same rules as the Earnings estimate. ◆ round-number milestones for your account and models.">Recent milestones</span><span class=sp></span><small>last 7 days</small></h2><div class=msl id=msl></div></div>
 <div class="card o9b"><h2>Best times<span class=sp></span><span class=seg id=bestseg role=group aria-label="Best times metric"></span></h2><div id=best></div><details class=hour-details id=best-hours hidden><summary>View hourly totals</summary>
 <div class=tw id=best-hour-table></div></details></div>
 </div>
 <div class="card ovw o9"><h2>Creator at a glance<span class=sp></span><small id=cprog></small></h2><div class=facts id=ckp></div></div>
 <div class="card ovw o10" id=earncard><h2><span class=tip tabindex=0 data-tip="What your models' lifetime downloads, prints and boosts are worth under MakerWorld's reward rules, using the PrintNexus earnings calculator's formula.">Earnings estimate</span><span class=sp></span><small>lifetime, all models</small></h2><div class=earn id=earn><p class=mut>Waiting for the first poll</p></div>
  <div class=earnfoot><p>An estimate, not a statement. MakerWorld can delay or reject rewards, and likes and comments earn nothing. Uses are downloads plus 2 per print.</p>
   <details class=earnset id=earnset><summary>Assumptions</summary><form class=row id=earnform>
    <label>Model reward adjustment<input type=number id=eadj min=0 max=3 step=0.01 inputmode=decimal></label>
    <label>Value per point, $<input type=number id=eusd min=0 max=10 step=0.0001 inputmode=decimal></label>
    <button class="btn sm pri" type=submit>Save</button><button class="btn sm ghost" type=button id=edef>Use defaults</button>
    <span class=hint>Adjustment seen from 1.0× to 1.5× (default 1.37×); it scales model milestone points only. $0.0816 is a $40 gift card for 490 points. Mark exclusive models under <a href="#models/earnings">Models</a>.</span></form></details></div></div>

</div></section>

<section class=view id=v-models aria-label=Models><div class=grid>
 <div class="card s12"><h2>All models<small id=mcount></small><span class=sp></span><a class=mut id=mclear href="#models" hidden style="font-size:13px">Show all models</a><span class=seg id=mseg role=group aria-label="Models period"></span></h2>
  <p class=freshness id=model-freshness></p><div class=mtools><label class=sr for=mq>Filter models</label><input type=search id=mq placeholder="Filter by name" autocomplete=off><label class=chk><input type=checkbox id=mact> Hide models with no downloads</label></div>
  <div class=model-focus id=model-focus hidden></div><div class=tw id=mtable></div></div>
 <div class="card s12" id=earnmodels><h2 id=earnings-title tabindex=-1>Earnings by model<small id=ecount></small><span class=sp></span><small>estimate, lifetime</small></h2>
  <p class=freshness id=enote></p><div class=tw id=etable></div></div>
 <div class="s12 ihead"><h2>Insights</h2><span class=seg id=iseg role=group aria-label="Insight group"></span><p id=inote></p></div>
 <div class="s12 lists" id=cgrid></div>
</div></section>

<section class=view id=v-activity aria-label=Activity><div class=grid>
 <div class="card s7"><h2>Today's activity<span class=sp></span><small id=feednote>newest first</small></h2><div class=afilter id=afilter hidden></div><div class=fsum id=fsum role=group aria-label="Filter by kind"></div><div class=feed id=feed></div></div>
 <div class="s5" style="display:flex;flex-direction:column;gap:16px;min-width:0">
  <div class=card><h2 id=needs-reply tabindex=-1>Needs a reply<span class=sp></span><small id=unans></small></h2><p class=freshness id=comment-freshness></p><p class=mut style="margin:-4px 0 8px;font-size:14px">Reply under Reviews &amp; Ratings on the model page. MakerWorld opens in a new tab.</p><div class=reply id=reply></div></div>
  <div class=card><h2>Recent comments</h2><div id=comments></div></div></div>
</div></section>

<section class=view id=v-export aria-labelledby=xh><div class=xp>
 <div class=card><h2 id=xh>Export analytics<span class=sp></span><small>CSV</small></h2>
  <div class=fields>
   <div class=fld><label for=xds>Data</label><select id=xds>
    <option value=account>Account, per day</option><option value=models>Models, per day</option>
    <option value=hourly>Account, per hour</option><option value=lifetime>Models, all-time totals</option></select></div>
   <div class=fld id=xgf><label for=xgr>Rows</label><select id=xgr></select></div>
   <div class=fld id=xrf><label for=xrg>Date range</label><select id=xrg>
    <option value=today>Today</option><option value=7d>Last 7 days</option><option value=30d selected>Last 30 days</option>
    <option value=90d>Last 90 days</option><option value=this_month>This month</option><option value=last_month>Last month</option>
    <option value=all>All stored data</option><option value=custom>Custom</option></select></div>
   <div class=fld id=xcf hidden><label for=xa>From</label><input type=date id=xa></div>
   <div class=fld id=xcf2 hidden><label for=xb>To</label><input type=date id=xb></div>
   <div class="fld wide"><span class=lt id=xml>Metrics</span><div class=xchips id=xmets role=group aria-labelledby=xml></div></div>
   <div class="fld wide" id=xmf><span class=lt id=xmdl>Models <span class=opt id=xmn>(all)</span></span>
    <div class=mtools style="margin:0"><label class=sr for=xq>Filter models</label><input type=search id=xq placeholder="Filter by name" autocomplete=off>
     <button class="btn sm ghost" type=button id=xall>Select shown</button><button class="btn sm ghost" type=button id=xnone>Clear</button></div>
    <div class=xmods id=xmods role=group aria-labelledby=xmdl></div><p class=hint>With none ticked, every model is exported.</p></div>
   <p class="hint fld wide" id=xnote aria-live=polite></p><button class="btn sm" type=button id=xretry hidden>Retry loading export options</button>
  </div></div>
 <div class=xsum><div class=card><h2>Your file</h2><dl id=xinfo></dl></div>
  <a class="btn pri dis" id=xgo href="#export" aria-disabled=true download>Download CSV</a>
  <button class=btn type=button id=xpv disabled>Preview</button></div>
 <div class=card id=xpcard hidden style="grid-column:1/-1"><h2>Preview<span class=sp></span><small id=xpn></small></h2><div class="tw xprev" id=xptab></div></div>
</div></section>

<section class=view id=v-health><div class=hsum id=hsum></div><div class=grid>
 <div class="card s12"><h2><span class=tip tabindex=0 data-tip="Each kind of data the poller fetches from MakerWorld, when it last worked, and any error. Three failures in a row turns a source red.">MakerWorld data sources</span></h2><div class=src id=srcs></div></div>
 <div class="card s6"><h2><span class=tip tabindex=0 data-tip="The poller caps how many requests it sends MakerWorld each hour, so it stays polite and isn't blocked. Background analytics pause first when it runs low.">Request budget</span><span class=sp></span><small>shared by every MakerWorld request</small></h2><div id=limiter></div></div>
 <div class="card s6"><h2>Poller</h2><dl class=kv id=pinfo></dl></div>
 <div class="card s12"><h2><span class=tip tabindex=0 data-tip="Desk panels the poller pushes to. Click a name to open that panel's web console.">Panels</span><span class=sp></span><small><a href="#settings/panels">Manage</a></small></h2><div class=src id=pnls></div></div>
</div></section>

<section class=view id=v-settings aria-labelledby=tab-settings><div class=set>
 <nav class=setnav id=setnav aria-label="Settings sections"><a href="#settings/mw" data-s=mw>Account<span class=dd aria-hidden=true></span><span class=sr></span></a><a href="#settings/panels" data-s=panels>Panels<span class=dd aria-hidden=true></span><span class=sr></span></a><a href="#settings/notify" data-s=notify>Notifications<span class=dd aria-hidden=true></span><span class=sr></span></a><a href="#settings/quiet" data-s=quiet>Quiet hours<span class=dd aria-hidden=true></span><span class=sr></span></a><a href="#settings/poll" data-s=poll>Polling<span class=dd aria-hidden=true></span><span class=sr></span></a><a href="#settings/adv" data-s=adv>Advanced<span class=dd aria-hidden=true></span><span class=sr></span></a></nav>
 <div class=form id=form><div class="card sec" aria-busy="true"><div class=skel style="width:28%;height:18px"></div><div class=skel></div><div class=skel style="width:72%"></div><div class=skel style="width:55%"></div></div></div>
</div></section>
</main>
""" + _FOOTER + r"""
<div class=savebar id=savebar role=region aria-label="Unsaved changes"><span class=msg><i aria-hidden=true></i><span id=savetx role=status></span></span>
 <button class="btn ghost sm" type=button id=discard>Discard</button><button class="btn pri sm" type=button id=save title="Save changes (Ctrl+S)">Save changes</button></div>
<div class=toast id=toast role=status aria-live=polite></div>
<div class=sr id=live aria-live=polite></div>
<div class=tipbox id=tipbox role=tooltip hidden></div>
<dialog class=dlg id=dlg aria-labelledby=dlgt aria-describedby=dlgm><form method=dialog><h2 id=dlgt></h2><p id=dlgm></p>
 <div class=row><button class=btn value=cancel autofocus>Cancel</button><button class="btn danger" value=ok id=dlgok></button></div></form></dialog>
<script>
"use strict";
const K=[["downloads","Downloads","d"],["prints","Prints","p"],["boosts","Boosts","b"],["likes","Likes","l"],["collects","Collects","c"]];
const KC={boosts:"b",likes:"l",collects:"c",downloads:"d",prints:"p",comment:"f",ratings:"r"};
// Kind icons: the same outlines the panels draw (firmware-color/tools/gen_icons.py).
const KICO={boosts:"M13 2 4 14h7l-1 8 9-12h-7l1-8z",likes:"M12 21s-7-4.5-9.5-9A5 5 0 0 1 12 6a5 5 0 0 1 9.5 6C19 16.5 12 21 12 21z",collects:"M6 3h12v18l-6-4-6 4z",downloads:"M12 3v12M7 10l5 5 5-5M4 21h16",prints:"M6 9V3h12v6M6 18H4v-7h16v7h-2M8 14h8v7H8z",comment:"M21 12a8 8 0 0 1-11.6 7.1L4 21l1.9-5.4A8 8 0 1 1 21 12z",ratings:"M12 3 14.53 9.12 21.13 9.63 16.09 13.93 17.64 20.37 12 16.9 6.36 20.37 7.91 13.93 2.87 9.63 9.47 9.12z"};
const kIco=(k,l)=>KICO[k]?`<svg class=kico width=16 height=16 viewBox="0 0 24 24" role=img aria-label="${esc(l)}"><title>${esc(l)}</title><path d="${KICO[k]}" fill="${k=="ratings"?"currentColor":"none"}" stroke=currentColor stroke-width=2.2 stroke-linecap=round stroke-linejoin=round /></svg>`:esc(l);
const WD=["Mon","Tue","Wed","Thu","Fri","Sat","Sun"],WDL=["Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"];
const EV={health:"Health alerts",comment:"Comments",follower:"Followers",download:"Downloads",print:"Prints",boost:"Boosts",like:"Likes",collect:"Collects",rating:"Ratings",points:"Points",summary:"Daily summary"};
let aMod=null,aKind=null,mFocus=null,iView="fix",S=null,C=null,topMode="today",bestM="downloads",mSort="d30",mDir=-1,mView="30",mQ="",mAct=false;
let creatorRequestFailed=false, dashboardLoading=false, replyDetail=null, replyRequest=0;
const SP={};  // model id -> [daily downloads] for sparklines
const $=id=>document.getElementById(id),fmt=n=>(+n||0).toLocaleString(),sg=n=>(n>0?"+":"")+fmt(n);
const esc=s=>String(s??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const ago=t=>{if(!t)return"never";const s=Date.now()/1e3-t;return s<45?"just now":s<3600?Math.round(s/60)+" min ago":s<86400?Math.round(s/3600)+" h ago":Math.round(s/86400)+" d ago"};
const hm=t=>new Date(t*1e3).toLocaleTimeString("en-US",{hour:"numeric",minute:"2-digit",hour12:true});
const hh=i=>(i%12||12)+":00 "+(i<12?"AM":"PM");
const q12=q=>q?String(q).split("-").map(x=>{const[h,m]=x.trim().split(":").map(Number);return isNaN(h)?x:(h%12||12)+":"+String(m||0).padStart(2,"0")+" "+(h<12?"AM":"PM")}).join("-"):"";
const pc=(v,d=1)=>v==null?"–":(100*v).toFixed(d)+"%",nf=(v,d=1)=>v==null?"–":(+v).toFixed(d);
const EXT=`<svg class=exi width=10 height=10 viewBox="0 0 10 10" aria-hidden=true><path d="M4 1.5H1.5v7h7V6M6 1.5h2.5V4M8.5 1.5L4.5 5.5" fill=none stroke=currentColor stroke-width=1.3 stroke-linecap=round stroke-linejoin=round /></svg>`;
const mwUrl=id=>`https://makerworld.com/en/models/${encodeURIComponent(id)}`;
// Model id by name, for events and comments that only carry the model's name.
const modelName=(id,fallback)=>S?.dash?.modelNames?.[id]||fallback;
const idOf=n=>{if(!C||!n)return null;for(const r of Object.values(C.models||{}))if(r.n==n)return r.id;return null};
// A row of small actions under an item: [label, href, tip, external?]
const DO=list=>`<span class=do>${list.filter(Boolean).map(([l,h,t,x])=>`<a href="${h}"${x?" target=_blank rel=noopener":""}${t?` data-tip="${esc(t)}"`:""}>${l}${x?EXT:""}</a>`).join("")}</span>`;
const OPEN=(id,l="Open on MakerWorld")=>id?[l,mwUrl(id),"Opens the model page in a new tab.",1]:null;
const TIP=(txt,t)=>`<span class=tip tabindex=0 data-tip="${esc(t)}">${txt}</span>`;
// Published but not public on MakerWorld (only the signed-in owner list has them).
const PRIV=id=>(S?.dash?.private||[]).includes(String(id))?`<span class=priv data-tip="Not public on MakerWorld">Private</span> `:"";
const mwLink=(id,n)=>PRIV(id)+(id?`<a href="https://makerworld.com/en/models/${encodeURIComponent(id)}" target=_blank rel=noopener title="${esc(n)}">${esc(n)}</a>`:`<span title="${esc(n)}">${esc(n)}</span>`);
function seg(el,items,cur,set){
 const active=el.contains(document.activeElement)?document.activeElement.dataset.k:null;
 el.innerHTML=items.map(([k,l])=>`<button type=button class="${k==cur?"on":""}" aria-pressed="${k==cur}" data-k="${k}">${l}</button>`).join("");
 el.querySelectorAll("button").forEach(b=>b.onclick=()=>{set(b.dataset.k);renderAll()});
 if(active)el.querySelector(`button[data-k="${active}"]`)?.focus({preventScroll:true});
}
function spark(vals,color,w=90,h=22,fill=true){if(!vals||!vals.length)return"";const mx=Math.max(1,...vals),n=vals.length,st=n>1?w/(n-1):0;
 const pts=vals.map((v,i)=>`${(i*st).toFixed(1)},${(h-2-(v/mx)*(h-4)).toFixed(1)}`).join(" ");
 return `<svg class=spark viewBox="0 0 ${w} ${h}" preserveAspectRatio=none aria-hidden=true>${fill?`<polygon points="0,${h} ${pts} ${w},${h}" fill="var(--${color})" opacity=.14 />`:""}<polyline points="${pts}" fill=none stroke="var(--${color})" stroke-width=1.6 stroke-linejoin=round vector-effect=non-scaling-stroke /></svg>`}
function table(head,rows,empty="Nothing yet"){return `<table><thead><tr>${head.map(h=>`<th>${h}</th>`).join("")}</tr></thead><tbody>`+
 (rows.length?rows.join(""):`<tr><td colspan=${head.length} class=empty>${empty}</td></tr>`)+"</tbody></table>"}

/* ---------- routing ---------- */
function tabScroll(){const tb=$("tabs"),ta=tb.querySelector(".on");if(ta&&tb.scrollWidth>tb.clientWidth)tb.scrollLeft=ta.offsetLeft-tb.clientWidth+ta.offsetWidth+24}
function focusDestination(id){
 const target=$(id);
 if(target){target.focus({preventScroll:true});target.scrollIntoView({block:"start"})}
}
function route(){const h=(location.hash||"#overview").slice(1),[v,sub]=h.split("/");const view=$("v-"+v)?v:"overview";document.body.dataset.view=view;
 document.querySelectorAll("section.view").forEach(s=>s.classList.toggle("on",s.id=="v-"+view));
 document.querySelectorAll("#tabs a").forEach(a=>{const on=a.dataset.v==view;a.classList.toggle("on",on);on?a.setAttribute("aria-current","page"):a.removeAttribute("aria-current")});
 tabScroll();
 if(view==="overview"&&sub==="actions")requestAnimationFrame(()=>focusDestination("needs-you"));
 if(view=="settings"){setTop();if(!F)loadSettings().then(()=>sub&&goSec(sub));else if(sub)setTimeout(()=>goSec(sub),30);spy()}
 if(view=="activity"){aMod=sub&&sub!="replies"&&!sub.startsWith("k:")?sub:null;
  aKind=sub&&sub.startsWith("k:")?sub.slice(2):null;
  if(S){renderActivity();if(aMod)loadModelReplies();if(aMod||sub=="replies")requestAnimationFrame(()=>focusDestination("needs-reply"))}}
 if(view=="models"){if(mFocus&&!sub){mQ="";$("mq").value="";}mFocus=null;
  if(!sub&&C)renderModels();
  if(sub=="earnings"){if(C)renderModels();requestAnimationFrame(()=>focusDestination("earnings-title"))}
  else if(sub&&["fix","grow","aud"].includes(sub)){iView=sub;if(C)renderModels();setTimeout(()=>$("iseg").scrollIntoView({behavior:SMOOTH,block:"start"}),60)}
  else if(sub&&sub.startsWith("sort:")){mSort=sub.slice(5);mDir=-1;mQ="";$("mq").value="";if(C)renderModels()}
  else if(sub){const r=(C&&C.models||{})[sub];if(r){mFocus=sub;mQ=modelName(sub,r.n);$("mq").value=mQ;mAct=false;$("mact").checked=false;renderModels();
   requestAnimationFrame(()=>focusDestination("model-focus-title"))}}
  loadSparks()}}
addEventListener("hashchange",route);

/* ---------- header, banner ---------- */
function renderHead(){const h=S.health||{},st=S.status||{};
 $("acct").textContent=S.account?"@"+S.account:"";
 const bad=h.auth=="expired",stale=h.stale,dead=h.loopStalled,pill=$("pill");
 const first=!st.lastPoll;  // just started: nothing has been polled yet, so auth and ok aren't known
 pill.className="pill "+(dead?"err":first?"":bad||st.ok===false?"err":stale||h.auth=="none"?"warn":"ok");
 $("pilltx").textContent=dead?"Not polling":first?"Starting, first poll soon":bad?"Cookie expired":stale?"Data stale":st.ok===false?"Last poll failed":(innerWidth<440?"":"Updated ")+ago(st.lastPoll);
 const bn=$("banner");
 if(dead){bn.className="banner err";bn.textContent=`The poller stopped checking MakerWorld at ${hm(h.loopTick)}. Restart makermonitor-poller to get it going again.`}
 else if(first)bn.className="banner";
 else if(bad){bn.className="banner err";bn.innerHTML=`MakerWorld rejected the cookie. Open makerworld.com so the extension refreshes it, or <a href="#settings/mw">paste one in Settings</a>.`}
 else if(stale){bn.className="banner warn";bn.textContent=`MakerWorld data hasn't updated since ${hm(h.since)}. Health has the details.`}
 else if(h.auth=="none"){bn.className="banner warn";bn.innerHTML=`No MakerWorld cookie yet, so only public stats show. <a href="#settings/mw">Add one in Settings</a>.`}
 else if(st.ok===false){bn.className="banner warn";bn.innerHTML=`The latest poll failed. <a href="#health">View poll status</a>.`}
 else bn.className="banner";
 const fails=Object.values(h.src||{}).filter(x=>x.f).length;const ct=$("cthl");ct.hidden=!fails;ct.textContent=fails;
 // The poller keeps the last 40 events, so a full list may be missing earlier ones.
 const nf=((S.dash||{}).feedAll||S.feed||[]).length;$("ctact").hidden=!nf;$("ctact").textContent=nf>=40?"40+":nf;tabScroll()}

/* ---------- overview ---------- */
// The poller's local hour (S.tz is its UTC offset in seconds), so the day lines
// line up with its hourly buckets even if the browser is in another timezone.
// Anchored at S.t, when today's numbers were measured.
const pSec=()=>{const t=S&&S.t?S.t:Date.now()/1e3;return S&&S.tz!=null?(((t+S.tz)%86400)+86400)%86400:(new Date(t*1e3).getHours()*3600+new Date(t*1e3).getMinutes()*60)};
const pHour=()=>Math.floor(pSec()/3600);
const sum=(a,n)=>(a||[]).slice(0,n).reduce((x,v)=>x+(+v||0),0);
// The day as a row of peg holes, one per hour: yesterday's count is the outline ring,
// today's (up to now) the filled peg, and the current hour gets a tick underneath.
const ARR=d=>`<svg width=10 height=10 viewBox="0 0 10 10" aria-hidden=true><path d="${d>0?"M5 1.2L9.2 7.6H.8z":"M5 8.8L.8 2.4h8.4z"}" fill=currentColor /></svg>`;
function pegs(td,yd,color,h,o={}){const W=240,H=o.big?24:20,cy=H/2-(o.big?0:1),R=o.big?4.8:3.4,r0=o.big?1.2:1.7,t=td||[],y=yd||[];
 const mx=Math.max(1,...t.slice(0,h+1).map(v=>+v||0),...y.map(v=>+v||0)),rad=v=>r0+(R-r0)*(o.lin?Math.max(0,+v||0)/mx:Math.sqrt(Math.max(0,+v||0)/mx));
 let s=`<svg class=pegs viewBox="0 0 ${W} ${H}" aria-hidden=true>`;
 for(let i=0;i<24;i++){const x=i*10+5,tv=i<=h?+t[i]||0:0,yv=+y[i]||0;s+=o.tip?`<g><title>${esc(o.tip(i))}</title>`:"<g>";
  s+=`<circle cx=${x} cy=${cy} r=${r0} fill="color-mix(in srgb,var(--ink3) 45%,var(--panel))"/>`;
  // Yesterday's ring sits at its true size, over today's peg; inside a bigger peg it is drawn in the panel colour.
  if(tv>0)s+=`<circle cx=${x} cy=${cy} r=${rad(tv).toFixed(2)} style="fill:var(--${color})"${i===o.hi?` stroke="var(--ink)" stroke-width=1.4`:""}/>`;
  if(yv>0)s+=`<circle cx=${x} cy=${cy} r=${rad(yv).toFixed(2)} fill=none stroke="var(--${tv>0&&rad(yv)<rad(tv)-.6?"panel":"ink3"})" stroke-width=1.2 />`;
  s+="</g>"}
 if(o.now!==false&&h>=0&&h<24)s+=`<rect x=${h*10+1.5} y=${H-1.8} width=7 height=1.8 rx=.9 fill="var(--acc)"/>`;
 return s+"</svg>"}
// Today tiles: today's hours as a filled line in the stat's colour, yesterday's as a dashed line.
function dayLine(td,yd,color,h,w=120,ht=30){const t=(td||[]).slice(0,h+1),all=[...t,...(yd||[])].map(v=>+v||0),mx=Math.max(1,...all),st=w/23;
 const pts=a=>a.map((v,i)=>`${(i*st).toFixed(1)},${(ht-2-((+v||0)/mx)*(ht-5)).toFixed(1)}`).join(" ");
 return `<svg class=dl viewBox="0 0 ${w} ${ht}" preserveAspectRatio=none aria-hidden=true>`+
  (yd&&yd.length?`<polyline points="${pts(yd)}" fill=none stroke="var(--ink3)" stroke-opacity=.55 stroke-width=1.2 stroke-dasharray="3 3" vector-effect=non-scaling-stroke />`:"")+
  (t.length>1?`<polygon points="0,${ht} ${pts(t)} ${((t.length-1)*st).toFixed(1)},${ht}" style="fill:var(--${color})" opacity=.14 /><polyline points="${pts(t)}" fill=none style="stroke:var(--${color})" stroke-width=1.7 stroke-linejoin=round vector-effect=non-scaling-stroke />`:"")+`</svg>`}
// Ratings have no hourly buckets, so that tile shows the average as stars.
const STAR=x=>{let d="";for(let i=0;i<10;i++){const r=i%2?4.6:10.5,t=-Math.PI/2+i*Math.PI/5;d+=(i?"L":"M")+(x+12+r*Math.cos(t)).toFixed(1)+" "+(12.6+r*Math.sin(t)).toFixed(1)}return d+"Z"};
function stars(a){const pct=Math.max(0,Math.min(100,a/5*100));
 return `<svg class=stars viewBox="0 0 120 24" role=img aria-label="${a?a.toFixed(1)+" out of 5 stars":"no ratings yet"}"><defs><linearGradient id=rg><stop offset="${pct}%" style="stop-color:var(--r)"/><stop offset="${pct}%" style="stop-color:color-mix(in srgb,var(--r) 22%,var(--panel2))"/></linearGradient></defs><path fill="url(#rg)" d="${[0,1,2,3,4].map(i=>STAR(i*24)).join("")}"/></svg>`}
const HAX=`<div class=hax>${[[0,"12 AM"],[6,"6 AM"],[12,"12 PM"],[18,"6 PM"],[23,"11 PM"]].map(([i,l])=>`<span style="left:${((i*10+5)/2.4).toFixed(2)}%">${l}</span>`).join("")}</div>`;
function brow(l,c,val,zero,body,cmp,k){return `<div class="brow${zero?" zero":""}" style="--k:var(--${c})"><div class=lab><span><span class=kdot></span>${k?`<a class=rowlink href="#activity/k:${k}" data-tip="See today's ${l.toLowerCase()}, model by model">${l}</a>`:l}</span><div class="val${zero?" zero":""}">${val}</div></div>${body}<div class=cmp>${cmp}</div></div>`}
const PTIP={downloads:"Downloads of your models' files.",prints:"Prints started from your print profiles.",boosts:"Boosts other people gave your models.",likes:"Likes on your models.",collects:"Times someone saved a model to a collection.",
 views:"Times a model page was opened.",impressions:"Times your models were shown in feeds, search and recommendations.",points:"MakerWorld points from all sources."};
const PERIOD_ROWS=[...K,["views","Views","ink2"],["impressions","Impressions","ink3"],["points","Points","r"]];
function wk7(vals,color){const mx=Math.max(1,...vals),n=vals.length,bw=64/7;
 return `<svg class=wk7 viewBox="0 0 64 20" aria-hidden=true>${vals.map((v,i)=>`<rect x="${(i*bw+1).toFixed(1)}" y="${(20-Math.max(1.5,v/mx*19)).toFixed(1)}" width="${(bw-2).toFixed(1)}" height="${Math.max(1.5,v/mx*19).toFixed(1)}" rx=1.5 style="fill:var(--${color})" opacity="${i==n-1?1:.55}"/>`).join("")}</svg>`}
$("hour-metric").innerHTML=K.map(([key,label])=>`<option value="${key}">${label}</option>`).join("");
function renderHourTable(){
 if(!S||!$("hour-details").open)return;
 const key=$("hour-metric").value,label=K.find(([k])=>k===key)[1],current=pHour();
 const offset=S.tz,minutes=Math.round(Math.abs(offset||0)/60);
 $("hour-zone").textContent=offset==null?"Hours use the poller’s time zone.":
  `Hours use the poller’s time zone (UTC${offset<0?"−":"+"}${Math.floor(minutes/60)}:${String(minutes%60).padStart(2,"0")}).`;
 const X=S.dash||{},span=X.hourSpan,previous=X.yhourSpan;
 const today=S.hours?.[key],yesterday=X.yhours?.[key];
 const localHour=t=>t?Math.floor((((t+(offset||0))%86400)+86400)%86400/3600):null;
 const first=localHour(span?.from??(!S.midnight?S.since:null)),last=localHour(span?.to)??current;
 const oldFirst=localHour(previous?.from),oldLast=localHour(previous?.to);
 const isToday=!span?.date||span.date===X.calendarDay;
 const count=(value,hour,from,to)=>value==null?"Unavailable":
  ((from!=null&&hour<from)||(to!=null&&hour>to))?"Not tracked":fmt(value);
 const rows=Array.from({length:24},(_,hour)=>`<tr><th scope=row>${hh(hour)}${hour===last&&isToday?" (partial)":""}</th>
  <td>${hour>last&&isToday&&hour>current?"Upcoming":count(today?.[hour],hour,first,last)}</td>
  <td>${count(yesterday?.[hour],hour,oldFirst,oldLast)}</td></tr>`);
 $("hour-table").innerHTML=`<table><caption>${label} by hour</caption><thead><tr><th scope=col>Hour</th>
 <th scope=col>${isToday?"Today":esc(span.date)}</th><th scope=col>${!isToday&&previous?.date?esc(previous.date):"Yesterday"}</th>
 </tr></thead><tbody>${rows.join("")}</tbody></table>`;

}
$("hour-details").addEventListener("toggle",renderHourTable);
$("hour-metric").addEventListener("change",renderHourTable);
function renderBestHourTable(){
 const best=$("best-hours"),ins=S?.insights,metric=ins?.[bestM];
 best.hidden=!(ins?.has&&metric?.hour);
 if(best.hidden||!best.open)return;
 const rows=metric.hour.map((n,hour)=>`<tr><th scope=row>${hh(hour)}</th><td>${n==null?"Unavailable":fmt(n)}</td></tr>`);
 $("best-hour-table").innerHTML=`<table><caption>${cap(bestM)} by hour across ${fmt(ins.days)} recorded days</caption>
  <thead><tr><th scope=col>Hour</th><th scope=col>Total</th></tr></thead><tbody>${rows.join("")}</tbody></table>`;
}
$("best-hours").addEventListener("toggle",renderBestHourTable);
function renderOverview(){const t=S.today||{},y=S.yday||{},hr=S.hours||{},X=S.dash||{},yh=X.yhours,h=pHour();
 const oldDay=X.hourSpan?.date&&X.calendarDay&&X.hourSpan.date!==X.calendarDay;
 $("today-title").textContent=oldDay?"Last recorded day":"Today";
 $("since").classList.toggle("show-date",Boolean(oldDay));
 $("since").textContent=oldDay?X.hourSpan.date:S.midnight?"since midnight":"since "+hm(S.since||0);
 $("ydaynote").textContent=yh?"dashed line: yesterday":"";
 const tot=S.total||{},m=S.month||{},ra=+tot.ratingAvg||0,rv=t.ratings||0;
 $("hero").innerHTML=K.map(([k,l,c])=>{const v=t[k]||0,yv=y[k],yk=yh&&yh[k];
  let cmp="&nbsp;";
  if(yv!=null){const d=v-yv;cmp=`<span class="${d>0?"up":d<0?"down":""}">${d?sg(d):"±0"}</span>${TIP(`vs ${fmt(yv)} yesterday`,`Yesterday's ${l.toLowerCase()} for the whole day.`)}`}
  return `<div class=stat style="--k:var(--${c})"><div class=lab><span><span class=kdot></span><a class=rowlink href="#activity/k:${k}" data-tip="See today's ${l.toLowerCase()}, model by model">${l}</a></span></div>
   <div class="val${v?"":" zero"}">${v?sg(v):"0"}</div><div class=cmp>${cmp}</div>${dayLine(hr[k],yk,c,h)}</div>`}).join("")+
  `<div class=stat style="--k:var(--r)"><div class=lab><span><span class=kdot></span>Ratings</span><span data-tip="Average of ${fmt(tot.ratings)} ratings">${ra?ra.toFixed(2)+" avg":""}</span></div>
   <div class="val${rv?"":" zero"}">${rv?sg(rv):"0"}</div><div class=cmp>${y.ratings!=null?`<span>vs ${fmt(y.ratings)} yesterday</span>`:`<span>${fmt(tot.ratings||0)} total</span>`}</div>${stars(ra)}</div>`;
 renderHourTable();
 const acu=S.account?"https://makerworld.com/en/@"+encodeURIComponent(S.account):null;
 $("herofoot").innerHTML=[t.points!=null?`<span>${TIP("Points today","MakerWorld points earned since midnight, from every source.")}<b>${sg(+(+t.points).toFixed(1))}${y.points!=null?`<small>yesterday ${nf(y.points)}</small>`:""}</b></span>`:"",
  m.yday_views!=null?`<span>${TIP("Views yesterday","Times someone opened one of your model pages yesterday. MakerWorld reports this a day late.")}<b>${fmt(m.yday_views)}</b></span>`:"",
  m.yday_impressions!=null?`<span>${TIP("Impressions yesterday","Times your models were shown in feeds, search and recommendations yesterday.")}<b>${fmt(m.yday_impressions)}</b></span>`:"",
  `<span>Followers<b>${acu?`<a class=rowlink href="${acu}" target=_blank rel=noopener data-tip="Opens your MakerWorld profile">${fmt(tot.followers)}</a>`:fmt(tot.followers)}</b></span>`,`<span>Models<b><a class=rowlink href="#models" data-tip="See every model in the Models table">${fmt(tot.models)}</a></b></span>`].join("");
 seg($("topseg"),[["today","Today"],["week","7 days"],["life","Lifetime"]],topMode,v=>topMode=v);
 const rows=((S.top||{})[topMode]||[]);
 $("top").innerHTML=table(["Model",`<span class=nophone data-tip="Daily downloads over the last 30 days">30 days</span>`,...K.map(([k,l,c])=>`<span data-tip="${esc(PTIP[k])} ${topMode=="today"?"Today":topMode=="week"?"Last 7 days":"All time"}."><span class=kdot style="--k:var(--${c})"></span>${l}</span>`)],
  rows.map(r=>`<tr><td class=nm>${mwLink(r.id,r.n)}</td><td class=nophone>${spark(SP[r.id],"d")}</td><td>${r.d}</td><td>${r.p}</td><td>${r.b}</td><td>${r.l}</td><td>${r.c}</td></tr>`),
  topMode=="today"?"No model activity yet today":"No activity yet");
 const fo=S.followers||{},fs=fo.series30||[],fm=Math.max(1,...fs.map(Math.abs));
 $("fol").innerHTML=[[fmt(fo.total),"total",""],[sg(fo.today||0),"today",fo.today],[sg(fo.d7||0),"7 days",fo.d7],[sg(fo.d30||0),"30 days",fo.d30]]
  .map(([v,l,n])=>`<div class=mini><b class="${n>0?"up":n<0?"down":""}">${v}</b><span>${l}</span></div>`).join("");
 $("folbars").innerHTML=fs.map((v,i)=>`<i class="${v<0?"neg":v>0?"hi":""}" style="height:${Math.max(3,Math.abs(v)/fm*100)}%" title="${fs.length-1-i?fs.length-1-i+" days ago":"today"}: ${sg(v)}"></i>`).join("");
 const days=S.days||[],d7={};days.forEach(d=>PERIOD_ROWS.forEach(([k])=>d7[k]=(d7[k]||0)+(+d[k]||0)));const a=(C&&C.acct)||{};
 const m30={boosts:m.boosts,likes:m.likes,collects:m.saves,downloads:m.downloads,prints:m.prints,views:m.views,impressions:m.impressions,points:m.points};
 // Views: both columns from the per-model analytics when there, so 7 and 30 days agree.
 if(a.views30!=null)m30.views=a.views30;
 const w7={views:a.views7,impressions:null,points:d7.points!=null?d7.points:null};
 const life={...tot,views:null,impressions:null};
 const cell=(v,s)=>v==null?"<span class=mut>–</span>":s?sg(Math.round(v)):fmt(Math.round(v));
 $("periods").innerHTML=table(["",`<span class=nophone>Daily</span>`,"7 days","30 days","Lifetime"],PERIOD_ROWS.map(([k,l,c])=>{
  const series=K.some(x=>x[0]==k)||k=="points"?days.map(d=>+d[k]||0):null,v7=k in w7?w7[k]:d7[k];
  if(m30[k]==null&&v7==null&&life[k]==null)return "";
  return `<tr><td class=nm><span class=kdot style="--k:var(--${c})"></span>${TIP(l,PTIP[k]||l)}</td><td class=nophone>${series&&series.length>1?wk7(series,c):""}</td><td>${cell(v7,K.some(x=>x[0]==k)||k=="points")}</td><td>${cell(m30[k],K.some(x=>x[0]==k)||k=="points")}</td><td>${cell(life[k])}</td></tr>`}).filter(Boolean));
 const ins=S.insights||{};seg($("bestseg"),[["downloads","Downloads"],["prints","Prints"],["boosts","Boosts"]],bestM,v=>bestM=v);
 const b=ins[bestM];
 if(!ins.has||!b){$("best").innerHTML=`<p class=mut>Best times show after a week of data. ${ins.days||0} of 7 days collected so far.</p>`}else{
  const mx=Math.max(1,...b.hour),wn=X.wdayN||[],tw=X.todayWd,tsum=sum((S.hours||{})[bestM],24),
   av=b.wday.map((v,i)=>wn[i]?(v-(i===tw?tsum:0))/wn[i]:null),bd=av.some(v=>v!=null)?av.indexOf(Math.max(...av.filter(v=>v!=null))):b.bestDay;
  $("best").innerHTML=`<p class=mut style="margin:0 0 6px;font-size:14px">${cap(bestM)} by hour of the day, all ${ins.days} days added up. The busiest hour is outlined.</p>
  ${pegs(b.hour,null,KC[bestM],23,{big:true,lin:true,hi:b.bestHour,now:false,tip:i=>`${hh(i)}: ${fmt(b.hour[i])} over ${ins.days} days`})}${HAX}
  <div class=wk>${b.wday.map((v,i)=>`<div class="${i==bd?"hi":""}" title="${av[i]!=null?nf(av[i])+" a day over "+wn[i]+" full "+WD[i]+(wn[i]==1?"":"s"):fmt(v)}">${WD[i]}<b>${av[i]!=null?nf(av[i]):fmt(v)}</b></div>`).join("")}</div>
  <p class=mut style="margin:12px 0 0">Busiest hour ${hh(b.bestHour)}, busiest day ${WDL[bd]||"–"}. Day cells show ${wn.length?"the average a day":"the total"}, over the last ${ins.days} days.</p>`}
 renderBestHourTable();renderActs();renderFunnel();renderKpis();renderMilestones()}
/* ---------- recent milestones ---------- */
// Round-number ladder (same as /kiosk): a milestone is crossing a multiple of msStep(v).
function msStep(v){v=Math.abs(v);if(v<100)return 10;if(v<1000)return 50;if(v<10000)return 250;if(v<100000)return 2500;
 return Math.pow(10,Math.floor(Math.log10(v)))/4}
const MSK=[["downloads","downloads"],["prints","prints"],["likes","likes"],["boosts","boosts"],["points","points"],["followers","followers"]];
// Account crossings per day (from daily deltas walked back from the total), plus per-model crossings this week.
function msCrossings(){const out=[],tot=S.total||{},days=S.days||[];
 const label=i=>{const n=days.length;if(i==n-1)return"today";if(i==n-2)return"yesterday";const w=WD.indexOf((days[i]||{}).d);return w>=0?WDL[w]:(n-1-i)+" days ago"};
 for(const [k,l] of MSK){if(tot[k]==null)continue;
  const s=k=="followers"?((S.followers||{}).series30||[]).slice(-days.length||-7):days.map(d=>+d[k]||0),n=s.length;let end=+tot[k];
  for(let i=n-1;i>=0;i--){const start=end-s[i],st=msStep(Math.max(start,1)),a=Math.floor(start/st),b=Math.floor(end/st);
   if(b>a&&start>0)out.push({t:`<b>${fmt(b*st)}</b> ${l}`,when:k=="followers"?(i==n-1?"today":i==n-2?"yesterday":(n-1-i)+" days ago"):label(i),age:n-1-i});
   end=start}}
 for(const [id,m] of Object.entries((C&&C.models)||{})){const life=m.life||{},w=m.w||{};
  for(const [k,l] of [["d","downloads"],["p","prints"]]){const end=+life[k]||0,start=end-(+w[k]||0);if(start<=0||end<=start)continue;
   const st=msStep(start),a=Math.floor(start/st),b=Math.floor(end/st);
   if(b>a)out.push({t:`<b>${fmt(b*st)}</b> ${l} <small>· ${PRIV(id)}<a href="#models/${encodeURIComponent(id)}">${esc(modelName(id,m.n))}</a></small>`,when:"this week",age:8})}}
 // MakerWorld reward milestones (model ladder from earnings.py): weighted uses now vs 7 days ago.
 const e=EST();
 for(const [id,em] of Object.entries((e&&e.models)||{})){const w=((C&&C.models||{})[id]||{}).w||{};
  const now=+em.u||0,was=now-(+w.d||0)-2*(+w.p||0);if(was<0||now<=was)continue;
  const n=rwCount(now)-rwCount(was);if(n<=0)continue;
  const p=(rwPts(now)-rwPts(was))*(em.x?1.25:1)*(+e.adjust||1);
  out.push({t:`<b>${fmt(rwTop(now))}</b> uses${n>1?` <small>(${n} reward milestones)</small>`:""} <small>· ${PRIV(id)}<a href="#models/${encodeURIComponent(id)}">${esc(modelName(id,id))}</a></small>`,
   pts:p,when:"this week",age:7,rw:1})}
 return out.sort((a,b)=>a.age-b.age||(b.pts||0)-(a.pts||0))}
// Reward ladder: [tier upper bound in uses, uses per milestone, points per milestone].
const RW=[[50,10,15],[500,25,12],[1000,50,20],[null,100,30]];
function rwWalk(u,f){let tot=0,lo=0,top=0;for(const [hi,st,p] of RW){if(u<=lo)break;const t=hi==null?u:Math.min(u,hi),k=Math.floor((t-lo)/st);
 tot+=f=="pts"?k*p:k;if(k)top=lo+k*st;if(hi==null)break;lo=hi}return f=="top"?top:tot}
const rwCount=u=>rwWalk(u,"n"),rwPts=u=>rwWalk(u,"pts"),rwTop=u=>rwWalk(u,"top");
function renderMilestones(){const x=msCrossings().slice(0,10);
 $("msl").innerHTML=x.length?x.map(r=>`<div class=fr><span><span class=f aria-hidden=true>${r.rw?"★":"◆"}</span>${r.t}</span><small>${r.rw?`<b class=rwp>+${pts(r.pts)} pts</b> · `:""}${r.when}</small></div>`).join("")+
  (x.some(r=>r.rw)?`<p class=mut style="margin:8px 0 0;font-size:14px">★ MakerWorld reward milestones (uses = downloads + 2 × prints), points estimated as in Earnings. ◆ round numbers.</p>`:"")
  :`<p class=mut>No milestones crossed in the last 7 days.</p>`}
function renderKpis(){if(!C)return;const a=C.acct||{},p=C.progress||{},fo=S.followers||{};
 const pv=p.private?`, ${p.private} private not counted`:"";
 $("cprog").textContent=p.of?(p.have<p.of?`analytics for ${p.have} of ${p.of} public models so far${pv}`:`analytics for all ${p.of} public models${pv}`):"waiting for the first poll";
 const e=(a.e100||{}).m||{},tr=a.ptsTrend=="up"?"up":a.ptsTrend=="down"?"down":"",dd=a.dpv7!=null&&a.dpv30!=null?(a.dpv7>a.dpv30?"up":a.dpv7<a.dpv30?"down":""):"";
 const row=(l,sub,v,extra="",tip="",href="")=>`<div class=fr><span>${tip?TIP(l,tip):l}${sub?`<small>${sub}</small>`:""}</span><b>${href?`<a class=rowlink href="${href}">${v}</a>`:v}${extra?`<small>${extra}</small>`:""}</b></div>`;
 const grp=(h,rows)=>`<div><h3>${h}</h3>${rows.join("")}</div>`;
 $("ckp").innerHTML=[
  grp("Conversion",[row("Downloads per view","7 days",`<span class="${dd}">${pc(a.dpv7)}</span>`,`30d ${pc(a.dpv30)}`,"Of the people who opened a model page, the share who downloaded. Low means the page doesn't sell the model.","#models/sort:dpv"),
   row("Prints per download","lifetime",pc(a.ppd,0),a.ppd30!=null?`30d ${pc(a.ppd30,0)}`:"","Prints divided by downloads. Can pass 100% when people print a model more than once.","#models/fix"),
   row("Likes, collects, comments","per 100 downloads, 30 days",nf(e.all),"","How much people engage after downloading, scaled to 100 downloads so small and big models compare.","#models/grow")]),
  grp("Points",[row("Points a day","7-day average",`<span class="${tr}">${nf(a.pts7avg)}</span>`,`was ${nf(a.ptsPrev7avg)}`,"Average MakerWorld points a day over the last week, against the week before."),
   row("Model points","7 / 30 days",`${nf(a.pts7,0)} / ${nf(a.pts30,0)}`,"","Points MakerWorld credits to your models directly.","#models/aud"),
   row("Points, all sources","30 days",S.month&&S.month.points!=null?nf(S.month.points,0):"–")]),
  grp("Audience",[row("New followers","7 / 30 days",`${sg(fo.d7||0)} / ${sg(fo.d30||0)}`,(a.fol30?`${a.fol30} via models`:""),"Net new followers. \"Via models\" counts the ones MakerWorld says came from a model page.","#models/aud"),
   row("Average rating",`${fmt(a.rt||0)} ratings`,a.rt?nf(a.ra,2):"–","","Average star rating across every rating on your models.","#models/sort:rt"),
   row("Unanswered comments","",`<a href="#activity/replies" style="color:${a.unans?"var(--acc)":"inherit"}" data-tip="List them, with links to reply">${a.unans??"–"}</a>`,"","Comments with no reply from you yet.")])].join("")}

/* ---------- earnings estimate ---------- */
// Lifetime estimate from the poller (/api/dashboard dash.earnings, replaced after a save).
const EST=()=>S?.dash?.earnings,
 usd=(v,d=2)=>"$"+(+v||0).toLocaleString(undefined,{minimumFractionDigits:d,maximumFractionDigits:d}),
 pts=v=>Math.round(+v||0).toLocaleString(),xf=v=>(+v).toFixed(2).replace(/0$/,"")+"×";
function renderEarn(){const e=EST(),box=$("earn");
 if(!e){box.innerHTML=`<p class=mut>Waiting for the first poll</p>`;return}
 const M=Object.entries(e.models||{}),lo=e.low,hi=e.high,span=Math.max(1e-9,hi.usd-lo.usd);
 const at=Math.min(100,Math.max(0,(e.usd-lo.usd)/span*100));
 const row=(l,sub,v)=>`<div class=fr><span>${l}${sub?`<small>${sub}</small>`:""}</span><b>${v}</b></div>`;
 const boosts=M.reduce((t,[,m])=>t+(m.nb||0),0);
 const near=M.filter(([,m])=>m.u>0).sort((a,b)=>a[1].need-b[1].need||b[1].pays-a[1].pays).slice(0,4);
 box.innerHTML=`<div><h3>Estimated value</h3><div class=tot>${usd(e.usd)}<small>${pts(e.points)} pts</small></div>
  <p class=rng>Likely <b>${usd(lo.usd,0)}</b> to <b>${usd(hi.usd,0)}</b></p>
  <div class=meter role=img aria-label="${esc(`At ${xf(e.adjust)}, between ${usd(lo.usd,0)} at 1.0× and ${usd(hi.usd,0)} at 1.5×`)}"><span></span><i style="left:calc(${at}% - 1.5px)"></i></div>
  <div class=ax><span>1.0×</span><span>${xf(e.adjust)} adjustment</span><span>1.5×</span></div></div>
 <div><h3>Where the points come from</h3>${row("Model milestones",`${xf(e.adjust)} reward adjustment`,pts(e.parts.model))}
  ${row("Print profile milestones","",pts(e.parts.profile))}${row("Boosts",`${fmt(boosts)} received`,pts(e.parts.boost))}
  ${row("Weighted uses","downloads + 2 × prints",fmt(e.uses))}</div>
 <div><h3>Next milestones</h3>${near.map(([id,m])=>row(`${PRIV(id)}<a class=nm href="#models/${encodeURIComponent(id)}" title="${esc(modelName(id,id))}">${esc(modelName(id,id))}</a>`,`${fmt(m.need)} more use${m.need==1?"":"s"}`,"+"+pts(m.pays)+" pts")).join("")||`<p class=mut>No uses yet</p>`}</div>`;
 const ia=$("eadj"),iu=$("eusd");
 if(document.activeElement!==ia)ia.value=e.adjust;if(document.activeElement!==iu)iu.value=e.usdPerPoint}
function earnLine(id){const m=EST()?.models?.[id];if(!m)return"";
 return `<p>Earnings estimate: ${pts(m.pts)} points, about ${usd(m.usd)}${m.u?` · next milestone in ${fmt(m.need)} use${m.need==1?"":"s"} (+${pts(m.pays)} pts)`:""}${m.x?" · Exclusive":""}</p>`}
function renderEarnModels(all,q){const e=EST(),box=$("etable");
 if(!e){box.innerHTML=`<p class=mut>Waiting for the first poll</p>`;$("ecount").textContent="";$("enote").textContent="";return}
 $("enote").textContent=`Points from complete milestones (model milestones × ${xf(e.adjust)}), print profile milestones and boosts. Exclusive Models get ×1.25 on milestones and 15 points a boost; MakerWorld's flag is read automatically, and you can tick any it misses.`;
 const ids=new Set(all.filter(r=>mFocus?r.id===mFocus:!q||String(r.n).toLowerCase().includes(q)).map(r=>r.id));
 const rows=Object.entries(e.models||{}).filter(([id,m])=>(ids.has(id)||!C)&&(m.u>0||m.boost>0)).sort((a,b)=>b[1].pts-a[1].pts);
 const idle=Object.values(e.models||{}).filter(m=>!(m.u>0||m.boost>0)).length;
 $("ecount").textContent=` ${rows.length} earning${idle?`, ${idle} with no uses yet`:""}`;
 const H=(l,t,c="")=>`<th${c?` class=${c}`:""}>${TIP(l,t)}</th>`;
 box.innerHTML=`<table><thead><tr>${H("Model","Model name. Opens its stats in MakerMonitor.")}${H("Uses","Downloads plus 2 per print, lifetime.")}${H("Model pts","Complete model milestones, with the reward adjustment.","nophone")}${H("Profile pts","Complete print profile milestones.","nophone")}${H("Boost pts","12 points a boost, 15 on an Exclusive Model.","nophone")}${H("Points","Total estimated points.")}${H("Value","Points × value per point.")}${H("Next milestone","Uses still needed for the next model milestone, and what it pays.","nophone")}${H("Exclusive","Tick if this is a MakerWorld Exclusive Model.")}</tr></thead><tbody>`+
  (rows.map(([id,m])=>{const n=modelName(id,id);return `<tr><td class=nm>${PRIV(id)}<a href="#models/${encodeURIComponent(id)}" title="${esc(n)}">${esc(n)}</a></td><td>${fmt(m.u)}</td><td class=nophone>${pts(m.base*e.adjust)}</td><td class=nophone>${pts(m.prof)}</td><td class=nophone>${pts(m.boost)}</td><td><b>${pts(m.pts)}</b></td><td>${usd(m.usd)}</td>
   <td class=nophone>${fmt(m.need)} <span class=mut>→ +${pts(m.pays)}</span></td><td>${m.auto?`<span class=tip tabindex=0 data-tip="MakerWorld lists this as an Exclusive Model">Yes</span>`:`<input type=checkbox data-x="${esc(id)}"${m.x?" checked":""} aria-label="${esc(n)} is an Exclusive Model">`}</td></tr>`}).join("")
  ||`<tr><td colspan=9 class=empty>${ids.size?"No earning models match the filter":"No models yet"}</td></tr>`)+"</tbody></table>";
 box.querySelectorAll("input[data-x]").forEach(c=>c.onchange=()=>{const cur=new Set(EST().exclusive||[]);c.checked?cur.add(c.dataset.x):cur.delete(c.dataset.x);saveEarn({exclusive:[...cur]},c)})}
async function saveEarn(body,el){if(el)el.disabled=true;
 try{const r=await api("/api/earnings",body);const j=await r.json().catch(()=>({}));
  if(!r.ok)throw new Error(j.detail||"Couldn’t save");
  if(j.earnings&&S?.dash)S.dash.earnings=j.earnings;
  toast("Earnings settings saved");renderEarn();if(C)renderModels()}
 catch(err){toast(err.message,"err");renderEarn();if(C)renderModels()}finally{if(el)el.disabled=false}}
$("earnform").onsubmit=ev=>{ev.preventDefault();saveEarn({adjust:+$("eadj").value,usd_per_point:+$("eusd").value})};
$("edef").onclick=()=>saveEarn({adjust:1.37,usd_per_point:0.0816});

/* ---------- to do and funnel ---------- */
const med=a=>{a=a.filter(v=>v!=null).sort((x,y)=>x-y);const n=a.length,i=n>>1;return n?(n%2?a[i]:(a[i-1]+a[i])/2):null};
// Models that lag the creator's own typical rates. Medians, so one hit model doesn't set the bar.
function diag(){const ms=Object.values(C&&C.models||{}),has=ms.filter(r=>r.has);
 const md=med(has.filter(r=>r.m.v>=30).map(r=>r.dpv30)),mp=med(ms.filter(r=>r.life.d>=15).map(r=>r.ppd));
 const miss=r=>Math.round(r.m.v*(md-r.dpv30));
 const under=md?has.filter(r=>r.m.v>=150&&r.dpv30!=null&&r.dpv30<md*.5).sort((a,b)=>miss(b)-miss(a)):[];
 const lowP=r=>mp&&r.life.d>=15&&r.ppd!=null&&r.ppd<mp*.5,lowR=r=>r.rt>=3&&r.ra!=null&&r.ra<4.5;
 const prn=ms.filter(r=>lowP(r)||lowR(r)).sort((a,b)=>b.life.d-a.life.d);
 return {ms,has,md,mp,under,prn,miss,lowP,lowR}}
// Models running well ahead of their own pace today (creator.find_spikes, the same check
// that feeds the daily summary notification).
const spikes=()=>((C&&C.acct)||{}).spikes||[];
const PARTS=["Night","Morning","Afternoon","Evening"],PARTH=["midnight to 6 AM","6 AM to noon","noon to 5 PM","5 PM to midnight"];
function renderActs(){const el=$("acts"),fresh=$("action-freshness");
 fresh.hidden=!creatorRequestFailed;
 fresh.textContent=creatorRequestFailed?(C?"Showing cached suggestions. Analytics refresh failed.":"Suggestions are unavailable while analytics can’t be loaded."):"";
 if(!C){el.innerHTML=`<li><div><span>Waiting for creator stats</span></div></li>`;$("actn").textContent="";return}
 const D=diag(),a=C.acct||{},it=[],add=(k,o,t,w,d,tip)=>it.push({k,o,t,w,d,tip});
 const mu=id=>((C.models||{})[id]||{}).unans||0,cmA=id=>mu(id)?[`Its ${mu(id)} unanswered comment${mu(id)==1?"":"s"}`,"#activity/"+id,"Lists them here, each with a link to reply on MakerWorld."]:null;
 const inM=id=>["View model stats","#models/"+id,"Shows the full name, downloads, views, and unanswered comments."];
 if(a.unans)add("now",0,`${fmt(a.unans)} comment${a.unans==1?"":"s"} waiting for a reply`,"",
  [["Review unanswered comments","#activity/replies","See the available comment previews and open their models on MakerWorld."]],"A comment counts as unanswered until you reply to it on MakerWorld. Rechecked every few hours.");
 const sp=spikes(),spk=new Set(sp.map(r=>r.id));
 sp.slice(0,2).forEach(r=>add("now",1,`${mwLink(r.id,r.n)} is spiking today`,`${fmt(r.today)} downloads so far, against about ${nf(r.exp)} by this time on a normal day. It may have been featured or linked somewhere, so answer its comments while people are looking.`,
  [OPEN(r.id),cmA(r.id),inM(r.id)],"Spiking means well ahead of this model's own usual pace for this time of day. The same check feeds the daily summary notification."));
 D.has.filter(r=>r.mom&&r.mom.rising&&!spk.has(r.id)).sort((x,y)=>y.mom.y-x.mom.y).slice(0,2).forEach(r=>
  add("now",1,`${mwLink(r.id,r.n)} is taking off`,`${fmt(r.mom.y)} downloads yesterday against ${nf(r.mom.avg)} a day before. A good moment to answer its comments and share it.`,
  [OPEN(r.id),cmA(r.id),inM(r.id)],"Taking off means yesterday's downloads were well above its average over the week before."));
 const la=a.launch;if(la&&la.day>=2&&la.pct!=null&&la.pct<25)
  add("fix",2,`${mwLink(la.id,la.n)} is launching slowly`,`Day ${la.day}: behind ${100-la.pct}% of your ${la.past} earlier launches. Share it, or try a stronger cover image.`,
  [OPEN(la.id,"Open to edit or share"),["Launch curve","#models/grow","Compares this launch with the median of your earlier launches, day by day."]],"Compares downloads so far with your earlier launches at the same day after publishing.");
 D.under.slice(0,2).forEach(r=>add("fix",3,`Improve the page for ${mwLink(r.id,r.n)}`,
  `${fmt(r.m.v)} views in 30 days but ${pc(r.dpv30)} downloaded, against ${pc(D.md)} for your typical model. That's about ${fmt(D.miss(r))} downloads missed. Try a new cover photo or a clearer description.`,
  [OPEN(r.id,"Open to edit"),inM(r.id)],"People open the page but few download. Listed when a model with 150+ views in 30 days converts at under half your median rate. Missed downloads = views × the gap to your median."));
 D.prn.slice(0,2).forEach(r=>D.lowR(r)?add("fix",4,`${mwLink(r.id,r.n)} is rated ${nf(r.ra,1)} out of 5`,`${fmt(r.rt)} ratings. Check Reviews &amp; Ratings on the model page for print problems.`,
  [OPEN(r.id,"Open model on MakerWorld"),inM(r.id)],"Listed when a model with 3 or more ratings averages under 4.5.")
  :add("fix",4,`Check the print profile for ${mwLink(r.id,r.n)}`,`${fmt(r.life.d)} downloads, ${pc(r.ppd,0)} printed, against ${pc(D.mp,0)} for your typical model. If it's meant to be printed as is, check its profile and plates.`,
  [OPEN(r.id,"Open model on MakerWorld"),inM(r.id)],"Printed = prints ÷ downloads. Listed when it's under half your median for models with 15+ downloads."));
 D.has.filter(r=>r.mom&&r.mom.stalled).sort((x,y)=>y.mom.avg-x.mom.avg).slice(0,1).forEach(r=>
  add("",5,`${mwLink(r.id,r.n)} has gone quiet`,`It averaged ${nf(r.mom.avg)} downloads a day and has none since yesterday. Check it's still listed and its files open.`,
  [OPEN(r.id,"Check it on MakerWorld"),inM(r.id)],"Averaged at least 1 download a day over the last week, then none since yesterday."));
 const pt=a.pubt;if(pt&&pt.bestWd>=0)add("",7,`Publish on a ${WDL[pt.bestWd]}`,`Your ${WDL[pt.bestWd]} launches got a median ${nf(pt.wd[pt.bestWd].med,0)} downloads in their first week, against ${nf(pt.med,0)} overall (${pt.wd[pt.bestWd].n} of ${pt.n} launches).`,
  [["When to publish","#models/grow","First-week downloads by the weekday and time of day each model was published."]],"Based on the median first-week downloads of your own launches. A small sample, so treat it as a hint.");
 if(a.ptsTrend=="down")add("",6,"Points are down",`${nf(a.pts7avg)} a day over the last week, ${nf(a.ptsPrev7avg)} the week before.`,
  [["Points by model","#models/aud","Which models earned points over the last 7 and 30 days."]],"Points come from MakerWorld for downloads, prints, boosts and other activity on your models.");
 it.sort((x,y)=>x.o-y.o);
 const next=$("next-action");
 next.hidden=!it.length;
 const shortcut=a.unans?`<a href="#activity/replies">${fmt(a.unans)} comment${a.unans==1?"":"s"} to reply to</a>`:it.length?`<a href="#overview/actions">Review ${fmt(it.length)} priority suggestion${it.length===1?"":"s"}</a>`:"";
 if(next.innerHTML!==shortcut)next.innerHTML=shortcut;
 $("actn").textContent=it.length?`${it.length} item${it.length==1?"":"s"}`:"";
 const row=x=>`<li class="${x.k}"><i aria-hidden=true></i><div><b>${x.t}</b>${x.w?`<span>${x.w}</span>`:""}${x.d?DO(x.d):""}${x.tip?`<span>${TIP("Why this?",x.tip)}</span>`:""}</div></li>`;
 const more=it.slice(2);
 const html=it.length?it.slice(0,2).map(row).join("")+(more.length?`<li><details class=more-actions><summary>Show ${more.length} more suggestions</summary><ul class=acts>${more.map(row).join("")}</ul></details></li>`:"")
  :`<li><div><b>No suggestions right now</b></div></li>`;
 if(el.dataset.rendered!==html){
  const expanded=el.querySelector(".more-actions")?.open;
  el.innerHTML=html;el.dataset.rendered=html;
  if(expanded&&more.length)el.querySelector(".more-actions").open=true;
 }
}
function renderFunnel(){const m=S.month||{},y=S.yday||{},el=$("fun");
 if(m.impressions==null||m.views==null){el.innerHTML=`<p class=mut>Needs the MakerWorld cookie for impressions and views.</p>`;return}
 const st=[["Impressions","shown in feeds and search",m.impressions,m.yday_impressions],["Views","opened the model page",m.views,m.yday_views],
  ["Downloads","",m.downloads,y.downloads],["Prints","",m.prints,y.prints]];
 const step=[["clicked through","the cover image and title"],["downloaded","the photos, description and files"],["printed","the print profiles and plates"]];
 const r=(a,b)=>a&&b!=null?b/a:null;let worst=null,html="";
 st.forEach(([l,sub,v,yv],i)=>{html+=`<div class=fs><span>${TIP(l,["How often your models were shown in feeds, search and recommendations.","How often someone opened a model page.","Downloads of your files.","Prints started from your print profiles."][i])}${sub?`<small>${sub}</small>`:""}</span><b>${fmt(v)}</b><small>${yv!=null?"yesterday "+fmt(yv):""}</small></div>`;
  if(i<3){const r30=r(v,st[i+1][2]),ry=r(yv,st[i+1][3]),d=r30&&ry!=null?ry/r30:null;
   if(d!=null&&d<.8&&(!worst||d<worst.d))worst={d,i,ry,r30};
   html+=`<div class=fc><span class=ar aria-hidden=true><svg width=10 height=12 viewBox="0 0 10 12"><path d="M5 1v9M1.8 7L5 10.2 8.2 7" fill=none stroke=currentColor stroke-width=1.5 stroke-linecap=round stroke-linejoin=round /></svg></span><span class=rt>${pc(r30,r30<.1?1:0)}</span>${step[i][0]}<span class="y ${d==null?"":d>=1.2?"up":d<.8?"down":"mut"}">${d!=null&&(d>=1.2||d<.8)?ARR(d>=1.2?1:-1):""}${ry!=null?"yesterday "+pc(ry,ry<.1?1:0):""}</span></div>`}});
 const ok=st.every(x=>x[3]!=null);
 html+=`<p class=take>${worst?`Weakest step yesterday: <b>${step[worst.i][0]}</b>, ${pc(worst.ry,1)} against ${pc(worst.r30,1)} usually. That step depends on ${step[worst.i][1]}.${DO([["Pages to fix","#models/fix","Models whose pages or print profiles convert worst."]])}`
  :ok?"Every step was at or near its 30-day rate yesterday.":"Yesterday's numbers aren't in yet."}</p>`;
 el.innerHTML=html}

/* ---------- models ---------- */
const SORTI=d=>`<svg width=10 height=10 viewBox="0 0 10 10" aria-hidden=true><path d="${d<0?"M2 3.5l3 3 3-3":"M2 6.5l3-3 3 3"}" fill=none stroke=currentColor stroke-width=1.6 stroke-linecap=round stroke-linejoin=round /></svg>`;
const MTIP={n:"Model name. Opens its stats in MakerMonitor.",d30:"Downloads in the period.",v30:"Times the model page was opened in the period.",dpv:"Downloads ÷ views: how well the page turns visitors into downloads.",
 p30:"Prints started in the period.",l30:"Likes in the period.",c30:"Times saved to a collection in the period.",pt30:"MakerWorld points credited to this model in the period.",
 rt:"Number of ratings, and the average out of 5.",mom:"Yesterday's downloads against the 7 days before. Spiking means well ahead of its usual pace today.",un:"Comments with no reply from you. Click a number to list them."};
const MCOLS=[["n","Model"],["sp","30 days"],["d30","Downloads"],["v30","Views"],["dpv","DL / view"],["p30","Prints"],["l30","Likes"],["c30","Collects"],["pt30","Points"],["rt","Ratings"],["mom","Trend"],["un","Unanswered"]];
function mrows(){return Object.values(C&&C.models||{}).map(r=>{const w=r.has?(mView=="7"?r.w||{}:r.m||{}):{};
 return {id:r.id,n:modelName(r.id,r.n),has:r.has,fetched:r.fetched,coverage:r.coverage?.[mView==="7"?"w":"m"],d30:w.d,v30:w.v,dpv:mView=="7"?r.dpv7:r.dpv30,p30:w.p,l30:w.l,c30:w.c,pt30:w.pt,
  rt:r.rt||0,ra:r.ra,mom:r.mom?(r.mom.dir=="up"?1:r.mom.dir=="down"?-1:0)+(r.mom.rising?1:0)-(r.mom.stalled?2:0):null,momr:r.mom,un:r.unans}})}
function renderModels(){
 const seen=Object.values(C?.models||{}).map(r=>r.fetched).filter(Boolean);
 const latest=seen.length?Math.max(...seen):null;
 $("model-freshness").textContent=(creatorRequestFailed?"Couldn’t refresh analytics. ":"")+
  (latest?`Latest model update ${ago(latest)}. Models update individually.`:"Waiting for model analytics.");
 if(!C){$("mtable").innerHTML=`<p class=mut>Waiting for creator stats</p>`;renderEarnModels([],"");return}
 seg($("mseg"),[["7","7 days"],["30","30 days"]],mView,v=>mView=v);
 const all=mrows(),q=mQ.trim().toLowerCase();
 const detail=$("model-focus"),selected=mFocus?all.find(r=>r.id===mFocus):null;
 detail.hidden=!selected;
 const detailNumber=value=>value==null?"Unavailable":fmt(value);
 const coverage=selected?.coverage;
 const through=coverage?.through?new Date(coverage.through+"T12:00:00").toLocaleDateString(undefined,{month:"short",day:"numeric"}):null;
 const coverageLine=coverage?(coverage.of?
  `<p>Analytics cover ${coverage.days} of ${coverage.of} eligible days${through?", through "+esc(through):""}.</p>`:
  "<p>No completed days since publication.</p>"):"";
 const detailHtml=selected?`<h3 id=model-focus-title tabindex=-1>${esc(selected.n)}</h3><p>Last ${mView} days: ${selected.has?`${detailNumber(selected.d30)} downloads · ${detailNumber(selected.v30)} views ·  ${selected.dpv==null?"Conversion unavailable":pc(selected.dpv)+" downloads per view"}`:"Waiting for MakerWorld analytics"}</p>${coverageLine}${earnLine(selected.id)}
 <p>Unanswered comments: ${selected.un==null?"Not checked":fmt(selected.un)}${selected.fetched?` · Analytics updated ${ago(selected.fetched)}`:""}</p>
 ${DO([OPEN(selected.id),selected.un?[`Review ${fmt(selected.un)} unanswered comment${selected.un===1?"":"s"}`,"#activity/"+encodeURIComponent(selected.id)]:null])}`:"";
 if(detail.dataset.rendered!==detailHtml){
  const active=detail.contains(document.activeElement)?document.activeElement:null;
  const href=active?.getAttribute("href");
  detail.innerHTML=detailHtml;detail.dataset.rendered=detailHtml;
  if(active)([...detail.querySelectorAll("a")].find(a=>a.getAttribute("href")===href)||$("model-focus-title"))?.focus({preventScroll:true});
 }
 const rows=all.filter(r=>(mFocus?r.id===mFocus:!q||String(r.n).toLowerCase().includes(q))&&(!mAct||(r.d30||0)>0))
  .sort((a,b)=>{const x=a[mSort],y=b[mSort];if(mSort=="n")return mDir*String(x).localeCompare(String(y));return mDir*(((x??-1e9))-((y??-1e9)))});
 const nh=all.filter(r=>!r.has).length;$("mclear").hidden=!q;
 $("mcount").textContent=(rows.length<all.length?`${rows.length} of ${all.length}`:all.length)+" models"+(nh?`, ${nh} still waiting for MakerWorld analytics`:"");
 const PH=new Set(["sp","v30","dpv","p30","l30","c30","pt30","rt","un"]);
 const head=MCOLS.map(([k,l])=>k=="sp"?`<th class=nophone>${l}</th>`:`<th${PH.has(k)?" class=nophone":""}${mSort==k?` aria-sort="${mDir<0?"descending":"ascending"}"`:""}><button type=button class=sb data-k="${k}" data-tip="${esc(MTIP[k]||"")} Click to sort.">${l}${mSort==k?SORTI(mDir):""}</button></th>`).join("");
 const spk=new Set(spikes().map(r=>r.id));
 const trend=(m,id)=>spk.has(id)?`<span class=up>${ARR(1)} spiking today</span>`:!m?"<span class=mut>–</span>":m.stalled?`<span class=down>stalled</span>`:m.rising?`<span class=up>${ARR(1)} rising fast</span>`:m.dir=="up"?`<span class=up>${ARR(1)} up</span>`:m.dir=="down"?`<span class=down>${ARR(-1)} down</span>`:"<span class=mut>flat</span>";
 const nv=v=>v==null?"<span class=mut>–</span>":fmt(Math.round(v));
 $("mtable").innerHTML=`<table><thead><tr>${head}</tr></thead><tbody>`+(rows.map(r=>`<tr><td class=nm>${PRIV(r.id)}<a href="#models/${encodeURIComponent(r.id)}" title="${esc(r.n)}">${esc(r.n)}</a></td><td class=nophone>${spark(SP[r.id],"d")}</td>
  <td>${nv(r.d30)}</td><td class=nophone>${nv(r.v30)}</td><td class=nophone>${pc(r.dpv)}</td><td class=nophone>${nv(r.p30)}</td><td class=nophone>${nv(r.l30)}</td><td class=nophone>${nv(r.c30)}</td><td class=nophone>${r.pt30==null?"<span class=mut>–</span>":nf(r.pt30)}</td>
  <td class=nophone>${r.rt?`${fmt(r.rt)} <span class=mut>at ${nf(r.ra,1)}</span>`:"<span class=mut>–</span>"}</td><td>${trend(r.momr,r.id)}</td><td class=nophone>${r.un?`<a href="#activity/${esc(r.id)}" style="color:var(--acc);font-weight:700" data-tip="List the unanswered comments on this model">${r.un}</a>`:`<span class=mut>${r.un==null?"Not checked":"0"}</span>`}</td></tr>`).join("")||`<tr><td colspan=12 class=empty>${all.length?"No models match the filter":"No models yet"}</td></tr>`)+"</tbody></table>";
 $("mtable").querySelectorAll("th .sb").forEach(b=>b.onclick=()=>{const k=b.dataset.k;if(mSort==k)mDir=-mDir;else{mSort=k;mDir=k=="n"?1:-1}renderModels();const f=$("mtable").querySelector(`.sb[data-k="${k}"]`);f&&f.focus()});
 renderEarnModels(all,q);renderInsights()}
function renderInsights(){const m=Object.values(C.models||{}),has=m.filter(r=>r.has),top=(arr,f,n=8)=>arr.sort(f).slice(0,n);
 const L=(id,n)=>`<td class=nm>${mwLink(id,n)}</td>`,R=cells=>`<tr>${cells}</tr>`;
 const conv=top(has.filter(r=>r.dpv30!=null&&r.m.v>=30),(x,y)=>y.dpv30-x.dpv30).map(r=>R(L(r.id,r.n)+`<td>${pc(r.dpv30)}</td><td>${fmt(r.m.v)}</td><td>${fmt(r.m.d)}</td><td>${pc(r.ppd,0)}</td>`));
 const eng=top(has.filter(r=>r.e100.m.all!=null&&r.m.d>=5),(x,y)=>y.e100.m.all-x.e100.m.all).map(r=>R(L(r.id,r.n)+`<td>${nf(r.e100.m.l)}</td><td>${nf(r.e100.m.c)}</td><td>${nf(r.e100.m.m)}</td><td>${nf(r.e100.life.all)}</td>`));
 const mom=has.filter(r=>r.mom);
 const rise=top(mom.filter(r=>r.mom.dir=="up"),(x,y)=>(y.mom.r||0)-(x.mom.r||0)).map(r=>R(L(r.id,r.n+(r.mom.rising?" · rising fast":""))+`<td>${r.mom.y}</td><td>${nf(r.mom.avg)}</td><td>×${nf(r.mom.r)}</td>`));
 const stall=top(mom.filter(r=>r.mom.stalled),(x,y)=>y.mom.avg-x.mom.avg).map(r=>R(L(r.id,r.n)+`<td>${nf(r.mom.avg)}</td>`));
 const pts=top(has.filter(r=>r.pts30>0),(x,y)=>y.pts30-x.pts30).map(r=>R(L(r.id,r.n)+`<td>${nf(r.pts7)}</td><td>${nf(r.pts30)}</td>`));
 const rated=top(m.filter(r=>r.rt>=3),(x,y)=>(y.ra-x.ra)||(y.rt-x.rt)).map(r=>R(L(r.id,r.n)+`<td>${nf(r.ra,2)}</td><td>${fmt(r.rt)}</td>`));
 const fol=top(has.filter(r=>r.fol30>0),(x,y)=>y.fol30-x.fol30).map(r=>R(L(r.id,r.n)+`<td>${r.fol7}</td><td>${r.fol30}</td>`));
 const D=diag();
 const under=D.under.slice(0,8).map(r=>R(L(r.id,r.n)+`<td>${fmt(r.m.v)}</td><td>${pc(r.dpv30)}</td><td>${fmt(D.miss(r))}</td>`));
 const prn=D.prn.slice(0,8).map(r=>R(L(r.id,r.n)+`<td>${fmt(r.life.d)}</td><td${D.lowP(r)?" class=down":""}>${pc(r.ppd,0)}</td><td${D.lowR(r)?" class=down":""}>${r.rt?nf(r.ra,1):"<span class=mut>–</span>"}</td>`));
 const la=(C.acct||{}).launch;let lc="<p class=mut>Shows once a model has launch data.</p>";
 if(la){const mx=Math.max(1,...la.cur,...la.med);const nd=la.cur[la.day-1]||0;lc=`<p class=ink2 style="margin-top:0">${mwLink(la.id,la.n)}: day ${la.day} of 7, ${fmt(nd)} download${nd==1?"":"s"} so far`+
  (la.pct!=null&&(nd>0||la.day>1)?`, ahead of ${la.pct}% of ${la.past} earlier launches`:la.past?`. The median of ${la.past} earlier launches is shown in grey.`:"")+`</p><div class=lc>${[0,1,2,3,4,5,6].map(i=>`<div title="day ${i+1}"><i style="background:var(--d);height:${la.cur[i]!=null?Math.max(3,la.cur[i]/mx*100):0}%"></i><i style="background:var(--line);height:${la.med.length?Math.max(3,la.med[i]/mx*100):0}%"></i></div>`).join("")}</div>
  <div class=axis><span>day 1</span><span>day 7</span></div><div class=legend style="margin-top:10px"><span><i style="background:var(--d)"></i>this launch</span><span><i style="background:var(--line)"></i>median launch</span></div>`}
 const pt=(C.acct||{}).pubt,ins=((S&&S.insights)||{}).downloads;let pub="<p class=mut>Shows once models have finished their first week.</p>";
 if(pt&&pt.n){const cell=(g,i,lab,tip)=>`<div class="${i==g.best?"hi":""}" title="${tip}: ${g.v[i].n} launch${g.v[i].n==1?"":"es"}${g.v[i].n?", median "+nf(g.v[i].med,1)+", best "+g.v[i].max:""}">${lab}<b>${g.v[i].n?nf(g.v[i].med,g.v[i].med%1?1:0):"–"}</b></div>`;
  const W={v:pt.wd,best:pt.bestWd},P={v:pt.part,best:pt.bestPart};
  const verdict=[pt.bestWd>=0?`${WDL[pt.bestWd]} launches did best`:"",pt.bestPart>=0?`${PARTS[pt.bestPart].toLowerCase()} launches (${PARTH[pt.bestPart]}) did best`:""].filter(Boolean);
  pub=`<p class=ink2 style="margin-top:0">Median downloads in the first week, by when the model was published. ${pt.n} launches so far.</p>
  <div class=wk>${pt.wd.map((_,i)=>cell(W,i,WD[i],WDL[i])).join("")}</div>
  <div class=wk style="grid-template-columns:repeat(4,1fr)">${PARTS.map((l,i)=>cell(P,i,l,PARTH[i])).join("")}</div>
  <p class=mut style="margin:12px 0 0">${verdict.length?cap(verdict.join("; "))+`, against a median of ${nf(pt.med,pt.med%1?1:0)} overall. That's a small sample, so treat it as a hint.`:`No publish time stands out yet: the differences are too small for ${pt.n} launches to tell apart.`}${ins&&ins.bestHour>=0?` Your downloads peak around ${hh(ins.bestHour)}, so a model published a few hours before that gets its first day in your busiest window.`:""}</p>`}
 const t=(C.acct||{}).tail||{},tb=(l,v)=>{v=v||[0,0];const s=v[0]+v[1],o=s?Math.round(100*v[0]/s):0;return `<div class=ink2>${l}: <b>${o}%</b> from models older than 30 days <span class=mut>(${fmt(v[0])} of ${fmt(s)})</span></div><div class=tail><i style="width:${o}%;background:var(--c)"></i><i style="width:${100-o}%;background:var(--d)"></i></div>`};
 const ITIP={"Best converting":"Highest downloads per view over 30 days, among models with 30+ views.","Most engaging":"Likes, collects and comments per 100 downloads over 30 days, among models with 5+ downloads.",
  "Pages to improve":"Models with 150+ views in 30 days whose downloads per view are under half your median. Missed = views × the gap. A better cover or description usually helps.",
  "Print problems":"Printed under half your median rate (models with 15+ downloads), or rated under 4.5 with 3+ ratings. Check the print profile, plates and reviews.",
  "Rising":"Yesterday's downloads against the average of the 7 days before.","Stalled":"Averaged 1+ download a day, then none since yesterday. Check it's still listed and its files open.",
  "Launch curve":"Your newest model's downloads for its first 7 days, against the median of your earlier launches.","When to publish":"Median first-week downloads by the weekday and time of day each model was published.",
  "Points by model":"Points MakerWorld credits to each model.","Best rated":"Highest average rating, among models with 3+ ratings.","Where followers come from":"Followers MakerWorld says came from each model's page.",
  "Long tail":"How much of your downloads come from models older than 30 days."};
 const card=(h,sub,b)=>`<div class="card fit"><h2>${TIP(h,ITIP[h]||h)}<span class=sp></span><small>${sub}</small></h2><div class=tw>${b}</div></div>`;
 const G={fix:[2,3,5],grow:[4,0,1,6,7],aud:[8,9,10,11]};
 seg($("iseg"),[["fix","To fix"],["grow","Growing"],["aud","Audience and points"]],iView,v=>iView=v);
 $("inote").textContent={fix:"Pages that convert badly, print problems and models that stopped.",grow:"What's rising, what converts and engages best, and how launches go.",aud:"Points, ratings, followers and the long tail."}[iView];
 const all=[
  card("Best converting","downloads per view, 30 days",table(["Model","DL/view","Views","Downloads","Prints/DL"],conv,"Needs models with 30+ views")),
  card("Most engaging","per 100 downloads, 30 days",table(["Model","Likes","Collects","Comments","Lifetime"],eng,"Needs models with 5+ downloads")),
  card("Pages to improve",D.md?`DL/view under half your ${pc(D.md)}`:"downloads per view",table(["Model","Views","DL/view","Missed DL"],under,"No page with 150+ views is lagging")),
  card("Print problems",D.mp?`under half your ${pc(D.mp,0)} printed, or under 4.5 stars`:"print rate and rating",table(["Model","Downloads","Printed","Rating"],prn,"No model stands out")),
  card("Rising","yesterday against the 7-day average",table(["Model","Yday","Avg","Ratio"],rise,"Nothing rising right now")),
  card("Stalled","averaged 1+ a day, none since yesterday",table(["Model","Was / day"],stall,"Nothing stalled")),
  card("Launch curve","first 7 days",lc),
  card("When to publish","first-week downloads by publish time",pub),
  card("Points by model","MakerWorld attribution",table(["Model","7 days","30 days"],pts)),
  card("Best rated","3 or more ratings",table(["Model","Average","Ratings"],rated,"No model has 3 ratings yet")),
  card("Where followers come from","by model",table(["Model","7 days","30 days"],fol)),
  card("Long tail","older models' share of downloads",tb("Today",t.today)+tb("7 days",t.w)+tb("30 days",t.m)+`<div class=legend><span><i style="background:var(--c)"></i>older than 30 days</span><span><i style="background:var(--d)"></i>newer</span></div>`),
 ];$("cgrid").innerHTML=G[iView].map(i=>all[i]).join("")}
let sparkAt=0;
async function loadSparks(){if(!C||!S||Date.now()-sparkAt<6e5)return;sparkAt=Date.now();
 const ids=new Set([...Object.keys(C&&C.models||{}),...["today","week","life"].flatMap(k=>((S&&S.top||{})[k]||[]).map(r=>r.id))].filter(Boolean));
 for(const id of ids){try{const r=await fetch("/v2/model?full=1&id="+encodeURIComponent(id));if(!r.ok)continue;const j=await r.json();
  const mw=(j.stats||{}).mwDays||[],own=j.days||[];
  SP[id]=mw.length?mw.map(d=>d.dl||0):own.map(d=>d.downloads||0)}catch(e){}}
 renderAll()}

/* ---------- activity ---------- */
async function loadModelReplies(){
 const model=aMod,request=++replyRequest;
 try{
  const response=await fetch("/api/dashboard/replies?"+new URLSearchParams({model}),{credentials:"same-origin"});
  if(!response.ok)throw new Error("Comment previews unavailable");
  const data=await response.json();
  if(model!==aMod||request!==replyRequest)return;
  replyDetail={model,...data};
 }catch(e){
  if(model!==aMod||request!==replyRequest)return;
  replyDetail={model,error:true};
 }
 if(document.body.dataset.view==="activity")renderActivity();
}
function renderActivity(){const all=(S.dash||{}).feedAll||S.feed||[],X=S.dash||{};
 const kOf=e=>e.k=="comment"?"comments":e.k,mn=aMod?modelName(aMod,C?.models?.[aMod]?.n):null;
 const f=all.filter(e=>(!aKind||kOf(e)==aKind)&&(!aMod||(e.mid?e.mid===aMod:e.m===C?.models?.[aMod]?.n)));
 // Totals come from today's counters, not the event list, which is capped at 40.
 const t=S.today||{},by=[...K.map(([k])=>[k,t[k]||0]),["ratings",t.ratings||0],["comments",all.filter(e=>e.k=="comment").length]].filter(x=>x[1]>0);
 $("feednote").textContent=all.length>=40?"latest 40, newest first":"newest first";
 const fl=$("afilter");fl.hidden=!(aKind||mn);fl.innerHTML=aKind||mn?`Showing ${aKind?esc(aKind):"everything"}${mn?" for "+esc(mn):""}. <a href="#activity">Show all</a>`:"";
 $("fsum").innerHTML=by.sort((a,b)=>b[1]-a[1]).map(([k,n])=>`<button type=button data-k="${k}" aria-pressed="${aKind==k}" data-tip="${aKind==k?"Show every kind again":"Show only "+k+" in the list below"}"><span class=kdot style="--k:var(--${KC[k]||KC[k.replace(/s$/,"")]||"f"})"></span><b>${fmt(n)}</b>${esc(n==1?k.replace(/s$/,""):k)}</button>`).join("");
 $("fsum").querySelectorAll("button").forEach(b=>b.onclick=()=>{location.hash=aKind==b.dataset.k?"#activity":"#activity/k:"+b.dataset.k});
 const detail=aMod&&replyDetail?.model===aMod?replyDetail:null;
 const un=(C&&C.acct||{}).unans;
 const ul=detail?.comments||(X.unans||[]).filter(c=>!aMod||c.mid===aMod);
 const um=Object.values(C&&C.models||{}).filter(r=>r.unans>0&&(!aMod||r.id==aMod)).sort((a,b)=>b.unans-a.unans);
 const scans=aMod?detail?.scan:X.commentScans;
 $("comment-freshness").textContent=scans?.total?
  `${fmt(scans.checked)} of ${fmt(scans.total)} ${scans.total===1?"model":"models"} checked.${scans.latest?` Latest check ${ago(scans.latest)}.`:""}`:
  "Counts come from cached comment scans.";
 const count=aMod?((C&&C.models||{})[aMod]||{}).unans:un;
 $("unans").textContent=count!=null?count+" unanswered":"";
 const byModel=um.length?`<div class=reply style="margin-bottom:${ul.length?"12px":"0"}">${um.map(r=>`<a href="${mwUrl(r.id)}" target=_blank rel=noopener data-tip="Open ${esc(r.n)} on MakerWorld to reply"><span>${esc(modelName(r.id,r.n))}</span><b>${r.unans}</b></a>`).join("")}</div>`:"";
 const previewNote=count>ul.length?`<p class=mut>Showing ${fmt(ul.length)} of ${fmt(count)} comment previews. Open a model to see its full discussion.</p>`:"";
 const unavailable=detail?.error?`<p class=mut>Couldn’t load model previews. <button class="btn sm" type=button id=retry-replies>Retry</button></p>`:"";
 const replyHtml=unavailable+(ul.length?previewNote+ul.map(c=>`<div class=ucm><span class=who>${esc(c.u)}</span> <span class=on>on <a class=rowlink href="#activity/${esc(c.mid)}">${esc(c.m)}</a>${c.t?" · "+ago(Date.parse(c.t)/1e3):""}</span><p>${esc(c.x)}</p>${DO([["Reply on MakerWorld",mwUrl(c.mid),"Opens the model page in a new tab. Open Reviews & Ratings there to find this comment.",1]])}</div>`).join("")
  :um.length?previewNote:`<p class=empty>${count===0?"No unanswered comments found among models checked.":"Waiting for comment scan results."}</p>`)+byModel;
 if($("reply").dataset.rendered!==replyHtml){$("reply").innerHTML=replyHtml;$("reply").dataset.rendered=replyHtml}
 if($("retry-replies"))$("retry-replies").onclick=loadModelReplies;
 $("feed").innerHTML=f.length?f.map(e=>{const c=KC[e.k]||"ink2",id=e.mid||idOf(e.m);return `<div class=ev style="--k:var(--${c})"><span class=tag>${kIco(e.k,e.k=="comment"?"comment":e.k=="ratings"?(e.n==1?"rating":"ratings"):e.n==1?e.k.replace(/s$/,""):e.k)}</span>
  <span class=m>${e.k=="comment"?`<span class=mut>new</span> · `:e.k=="ratings"&&e.s?`<span class=mut>${esc(e.s)} stars</span> · `:""}${id?`<a class=rowlink href="${mwUrl(id)}" target=_blank rel=noopener data-tip="Open on MakerWorld">${esc(e.m)}</a>`:esc(e.m)}</span><span class=t>${ago(e.t)}</span></div>`}).join("")
  :`<p class=empty>${aKind||mn?"Nothing matches this filter today. <a href=\"#activity\">Show all</a>":"Nothing yet today. New downloads, prints, boosts, likes, ratings and comments show here as they happen."}</p>`;
 const cm=(X.comments||[]).length?X.comments.map(c=>({u:c.user,x:c.text,m:c.model,t:c.time,mid:c.designId})):(S.comments||[]).map(c=>({...c,mid:idOf(c.m)}));
 $("comments").innerHTML=cm.length?cm.map(c=>`<div class=cm><span class=who>${esc(c.u)}</span> <span class=on>on ${c.mid?`<a class=rowlink href="${mwUrl(c.mid)}" target=_blank rel=noopener data-tip="Open the model on MakerWorld to read or reply">${esc(c.m)}</a>`:esc(c.m)}${c.t?" · "+ago(Date.parse(c.t)/1e3):""}</span><p>${esc(c.x)}</p></div>`).join("")
  :`<p class=empty>No comments yet</p>`}

/* ---------- health ---------- */
function renderHealth(){const h=S.health||{},src=h.src||{},st=S.status||{};
 const NAMES={models:["Models and totals","every poll"],reach:["30-day reach","every poll"],points:["Points","every poll"],"model-stats":["Per-model analytics","background"]};
 const cookieNeeded=h.auth==="none"||h.auth==="expired";
 const unchecked=Object.keys(NAMES).some(k=>!src[k]?.t&&!src[k]?.f);
 const sf=Object.keys(NAMES).filter(k=>(src[k]||{}).f).length,pl0=h.panels||[],pu=pl0.filter(p=>p.up).length,lm0=h.limiter||{};
 $("hsum").innerHTML=[
  `<span class="badge ${sf||unchecked||cookieNeeded?"warn":"ok"}"><span class=bd></span>${sf?sf+" of "+Object.keys(NAMES).length+" sources failing":cookieNeeded?"Some sources need a MakerWorld cookie":unchecked?"Waiting for all sources to report":"All MakerWorld sources OK"}</span>`,
  `<span class="badge ${h.auth=="ok"?"ok":h.auth=="expired"?"err":"warn"}"><span class=bd></span>Cookie ${{ok:"accepted",expired:"rejected",none:"not set"}[h.auth]||"unknown"}</span>`,
  pl0.length?`<span class="badge ${pu==pl0.length?"ok":pu?"warn":"err"}"><span class=bd></span>${pu} of ${pl0.length} panels answering</span>`:"",
  `<span class="badge ${lm0.paused?"err":Object.keys(lm0.backoff_until||{}).length?"warn":"mut"}"><span class=bd></span>${fmt(lm0.requests_last_hour||0)} of ${fmt(lm0.budget||0)} requests this hour</span>`].join("");
 $("srcs").innerHTML=Object.entries(NAMES).map(([k,[l,when]])=>{const x=src[k]||{};const cls=!x.t&&!x.f?"idle":x.f>=3?"err":x.f?"warn":"";
  return `<div class="${cls}"><b>${l}</b><div class=ink2>Last good ${ago(x.t)}</div><div class=mut>Runs ${when=="background"?"in the background":"every poll"}</div>
   <div class=mut>${x.f?x.f+" failure"+(x.f>1?"s":"")+" in a row":"No failures"}</div>${x.e?`<div class=e>${esc(x.e)}</div>`:""}</div>`}).join("")+
  `<div class="${h.auth=="expired"?"err":h.auth=="ok"?"":"warn"}"><b>Cookie</b><div class=ink2>${{ok:"Accepted by MakerWorld",expired:"Rejected, needs a refresh",none:"Not set, public stats only"}[h.auth]||"Unknown"}</div>
   <div class=mut>${st.cookieAge>=0?"Sent "+ago(Date.now()/1e3-st.cookieAge):"Never sent"}</div><div style="margin-top:6px"><a href="#settings/mw">Manage</a></div></div>`;
 const lm=h.limiter||{},bo=Object.entries(lm.backoff_until||{}),used=lm.requests_last_hour||0,bud=lm.budget||0,bk=lm.by_kind||{};
 $("limiter").innerHTML=`<div class=row style="justify-content:space-between"><b class=num style="font-size:22px">${used} <span class=mut style="font-size:16px">of ${bud||"–"} this hour</span></b>${lm.paused?`<span class="badge err">paused</span>`:""}</div>
  <div class=meter><i style="width:${bud?Math.min(100,used/bud*100):0}%"></i></div>
  <div class=mut>${Object.entries(bk).map(([k,v])=>`${esc(k)} ${v}`).join(" · ")||""}</div>
  ${bo.length?`<p style="color:var(--warn)">Backing off: ${bo.map(([k,t])=>esc(k)+" until "+hm(t)).join(", ")}</p>`:`<p class=mut>No backoff in effect.</p>`}`;
 $("pinfo").innerHTML=[["Last poll",st.lastPoll?hm(st.lastPoll)+" ("+ago(st.lastPoll)+")":"never"],["Result",!st.lastPoll?"Waiting for first poll":st.ok===false?esc(st.msg||"failed"):"ok"],
  ["Poll interval",(st.pollSec||"–")+" s"],["Panel URL",esc(S.poller||"–")],["Data as of",S.t?new Date(S.t*1e3).toLocaleDateString()+" "+hm(S.t):"–"]]
  .map(([k,v])=>`<dt>${k}</dt><dd>${v}</dd>`).join("")
 const pl=[...(h.panels||[])].sort((a,b)=>(b.up===true)-(a.up===true));
 $("pnls").innerHTML=pl.length?pl.map(p=>`<div class="${p.up===false?"err":p.type=="unknown"?"idle":""}"><b><a class=rowlink href="http://${esc(String(p.host).replace(/^https?:\/\//,""))}/" target=_blank rel=noopener data-tip="Open this panel's own web console (works while it's online)">${esc(p.host)}${EXT}</a></b>
   <div class=ink2>${PTYPE[p.type]||"Not detected yet"}${p.board?" · "+esc(p.board):""}${p.fw?" · v"+esc(p.fw):""}</div>
   <div class=mut>${p.up===false?"Not answering":p.up?"Answering":"Not checked yet"}</div></div>`).join("")
  :`<p class=mut>No panels. Add one under <a href="#settings/panels">Settings</a>.</p>`}
const PTYPE={color:"Color panel",eink:"E-paper panel"};

$("mq").oninput=e=>{mFocus=null;mQ=e.target.value;renderModels()};$("mact").onchange=e=>{mAct=e.target.checked;renderModels()};
function renderAll(){
 if(!S)return;
 const sortKey=document.activeElement?.closest("#mtable .sb")?.dataset.k;
 renderHead();renderOverview();renderEarn();renderActivity();renderHealth();renderModels();
 if(sortKey)$("mtable").querySelector(`.sb[data-k="${sortKey}"]`)?.focus({preventScroll:true});
}
function showDashboardError(message){
 $("dashboard-error-text").textContent=message;
 $("dashboard-error").hidden=!message;
 $("dashboard-error").className=message?"banner err":"banner";
}
async function load(){
 if(dashboardLoading)return;
 dashboardLoading=true;$("retry-dashboard").disabled=true;
 try{
  const [creator,dashboard]=await Promise.allSettled([fetch("/v2/creator"),fetch("/api/dashboard",{credentials:"same-origin"})]);
  try{
   if(creator.status!=="fulfilled"||!creator.value.ok)throw new Error("Creator data unavailable");
   C=await creator.value.json();creatorRequestFailed=false;
  }catch(e){creatorRequestFailed=true}
  if(dashboard.status!=="fulfilled")throw new Error("Poller unreachable");
  const response=dashboard.value;
  if(!response.ok)throw new Error([401,403].includes(response.status)?"Dashboard session expired. Sign in again.":"Dashboard data unavailable");
  const first=!S;S=await response.json();
  showDashboardError("");if(first)route();renderAll();loadSparks();
  if(!first&&aMod&&document.body.dataset.view==="activity")loadModelReplies();
  dispatchEvent(new Event("dashboard-refreshed"));
 }catch(e){
  $("pill").className="pill err";$("pilltx").textContent="Update failed";
  showDashboardError(e.message+(S?" Showing the last loaded data.":""));
 }finally{dashboardLoading=false;$("retry-dashboard").disabled=false}
}
$("retry-dashboard").onclick=load;

/* ---------- settings ---------- */
// F is the form (the /api/settings shape), F0 the last loaded copy. HOSTS keeps
// the panel rows being edited, empty ones included; F.top.panels is the clean list.
let F=null,F0=null,F0raw="",KEEP="__keep__",ERR={},TRIED=false,COOK=null,COOKIN="",HOSTS=[],QLAST="",FL={},EC={},FORCED="";
const open=new Set(),TOUCH=new Set(),PCHK={},CTEST={},REPL=new Set(),SHOW=new Set();
const SECS=[["mw","Account"],["panels","Panels"],["notify","Notifications"],["quiet","Quiet hours"],["poll","Polling"],["adv","Advanced"]],SECN=Object.fromEntries(SECS);
const TYPE_LABEL={discord:"Discord",pushbullet:"Pushbullet",pushover:"Pushover",ntfy:"ntfy",telegram:"Telegram",homeassistant:"Home Assistant",apprise:"Apprise (Slack, Matrix, Gotify...)",webhook:"Webhook"};
const SMOOTH=matchMedia("(prefers-reduced-motion:reduce)").matches?"auto":"smooth";
const CHEV=`<svg class=chev width=16 height=16 viewBox="0 0 16 16" aria-hidden="true"><path d="M4 6l4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>`;
const CHECK=`<svg width=12 height=12 viewBox="0 0 16 16" aria-hidden="true"><path d="M3 8.5l3.2 3L13 4.5" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>`;
const PLUS=`<svg width=14 height=14 viewBox="0 0 16 16" aria-hidden="true"><path d="M8 3v10M3 8h10" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>`;
const LOCK=`<svg width=12 height=12 viewBox="0 0 16 16" aria-hidden="true"><rect x="3" y="7" width="10" height="7" rx="1.5" fill="currentColor"/><path d="M5 7V5a3 3 0 0 1 6 0v2" fill="none" stroke="currentColor" stroke-width="1.6"/></svg>`;
const SKEL=`<div class="card sec" aria-busy="true"><div class=skel style="width:28%;height:18px"></div><div class=skel></div><div class=skel style="width:72%"></div><div class=skel style="width:55%"></div></div>`;
const api=(u,b)=>fetch(u,b?{method:"POST",credentials:"same-origin",headers:{"Content-Type":"application/json","X-MM":"1"},body:JSON.stringify(b)}:{credentials:"same-origin"});
const pid=p=>"f-"+String(p).replace(/[^\w-]/g,"-");
// Empty, null and missing all mean "not set", so they compare equal.
const blank=v=>v===""||v===null||v===undefined||(v&&typeof v=="object"&&!Array.isArray(v)&&Object.values(v).every(blank));
const js=o=>JSON.stringify(o,(k,v)=>k=="_set"||k&&blank(v)?undefined:v);
const SLICE={mw:f=>[f.top.handle],panels:f=>[f.top.panels],adv:f=>[f.top.user_agent],poll:f=>[f.top.poll_interval_sec,f.top.page_cycle_sec],
 notify:f=>[f.notifications.defaults.events,f.notifications.defaults.min_count,f.notifications.defaults.summary_time,f.notifications.channels],
 quiet:f=>[f.notifications.defaults.quiet_hours,f.notifications.defaults.quiet_digest,f.notifications.defaults.urgent]};
const SLOTS=[...Array(96)].map((_,i)=>String(i>>2).padStart(2,"0")+":"+String((i&3)*15).padStart(2,"0"));
const t12=s=>{const[h,m]=String(s).split(":").map(Number);return isNaN(h)?s:(h%12||12)+":"+String(m||0).padStart(2,"0")+" "+(h<12?"AM":"PM")};
const qtxt=q=>q?String(q).split("-").map(t12).join(" to "):"";
const mins=s=>{const[h,m]=String(s).split(":").map(Number);return h*60+(m||0)};
function qdur(a,b){const d=(mins(b)-mins(a)+1440)%1440;if(!d)return"Start and end are the same time.";
 const h=Math.floor(d/60),r=d%60;return (h?h+(h==1?" hour":" hours"):"")+(r?(h?" ":"")+r+" min":"")+(mins(b)<mins(a)?", overnight":"")}
const dur=s=>{s=+s;if(!s||s<0)return"";const m=Math.floor(s/60),r=s%60;return s<60?s+" s":m+" min"+(r?" "+r+" s":"")};
function toast(msg,kind="ok"){const t=$("toast");t.textContent=msg;t.className="toast show "+kind;clearTimeout(toast.h);toast.h=setTimeout(()=>t.className="toast",kind=="err"?8000:4000)}
function say(msg){const l=$("live");l.textContent="";setTimeout(()=>l.textContent=msg,40)}
function ask(title,msg,ok){return new Promise(res=>{const d=$("dlg");$("dlgt").textContent=title;$("dlgm").textContent=msg;$("dlgok").textContent=ok;
 d.returnValue="";d.onclose=()=>res(d.returnValue=="ok");d.showModal()})}
function setTop(){const r=document.documentElement.style,nav=$("setnav");r.setProperty("--toph",document.querySelector(".top").offsetHeight+"px");
 r.setProperty("--navh",(matchMedia("(max-width:900px)").matches&&nav?nav.offsetHeight:0)+"px")}

async function loadSettings(){let r=null,err="";
 try{r=await api("/api/settings");if(!r.ok)err=r.status==401||r.status==403?"Settings need the dashboard token. Reopen this page with ?token=<api_token> at the end of the address.":`The poller answered with an error (HTTP ${r.status}).`}
 catch(e){err="The poller didn't answer. Check that it's running, then try again."}
 if(err){F=null;$("form").innerHTML=`<div class="card sec state" role=alert><h2>Settings didn't load</h2><p>${esc(err)}</p><button class="btn pri" type=button id=retry>Try again</button></div>`;
  $("retry").onclick=()=>{$("form").innerHTML=SKEL;loadSettings()};paint();return}
 F=await r.json();KEEP=F.keep;F.notifications.channels.forEach(c=>{c._set={};Object.keys(c).forEach(k=>{if(c[k]===KEEP)c._set[k]=1})});
 if(!Array.isArray(F.top.panels))F.top.panels=String(F.top.panels||"").split(",").map(s=>s.trim()).filter(Boolean);
 F0raw=JSON.stringify(F);F0=JSON.parse(F0raw);HOSTS=[...F.top.panels];QLAST=F.notifications.defaults.quiet_hours||"";
 ERR={};TRIED=false;TOUCH.clear();open.clear();REPL.clear();SHOW.clear();
 for(const k in PCHK)delete PCHK[k];for(const k in CTEST)delete CTEST[k];
 renderSettings();loadCookie()}
function changedSecs(){return F&&F0?SECS.map(s=>s[0]).filter(s=>js(SLICE[s](F))!=js(SLICE[s](F0))):[]}
function secOf(p){if(p=="top.handle")return"mw";if(p.startsWith("top.panels"))return"panels";if(/^top\.(poll|page)/.test(p))return"poll";
 if(p.startsWith("top."))return"adv";if(/^defaults\.(quiet|urgent)/.test(p))return"quiet";return p=="form"?"":"notify"}

/* Client checks mirror the server's (settings_api.build), with the same messages. */
function validate(){const e={},t=F.top,d=F.notifications.defaults;
 if(!String(t.handle||"").trim())e["top.handle"]="required";
 const pi=String(t.poll_interval_sec??"").trim(),pc=String(t.page_cycle_sec??"").trim();
 if(!/^\d+$/.test(pi))e["top.poll_interval_sec"]="must be a whole number";else if(+pi<60||+pi>3600)e["top.poll_interval_sec"]="60 to 3600 seconds";
 if(!/^\d+$/.test(pc))e["top.page_cycle_sec"]="must be a whole number";else if(+pc<10)e["top.page_cycle_sec"]="at least 10 seconds";
 HOSTS.forEach((h,i)=>{const x=String(h).trim().replace(/^https?:\/\//,"").replace(/\/$/,"");if(/[\s\/]/.test(x))e["top.panels."+i]=`"${x}" is not a host name or address`});
 Object.entries(d.min_count||{}).forEach(([k,v])=>{if(v!==""&&v!=null&&!/^\d+$/.test(String(v).trim()))e["defaults.min_count."+k]="whole number"});
 if(!(d.events||[]).length)e["defaults.events"]="pick at least one event";
 F.notifications.channels.forEach((c,j)=>{const P=`channels.${j}.`;
  if(Array.isArray(c.events)&&!c.events.length)e[P+"events"]="pick at least one event, or use the defaults";
  (F.schema.types[c.type]||[]).forEach(f=>{const v=c[f.key];
   if(f.required&&(v==null||String(v).trim()===""))e[P+f.key]=f.label+" is required";
   else if(f.kind=="int"&&v!=null&&v!==""&&!/^-?\d+$/.test(String(v).trim()))e[P+f.key]=f.label+" must be a whole number"})});
 return e}
function shownErr(){const ce=validate(),E={...ERR};for(const k in ce)if(TRIED||TOUCH.has(k))E[k]=ce[k];return E}

/* Errors, nav markers, the save bar and live hints, without re-rendering the form. */
function paint(){const ch=changedSecs(),d=ch.length>0;document.body.classList.toggle("dirty",d);if($("ctset").hidden==d){$("ctset").hidden=!d;tabScroll()}
 const stx=d?`Unsaved changes<span class=where> in ${esc(ch.map(s=>SECN[s]).join(", "))}</span>`:"";if($("savetx").innerHTML!=stx)$("savetx").innerHTML=stx;
 const E=F?shownErr():{},bad=new Set(Object.keys(E).map(secOf));EC=E;
 document.querySelectorAll(".setnav a").forEach(a=>{const s=a.dataset.s,b=bad.has(s),c=ch.includes(s);a.classList.toggle("bad",b);a.classList.toggle("dirty",c);
  a.querySelector(".sr").textContent=b?" (needs fixing)":c?" (unsaved)":""});
 if(!F)return d;
 document.querySelectorAll("#form [data-e]").forEach(el=>el.textContent=E[el.dataset.e]?cap(E[el.dataset.e]):"");
 document.querySelectorAll("#form [data-v]").forEach(c=>{if(/^(INPUT|SELECT|TEXTAREA)$/.test(c.tagName))c.setAttribute("aria-invalid",!!E[c.dataset.v]);else c.classList.toggle("bad",!!E[c.dataset.v])});
 document.querySelectorAll("#form .ch").forEach(c=>{const P=`channels.${c.dataset.j}.`;c.classList.toggle("bad",Object.keys(E).some(k=>k.startsWith(P)))});
 const t=F.top,eff=F.effective.poll_interval_sec,np=$("n-poll"),nc=$("n-cyc");
 const pph=+t.poll_interval_sec>=60?Math.round(3600/+t.poll_interval_sec*10)/10:0;
 if(np)np.textContent=[dur(t.poll_interval_sec)&&"= "+dur(t.poll_interval_sec)+(pph?`, ${pph} ${pph==1?"poll":"polls"} an hour`:""),eff!=F0.top.poll_interval_sec?`Running at ${dur(eff)} right now, set from a panel. Saving here takes over.`:""].filter(Boolean).join(". ");
 if(nc)nc.textContent=dur(t.page_cycle_sec)?"= "+dur(t.page_cycle_sec):"";
 const sh=$("sumhint");if(sh){const nd=F.notifications.defaults,on=F.notifications.channels.filter(c=>c.enabled!==false&&(c.events==null?nd.events||[]:c.events).includes("summary")).length;
  sh.textContent=on?`Goes to ${on} channel${on==1?"":"s"}.`:"No channel gets it yet. Tick Daily summary under Default events, or in a channel's own events."}
 summary(E);return d}
const cap=s=>String(s).charAt(0).toUpperCase()+String(s).slice(1);
function summary(E){const s=$("esum");if(!s)return;const ks=Object.keys(E);if(!TRIED||!ks.length){s.hidden=true;return}
 s.hidden=false;const html=`<h2>${ks.length==1?"1 thing needs":ks.length+" things need"} fixing before you can save</h2><ul>${ks.map(k=>`<li><button type=button class=lnk data-goto="${esc(k)}">${esc(errLabel(k))}</button>: ${esc(E[k])}</li>`).join("")}</ul>`;if(s.innerHTML==html)return;s.innerHTML=html;
 s.querySelectorAll("[data-goto]").forEach(b=>b.onclick=()=>goto(b.dataset.goto))}
function errLabel(k){const m=k.match(/^channels\.(\d+)\.(\w+)/);
 if(m){const c=F.notifications.channels[+m[1]],n=c?(c.name||TYPE_LABEL[c.type]||c.type):"Channel";return n+" › "+(FL[k]||{form:"Settings",env:"Environment",type:"Type"}[m[2]]||m[2])}
 if(k=="form")return"config.yaml";if(k.startsWith("top.panels."))return"Panel "+(+k.split(".")[2]+1);return FL[k]||k}
function goto(k){const m=k.match(/^channels\.(\d+)\./);if(m&&!open.has(+m[1])){open.add(+m[1]);renderSettings()}
 let el=document.querySelector(`#form [data-v="${CSS.escape(k)}"]`);if(el&&!/^(INPUT|SELECT|TEXTAREA)$/.test(el.tagName))el=el.querySelector("button,input")||el;
 el=el||(m?document.querySelector(`#form .ch[data-j="${m[1]}"]`):null)||$("sec-"+(secOf(k)||"mw"));
 el.scrollIntoView({behavior:SMOOTH,block:"center"});el.focus&&el.focus({preventScroll:true})}
function goSec(s){const el=$("sec-"+s);if(!el)return;FORCED=s;el.scrollIntoView({behavior:SMOOTH,block:"start"});
 const h=el.querySelector("h2");h.tabIndex=-1;h.focus({preventScroll:true});spy()}
function spy(){cancelAnimationFrame(spy.f);spy.f=requestAnimationFrame(()=>{if(document.body.dataset.view!="settings"||!F)return;
 const lim=document.querySelector(".top").offsetHeight+($("setnav").offsetHeight&&matchMedia("(max-width:900px)").matches?$("setnav").offsetHeight:0)+60;
 let cur=SECS[0][0];for(const[s]of SECS){const el=$("sec-"+s);if(el&&el.getBoundingClientRect().top<=lim)cur=s}
 if(FORCED)cur=FORCED;else if(scrollY>0&&innerHeight+scrollY>=document.documentElement.scrollHeight-4)cur=SECS[SECS.length-1][0];
 const nav=$("setnav");nav.querySelectorAll("a").forEach(a=>a.dataset.s==cur?a.setAttribute("aria-current","true"):a.removeAttribute("aria-current"));
 const a=nav.querySelector("[aria-current]");if(a&&nav.scrollWidth>nav.clientWidth)nav.scrollLeft=a.offsetLeft-nav.clientWidth/2+a.offsetWidth/2})}
addEventListener("scroll",spy,{passive:true});
["wheel","touchstart","keydown"].forEach(e=>addEventListener(e,()=>{if(FORCED){FORCED="";spy()}},{passive:true}));

/* ---- building blocks ---- */
const ctlAttrs=p=>{const id=pid(p);return `id=${id} data-p="${p}" data-v="${p}" aria-describedby="${id}-h ${id}-e"`};
const errEl=p=>`<p class=ferr id=${pid(p)}-e data-e="${p}"></p>`;
function inp(p,val,{type="text",ph="",min,max}={}){
 return `<input type=${type} ${ctlAttrs(p)} value="${esc(val??"")}" placeholder="${esc(ph)}"${min!=null?` min=${min}`:""}${max!=null?` max=${max}`:""}${type=="number"?" step=1 inputmode=numeric":""} autocomplete=off spellcheck=false>`}
function sec(id,title,desc,body,aside=""){return `<section class="card sec" id=sec-${id} aria-labelledby=h-${id}><header><div><h2 id=h-${id}>${title}</h2><p>${desc}</p></div><span class=sp></span>${aside}</header>${body}</section>`}
function srow(p,label,help,ctl,{forId=true}={}){const id=pid(p);FL[p]=FL[p]||label.replace(/<[^>]+>/g,"");
 return `<div class=srow><div class=lab>${forId?`<label for=${id}>${label}</label>`:`<span class=lt id=${id}-l>${label}</span>`}${help?`<p id=${id}-h>${help}</p>`:""}</div><div class=ctl>${ctl}${errEl(p)}</div></div>`}
function fld(p,label,{type="text",val,ph="",hint="",wide=false,opt=false,area=false}={}){const id=pid(p);FL[p]=label;
 const ctl=area?`<textarea ${ctlAttrs(p)} placeholder="${esc(ph)}" spellcheck=false>${esc(val??"")}</textarea>`:inp(p,val,{type,ph});
 return `<div class="fld${wide?" wide":""}"><label for=${id}>${esc(label)}${opt?` <span class=opt>(optional)</span>`:""}</label>${ctl}<p class=help id=${id}-h>${esc(hint)}</p>${errEl(p)}</div>`}
function evChips(path,list,lab){const on=new Set(list||[]);
 return `<div class=chips role=group aria-labelledby=${lab} data-ev="${path}" data-v="${path}">${Object.keys(EV).map(k=>`<button type=button class="chip${on.has(k)?" on":""}" data-k=${k} aria-pressed=${on.has(k)}>${on.has(k)?CHECK:""}${EV[k]}</button>`).join("")}</div>`}
function qPick(p,val){const id=pid(p),[a,b]=String(val||"23:00-07:00").split("-").map(s=>s.trim());
 const opts=cur=>(SLOTS.includes(cur)?SLOTS:[...SLOTS,cur].sort()).map(s=>`<option value="${esc(s)}"${s==cur?" selected":""}>${t12(s)}</option>`).join("");
 return `<div class=qh><label for=${id}-a>From</label><select id=${id}-a data-q="${p}" data-v="${p}" aria-describedby="${id}-d ${id}-e">${opts(a)}</select>
  <label for=${id}-b>to</label><select id=${id}-b data-q="${p}" data-v="${p}" aria-describedby="${id}-d ${id}-e">${opts(b)}</select></div><p class=help id=${id}-d>${qdur(a,b)}</p>`}
function cookieHTML(){const c=COOK;if(!c)return `<div class=skel style="width:60%;margin:8px 0"></div>`;
 const a=c.auth,b=a=="ok"?`<span class="badge ok"><i class=bd></i>Working</span>`:a=="expired"?`<span class="badge err"><i class=bd></i>Rejected by MakerWorld</span>`:
  c.present?`<span class="badge mut"><i class=bd></i>Not checked yet</span>`:`<span class="badge mut"><i class=bd></i>Not set</span>`;
 return `<div class=cookie>${b}<span class=ink2 title="${esc((c.names||[]).join(", "))}">${c.present?`Saved ${c.age!=null?ago(Date.now()/1e3-c.age):""}, ${c.names.length} cookies`:"No cookie saved yet."}</span></div>
  ${c.lastError&&!c.lastOk?`<p class=ferr>${esc(c.lastError)}</p>`:""}`}
function pnlRow(h,i){const key=h.trim(),p="top.panels."+i,ck=PCHK[key],d=(F.panels_detected||[]).find(x=>x.host==key||x.host==key.replace(/:80$/,""));
 let cls="mut",st="Not checked yet",meta="";
 if(ck){cls=ck.ok?"ok":"err";st=ck.ok?"Online":"No answer";meta=ck.ok?esc(ck.msg):"Checked just now"}
 else if(!key)st="New";
 else if(!d)st="Not saved yet";
 else if(d.up===true){cls="ok";st="Online"}else if(d.up===false){cls="err";st="Offline"}
 if(!meta&&d&&d.type!="unknown")meta=[PTYPE[d.type],d.board,d.fw&&"v"+d.fw].filter(Boolean).map(esc).join(" · ");
 FL[p]="Panel "+(i+1);
 return `<div class=pnl><div class=pt><label class=sr for=${pid(p)}>Panel ${i+1} address</label>
  <input type=text id=${pid(p)} data-host=${i} data-v="${p}" aria-describedby="${pid(p)}-s ${pid(p)}-e" value="${esc(h)}" placeholder="makermonitor.local or 192.168.1.50" autocomplete=off spellcheck=false inputmode=url>
  <button class="btn sm" type=button id=ck-${i} data-check=${i}${key?"":" disabled"}>Check</button>
  <button class="btn sm ghost dan" type=button id=rh-${i} data-rmhost=${i} aria-label="Remove panel ${esc(key||i+1)}">Remove</button></div>
  <div class=pm id=${pid(p)}-s><span class="badge ${cls}"><i class=bd></i>${st}</span>${meta?`<span>${meta}</span>`:""}</div>${errEl(p)}</div>`}
function secretFld(c,j,f){const p=`channels.${j}.${f.key}`,id=pid(p),saved=c._set&&c._set[f.key],area=f.kind=="lines"||f.kind=="headers";FL[p]=f.label;
 const lab=esc(f.label)+(f.required?"":` <span class=opt>(optional)</span>`);
 if(saved&&c[f.key]===KEEP&&!REPL.has(p))return `<div class="fld${area?" wide":""}"><span class=lt id=${id}-l>${lab}</span>
  <div class=secret><span class="badge mut">${LOCK} Saved and hidden</span><button type=button class="btn sm" id=${id} data-repl="${p}" aria-describedby=${id}-l>Replace</button></div>
  <p class=help>${esc(f.hint)}</p>${errEl(p)}</div>`;
 const shown=SHOW.has(p),v=c[f.key]===KEEP?"":c[f.key]??"";
 const ctl=area?`<textarea ${ctlAttrs(p)}${shown?"":" class=mask"} spellcheck=false autocomplete=off>${esc(v)}</textarea>`
  :`<input type=${shown?"text":"password"} ${ctlAttrs(p)} value="${esc(v)}" autocomplete=new-password spellcheck=false>`;
 return `<div class="fld${area?" wide":""}"><label for=${id}>${lab}</label><div class=pw>${ctl}
  <button type=button class="btn sm ghost" id=${id}-show data-show="${p}" aria-pressed=${shown} aria-controls=${id}>${shown?"Hide":"Show"}</button>
  ${saved?`<button type=button class="btn sm ghost" id=${id}-keep data-keep="${p}">Keep saved</button>`:""}</div>
  <p class=help id=${id}-h>${saved?"What you type replaces the saved value when you save.":esc(f.hint)}</p>${errEl(p)}</div>`}
function chanCard(c,j){const P=`channels.${j}`,isOpen=open.has(j),name=c.name||TYPE_LABEL[c.type]||c.type,on=c.enabled!==false,usesDef=c.events==null,ct=CTEST[j];
 const d=F.notifications.defaults,cq=!!c.quiet_hours;
 const sum=[usesDef?"Default events":`${(c.events||[]).length} events`,cq?"Quiet "+qtxt(c.quiet_hours):""].filter(Boolean).join(" · ");
 const errs=["form","env","type"].map(k=>ERR[P+"."+k]).filter(Boolean);
 const fields=(F.schema.types[c.type]||[]).map(f=>{const p=P+"."+f.key;FL[p]=f.label;
  if(f.secret)return secretFld(c,j,f);
  if(f.kind=="bool")return `<div class=fld><span class=lt>${esc(f.label)}</span><label class=chk><input type=checkbox ${ctlAttrs(p)}${c[f.key]?" checked":""}> ${esc(cap(f.hint))}</label></div>`;
  const area=f.kind=="lines"||f.kind=="headers";
  return fld(p,f.label,{type:f.kind=="int"?"number":"text",val:c[f.key],hint:cap(f.hint),area,wide:area,opt:!f.required})}).join("");
 return `<div class="ch${on?"":" off"}" data-j=${j}>
 <div class=chh><label class=sw title="${on?"Sending":"Paused"}"><input type=checkbox role=switch id=en-${j} data-en=${j}${on?" checked":""} aria-label="Send to ${esc(name)}"><i></i></label>
  <button type=button class=chx id=tg-${j} data-tog=${j} aria-expanded=${isOpen} aria-controls=chb-${j}><span class=nm>${esc(name)}</span><span class=ty>${esc(TYPE_LABEL[c.type]||c.type)}</span>
   ${on?"":`<span class="badge mut">Paused</span>`}<span class=sum>${esc(sum)}</span>${CHEV}</button></div>
 <div class=chb id=chb-${j}${isOpen?"":" hidden"}>${errs.length?`<p class=ferr role=alert>${errs.map(esc).join("<br>")}</p>`:""}
  <div class=fields>${fld(P+".name","Name",{val:c.name||"",ph:TYPE_LABEL[c.type]||c.type,hint:"Shown in the log. Must be unique.",opt:true})}${fields}
  <div class="fld wide"><span class=lt id=${pid(P+".events")}-l>Events</span><label class=chk><input type=checkbox id=ud-${j} data-usedef=${j}${usesDef?" checked":""}> Use the default events</label>
   ${usesDef?"":evChips(P+".events",c.events,pid(P+".events")+"-l")}${errEl(P+".events")}</div>
  <div class="fld wide"><label for=qm-${j}>Quiet hours</label><select id=qm-${j} data-qmode=${j} class=narrow>
   <option value=def${cq?"":" selected"}>Same as the default (${d.quiet_hours?esc(qtxt(d.quiet_hours)):"none"})</option><option value=own${cq?" selected":""}>Its own quiet hours</option></select>
   ${cq?qPick(P+".quiet_hours",c.quiet_hours):""}${errEl(P+".quiet_hours")}</div></div>
  <div class=chf><button class="btn sm" type=button id=tst-${j} data-testch=${j} aria-describedby=tsth-${j}>Send test message</button>
   <span class="res${ct?" "+(ct.ok?"ok":"no"):""}" id=res-ch-${j} aria-live=polite>${ct?esc(ct.msg):""}</span><span class=sp></span>
   <span class=sr id=tsth-${j}>Uses what is in the form now, saved or not</span>
   <button class="btn sm ghost dan" type=button id=rm-${j} data-rmch=${j}>Remove channel</button></div></div></div>`}

function renderSettings(){if(!F)return;const t=F.top,n=F.notifications,d=n.defaults,ro=F.readonly;FL={};EC=shownErr();
 const fa=document.activeElement,fid=fa&&$("form").contains(fa)?fa.id:"";
 const dg=d.quiet_digest==null?true:d.quiet_digest,qon=!!d.quiet_hours,np=HOSTS.filter(h=>h.trim()).length;
 const pup=HOSTS.map(h=>{const k=h.trim(),c=PCHK[k],x=(F.panels_detected||[]).find(y=>y.host==k||y.host==k.replace(/:80$/,""));return c?c.ok:x?x.up:null}),nup=pup.filter(v=>v===true).length,ndn=pup.filter(v=>v===false).length;
 $("form").innerHTML=`<div id=esum class=esum tabindex=-1 hidden></div>
 ${sec("mw","Account","Your MakerWorld profile, and the browser cookie that unlocks points and creator stats.",
  srow("top.handle","MakerWorld handle","Your username, as in makerworld.com/en/@<i>handle</i>.",`<div class="affix narrow"><span class=pre aria-hidden=true>@</span>${inp("top.handle",t.handle,{ph:"yourname"})}</div>`)+
  srow("cookie","Login cookie","The browser extension sends this each time you visit MakerWorld. Without it, only public stats are polled.",
   `<div id=cookiebox aria-live=polite>${cookieHTML()}</div>
   <details class=disc id=cookiedisc${COOKIN?" open":""}><summary>Paste a cookie by hand</summary><div>
    <label class=sr for=cookiein>Cookie</label><textarea id=cookiein class=mask spellcheck=false autocomplete=off aria-describedby=cookie-h2 placeholder="token=...; or the whole Cookie header from your browser's developer tools">${esc(COOKIN)}</textarea>
    <p class=help id=cookie-h2>Saved right away, separately from the Save button, and starts a poll.</p>
    <div class=row><button class="btn sm" id=cookiebtn type=button>Save cookie</button></div></div></details>`,{forId:false}))}
 ${sec("panels","Panels","Color and e-paper displays the poller sends stats to. It works out which kind each one is.",
  `<div class="srow full"><div class=plist>${HOSTS.length?HOSTS.map(pnlRow).join(""):`<div class=empty-st><b>No panels yet</b><p>Add a panel by the address on its Connect the poller page.</p></div>`}</div>${errEl("top.panels")}
   <div class=row><button class="btn sm" id=addhost type=button>${PLUS} Add panel</button></div>
   <p class=help>An IP address or a .local name. makermonitor.local keeps working when the router hands out a new address. Add :port for a port other than 80.</p></div>`,
  `<span class=cnt>${np?np+(np==1?" panel":" panels")+(nup?` · <b>${nup} online</b>`:"")+(ndn?` · <em>${ndn} offline</em>`:""):""}</span>`)}
 ${sec("notify","Notifications","Messages to your phone or chat when things happen. Each poll sends at most one message per channel.",
  `<div class="srow full"><div class=lab><span class=lt>Channels</span><p>Switch a channel off to pause it and keep its settings.</p></div>
   <div class=chans>${n.channels.map(chanCard).join("")||`<div class=empty-st><b>No channels yet</b><p>Add one below to start getting messages.</p></div>`}</div>
   <div class="row addch"><label class=sr for=newtype>New channel type</label><select id=newtype>${Object.keys(F.schema.types).map(k=>`<option value=${k}>${TYPE_LABEL[k]||k}</option>`).join("")}</select>
    <button class="btn sm" id=addch type=button>${PLUS} Add channel</button></div></div>`+
  srow("defaults.events","Default events","Channels send these unless they pick their own.",evChips("defaults.events",d.events,pid("defaults.events")+"-l"),{forId:false})+
  srow("defaults.summary_time","Daily summary","Sent once a day to channels with the Daily summary event: the day's totals, top models, any spikes, unanswered comments and the page most worth improving.",
   `<select ${ctlAttrs("defaults.summary_time")} class=narrow>${(SLOTS.includes(d.summary_time||"21:00")?SLOTS:[...SLOTS,d.summary_time].sort()).map(x=>`<option value="${x}"${x==(d.summary_time||"21:00")?" selected":""}>${t12(x)}</option>`).join("")}</select>
   <p class=help id=sumhint></p>`)+
  srow("defaults.min_count","Hold small amounts","Wait until an event adds up to this many, then send it. Smaller amounts roll into the next message. Empty or 1 sends every time.",
   `<div class=mini-grid>${F.schema.grouped.map(k=>{const p="defaults.min_count."+k;FL[p]="Hold "+EV[k].toLowerCase();
    return `<div class=fld><label for=${pid(p)}>${EV[k]}</label>${inp(p,(d.min_count||{})[k]??"",{type:"number",ph:"1",min:1})}${errEl(p)}</div>`}).join("")}</div>`,{forId:false}))}
 ${sec("quiet","Quiet hours","Nothing is sent during quiet hours except urgent events. Each panel has its own quiet hours, set on the panel.",
  srow("defaults.quiet_hours","Quiet hours","Applies to every channel unless a channel sets its own.",
   `<div class=swrow><label class=sw><input type=checkbox role=switch id=qon${qon?" checked":""} aria-label="Quiet hours" aria-describedby=qon-t><i></i></label><span id=qon-t>${qon?"On":"Off. Messages go out any time."}</span></div>${qon?qPick("defaults.quiet_hours",d.quiet_hours):""}`,{forId:false})+
  (qon?srow("defaults.quiet_digest","When quiet hours end","A catch-up message lists what happened while it was quiet.",
   `<select ${ctlAttrs("defaults.quiet_digest")}>${[["true","Send a catch-up if anything happened"],["always","Always send a catch-up, even after a quiet night"],["false","Don't send a catch-up"]].map(([k,l])=>`<option value="${k}"${String(dg)==k?" selected":""}>${l}</option>`).join("")}</select>`)+
  srow("defaults.urgent","Urgent events","Sent right away, even during quiet hours.",evChips("defaults.urgent",d.urgent||["health"],pid("defaults.urgent")+"-l"),{forId:false}):""))}
 ${sec("poll","Polling","How often MakerWorld is checked, and how long e-paper pages stay up.",
  srow("top.poll_interval_sec","Check MakerWorld every","60 to 3600 seconds. Every MakerWorld request shares a budget of about 120 an hour.",
   `<div class=unitrow><div class=affix>${inp("top.poll_interval_sec",t.poll_interval_sec,{type:"number",min:60,max:3600})}<span class=suf aria-hidden=true>seconds</span></div><span class=help id=n-poll aria-live=polite></span></div>`)+
  srow("top.page_cycle_sec","E-paper page time","How long each e-paper page shows before it flips. At least 10 seconds.",
   `<div class=unitrow><div class=affix>${inp("top.page_cycle_sec",t.page_cycle_sec,{type:"number",min:10})}<span class=suf aria-hidden=true>seconds</span></div><span class=help id=n-cyc></span></div>`))}
 ${sec("adv","Advanced","Rarely needed.",
  srow("top.user_agent","Browser user agent","Should roughly match the browser the cookie came from.",inp("top.user_agent",t.user_agent))+
  `<div class=srow><div class=lab><span class=lt>Set in config.yaml only</span><p>Changing these needs a poller restart.</p></div><div class=ctl>
   <dl class=kv><dt>Listen</dt><dd>${esc(ro.listen_host)}:${esc(ro.listen_port)}</dd><dt>Data folder</dt><dd>${esc(ro.data_dir).replace(/\//g,"/<wbr>")}</dd><dt>Dashboard token</dt><dd>api_token</dd><dt>Config file</dt><dd>${esc(F.path).replace(/\//g,"/<wbr>")}</dd></dl>
   <p class=help>Every save keeps the previous file as config.yaml.bak-&lt;time&gt; next to it (the last 10).</p></div></div>`)}`;
 bindSettings();paint();spy();
 if(fid&&$(fid))$(fid).focus({preventScroll:true})}

function setPath(path,val){const p=path.split(".");let o=p[0]=="top"?F.top:p[0]=="defaults"?F.notifications.defaults:F.notifications.channels;
 for(let i=1;i<p.length-1;i++){o[p[i]]=o[p[i]]??{};o=o[p[i]]}o[p[p.length-1]]=val}
function syncHosts(){F.top.panels=HOSTS.map(h=>h.trim()).filter(Boolean)}
function dropKeys(prefix){for(const k of Object.keys(ERR))if(k.startsWith(prefix))delete ERR[k];for(const k of [...TOUCH])if(k.startsWith(prefix))TOUCH.delete(k)}
function bindSettings(){const form=$("form"),all=(s,f)=>form.querySelectorAll(s).forEach(f);
 all("[data-p]",el=>el.oninput=el.onchange=ev=>{const p=el.dataset.p;let v=el.type=="checkbox"?el.checked:el.value;
  if(el.type=="number"&&v!==""&&/^-?\d+$/.test(v))v=Number(v);
  const m=p.match(/^channels\.(\d+)\.(\w+)$/);if(m){const c=F.notifications.channels[+m[1]];if(c._set&&c._set[m[2]]&&v==="")v=KEEP}
  if(p=="defaults.quiet_digest")v=v=="true"?true:v=="false"?false:"always";
  setPath(p,v);delete ERR[p];if(ev.type=="change")TOUCH.add(p);paint()});
 all("[data-ev]",box=>box.querySelectorAll(".chip").forEach(b=>b.onclick=()=>{const on=!b.classList.contains("on");b.classList.toggle("on",on);b.setAttribute("aria-pressed",on);
  b.innerHTML=(on?CHECK:"")+EV[b.dataset.k];const p=box.dataset.ev;setPath(p,[...box.querySelectorAll(".chip.on")].map(x=>x.dataset.k));delete ERR[p];TOUCH.add(p);paint()}));
 all("[data-host]",el=>{const i=+el.dataset.host;
  el.oninput=()=>{HOSTS[i]=el.value;syncHosts();delete ERR["top.panels"];delete ERR["top.panels."+i];$("ck-"+i).disabled=!el.value.trim();paint()};
  el.onchange=()=>{TOUCH.add("top.panels."+i);paint()};
  el.onkeydown=e=>{if(e.key=="Enter"){e.preventDefault();$("ck-"+i).click()}}});
 all("[data-check]",b=>b.onclick=async()=>{const i=+b.dataset.check,host=HOSTS[i].trim();if(!host)return;b.disabled=true;b.textContent="Checking…";
  let j;try{const r=await api("/api/settings/panel",{host});j=await r.json()}catch(e){j={ok:false,msg:"poller unreachable"}}
  PCHK[host]={ok:!!j.ok,msg:j.msg||"no answer"};say(host+": "+(j.ok?"online, "+j.msg:j.msg||"no answer"));renderSettings()});
 all("[data-rmhost]",b=>b.onclick=async()=>{const i=+b.dataset.rmhost,h=HOSTS[i].trim();
  if(h&&!await ask(`Remove ${h}?`,"It stops getting stats once you save. Discard undoes this until then.","Remove panel"))return;
  HOSTS.splice(i,1);syncHosts();dropKeys("top.panels");renderSettings();const nx=$("rh-"+Math.min(i,HOSTS.length-1));(nx||$("addhost")).focus()});
 $("addhost").onclick=()=>{HOSTS.push("");renderSettings();$(pid("top.panels."+(HOSTS.length-1))).focus()};
 all("[data-tog]",b=>b.onclick=()=>{const j=+b.dataset.tog;open.has(j)?open.delete(j):open.add(j);renderSettings()});
 all("[data-en]",el=>el.onchange=()=>{const c=F.notifications.channels[+el.dataset.en];if(el.checked)delete c.enabled;else c.enabled=false;renderSettings()});
 all("[data-usedef]",el=>el.onchange=()=>{const j=+el.dataset.usedef,c=F.notifications.channels[j];
  c.events=el.checked?null:[...(F.notifications.defaults.events||Object.keys(EV))];dropKeys(`channels.${j}.events`);renderSettings()});
 all("[data-qmode]",el=>el.onchange=()=>{const j=+el.dataset.qmode,c=F.notifications.channels[j];
  c.quiet_hours=el.value=="own"?(F.notifications.defaults.quiet_hours||"23:00-07:00"):"";dropKeys(`channels.${j}.quiet_hours`);renderSettings()});
 all("[data-q]",s=>s.onchange=()=>{const p=s.dataset.q,[a,b]=[...form.querySelectorAll(`[data-q="${p}"]`)].map(x=>x.value);
  setPath(p,a+"-"+b);if(p=="defaults.quiet_hours")QLAST=a+"-"+b;delete ERR[p];$(pid(p)+"-d").textContent=qdur(a,b);paint()});
 const qo=$("qon");qo.onchange=()=>{const d=F.notifications.defaults;if(qo.checked)d.quiet_hours=QLAST||"23:00-07:00";else{QLAST=d.quiet_hours||QLAST;d.quiet_hours=""}
  delete ERR["defaults.quiet_hours"];renderSettings()};
 all("[data-repl]",b=>b.onclick=()=>{REPL.add(b.dataset.repl);renderSettings();$(pid(b.dataset.repl)).focus()});
 all("[data-keep]",b=>b.onclick=()=>{const p=b.dataset.keep;REPL.delete(p);SHOW.delete(p);setPath(p,KEEP);delete ERR[p];TOUCH.delete(p);renderSettings();$(pid(p)).focus()});
 all("[data-show]",b=>b.onclick=()=>{const p=b.dataset.show;SHOW.has(p)?SHOW.delete(p):SHOW.add(p);renderSettings()});
 all("[data-rmch]",b=>b.onclick=async()=>{const j=+b.dataset.rmch,c=F.notifications.channels[j],nm=c.name||TYPE_LABEL[c.type]||c.type;
  if(!await ask(`Remove ${nm}?`,"It stops getting messages once you save. Discard undoes this until then.","Remove channel"))return;
  F.notifications.channels.splice(j,1);const o=[...open].filter(x=>x!=j).map(x=>x>j?x-1:x);open.clear();o.forEach(x=>open.add(x));
  for(const k in CTEST)delete CTEST[k];dropKeys("channels.");renderSettings();$("addch").focus()});
 $("addch").onclick=()=>{const type=$("newtype").value;F.notifications.channels.push({type,events:null,_set:{}});const j=F.notifications.channels.length-1;open.add(j);
  renderSettings();const first=$("chb-"+j).querySelector("input,textarea,button");first&&first.focus();$("tg-"+j).scrollIntoView({behavior:SMOOTH,block:"start"})};
 all("[data-testch]",b=>b.onclick=async()=>{const j=+b.dataset.testch,res=$("res-ch-"+j);res.className="res";res.textContent="Sending…";b.disabled=true;let o;
  try{const r=await api("/api/settings/test",clean(F.notifications.channels[j]));o=await r.json()}catch(e){o={ok:false,errors:{x:"poller unreachable"}}}
  b.disabled=false;CTEST[j]={ok:!!o.ok,msg:o.ok?"Sent. Check the app or channel.":Object.values(o.errors||{}).join("; ")||o.detail||"Failed"};
  res.className="res "+(o.ok?"ok":"no");res.textContent=CTEST[j].msg});
 $("cookiein").oninput=e=>{COOKIN=e.target.value};
 $("cookiebtn").onclick=async()=>{const v=$("cookiein").value.trim();if(!v){toast("Paste a cookie first.","err");$("cookiein").focus();return}
  const r=await api("/api/settings/cookie",{cookie:v}).catch(()=>null);
  if(r&&r.ok){const j=await r.json();COOKIN="";$("cookiein").value="";$("cookiedisc").open=false;renderCookie(j);toast(j.changed?"Cookie saved. Polling MakerWorld now.":"That cookie was already saved.")}
  else toast("Cookie not saved: "+(r?((await r.json().catch(()=>({}))).detail||"HTTP "+r.status):"poller unreachable"),"err")}}
function clean(c){const o={};for(const[k,v]of Object.entries(c))if(k!="_set")o[k]=v;return o}
async function loadCookie(){try{const r=await api("/api/settings/cookie");if(r.ok)renderCookie(await r.json())}catch(e){}}
function renderCookie(c){COOK=c;const b=$("cookiebox");if(b)b.innerHTML=cookieHTML()}
function focusSum(){const s=$("esum");if(s&&!s.hidden){s.scrollIntoView({behavior:SMOOTH,block:"center"});s.focus({preventScroll:true})}}
function openBad(E){F.notifications.channels.forEach((c,i)=>{if(Object.keys(E).some(k=>k.startsWith(`channels.${i}.`)))open.add(i)})}
async function save(){if(!F||save.busy)return;syncHosts();TRIED=true;const ce=validate();
 if(Object.keys(ce).length){openBad(ce);renderSettings();toast("Not saved. Fix the fields listed at the top.","err");focusSum();return}
 save.busy=true;const btn=$("save");btn.disabled=true;btn.textContent="Saving…";
 const body={rev:F.rev,top:F.top,notifications:{defaults:F.notifications.defaults,channels:F.notifications.channels.map(clean)}};
 let r=null,j;try{r=await api("/api/settings",body);j=await r.json()}catch(e){j={ok:false,detail:"poller unreachable"}}
 save.busy=false;btn.disabled=false;btn.textContent="Save changes";
 if(r&&r.status==409){toast((j.detail||"config.yaml changed on disk")+". Reload the page to see it; your edits are still here.","err");return}
 if(j.ok){toast(j.changed?"Saved and applied. Backup: "+j.backup:"No changes to save.");await loadSettings();setTimeout(load,1500);return}
 ERR=j.errors||{};toast(j.detail?"Not saved: "+j.detail:"Not saved. Fix the fields listed at the top.","err");openBad(ERR);renderSettings();focusSum()}
$("save").onclick=save;
$("discard").onclick=async()=>{if(!await ask("Discard your changes?","Everything you changed since the last save goes back to how it was.","Discard changes"))return;
 F=JSON.parse(F0raw);HOSTS=[...F.top.panels];QLAST=F.notifications.defaults.quiet_hours||"";ERR={};TRIED=false;TOUCH.clear();open.clear();REPL.clear();SHOW.clear();
 renderSettings();toast("Changes discarded.")};
document.querySelectorAll(".setnav a").forEach(a=>a.onclick=e=>{e.preventDefault();history.replaceState(null,"","#settings/"+a.dataset.s);goSec(a.dataset.s)});
addEventListener("keydown",e=>{if((e.ctrlKey||e.metaKey)&&e.key.toLowerCase()=="s"&&document.body.dataset.view=="settings"&&F){e.preventDefault();if(changedSecs().length)save()}});
addEventListener("beforeunload",e=>{if(changedSecs().length){e.preventDefault();e.returnValue=""}});
addEventListener("resize",()=>{setTop();spy()});

(()=>{const tb=$("tipbox");let cur=null;
 const show=el=>{cur=el;tb.textContent=el.dataset.tip;tb.hidden=false;const r=el.getBoundingClientRect(),w=tb.offsetWidth,h=tb.offsetHeight;
  let x=Math.min(Math.max(8,r.left+r.width/2-w/2),innerWidth-w-8),y=r.top-h-8;if(y<8)y=r.bottom+8;tb.style.left=x+"px";tb.style.top=y+"px";el.setAttribute("aria-describedby","tipbox")};
 const hide=()=>{if(cur)cur.removeAttribute("aria-describedby");cur=null;tb.hidden=true};
 addEventListener("mouseover",e=>{const el=e.target.closest("[data-tip]");el?el!==cur&&show(el):cur&&hide()});
 addEventListener("focusin",e=>{const el=e.target.closest("[data-tip]");el?show(el):hide()});
 addEventListener("click",e=>{const el=e.target.closest(".tip[data-tip]");if(el){e.preventDefault();cur===el?hide():show(el)}});
 addEventListener("scroll",hide,{passive:true});addEventListener("keydown",e=>{if(e.key=="Escape")hide()})})();
setTop();route();load();setInterval(load,60000);
</script>
<script>
"use strict";
/* ---------- export ---------- */
(()=>{const XM={impressions:"Impressions",views:"Views",downloads:"Downloads",prints:"Prints",likes:"Likes",collects:"Collects",boosts:"Boosts",points:"Points",followers:"Followers",ratings:"Ratings",comments:"Comments"};
 const XG={daily:"Per day",total:"Range totals",hourly:"Per hour"};
 const XGM={daily:"Per model, per day",total:"Per model, range totals"};
 const XN={account:"Account totals from the poller's own tracking (about 30 days kept), plus today so far. Views and impressions come from MakerWorld's analytics where fetched.",
  models:"MakerWorld's own per-model analytics (about 35 days, complete days only), filled in from the poller's tracking for days MakerWorld hasn't finished, including today. The source column says which.",
  hourly:"Activity by hour of the day (about 35 days kept), plus today so far.",
  lifetime:"Every model's all-time totals from the last poll, with comments, ratings and publish date. No date range."};
 let M=null,loadingMeta=false,previewing=false,metaFailed=false;
 const picks={models:new Set(),lifetime:new Set()},mets={},x=$;
 const chosenIds=()=>picks[x("xds").value]||new Set();
 const eligible=ds=>M.models.filter(model=>ds==="lifetime"?model.lifetime!==false:!model.dates||model.dates.length);
 const dfmt=d=>{if(!d)return"";const[y,m,dd]=d.split("-").map(Number);return new Date(y,m-1,dd).toLocaleDateString("en-US",{month:"short",day:"numeric",year:"numeric"})};
 const iso=(Y,Mo,Da)=>{const z=new Date(Y,Mo,Da);return z.getFullYear()+"-"+String(z.getMonth()+1).padStart(2,"0")+"-"+String(z.getDate()).padStart(2,"0")};
 function range(){const t=M.today,[y,m,d]=t.split("-").map(Number);
  switch(x("xrg").value){case"today":return[t,t];case"7d":return[iso(y,m-1,d-6),t];case"30d":return[iso(y,m-1,d-29),t];case"90d":return[iso(y,m-1,d-89),t];
   case"this_month":return[iso(y,m-1,1),t];case"last_month":return[iso(y,m-2,1),iso(y,m-1,0)];case"custom":return[x("xa").value||null,x("xb").value||null];default:return[null,null]}}
 const hasMods=ds=>ds=="models"||ds=="lifetime";
 function q(){const ds=x("xds").value,info=M.datasets[ds],p=new URLSearchParams({dataset:ds}),pick=chosenIds();
  if(ds!="lifetime"){
   const [a,b]=range();
   p.set("range",a||b?"custom":"all");
   if(a)p.set("start",a);if(b)p.set("end",b);
  }
  const on=info.metrics.filter(k=>mets[ds].has(k));if(on.length<info.metrics.length)p.set("metrics",on.join(","));
  if(info.granularity.length>1)p.set("granularity",x("xgr").value);
  if(hasMods(ds)&&pick.size)p.set("models",[...pick].join(","));return p}
 function update(){if(!M)return;const ds=x("xds").value,info=M.datasets[ds],nr=ds=="lifetime",cu=!nr&&x("xrg").value=="custom",pick=chosenIds();
  x("xrf").hidden=nr;x("xcf").hidden=x("xcf2").hidden=!cu;x("xgf").hidden=info.granularity.length<2;x("xmf").hidden=!hasMods(ds);
  const on=info.metrics.filter(k=>mets[ds].has(k)),[a,b]=nr?[null,null]:range(),sp=info.span;
  const invalid=Boolean(a&&b&&a>b);
  const available=eligible(ds),chosen=available.filter(model=>!pick.size||pick.has(model.id));
  const selectedDates=ds==="models"&&pick.size&&Array.isArray(info.dates)
   ?[...new Set(chosen.flatMap(model=>model.dates||[]))].sort():info.dates;
  const dates=selectedDates?.filter(date=>(!a||date>=a)&&(!b||date<=b));
  const lo=dates?dates[0]:sp&&(a&&a>sp.from?a:sp.from);
  const hi=dates?dates.at(-1):sp&&(b&&b<sp.to?b:sp.to);
  const hasDates=nr?Boolean(chosen.length):Boolean(lo&&hi&&lo<=hi);
  const included=nr?chosen.length:chosen.filter(model=>!model.dates||
    model.dates.some(date=>(!a||date>=a)&&(!b||date<=b))).length;
  const partial=M.partialDay&&(dates?dates.includes(M.partialDay):lo<=M.partialDay&&M.partialDay<=hi);
  let warn="";
  if(metaFailed)warn="Couldn’t refresh the available dates. Try again.";
  else if(!on.length)warn="Pick at least one metric.";
  else if(invalid)warn="Choose an end date on or after the start date.";
  else if(!hasDates)warn="No stored data in this range. Choose another range or model.";
  else if(!nr&&((a&&lo>a)||(b&&hi<b)))warn="Only the stored dates within your selection are included.";
  x("xnote").textContent=XN[ds]+(warn?" "+warn:"");
  const rows=ds=="models"?XGM[x("xgr").value]:ds=="lifetime"?"Per model":XG[x("xgr").value];
  x("xinfo").innerHTML=`<dt>Data</dt><dd>${esc(x("xds").selectedOptions[0].text)}</dd>`+
   (nr?"":`<dt>Stored dates</dt><dd>${!invalid&&hasDates?esc(dfmt(lo))+(hi!==lo?" – "+esc(dfmt(hi)):"")+(dates?` (${dates.length} day${dates.length===1?"":"s"})`:""):"None in selection"}${!invalid&&hasDates&&partial?" · includes partial day":""}</dd>`)+
   `<dt>Rows</dt><dd>${esc(rows)}</dd><dt>Columns</dt><dd>${on.length?esc(on.map(k=>XM[k]).join(", ")):"None"}</dd>`+
   (hasMods(ds)?`<dt>Models</dt><dd>${`${included} with data${pick.size?` of ${pick.size} selected`:""}`}</dd>`:"");
  x("xmn").textContent=pick.size?`(${pick.size} selected)`:"(all)";
  const ok=on.length>0&&!invalid&&hasDates&&!metaFailed;x("xgo").href=ok?"/api/export.csv?"+q():"#export";x("xgo").classList.toggle("dis",!ok);
  ok?x("xgo").removeAttribute("aria-disabled"):x("xgo").setAttribute("aria-disabled","true");x("xpv").disabled=!ok||previewing}
 function renderDs(){if(!M)return;const ds=x("xds").value,info=M.datasets[ds],G=ds=="models"?XGM:XG;
  x("xgr").innerHTML=info.granularity.map(g=>`<option value=${g}>${G[g]}</option>`).join("");
  x("xmets").innerHTML=info.metrics.map(k=>`<label class=chk><input type=checkbox value=${k} ${mets[ds].has(k)?"checked":""}> ${XM[k]}</label>`).join("");
  x("xmets").querySelectorAll("input").forEach(i=>i.onchange=()=>{i.checked?mets[ds].add(i.value):mets[ds].delete(i.value);x("xpcard").hidden=true;update()});
  x("xpcard").hidden=true;update()}
 const shown=()=>{const f=x("xq").value.trim().toLowerCase();return eligible(x("xds").value).filter(m=>!f||(m.name||m.id).toLowerCase().includes(f))};
 function renderMods(){if(!M)return;const ms=shown(),pick=chosenIds();
  x("xmods").innerHTML=ms.map(m=>`<label><input type=checkbox value="${esc(m.id)}" ${pick.has(m.id)?"checked":""}><span title="${esc(m.name)}">${esc(m.name||"Model "+m.id)}</span></label>`).join("")||`<p class=hint>No models match.</p>`;
  x("xmods").querySelectorAll("input").forEach(i=>i.onchange=()=>{i.checked?pick.add(i.value):pick.delete(i.value);x("xpcard").hidden=true;update()})}
 function parse(t){const rows=[];let r=[],c="",qt=false;for(let i=0;i<t.length;i++){const ch=t[i];
  if(qt){if(ch=='"'){if(t[i+1]=='"'){c+='"';i++}else qt=false}else c+=ch}
  else if(ch=='"')qt=true;else if(ch==","){r.push(c);c=""}else if(ch=="\n"){r.push(c.replace(/\r$/,""));rows.push(r);r=[];c=""}else c+=ch}
  if(c||r.length){r.push(c);rows.push(r)}return rows}
 async function preview(){const b=x("xpv"),query=String(q());previewing=true;b.disabled=true;b.textContent="Loading…";
  try{const r=await fetch("/api/export.csv?"+query,{credentials:"same-origin"}),t=(await r.text()).replace(/^\uFEFF/,"");
   if(!r.ok){let m=t;try{m=JSON.parse(t).detail}catch(e){}throw new Error(m)}
   if(String(q())!==query)return;
   const rows=parse(t),head=rows.shift()||[],n=rows.length;
   x("xpn").textContent=n?`first ${Math.min(n,10)} of ${fmt(n)} rows`:"no rows";
   x("xptab").innerHTML=table(head.map(esc),rows.slice(0,10).map(r=>"<tr>"+r.map(v=>`<td>${esc(v)}</td>`).join("")+"</tr>"),"No data in this range");
   x("xpcard").hidden=false}
  catch(e){toast("Preview failed: "+e.message,"err")}
  finally{previewing=false;b.textContent="Preview";update()}}
 async function load(){
  if(loadingMeta)return;
  const initial=!M;
  loadingMeta=true;x("xretry").hidden=true;
  if(initial)x("xnote").textContent="Loading export options…";
  try{
   const r=await fetch("/api/export/meta",{credentials:"same-origin"});
   if(!r.ok)throw new Error("Export options unavailable");
   const previous=M,updated=await r.json();
   M=updated;metaFailed=false;
   if(previous?.today!==M.today)x("xpcard").hidden=true;
  }catch(e){
   metaFailed=true;
   if(M)update();else x("xnote").textContent="Couldn’t load export options. Try again.";
   x("xretry").hidden=false;return;
  }finally{loadingMeta=false}
  x("xa").max=x("xb").max=M.today;
  if(initial){
   for(const k in M.datasets)mets[k]=new Set(M.datasets[k].metrics);
   x("xb").value=M.today;renderMods();renderDs();
  }else update();
 }
 x("xretry").onclick=load;
 ["xrg","xa","xb","xgr"].forEach(i=>x(i).addEventListener("change",()=>{x("xpcard").hidden=true;update()}));
 x("xds").onchange=()=>{renderMods();renderDs()};x("xq").oninput=renderMods;x("xpv").onclick=preview;
 x("xall").onclick=()=>{if(!M)return;shown().forEach(m=>chosenIds().add(m.id));renderMods();update()};
 x("xnone").onclick=()=>{chosenIds().clear();renderMods();update()};
 x("xgo").onclick=e=>{if(x("xgo").classList.contains("dis"))e.preventDefault()};
 const chk=()=>{if(document.body.dataset.view=="export")load()};addEventListener("hashchange",chk);addEventListener("dashboard-refreshed",chk);chk()})();
</script></body></html>"""
