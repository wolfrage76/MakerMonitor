"""LAN dashboard served by the poller at / (and /dashboard).

One static page with five views (Overview, Models, Activity, Health,
Settings). It reads /api/dashboard (the state the color panels get, plus
health and best-time insights) and /v2/creator every 60 s, /v2/model for
per-model sparklines, and /api/settings* for the Settings view. No external
assets, light and dark themes.
"""

_BASE_CSS = r"""
:root{color-scheme:light dark;
--bg:#eef1f5;--panel:#ffffff;--panel2:#f6f8fb;--line:#d5dce5;--ink:#0f1722;--ink2:#465466;--ink3:#5f6c7d;
--acc:#0b7499;--acc-ink:#ffffff;--acc2:#38c6f0;--acc2-ink:#04202b;--ok:#07714d;--warn:#9a5f00;--err:#c42b40;
--b:#d98a00;--l:#e0457b;--c:#2f80ed;--d:#109e6a;--p:#7b5cf0;--f:#0f8fb8;--r:#c99a00;
--shadow:0 1px 2px rgba(15,23,34,.06),0 4px 16px rgba(15,23,34,.04);--r:14px;
--mono:ui-monospace,"SF Mono","JetBrains Mono","Cascadia Mono",Menlo,Consolas,monospace;
--sans:ui-sans-serif,"Inter","Segoe UI Variable","Segoe UI",system-ui,-apple-system,Roboto,sans-serif;
--display:ui-rounded,"SF Pro Rounded","Nunito","Segoe UI Variable Display","Segoe UI",system-ui,sans-serif}
@media (prefers-color-scheme:dark){:root{
--bg:#0b1016;--panel:#121922;--panel2:#18212c;--line:#2a3747;--ink:#e8eef5;--ink2:#a9b6c5;--ink3:#8695a8;
--acc:#38c6f0;--acc-ink:#04202b;--acc2:#0b7499;--acc2-ink:#ffffff;--ok:#34d399;--warn:#f5b13d;--err:#ff6b7d;
--b:#ffb020;--l:#ff5c93;--c:#5aa2ff;--d:#34d399;--p:#a78bfa;--f:#38c6f0;--r:#ffd23f;
--shadow:0 1px 2px rgba(0,0,0,.3)}}
*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 var(--sans)}
a{color:inherit}button,input,select,textarea{font:inherit;color:inherit}
:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
"""

LOGIN_HTML = """<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>MakerMonitor</title>
<style>""" + _BASE_CSS + """
main{min-height:100vh;display:grid;place-items:center;padding:16px}
.box{max-width:440px;background:var(--panel);border:1px solid var(--line);border-radius:var(--r);padding:28px;box-shadow:var(--shadow)}
h1{font:700 22px var(--display);margin:0 0 12px}code{font-family:var(--mono);color:var(--acc);word-break:break-all}
p{color:var(--ink2)}</style></head><body><main><div class=box><h1>MakerMonitor</h1>
<p>Open this page once with your poller token:</p><p><code>http://&lt;this-pc&gt;:8787/?token=&lt;api_token&gt;</code></p>
<p>The token is <code>api_token</code> in <code>poller/config.yaml</code>. This browser remembers it after that.</p>
</div></main></body></html>"""

DASHBOARD_HTML = r"""<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>MakerMonitor</title>
<style>""" + _BASE_CSS + r"""
/* ---- shell ---- */
.top{position:sticky;top:0;z-index:20;background:color-mix(in srgb,var(--bg) 88%,transparent);backdrop-filter:blur(10px);
 border-bottom:1px solid var(--line)}
.bar{max-width:1280px;margin:0 auto;display:flex;align-items:center;gap:14px;padding:12px 20px 0;flex-wrap:wrap}
.brand{display:flex;align-items:center;gap:10px;font:800 17px var(--display);letter-spacing:.2px}
.mark{width:26px;height:26px;border-radius:8px;display:grid;place-items:center;
 background:conic-gradient(var(--b) 0 20%,var(--l) 0 40%,var(--c) 0 60%,var(--d) 0 80%,var(--p) 0)}
.mark i{width:10px;height:10px;border-radius:50%;background:var(--bg)}
.acct{color:var(--ink3);font-weight:500}
.pill{margin-left:auto;display:inline-flex;align-items:center;gap:8px;padding:5px 12px;border-radius:999px;
 background:var(--panel);border:1px solid var(--line);color:var(--ink2);font-size:13px;white-space:nowrap}
.pill .dot{width:8px;height:8px;border-radius:50%;background:var(--ink3)}
.pill.ok .dot{background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 22%,transparent)}
.pill.warn .dot{background:var(--warn)}.pill.err .dot{background:var(--err)}
nav.tabs{max-width:1280px;margin:0 auto;display:flex;gap:4px;padding:8px 14px 0;overflow-x:auto;scrollbar-width:none}
nav.tabs::-webkit-scrollbar{display:none}
nav.tabs a{padding:9px 12px 11px;text-decoration:none;color:var(--ink2);font-weight:600;border-bottom:2px solid transparent;white-space:nowrap}
nav.tabs a:hover{color:var(--ink)}nav.tabs a.on{color:var(--ink);border-color:var(--acc)}
nav.tabs a .ct{margin-left:6px;font:600 11px var(--mono);padding:1px 6px;border-radius:999px;background:var(--panel2);color:var(--ink3)}
main{max-width:1280px;margin:0 auto;padding:20px}
section.view{display:none}section.view.on{display:block;animation:in .18s ease-out}
@keyframes in{from{opacity:.4;transform:translateY(3px)}to{opacity:1;transform:none}}
@media (prefers-reduced-motion:reduce){section.view.on{animation:none}}
.grid{display:grid;gap:16px;grid-template-columns:repeat(12,1fr)}
.s12{grid-column:span 12}.s8{grid-column:span 8}.s7{grid-column:span 7}.s6{grid-column:span 6}.s5{grid-column:span 5}.s4{grid-column:span 4}
@media (max-width:1000px){.s8,.s7,.s6,.s5,.s4{grid-column:span 12}}
.card{background:var(--panel);border:1px solid var(--line);border-radius:var(--r);padding:18px;box-shadow:var(--shadow);min-width:0}
.card>h2,.h2{display:flex;align-items:center;gap:10px;margin:0 0 14px;font:700 15px var(--display)}
.h2 small,.card>h2 small{font:500 12px var(--sans);color:var(--ink3)}
.card>h2 .sp{flex:1}
.mut{color:var(--ink3)}.ink2{color:var(--ink2)}.num{font-family:var(--mono);font-variant-numeric:tabular-nums}
.up{color:var(--ok)}.down{color:var(--err)}
.banner{display:none;margin-bottom:16px;padding:12px 16px;border-radius:var(--r);font-weight:600;border:1px solid}
.banner.err{display:block;color:var(--err);border-color:color-mix(in srgb,var(--err) 40%,transparent);background:color-mix(in srgb,var(--err) 8%,var(--panel))}
.banner.warn{display:block;color:var(--warn);border-color:color-mix(in srgb,var(--warn) 40%,transparent);background:color-mix(in srgb,var(--warn) 8%,var(--panel))}
.banner a{font-weight:700}
.seg{display:inline-flex;background:var(--panel2);border:1px solid var(--line);border-radius:10px;padding:2px}
.seg button{border:0;background:none;padding:4px 10px;border-radius:8px;color:var(--ink2);cursor:pointer;font-size:12.5px;font-weight:600}
.seg button.on{background:var(--panel);color:var(--ink);box-shadow:var(--shadow)}
/* ---- today hero: one tile per stat, each with its own filament color ---- */
.hero{display:grid;grid-template-columns:repeat(6,1fr);gap:12px}
@media (max-width:1100px){.hero{grid-template-columns:repeat(3,1fr)}}
.stat{position:relative;padding:14px 14px 10px;border-radius:12px;background:var(--panel2);overflow:hidden;min-width:0}
.stat:before{content:"";position:absolute;left:0;top:0;bottom:0;width:4px;background:var(--k)}
.stat .lab{font-size:12px;font-weight:600;color:var(--ink2);display:flex;justify-content:space-between;gap:6px}
.stat .val{font:800 34px/1.1 var(--display);margin:6px 0 2px;color:var(--ink);font-variant-numeric:tabular-nums}
.stat .val.zero{color:var(--ink3)}
.stat .cmp{font-size:12px;color:var(--ink3)}
.stat svg{display:block;width:100%;height:28px;margin-top:6px}
.stat svg.stars{width:120px;height:24px;margin-top:8px}
.herofoot{display:flex;gap:18px;flex-wrap:wrap;margin-top:14px;color:var(--ink2);font-size:13px}
.herofoot b{font-family:var(--mono);color:var(--ink)}
@media (max-width:760px){.hero{grid-template-columns:repeat(2,1fr)}.stat .val{font-size:28px}}
/* ---- tables ---- */
.tw{overflow-x:auto;margin:0 -4px}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{padding:8px 6px;text-align:right;white-space:nowrap}
th{font-weight:600;color:var(--ink3);font-size:11.5px;text-transform:uppercase;letter-spacing:.4px;border-bottom:1px solid var(--line)}
td{border-bottom:1px solid color-mix(in srgb,var(--line) 60%,transparent);font-family:var(--mono);font-variant-numeric:tabular-nums}
tr:last-child td{border-bottom:0}
th:first-child,td:first-child{text-align:left}
td.nm{font-family:var(--sans);max-width:320px;overflow:hidden;text-overflow:ellipsis;font-weight:500}
td.nm a{text-decoration:none}td.nm a:hover{text-decoration:underline;color:var(--acc)}
th.sort{cursor:pointer;user-select:none}th.sort:hover{color:var(--ink)}th.sort.on{color:var(--acc)}
td svg.spark{width:90px;height:22px;vertical-align:middle}
.kdot{display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--k);margin-right:6px;vertical-align:1px}
.empty{padding:18px 4px;color:var(--ink3);text-align:center;font-family:var(--sans)}
/* ---- charts ---- */
.bars{display:flex;align-items:flex-end;gap:2px;height:96px}
.bars i{flex:1;background:color-mix(in srgb,var(--acc) 22%,var(--panel2));border-radius:3px 3px 0 0;min-height:2px}
.bars i.hi{background:var(--acc)}.bars i.neg{background:var(--err)}
.axis{display:flex;justify-content:space-between;color:var(--ink3);font-size:11px;margin-top:6px;font-family:var(--mono)}
.wk{display:grid;grid-template-columns:repeat(7,1fr);gap:6px;margin-top:14px}
.wk div{background:var(--panel2);border-radius:8px;padding:8px 0;text-align:center;font-size:11.5px;color:var(--ink3)}
.wk div.hi{background:color-mix(in srgb,var(--acc) 16%,var(--panel2));color:var(--acc)}
.wk b{display:block;font:700 15px var(--mono);color:var(--ink)}
.minis{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-bottom:14px}
.mini{background:var(--panel2);border-radius:10px;padding:10px 12px}
.mini b{display:block;font:800 22px var(--display);font-variant-numeric:tabular-nums}.mini span{font-size:12px;color:var(--ink3)}
@media (max-width:760px){.minis{grid-template-columns:repeat(2,1fr)}}
.kp{display:grid;grid-template-columns:repeat(auto-fill,minmax(170px,1fr));gap:10px}
.kp div{background:var(--panel2);border-radius:10px;padding:12px}
.kp b{display:block;font:800 20px var(--display);font-variant-numeric:tabular-nums}.kp span{font-size:12px;color:var(--ink3)}
.lists{display:grid;grid-template-columns:repeat(auto-fit,minmax(340px,1fr));gap:16px}
.lc{display:flex;align-items:flex-end;gap:8px;height:110px}.lc div{flex:1;display:flex;gap:3px;align-items:flex-end;height:100%}
.lc i{flex:1;border-radius:3px 3px 0 0;min-height:2px}
.tail{display:flex;height:10px;border-radius:999px;overflow:hidden;background:var(--panel2);margin:6px 0 12px}
.legend{display:flex;gap:14px;font-size:12px;color:var(--ink3);flex-wrap:wrap}.legend i{display:inline-block;width:10px;height:10px;border-radius:3px;margin-right:5px;vertical-align:-1px}
/* ---- feed ---- */
.feed{display:flex;flex-direction:column}
.ev{display:grid;grid-template-columns:58px 1fr auto;gap:10px;align-items:center;padding:9px 0;border-bottom:1px solid color-mix(in srgb,var(--line) 60%,transparent)}
.ev:last-child{border:0}
.tag{font:700 12px var(--mono);padding:3px 0;border-radius:6px;text-align:center;color:var(--k);background:color-mix(in srgb,var(--k) 14%,transparent)}
.ev .m{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.ev .t{color:var(--ink3);font-size:12px;font-family:var(--mono)}
.cm{padding:12px 0;border-bottom:1px solid color-mix(in srgb,var(--line) 60%,transparent)}.cm:last-child{border:0}
.cm .who{font-weight:600}.cm .on{color:var(--ink3);font-size:12px}.cm p{margin:4px 0 0;color:var(--ink2)}
/* ---- health ---- */
.src{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px}
.src>div{background:var(--panel2);border-radius:12px;padding:14px;border-left:4px solid var(--ok)}
.src>div.warn{border-color:var(--warn)}.src>div.err{border-color:var(--err)}.src>div.idle{border-color:var(--ink3)}
.src b{display:block;font-weight:700;margin-bottom:4px}.src .e{margin-top:6px;font:12px var(--mono);color:var(--err);word-break:break-word}
.meter{height:8px;border-radius:999px;background:var(--panel2);overflow:hidden;margin:8px 0}.meter i{display:block;height:100%;background:var(--acc)}
dl.kv{display:grid;grid-template-columns:max-content 1fr;gap:6px 16px;margin:0}dl.kv dt{color:var(--ink3)}dl.kv dd{margin:0;font-family:var(--mono);word-break:break-all}
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
.btn.sm{min-height:32px;padding:5px 11px;font-size:13px}
.btn svg{flex:none}
.lnk{border:0;background:none;padding:0;color:var(--ink);font-weight:600;text-decoration:underline;cursor:pointer}
.badge{display:inline-flex;align-items:center;gap:6px;padding:3px 10px;border-radius:999px;font-weight:700;font-size:12.5px;white-space:nowrap}
.badge .bd{width:7px;height:7px;border-radius:50%;background:currentColor}
.badge.ok{background:color-mix(in srgb,var(--ok) 13%,var(--panel));color:var(--ok)}
.badge.err{background:color-mix(in srgb,var(--err) 11%,var(--panel));color:var(--err)}
.badge.warn{background:color-mix(in srgb,var(--warn) 13%,var(--panel));color:var(--warn)}
.badge.mut{background:var(--panel2);color:var(--ink2);box-shadow:inset 0 0 0 1px var(--line)}
input[type=text],input[type=number],input[type=password],select,textarea{width:100%;min-height:40px;padding:8px 11px;border-radius:9px;
 border:1px solid var(--line);background:var(--panel2);min-width:0;outline:none}
select{cursor:pointer}
textarea{min-height:76px;resize:vertical;font-family:var(--mono);font-size:12.5px;line-height:1.5}
textarea.mask{-webkit-text-security:disc}
input:focus,select:focus,textarea:focus{border-color:var(--acc);box-shadow:0 0 0 3px color-mix(in srgb,var(--acc) 24%,transparent)}
[aria-invalid=true]{border-color:var(--err)!important}
[aria-invalid=true]:focus{box-shadow:0 0 0 3px color-mix(in srgb,var(--err) 22%,transparent)}
input[type=checkbox]{width:18px;height:18px;accent-color:var(--acc);margin:0;flex:none}
.sw{position:relative;width:44px;height:26px;flex:none;display:inline-block}.sw input{position:absolute;inset:0;opacity:0;width:100%;height:100%;margin:0;cursor:pointer;z-index:1}
.sw i{position:absolute;inset:0;border-radius:999px;background:color-mix(in srgb,var(--ink3) 45%,var(--line));transition:background .15s}
.sw i:after{content:"";position:absolute;width:20px;height:20px;left:3px;top:3px;border-radius:50%;background:#fff;box-shadow:0 1px 2px rgba(0,0,0,.25);transition:transform .15s}
.sw input:checked+i{background:var(--ok)}.sw input:checked+i:after{transform:translateX(18px)}
.sw input:focus-visible+i{outline:2px solid var(--acc);outline-offset:2px}
@media (prefers-reduced-motion:reduce){.sw i,.sw i:after{transition:none}}
.chips{display:flex;flex-wrap:wrap;gap:6px}
.chip{display:inline-flex;align-items:center;gap:5px;min-height:32px;padding:5px 12px;border-radius:999px;border:1px solid var(--line);background:var(--panel2);
 cursor:pointer;font-size:13px;font-weight:600;color:var(--ink2)}
.chip:hover{border-color:var(--ink3);color:var(--ink)}
.chip.on{background:color-mix(in srgb,var(--acc) 12%,var(--panel));border-color:var(--acc);color:var(--ink)}
.chip svg{color:var(--acc)}
.chips.bad .chip{border-color:color-mix(in srgb,var(--err) 50%,var(--line))}
.res{font-size:13px;font-weight:600}.res.ok{color:var(--ok)}.res.no{color:var(--err)}
.skel{height:12px;border-radius:6px;margin:14px 0;background:linear-gradient(90deg,var(--panel2) 25%,var(--line) 50%,var(--panel2) 75%);
 background-size:200% 100%;animation:sk 1.4s linear infinite}
@keyframes sk{to{background-position:-200% 0}}
@media (prefers-reduced-motion:reduce){.skel{animation:none}}
/* ---- settings ---- */
.set{display:grid;grid-template-columns:188px minmax(0,1fr);gap:28px;align-items:start;max-width:1080px;margin:0 auto}
.setnav{position:sticky;top:calc(var(--toph,104px) + 20px);display:flex;flex-direction:column;gap:2px}
.setnav a{display:flex;align-items:center;gap:8px;padding:9px 12px;border-radius:9px;text-decoration:none;color:var(--ink2);font-weight:600}
.setnav a:hover{background:var(--panel);color:var(--ink)}
.setnav a[aria-current]{background:var(--panel);color:var(--ink);box-shadow:inset 3px 0 0 var(--acc),var(--shadow)}
.setnav .dd{margin-left:auto;width:8px;height:8px;border-radius:50%;flex:none}
.setnav a.dirty .dd{background:var(--warn)}.setnav a.bad .dd{background:var(--err)}
.form{display:flex;flex-direction:column;gap:20px;padding-bottom:110px}
.sec{scroll-margin-top:calc(var(--toph,104px) + var(--navh,0px) + 16px);padding:22px 24px 6px}
.sec>header{display:flex;gap:12px;align-items:flex-start;padding-bottom:16px}
.sec>header h2{margin:0;font:700 18px var(--display);outline:none}
.sec>header p{margin:4px 0 0;color:var(--ink3);font-size:13px;max-width:64ch}
.sec>header .sp{flex:1}
.sec.state{padding:22px 24px}.sec.state h2{margin:0 0 6px;font:700 18px var(--display)}.sec.state p{color:var(--ink2);margin:0 0 14px}
.srow{display:grid;grid-template-columns:minmax(0,5fr) minmax(0,7fr);gap:10px 32px;padding:18px 0;border-top:1px solid var(--line)}
.srow.full{grid-template-columns:minmax(0,1fr)}
.lab>label,.lt{display:block;font-weight:650;color:var(--ink)}
.lab p{margin:3px 0 0;color:var(--ink3);font-size:12.5px;max-width:52ch}
.ctl{min-width:0;display:flex;flex-direction:column;gap:8px;align-items:stretch}
.help{color:var(--ink3);font-size:12.5px;margin:0}
.help:empty,.ferr:empty{display:none}
.ferr{color:var(--err);font-size:12.5px;font-weight:600;margin:0}
.narrow{max-width:260px}
.affix{display:flex;align-items:stretch;border:1px solid var(--line);border-radius:9px;background:var(--panel2);overflow:hidden}
.affix:focus-within{border-color:var(--acc);box-shadow:0 0 0 3px color-mix(in srgb,var(--acc) 24%,transparent)}
.affix:has([aria-invalid=true]){border-color:var(--err)}
.affix input{border:0!important;border-radius:0;background:none;box-shadow:none!important}
.affix span{display:grid;place-items:center;padding:0 11px;color:var(--ink2);font-size:13px;background:var(--panel)}
.affix .pre{border-right:1px solid var(--line)}.affix .suf{border-left:1px solid var(--line)}
.unitrow{display:flex;gap:12px;align-items:center;flex-wrap:wrap}.unitrow .affix{flex:0 1 220px}
.fields{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:16px}
.fld{display:flex;flex-direction:column;gap:6px;min-width:0}.fld.wide{grid-column:1/-1}
.fld>label,.fld>.lt{font-size:13px;font-weight:600;color:var(--ink)}
.opt{font-weight:500;color:var(--ink3)}
.chk{display:inline-flex;align-items:center;gap:8px;font-weight:500;cursor:pointer;min-height:32px}
.mini-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(104px,1fr));gap:12px}
.mini-grid label{font-size:12.5px;font-weight:600;color:var(--ink2)}
.qh{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.qh select{width:auto;min-width:128px}.qh label{font-size:13px;color:var(--ink2);font-weight:600}
.swrow{display:flex;align-items:center;gap:10px;min-height:32px}.swrow span{color:var(--ink2);font-weight:600}
.cookie{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
details.disc{border:1px solid var(--line);border-radius:10px;background:var(--panel2)}
details.disc>summary{cursor:pointer;padding:9px 12px;font-weight:600;color:var(--ink2);list-style-position:inside}
details.disc>summary:hover{color:var(--ink)}
details.disc[open]>summary{border-bottom:1px solid var(--line)}
details.disc>div{padding:12px;display:flex;flex-direction:column;gap:8px}
details.disc textarea{background:var(--panel)}
.empty-st{border:1px dashed var(--line);border-radius:12px;padding:18px;text-align:center;color:var(--ink3)}
.empty-st b{display:block;color:var(--ink2);margin-bottom:2px}.empty-st p{margin:0;font-size:13px}
.cnt{font-size:12.5px;color:var(--ink3);white-space:nowrap;padding-top:4px}
.plist,.chans{display:flex;flex-direction:column;gap:10px}
.pnl{border:1px solid var(--line);border-radius:12px;background:var(--panel2);padding:12px}
.pnl .pt{display:flex;gap:8px;align-items:center}.pnl .pt input{flex:1;background:var(--panel)}
.pnl .pm{display:flex;gap:6px 12px;align-items:center;flex-wrap:wrap;margin-top:10px;font-size:13px;color:var(--ink2)}
.pnl .ferr{margin-top:8px}
@media (max-width:560px){.pnl .pt{flex-wrap:wrap}.pnl .pt input{flex:1 1 100%}}
.ch{border:1px solid var(--line);border-radius:12px;background:var(--panel2)}
.ch.bad{border-color:color-mix(in srgb,var(--err) 55%,var(--line))}
.chh{display:flex;align-items:center;gap:10px;padding:8px 10px 8px 14px}
.chx{flex:1;min-width:0;display:flex;align-items:center;gap:6px 10px;flex-wrap:wrap;border:0;background:none;padding:8px 6px;text-align:left;cursor:pointer;border-radius:8px;color:var(--ink)}
.chx:hover{background:color-mix(in srgb,var(--ink) 4%,transparent)}
.chx .nm{font-weight:700;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.chx .ty{font:600 11px var(--mono);text-transform:uppercase;letter-spacing:.5px;color:var(--ink3)}
.chx .sum{margin-left:auto;color:var(--ink3);font-size:12.5px}
.chx .chev{flex:none;color:var(--ink3);transition:transform .15s}.chx[aria-expanded=true] .chev{transform:rotate(180deg)}
.ch.off .nm{color:var(--ink3)}
.chb{padding:16px;border-top:1px solid var(--line)}
.chb input,.chb select,.chb textarea{background:var(--panel)}
.chb>.ferr{margin:0 0 12px}
.chf{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:18px;padding-top:14px;border-top:1px solid var(--line)}.chf .sp{flex:1}
.pw{display:flex;gap:6px;align-items:flex-start}.pw input,.pw textarea{flex:1}
.secret{display:flex;gap:8px;align-items:center;flex-wrap:wrap;min-height:40px}
.addch{margin-top:12px}.addch select{width:auto;min-width:200px}
.esum{border:1px solid color-mix(in srgb,var(--err) 45%,var(--line));background:color-mix(in srgb,var(--err) 6%,var(--panel));border-radius:var(--r);padding:14px 18px;outline:none}
.esum:focus-visible{box-shadow:0 0 0 3px color-mix(in srgb,var(--err) 30%,transparent)}
.esum h2{margin:0 0 6px;font:700 15px var(--display);color:var(--err)}.esum ul{margin:0;padding-left:20px}.esum li{margin:3px 0}
@media (max-width:900px){.set{grid-template-columns:minmax(0,1fr);gap:12px}
 .setnav{top:var(--toph,96px);z-index:15;flex-direction:row;gap:6px;overflow-x:auto;scrollbar-width:none;margin:-8px -14px 0;padding:10px 14px;
  background:color-mix(in srgb,var(--bg) 90%,transparent);backdrop-filter:blur(10px)}
 .setnav::-webkit-scrollbar{display:none}
 .setnav a{flex:none;padding:6px 13px;border:1px solid var(--line);border-radius:999px;background:var(--panel);font-size:13px}
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
.toast{position:fixed;right:18px;bottom:18px;z-index:40;padding:12px 16px;border-radius:12px;background:var(--panel);border:1px solid var(--line);border-left-width:4px;
 box-shadow:0 10px 30px rgba(0,0,0,.18);font-weight:600;display:none;max-width:min(440px,calc(100vw - 24px))}
.toast.show{display:block}.toast.ok{border-left-color:var(--ok)}.toast.err{border-left-color:var(--err);color:var(--err)}
body[data-view=settings].dirty .toast{bottom:84px}
@media (max-width:560px){.toast{left:12px;right:12px;max-width:none}}
dialog.dlg{border:1px solid var(--line);border-radius:16px;background:var(--panel);color:var(--ink);padding:22px;width:min(420px,calc(100vw - 32px));
 box-shadow:0 20px 50px rgba(0,0,0,.3)}
dialog.dlg::backdrop{background:rgba(8,12,18,.5)}
dialog.dlg h2{margin:0 0 8px;font:700 17px var(--display)}dialog.dlg p{margin:0 0 20px;color:var(--ink2)}
dialog.dlg .row{justify-content:flex-end}
@media (max-width:760px){main{padding:14px}.bar{padding:10px 14px 0}.card{padding:14px}nav.tabs{padding:4px 6px 0;gap:0}
 nav.tabs a{padding:8px 10px 10px}#ydaynote{display:none}.acct{display:none}}
</style></head><body>
<a class=skip href="#main">Skip to content</a>
<div class=top>
 <div class=bar><div class=brand><span class=mark><i></i></span>MakerMonitor</div><span class=acct id=acct></span>
  <span class=pill id=pill><span class=dot></span><span id=pilltx>Loading</span></span></div>
 <nav class=tabs id=tabs aria-label="Dashboard">
  <a href="#overview" data-v=overview>Overview</a><a href="#models" data-v=models>Models</a>
  <a href="#activity" data-v=activity>Activity<span class=ct id=ctact hidden></span></a>
  <a href="#health" data-v=health>Health<span class=ct id=cthl hidden></span></a><a href="#settings" data-v=settings id=tab-settings>Settings<span class=ct id=ctset hidden title="Unsaved changes">Unsaved</span></a></nav>
</div>
<main id=main tabindex=-1>
<div class=banner id=banner></div>

<section class=view id=v-overview><div class=grid>
 <div class="card s12"><h2>Today<small id=since></small><span class=sp></span><small id=ydaynote></small></h2>
  <div class=hero id=hero></div><div class=herofoot id=herofoot></div></div>
 <div class="card s7"><h2>Top models<span class=sp></span><span class=seg id=topseg></span></h2><div class=tw id=top></div></div>
 <div class="card s5"><h2>Followers<span class=sp></span><small>last 30 days</small></h2><div class=minis id=fol></div>
  <div class=bars id=folbars style="height:70px"></div><div class=axis><span>30 days ago</span><span>today</span></div></div>
 <div class="card s5"><h2>7 and 30 days</h2><div class=tw id=periods></div></div>
 <div class="card s7"><h2>Best times<span class=sp></span><span class=seg id=bestseg></span></h2><div id=best></div></div>
 <div class="card s12"><h2>Creator at a glance<span class=sp></span><small id=cprog></small></h2><div class=kp id=ckp></div></div>
</div></section>

<section class=view id=v-models><div class=grid>
 <div class="card s12"><h2>All models<small id=mcount></small><span class=sp></span><span class=seg id=mseg></span></h2>
  <div class=tw id=mtable></div></div>
 <div class="s12 lists" id=cgrid></div>
</div></section>

<section class=view id=v-activity><div class=grid>
 <div class="card s7"><h2>Today's activity<span class=sp></span><small>newest first</small></h2><div class=feed id=feed></div></div>
 <div class="card s5"><h2>Recent comments<span class=sp></span><small id=unans></small></h2><div id=comments></div></div>
</div></section>

<section class=view id=v-health><div class=grid>
 <div class="card s12"><h2>MakerWorld data sources</h2><div class=src id=srcs></div></div>
 <div class="card s6"><h2>Request budget<span class=sp></span><small>shared by every MakerWorld request</small></h2><div id=limiter></div></div>
 <div class="card s6"><h2>Poller</h2><dl class=kv id=pinfo></dl></div>
 <div class="card s12"><h2>Panels<span class=sp></span><small><a href="#settings/panels">Manage</a></small></h2><div class=src id=pnls></div></div>
</div></section>

<section class=view id=v-settings aria-labelledby=tab-settings><div class=set>
 <nav class=setnav id=setnav aria-label="Settings sections"><a href="#settings/mw" data-s=mw>Account<span class=dd aria-hidden=true></span><span class=sr></span></a><a href="#settings/panels" data-s=panels>Panels<span class=dd aria-hidden=true></span><span class=sr></span></a><a href="#settings/notify" data-s=notify>Notifications<span class=dd aria-hidden=true></span><span class=sr></span></a><a href="#settings/quiet" data-s=quiet>Quiet hours<span class=dd aria-hidden=true></span><span class=sr></span></a><a href="#settings/poll" data-s=poll>Polling<span class=dd aria-hidden=true></span><span class=sr></span></a><a href="#settings/adv" data-s=adv>Advanced<span class=dd aria-hidden=true></span><span class=sr></span></a></nav>
 <div class=form id=form><div class="card sec" aria-busy="true"><div class=skel style="width:28%;height:18px"></div><div class=skel></div><div class=skel style="width:72%"></div><div class=skel style="width:55%"></div></div></div>
</div></section>
</main>
<div class=savebar id=savebar role=region aria-label="Unsaved changes"><span class=msg><i aria-hidden=true></i><span id=savetx role=status></span></span>
 <button class="btn ghost sm" type=button id=discard>Discard</button><button class="btn pri sm" type=button id=save title="Save changes (Ctrl+S)">Save changes</button></div>
<div class=toast id=toast role=status aria-live=polite></div>
<div class=sr id=live aria-live=polite></div>
<dialog class=dlg id=dlg aria-labelledby=dlgt aria-describedby=dlgm><form method=dialog><h2 id=dlgt></h2><p id=dlgm></p>
 <div class=row><button class=btn value=cancel autofocus>Cancel</button><button class="btn danger" value=ok id=dlgok></button></div></form></dialog>
<script>
"use strict";
const K=[["downloads","Downloads","d"],["prints","Prints","p"],["boosts","Boosts","b"],["likes","Likes","l"],["collects","Collects","c"]];
const KC={boosts:"b",likes:"l",collects:"c",downloads:"d",prints:"p",comment:"f",ratings:"r"};
const WD=["Mon","Tue","Wed","Thu","Fri","Sat","Sun"];
const EV={health:"Health alerts",comment:"Comments",follower:"Followers",download:"Downloads",print:"Prints",boost:"Boosts",like:"Likes",collect:"Collects",rating:"Ratings",points:"Points"};
let S=null,C=null,topMode="today",bestM="downloads",mSort="d30",mDir=-1,mView="30";
const SP={};  // model id -> [daily downloads] for sparklines
const $=id=>document.getElementById(id),fmt=n=>(+n||0).toLocaleString(),sg=n=>(n>0?"+":"")+fmt(n);
const esc=s=>String(s??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const ago=t=>{if(!t)return"never";const s=Date.now()/1e3-t;return s<45?"just now":s<3600?Math.round(s/60)+" min ago":s<86400?Math.round(s/3600)+" h ago":Math.round(s/86400)+" d ago"};
const hm=t=>new Date(t*1e3).toLocaleTimeString("en-US",{hour:"numeric",minute:"2-digit",hour12:true});
const hh=i=>(i%12||12)+":00 "+(i<12?"AM":"PM");
const q12=q=>q?String(q).split("-").map(x=>{const[h,m]=x.trim().split(":").map(Number);return isNaN(h)?x:(h%12||12)+":"+String(m||0).padStart(2,"0")+" "+(h<12?"AM":"PM")}).join("-"):"";
const pc=(v,d=1)=>v==null?"–":(100*v).toFixed(d)+"%",nf=(v,d=1)=>v==null?"–":(+v).toFixed(d);
const mwLink=(id,n)=>id?`<a href="https://makerworld.com/en/models/${encodeURIComponent(id)}" target=_blank rel=noopener title="${esc(n)}">${esc(n)}</a>`:`<span title="${esc(n)}">${esc(n)}</span>`;
function seg(el,items,cur,set){el.innerHTML=items.map(([k,l])=>`<button class="${k==cur?"on":""}" data-k="${k}">${l}</button>`).join("");
 el.querySelectorAll("button").forEach(b=>b.onclick=()=>{set(b.dataset.k);renderAll()})}
function spark(vals,color,w=90,h=22,fill=true){if(!vals||!vals.length)return"";const mx=Math.max(1,...vals),n=vals.length,st=n>1?w/(n-1):0;
 const pts=vals.map((v,i)=>`${(i*st).toFixed(1)},${(h-2-(v/mx)*(h-4)).toFixed(1)}`).join(" ");
 return `<svg class=spark viewBox="0 0 ${w} ${h}" preserveAspectRatio=none aria-hidden=true>${fill?`<polygon points="0,${h} ${pts} ${w},${h}" fill="var(--${color})" opacity=.14 />`:""}<polyline points="${pts}" fill=none stroke="var(--${color})" stroke-width=1.6 stroke-linejoin=round vector-effect=non-scaling-stroke /></svg>`}
function table(head,rows,empty="Nothing yet"){return `<table><thead><tr>${head.map(h=>`<th>${h}</th>`).join("")}</tr></thead><tbody>`+
 (rows.length?rows.join(""):`<tr><td colspan=${head.length} class=empty>${empty}</td></tr>`)+"</tbody></table>"}

/* ---------- routing ---------- */
function tabScroll(){const tb=$("tabs"),ta=tb.querySelector(".on");if(ta&&tb.scrollWidth>tb.clientWidth)tb.scrollLeft=ta.offsetLeft-tb.clientWidth+ta.offsetWidth+24}
function route(){const h=(location.hash||"#overview").slice(1),[v,sub]=h.split("/");const view=$("v-"+v)?v:"overview";document.body.dataset.view=view;
 document.querySelectorAll("section.view").forEach(s=>s.classList.toggle("on",s.id=="v-"+view));
 document.querySelectorAll("#tabs a").forEach(a=>{const on=a.dataset.v==view;a.classList.toggle("on",on);on?a.setAttribute("aria-current","page"):a.removeAttribute("aria-current")});
 tabScroll();
 if(view=="settings"){setTop();if(!F)loadSettings().then(()=>sub&&goSec(sub));else if(sub)setTimeout(()=>goSec(sub),30);spy()}
 if(view=="models")loadSparks()}
addEventListener("hashchange",route);

/* ---------- header, banner ---------- */
function renderHead(){const h=S.health||{},st=S.status||{};
 $("acct").textContent=S.account?"@"+S.account:"";
 const bad=h.auth=="expired",stale=h.stale,dead=h.loopStalled,pill=$("pill");
 const first=!st.lastPoll;  // just started: nothing has been polled yet, so auth and ok aren't known
 pill.className="pill "+(dead?"err":first?"":bad||st.ok===false?"err":stale||h.auth=="none"?"warn":"ok");
 $("pilltx").textContent=dead?"Not polling":first?"Starting, first poll soon":bad?"Cookie expired":stale?"Data stale":st.ok===false?"Last poll failed":"Updated "+ago(st.lastPoll);
 const bn=$("banner");
 if(dead){bn.className="banner err";bn.textContent=`The poller stopped checking MakerWorld at ${hm(h.loopTick)}. Restart makermonitor-poller to get it going again.`}
 else if(first)bn.className="banner";
 else if(bad){bn.className="banner err";bn.innerHTML=`MakerWorld rejected the cookie. Open makerworld.com so the extension refreshes it, or <a href="#settings/mw">paste one in Settings</a>.`}
 else if(stale){bn.className="banner warn";bn.textContent=`MakerWorld data hasn't updated since ${hm(h.since)}. Health has the details.`}
 else if(h.auth=="none"){bn.className="banner warn";bn.innerHTML=`No MakerWorld cookie yet, so only public stats show. <a href="#settings/mw">Add one in Settings</a>.`}
 else bn.className="banner";
 const fails=Object.values(h.src||{}).filter(x=>x.f).length;const ct=$("cthl");ct.hidden=!fails;ct.textContent=fails;
 const nf=(S.feed||[]).length;$("ctact").hidden=!nf;$("ctact").textContent=nf;tabScroll()}

/* ---------- overview ---------- */
// Ratings have no hourly buckets, so the tile shows the average as stars instead of a sparkline.
function ratingTile(t,y,tot){const v=t.ratings||0,a=+tot.ratingAvg||0,pct=Math.max(0,Math.min(100,a/5*100));
 return `<div class=stat style="--k:var(--r)"><div class=lab><span>Ratings</span><span title="average of ${fmt(tot.ratings)} ratings">${a?a.toFixed(2)+"★":""}</span></div>
  <div class="val${v?"":" zero"}">${v?sg(v):"0"}</div><div class=cmp>${y.ratings!=null?"yesterday "+fmt(y.ratings):fmt(tot.ratings||0)+" total"}</div>
  <svg class=stars viewBox="0 0 120 24" role=img aria-label="${a?a.toFixed(1)+" out of 5 stars":"no ratings yet"}"><defs><linearGradient id=rg>
   <stop offset="${pct}%" stop-color="var(--r)"/><stop offset="${pct}%" stop-color="color-mix(in srgb,var(--r) 22%,var(--panel2))"/></linearGradient></defs>
   <path fill="url(#rg)" d="${[0,1,2,3,4].map(i=>STAR(i*24)).join("")}"/></svg></div>`}
const STAR=x=>{let d="";for(let i=0;i<10;i++){const r=i%2?4.6:10.5,t=-Math.PI/2+i*Math.PI/5;d+=(i?"L":"M")+(x+12+r*Math.cos(t)).toFixed(1)+" "+(12.6+r*Math.sin(t)).toFixed(1)}return d+"Z"};
function renderOverview(){const t=S.today||{},y=S.yday||{},hr=S.hours||{};
 $("since").textContent=S.midnight?"since midnight":"since "+hm(S.since||0);
 $("ydaynote").textContent=y.date?"compared with all of yesterday":"";
 $("hero").innerHTML=K.map(([k,l,c])=>{const v=t[k]||0,yv=y[k];const hv=hr[k]||[];
  return `<div class=stat style="--k:var(--${c})"><div class=lab><span>${l}</span></div>
   <div class="val${v?"":" zero"}">${v?sg(v):"0"}</div><div class=cmp>${yv!=null?"yesterday "+fmt(yv):"&nbsp;"}</div>
   ${hv.length>1?spark(hv,c,120,28):"<svg></svg>"}</div>`}).join("")+ratingTile(t,y,S.total||{});
 const tot=S.total||{};
 $("herofoot").innerHTML=[t.points!=null?`<span>Points today <b>${sg(+(+t.points).toFixed(1))}</b></span>`:"",
  `<span>Lifetime downloads <b>${fmt(tot.downloads)}</b></span>`,`<span>Prints <b>${fmt(tot.prints)}</b></span>`,
  `<span>Points <b>${fmt(Math.round(tot.points||0))}</b></span>`,`<span>Models <b>${fmt(tot.models)}</b></span>`].join("");
 seg($("topseg"),[["today","Today"],["week","7 days"],["life","Lifetime"]],topMode,v=>topMode=v);
 const rows=((S.top||{})[topMode]||[]);
 $("top").innerHTML=table(["Model","30 days",...K.map(([,l,c])=>`<span class=kdot style="--k:var(--${c})"></span>${l.slice(0,2)}`)],
  rows.map(r=>`<tr><td class=nm>${mwLink(r.id,r.n)}</td><td>${spark(SP[r.id],"d")}</td><td>${r.d}</td><td>${r.p}</td><td>${r.b}</td><td>${r.l}</td><td>${r.c}</td></tr>`),
  topMode=="today"?"No model activity yet today":"No activity yet");
 const fo=S.followers||{},fs=fo.series30||[],fm=Math.max(1,...fs.map(Math.abs));
 $("fol").innerHTML=[[fmt(fo.total),"total",""],[sg(fo.today||0),"today",fo.today],[sg(fo.d7||0),"7 days",fo.d7],[sg(fo.d30||0),"30 days",fo.d30]]
  .map(([v,l,n])=>`<div class=mini><b class="${n>0?"up":n<0?"down":""}">${v}</b><span>${l}</span></div>`).join("");
 $("folbars").innerHTML=fs.map((v,i)=>`<i class="${v<0?"neg":v>0?"hi":""}" style="height:${Math.max(3,Math.abs(v)/fm*100)}%" title="${fs.length-1-i?fs.length-1-i+" days ago":"today"}: ${sg(v)}"></i>`).join("");
 const d7={};(S.days||[]).forEach(d=>K.forEach(([k])=>d7[k]=(d7[k]||0)+(d[k]||0)));const m=S.month||{};
 const m30={boosts:m.boosts,likes:m.likes,collects:m.saves,downloads:m.downloads,prints:m.prints};
 $("periods").innerHTML=table(["","7 days","30 days","Lifetime"],K.map(([k,l,c])=>
  `<tr><td class=nm><span class=kdot style="--k:var(--${c})"></span>${l}</td><td>${sg(d7[k]||0)}</td><td>${m30[k]!=null?sg(m30[k]):"–"}</td><td>${fmt(tot[k])}</td></tr>`).concat(
  m.views!=null?[`<tr><td class=nm class=mut>Views</td><td>–</td><td>${fmt(m.views)}</td><td>–</td></tr>`]:[]));
 const ins=S.insights||{};seg($("bestseg"),[["downloads","Downloads"],["prints","Prints"],["boosts","Boosts"]],bestM,v=>bestM=v);
 const b=ins[bestM];
 if(!ins.has||!b){$("best").innerHTML=`<p class=mut>Best times show after a week of data. ${ins.days||0} of 7 days collected so far.</p>`}else{
  const mx=Math.max(1,...b.hour);
  $("best").innerHTML=`<div class=bars>${b.hour.map((v,i)=>`<i class="${i==b.bestHour?"hi":""}" style="height:${Math.max(3,v/mx*100)}%" title="${hh(i)}, ${v}"></i>`).join("")}</div>
  <div class=axis><span>12a</span><span>6a</span><span>12p</span><span>6p</span><span>12a</span></div>
  <div class=wk>${b.wday.map((v,i)=>`<div class="${i==b.bestDay?"hi":""}">${WD[i]}<b>${fmt(v)}</b></div>`).join("")}</div>
  <p class=mut style="margin:12px 0 0">Busiest hour ${hh(b.bestHour)}, busiest day ${WD[b.bestDay]||"–"}, over the last ${ins.days} days.</p>`}
 renderKpis()}
function renderKpis(){if(!C)return;const a=C.acct||{},p=C.progress||{};
 $("cprog").textContent=p.of?(p.have<p.of?`analytics for ${p.have} of ${p.of} models so far`:`analytics for all ${p.of} models`):"waiting for the first poll";
 const e=(a.e100||{}).m||{},tr=a.ptsTrend=="up"?"up":a.ptsTrend=="down"?"down":"";
 $("ckp").innerHTML=[[pc(a.dpv7),"downloads per view, 7 days"],[pc(a.dpv30),"downloads per view, 30 days"],[pc(a.ppd,0),"prints per download"],
  [nf(e.all),"likes, collects, comments per 100 downloads"],[`${nf(a.pts7)} / ${nf(a.pts30)}`,"model points, 7 / 30 days"],
  [`<span class="${tr}">${nf(a.pts7avg)}</span>`,`points a day (was ${nf(a.ptsPrev7avg)})`],
  [`${a.fol7??"–"} / ${a.fol30??"–"}`,"followers from models, 7 / 30 days"],[a.rt?`${nf(a.ra,2)}★`:"–",`average of ${fmt(a.rt||0)} ratings`],[`<a href="#activity" style="text-decoration:none">${a.unans??"–"}</a>`,"unanswered comments"]]
  .map(([v,l])=>`<div><b>${v}</b><span>${l}</span></div>`).join("")}

/* ---------- models ---------- */
const MCOLS=[["n","Model"],["sp","30 days"],["d30","Downloads"],["v30","Views"],["dpv","DL / view"],["p30","Prints"],["l30","Likes"],["c30","Collects"],["pt30","Points"],["rt","Ratings"],["mom","Trend"],["un","Unanswered"]];
function mrows(){return Object.values(C&&C.models||{}).map(r=>{const w=r.has?(mView=="7"?r.w||{}:r.m||{}):{};
 return {id:r.id,n:r.n,has:r.has,d30:w.d,v30:w.v,dpv:mView=="7"?r.dpv7:r.dpv30,p30:w.p,l30:w.l,c30:w.c,pt30:w.pt,
  rt:r.rt||0,ra:r.ra,mom:r.mom?(r.mom.dir=="up"?1:r.mom.dir=="down"?-1:0)+(r.mom.rising?1:0)-(r.mom.stalled?2:0):null,momr:r.mom,un:r.unans||0}})}
function renderModels(){if(!C){$("mtable").innerHTML=`<p class=mut>Waiting for creator stats</p>`;return}
 seg($("mseg"),[["7","7 days"],["30","30 days"]],mView,v=>mView=v);
 const rows=mrows().sort((a,b)=>{const x=a[mSort],y=b[mSort];if(mSort=="n")return mDir*String(x).localeCompare(String(y));return mDir*(((x??-1e9))-((y??-1e9)))});
 const nh=rows.filter(r=>!r.has).length;$("mcount").textContent=rows.length+" models"+(nh?`, ${nh} still waiting for MakerWorld analytics`:"");
 const head=MCOLS.map(([k,l])=>k=="sp"?`<th>${l}</th>`:`<th class="sort${mSort==k?" on":""}" data-k="${k}">${l}${mSort==k?(mDir<0?" ↓":" ↑"):""}</th>`).join("");
 const trend=m=>!m?"<span class=mut>–</span>":m.stalled?`<span class=down>stalled</span>`:m.rising?`<span class=up>rising fast</span>`:m.dir=="up"?`<span class=up>up</span>`:m.dir=="down"?`<span class=down>down</span>`:"<span class=mut>flat</span>";
 const nv=v=>v==null?"<span class=mut>–</span>":fmt(Math.round(v));
 $("mtable").innerHTML=`<table><thead><tr>${head}</tr></thead><tbody>`+(rows.map(r=>`<tr><td class=nm>${mwLink(r.id,r.n)}</td><td>${spark(SP[r.id],"d")}</td>
  <td>${nv(r.d30)}</td><td>${nv(r.v30)}</td><td>${pc(r.dpv)}</td><td>${nv(r.p30)}</td><td>${nv(r.l30)}</td><td>${nv(r.c30)}</td><td>${r.pt30==null?"<span class=mut>–</span>":nf(r.pt30)}</td>
  <td>${r.rt?`${fmt(r.rt)} <span class=mut>${nf(r.ra,1)}★</span>`:"<span class=mut>–</span>"}</td><td>${trend(r.momr)}</td><td>${r.un?`<b style="color:var(--warn)">${r.un}</b>`:"<span class=mut>0</span>"}</td></tr>`).join("")||`<tr><td colspan=12 class=empty>No models yet</td></tr>`)+"</tbody></table>";
 $("mtable").querySelectorAll("th.sort").forEach(th=>th.onclick=()=>{const k=th.dataset.k;if(mSort==k)mDir=-mDir;else{mSort=k;mDir=k=="n"?1:-1}renderModels()});
 renderInsights()}
function renderInsights(){const m=Object.values(C.models||{}),has=m.filter(r=>r.has),top=(arr,f,n=8)=>arr.sort(f).slice(0,n);
 const L=(id,n)=>`<td class=nm>${mwLink(id,n)}</td>`,R=cells=>`<tr>${cells}</tr>`;
 const conv=top(has.filter(r=>r.dpv30!=null&&r.m.v>=30),(x,y)=>y.dpv30-x.dpv30).map(r=>R(L(r.id,r.n)+`<td>${pc(r.dpv30)}</td><td>${fmt(r.m.v)}</td><td>${fmt(r.m.d)}</td><td>${pc(r.ppd,0)}</td>`));
 const eng=top(has.filter(r=>r.e100.m.all!=null&&r.m.d>=5),(x,y)=>y.e100.m.all-x.e100.m.all).map(r=>R(L(r.id,r.n)+`<td>${nf(r.e100.m.l)}</td><td>${nf(r.e100.m.c)}</td><td>${nf(r.e100.m.m)}</td><td>${nf(r.e100.life.all)}</td>`));
 const mom=has.filter(r=>r.mom);
 const rise=top(mom.filter(r=>r.mom.dir=="up"),(x,y)=>(y.mom.r||0)-(x.mom.r||0)).map(r=>R(L(r.id,r.n+(r.mom.rising?" · rising fast":""))+`<td>${r.mom.y}</td><td>${nf(r.mom.avg)}</td><td>×${nf(r.mom.r)}</td>`));
 const stall=top(mom.filter(r=>r.mom.stalled),(x,y)=>y.mom.avg-x.mom.avg).map(r=>R(L(r.id,r.n)+`<td>${nf(r.mom.avg)}</td>`));
 const pts=top(has.filter(r=>r.pts30>0),(x,y)=>y.pts30-x.pts30).map(r=>R(L(r.id,r.n)+`<td>${nf(r.pts7)}</td><td>${nf(r.pts30)}</td>`));
 const rated=top(m.filter(r=>r.rt>=3),(x,y)=>(y.ra-x.ra)||(y.rt-x.rt)).map(r=>R(L(r.id,r.n)+`<td>${nf(r.ra,2)}★</td><td>${fmt(r.rt)}</td>`));
 const fol=top(has.filter(r=>r.fol30>0),(x,y)=>y.fol30-x.fol30).map(r=>R(L(r.id,r.n)+`<td>${r.fol7}</td><td>${r.fol30}</td>`));
 const la=(C.acct||{}).launch;let lc="<p class=mut>Shows once a model has launch data.</p>";
 if(la){const mx=Math.max(1,...la.cur,...la.med);lc=`<p class=ink2 style="margin-top:0">${esc(la.n)}: day ${la.day} of 7, ${la.cur[la.day-1]} downloads`+
  (la.pct!=null?`, ahead of ${la.pct}% of ${la.past} earlier launches`:"")+`</p><div class=lc>${[0,1,2,3,4,5,6].map(i=>`<div title="day ${i+1}"><i style="background:var(--acc);height:${la.cur[i]!=null?Math.max(3,la.cur[i]/mx*100):0}%"></i><i style="background:var(--line);height:${la.med.length?Math.max(3,la.med[i]/mx*100):0}%"></i></div>`).join("")}</div>
  <div class=axis><span>day 1</span><span>day 7</span></div><div class=legend style="margin-top:10px"><span><i style="background:var(--acc)"></i>this launch</span><span><i style="background:var(--line)"></i>median launch</span></div>`}
 const t=(C.acct||{}).tail||{},tb=(l,v)=>{v=v||[0,0];const s=v[0]+v[1],o=s?Math.round(100*v[0]/s):0;return `<div class=ink2>${l}: <b>${o}%</b> from models older than 30 days <span class=mut>(${fmt(v[0])} of ${fmt(s)})</span></div><div class=tail><i style="width:${o}%;background:var(--c)"></i><i style="width:${100-o}%;background:var(--d)"></i></div>`};
 const card=(h,sub,b)=>`<div class=card><h2>${h}<span class=sp></span><small>${sub}</small></h2><div class=tw>${b}</div></div>`;
 $("cgrid").innerHTML=[
  card("Best converting","downloads per view, 30 days",table(["Model","DL/view","Views","DL","Prints/DL"],conv,"Needs models with 30+ views")),
  card("Most engaging","per 100 downloads, 30 days",table(["Model","Likes","Coll.","Comm.","Lifetime"],eng,"Needs models with 5+ downloads")),
  card("Rising","yesterday against the 7-day average",table(["Model","Yday","Avg","Ratio"],rise,"Nothing rising right now")),
  card("Stalled","averaged 1+ a day, none since yesterday",table(["Model","Was / day"],stall,"Nothing stalled")),
  card("Launch curve","first 7 days",lc),
  card("Points by model","MakerWorld attribution",table(["Model","7 days","30 days"],pts)),
  card("Best rated","3 or more ratings",table(["Model","Average","Ratings"],rated,"No model has 3 ratings yet")),
  card("Where followers come from","by model",table(["Model","7 days","30 days"],fol)),
  card("Long tail","older models' share of downloads",tb("Today",t.today)+tb("7 days",t.w)+tb("30 days",t.m)+`<div class=legend><span><i style="background:var(--c)"></i>older than 30 days</span><span><i style="background:var(--d)"></i>newer</span></div>`),
 ].join("")}
let sparkAt=0;
async function loadSparks(){if(!C||!S||Date.now()-sparkAt<6e5)return;sparkAt=Date.now();
 const ids=new Set([...Object.keys(C&&C.models||{}),...["today","week","life"].flatMap(k=>((S&&S.top||{})[k]||[]).map(r=>r.id))].filter(Boolean));
 for(const id of ids){try{const r=await fetch("/v2/model?full=1&id="+encodeURIComponent(id));if(!r.ok)continue;const j=await r.json();
  const mw=(j.stats||{}).mwDays||[],own=j.days||[];
  SP[id]=mw.length?mw.map(d=>d.dl||0):own.map(d=>d.downloads||0)}catch(e){}}
 renderAll()}

/* ---------- activity ---------- */
function renderActivity(){const f=S.feed||[];
 $("feed").innerHTML=f.length?f.map(e=>{const c=KC[e.k]||"ink2";return `<div class=ev style="--k:var(--${c})"><span class=tag>${e.k=="comment"?"new":"+"+e.n}</span>
  <span class=m><span class=mut>${esc(e.k=="comment"?"comment":e.k=="ratings"?(e.n==1?"rating":"ratings")+(e.s?" "+e.s+"★":""):e.k)}</span> · ${esc(e.m)}</span><span class=t>${ago(e.t)}</span></div>`}).join("")
  :`<p class=empty>Nothing yet today. New downloads, prints, boosts, likes, ratings and comments show here as they happen.</p>`;
 const cm=S.comments||[];const un=(C&&C.acct||{}).unans;$("unans").textContent=un!=null?un+" unanswered":"";
 $("comments").innerHTML=cm.length?cm.map(c=>`<div class=cm><span class=who>${esc(c.u)}</span> <span class=on>on ${esc(c.m)}${c.t?" · "+ago(Date.parse(c.t)/1e3):""}</span><p>${esc(c.x)}</p></div>`).join("")
  :`<p class=empty>No comments yet</p>`}

/* ---------- health ---------- */
function renderHealth(){const h=S.health||{},src=h.src||{},st=S.status||{};
 const NAMES={models:["Models and totals","every poll"],reach:["30-day reach","every poll"],points:["Points","every poll"],"model-stats":["Per-model analytics","background"]};
 $("srcs").innerHTML=Object.entries(NAMES).map(([k,[l,when]])=>{const x=src[k]||{};const cls=!x.t&&!x.f?"idle":x.f>=3?"err":x.f?"warn":"";
  return `<div class="${cls}"><b>${l}</b><div class=ink2>Last good: ${ago(x.t)} <span class=mut>(${when})</span></div>
   <div class=mut>${x.f?x.f+" failure"+(x.f>1?"s":"")+" in a row":"No failures"}</div>${x.e?`<div class=e>${esc(x.e)}</div>`:""}</div>`}).join("")+
  `<div class="${h.auth=="expired"?"err":h.auth=="ok"?"":"warn"}"><b>Cookie</b><div class=ink2>${{ok:"Accepted by MakerWorld",expired:"Rejected, needs a refresh",none:"Not set, public stats only"}[h.auth]||"Unknown"}</div>
   <div class=mut>${st.cookieAge>=0?"Sent "+ago(Date.now()/1e3-st.cookieAge):"Never sent"}</div><div style="margin-top:6px"><a href="#settings/mw">Manage</a></div></div>`;
 const lm=h.limiter||{},bo=Object.entries(lm.backoff_until||{}),used=lm.requests_last_hour||0,bud=lm.budget||0,bk=lm.by_kind||{};
 $("limiter").innerHTML=`<div class=row style="justify-content:space-between"><b class=num style="font-size:20px">${used} <span class=mut style="font-size:14px">of ${bud||"–"} this hour</span></b>${lm.paused?`<span class="badge err">paused</span>`:""}</div>
  <div class=meter><i style="width:${bud?Math.min(100,used/bud*100):0}%"></i></div>
  <div class=mut>${Object.entries(bk).map(([k,v])=>`${esc(k)} ${v}`).join(" · ")||""}</div>
  ${bo.length?`<p style="color:var(--warn)">Backing off: ${bo.map(([k,t])=>esc(k)+" until "+hm(t)).join(", ")}</p>`:`<p class=mut>No backoff in effect.</p>`}`;
 $("pinfo").innerHTML=[["Last poll",st.lastPoll?hm(st.lastPoll)+" ("+ago(st.lastPoll)+")":"never"],["Result",st.ok===false?esc(st.msg||"failed"):"ok"],
  ["Poll interval",(st.pollSec||"–")+" s"],["Panel URL",esc(S.poller||"–")],["Data as of",S.t?new Date(S.t*1e3).toLocaleDateString()+" "+hm(S.t):"–"]]
  .map(([k,v])=>`<dt>${k}</dt><dd>${v}</dd>`).join("")
 const pl=h.panels||[];
 $("pnls").innerHTML=pl.length?pl.map(p=>`<div class="${p.up===false?"err":p.type=="unknown"?"idle":""}"><b>${esc(p.host)}</b>
   <div class=ink2>${PTYPE[p.type]||"Not detected yet"}${p.board?" · "+esc(p.board):""}${p.fw?" · v"+esc(p.fw):""}</div>
   <div class=mut>${p.up===false?"Not answering":p.up?"Answering":"Not checked yet"}</div></div>`).join("")
  :`<p class=mut>No panels. Add one under <a href="#settings/panels">Settings</a>.</p>`}
const PTYPE={color:"Color panel",eink:"E-paper panel"};

function renderAll(){if(!S)return;renderHead();renderOverview();renderActivity();renderHealth();renderModels()}
async function load(){try{const c=await fetch("/v2/creator");if(c.ok)C=await c.json()}catch(e){}
 try{const r=await fetch("/api/dashboard",{credentials:"same-origin"});if(r.ok){S=await r.json();renderAll();loadSparks()}
  else{$("pill").className="pill err";$("pilltx").textContent="Unauthorized: reopen with ?token="}}
 catch(e){$("pill").className="pill err";$("pilltx").textContent="Poller unreachable"}}

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
 notify:f=>[f.notifications.defaults.events,f.notifications.defaults.min_count,f.notifications.channels],
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
 if(np)np.textContent=[dur(t.poll_interval_sec)&&"= "+dur(t.poll_interval_sec),eff!=F0.top.poll_interval_sec?`Running at ${dur(eff)} right now, set from a panel. Saving here takes over.`:""].filter(Boolean).join(". ");
 if(nc)nc.textContent=dur(t.page_cycle_sec)?"= "+dur(t.page_cycle_sec):"";
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
  `<span class=cnt>${np?np+(np==1?" panel":" panels"):""}</span>`)}
 ${sec("notify","Notifications","Messages to your phone or chat when things happen. Each poll sends at most one message per channel.",
  `<div class="srow full"><div class=lab><span class=lt>Channels</span><p>Switch a channel off to pause it and keep its settings.</p></div>
   <div class=chans>${n.channels.map(chanCard).join("")||`<div class=empty-st><b>No channels yet</b><p>Add one below to start getting messages.</p></div>`}</div>
   <div class="row addch"><label class=sr for=newtype>New channel type</label><select id=newtype>${Object.keys(F.schema.types).map(k=>`<option value=${k}>${TYPE_LABEL[k]||k}</option>`).join("")}</select>
    <button class="btn sm" id=addch type=button>${PLUS} Add channel</button></div></div>`+
  srow("defaults.events","Default events","Channels send these unless they pick their own.",evChips("defaults.events",d.events,pid("defaults.events")+"-l"),{forId:false})+
  srow("defaults.min_count","Hold small amounts","Wait until an event adds up to this many, then send it. Smaller amounts roll into the next message. Empty or 1 sends every time.",
   `<div class=mini-grid>${F.schema.grouped.map(k=>{const p="defaults.min_count."+k;FL[p]="Hold "+EV[k].toLowerCase();
    return `<div class=fld><label for=${pid(p)}>${EV[k]}</label>${inp(p,(d.min_count||{})[k]??"",{type:"number",ph:"1",min:1})}${errEl(p)}</div>`}).join("")}</div>`,{forId:false}))}
 ${sec("quiet","Quiet hours","Nothing is sent during quiet hours except urgent events. Each panel has its own quiet hours, set on the panel.",
  srow("defaults.quiet_hours","Quiet hours","Applies to every channel unless a channel sets its own.",
   `<div class=swrow><label class=sw><input type=checkbox role=switch id=qon${qon?" checked":""} aria-label="Quiet hours" aria-describedby=qon-t><i></i></label><span id=qon-t>${qon?"On":"Off. Messages go out any time."}</span></div>${qon?qPick("defaults.quiet_hours",d.quiet_hours):""}`,{forId:false})+
  srow("defaults.quiet_digest","When quiet hours end","A catch-up message lists what happened while it was quiet.",
   `<select ${ctlAttrs("defaults.quiet_digest")}>${[["true","Send a catch-up if anything happened"],["always","Always send a catch-up, even after a quiet night"],["false","Don't send a catch-up"]].map(([k,l])=>`<option value="${k}"${String(dg)==k?" selected":""}>${l}</option>`).join("")}</select>`)+
  srow("defaults.urgent","Urgent events","Sent right away, even during quiet hours.",evChips("defaults.urgent",d.urgent||["health"],pid("defaults.urgent")+"-l"),{forId:false}))}
 ${sec("poll","Polling","How often MakerWorld is checked, and how long e-paper pages stay up.",
  srow("top.poll_interval_sec","Check MakerWorld every","60 to 3600 seconds. Every MakerWorld request shares a budget of about 120 an hour.",
   `<div class=unitrow><div class=affix>${inp("top.poll_interval_sec",t.poll_interval_sec,{type:"number",min:60,max:3600})}<span class=suf aria-hidden=true>seconds</span></div><span class=help id=n-poll aria-live=polite></span></div>`)+
  srow("top.page_cycle_sec","E-paper page time","How long each e-paper page shows before it flips. At least 10 seconds.",
   `<div class=unitrow><div class=affix>${inp("top.page_cycle_sec",t.page_cycle_sec,{type:"number",min:10})}<span class=suf aria-hidden=true>seconds</span></div><span class=help id=n-cyc></span></div>`))}
 ${sec("adv","Advanced","Rarely needed.",
  srow("top.user_agent","Browser user agent","Should roughly match the browser the cookie came from.",inp("top.user_agent",t.user_agent))+
  `<div class=srow><div class=lab><span class=lt>Set in config.yaml only</span><p>Changing these needs a poller restart.</p></div><div class=ctl>
   <dl class=kv><dt>Listen</dt><dd>${esc(ro.listen_host)}:${esc(ro.listen_port)}</dd><dt>Data folder</dt><dd>${esc(ro.data_dir)}</dd><dt>Dashboard token</dt><dd>api_token</dd><dt>Config file</dt><dd>${esc(F.path)}</dd></dl>
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

setTop();route();load();setInterval(load,60000);
</script></body></html>"""
