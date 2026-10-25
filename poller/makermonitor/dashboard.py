"""LAN dashboard served by the poller at / (and /dashboard).

One static page with five views (Overview, Models, Activity, Health,
Settings). It reads /api/dashboard (the state the color panels get, plus
health and best-time insights) and /v2/creator every 60 s, /v2/model for
per-model sparklines, and /api/settings* for the Settings view. No external
assets, light and dark themes.
"""

_BASE_CSS = r"""
:root{color-scheme:light dark;
--bg:#eef1f5;--panel:#ffffff;--panel2:#f6f8fb;--line:#dbe1e9;--ink:#0f1722;--ink2:#465466;--ink3:#7a8797;
--acc:#0f8fb8;--acc-ink:#ffffff;--ok:#0e9f6e;--warn:#c27c0e;--err:#d6364a;
--b:#d98a00;--l:#e0457b;--c:#2f80ed;--d:#109e6a;--p:#7b5cf0;--f:#0f8fb8;
--shadow:0 1px 2px rgba(15,23,34,.06),0 4px 16px rgba(15,23,34,.04);--r:14px;
--mono:ui-monospace,"SF Mono","JetBrains Mono","Cascadia Mono",Menlo,Consolas,monospace;
--sans:ui-sans-serif,"Inter","Segoe UI Variable","Segoe UI",system-ui,-apple-system,Roboto,sans-serif;
--display:ui-rounded,"SF Pro Rounded","Nunito","Segoe UI Variable Display","Segoe UI",system-ui,sans-serif}
@media (prefers-color-scheme:dark){:root{
--bg:#0b1016;--panel:#121922;--panel2:#18212c;--line:#243040;--ink:#e8eef5;--ink2:#a9b6c5;--ink3:#6f7e90;
--acc:#38c6f0;--acc-ink:#04202b;--ok:#34d399;--warn:#f5b13d;--err:#ff6b7d;
--b:#ffb020;--l:#ff5c93;--c:#5aa2ff;--d:#34d399;--p:#a78bfa;--f:#38c6f0;
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
.hero{display:grid;grid-template-columns:repeat(5,1fr);gap:12px}
.stat{position:relative;padding:14px 14px 10px;border-radius:12px;background:var(--panel2);overflow:hidden;min-width:0}
.stat:before{content:"";position:absolute;left:0;top:0;bottom:0;width:4px;background:var(--k)}
.stat .lab{font-size:12px;font-weight:600;color:var(--ink2);display:flex;justify-content:space-between;gap:6px}
.stat .val{font:800 34px/1.1 var(--display);margin:6px 0 2px;color:var(--ink);font-variant-numeric:tabular-nums}
.stat .val.zero{color:var(--ink3)}
.stat .cmp{font-size:12px;color:var(--ink3)}
.stat svg{display:block;width:100%;height:28px;margin-top:6px}
.herofoot{display:flex;gap:18px;flex-wrap:wrap;margin-top:14px;color:var(--ink2);font-size:13px}
.herofoot b{font-family:var(--mono);color:var(--ink)}
@media (max-width:760px){.hero{grid-template-columns:repeat(2,1fr)}.stat .val{font-size:28px}.hero .stat:last-child{grid-column:span 2}}
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
/* ---- settings ---- */
.set{display:grid;grid-template-columns:220px 1fr;gap:24px;align-items:start}
.setnav{position:sticky;top:120px;display:flex;flex-direction:column;gap:2px}
.setnav a{padding:8px 12px;border-radius:8px;text-decoration:none;color:var(--ink2);font-weight:600}
.setnav a:hover{background:var(--panel)}
@media (max-width:900px){.set{grid-template-columns:1fr}.setnav{display:none}}
.form{display:flex;flex-direction:column;gap:16px}
.form .card{scroll-margin-top:120px}
.desc{margin:-8px 0 16px;color:var(--ink3);font-size:13px}
.fields{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:14px 16px}
.fld{display:flex;flex-direction:column;gap:5px;min-width:0}.fld.wide{grid-column:1/-1}
.fld>span{font-size:12.5px;font-weight:600;color:var(--ink2)}
.fld small{color:var(--ink3);font-size:12px}
.fld .err{color:var(--err);font-size:12px;font-weight:600}
input[type=text],input[type=number],input[type=password],select,textarea{width:100%;padding:8px 10px;border-radius:9px;border:1px solid var(--line);
 background:var(--panel2);outline:none;min-width:0}
textarea{min-height:70px;resize:vertical;font-family:var(--mono);font-size:12.5px}
input:focus,select:focus,textarea:focus{border-color:var(--acc);box-shadow:0 0 0 3px color-mix(in srgb,var(--acc) 20%,transparent)}
.bad input,.bad select,.bad textarea{border-color:var(--err)}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.btn{display:inline-flex;align-items:center;gap:6px;padding:8px 14px;border-radius:9px;border:1px solid var(--line);background:var(--panel);
 font-weight:600;cursor:pointer;white-space:nowrap}
.btn:hover{border-color:var(--ink3)}.btn:disabled{opacity:.55;cursor:default}
.btn.pri{background:var(--acc);border-color:var(--acc);color:var(--acc-ink)}.btn.pri:hover{filter:brightness(1.07)}
.btn.ghost{background:none;border-color:transparent;color:var(--ink2)}.btn.ghost:hover{background:var(--panel2)}
.btn.dan{color:var(--err)}.btn.sm{padding:5px 10px;font-size:12.5px}
.chips{display:flex;flex-wrap:wrap;gap:6px}
.chip{padding:5px 11px;border-radius:999px;border:1px solid var(--line);background:var(--panel2);cursor:pointer;font-size:12.5px;font-weight:600;color:var(--ink2)}
.chip.on{background:var(--acc);border-color:var(--acc);color:var(--acc-ink)}
.hosts{display:flex;flex-direction:column;gap:8px}
.host{display:grid;grid-template-columns:1fr 90px auto auto;gap:8px;align-items:center}
@media (max-width:560px){.host{grid-template-columns:1fr auto auto}.host .port{display:none}}
.res{font-size:12.5px}.res.ok{color:var(--ok)}.res.no{color:var(--err)}
.ch{border:1px solid var(--line);border-radius:12px;background:var(--panel2);margin-bottom:12px}
.ch>header{display:flex;align-items:center;gap:10px;padding:12px 14px;cursor:pointer}
.ch>header b{font-weight:700}.ch>header .ty{font:600 11px var(--mono);text-transform:uppercase;color:var(--ink3);letter-spacing:.5px}
.ch>header .sp{flex:1}.ch .body{padding:4px 14px 16px;border-top:1px solid var(--line);display:none}.ch.open .body{display:block}
.ch.off>header b{color:var(--ink3);text-decoration:line-through}
.sw{position:relative;width:38px;height:22px;flex:none}.sw input{opacity:0;width:0;height:0;position:absolute}
.sw i{position:absolute;inset:0;border-radius:999px;background:var(--line);transition:.15s}
.sw i:after{content:"";position:absolute;width:16px;height:16px;left:3px;top:3px;border-radius:50%;background:#fff;transition:.15s}
.sw input:checked+i{background:var(--ok)}.sw input:checked+i:after{transform:translateX(16px)}
.sw input:focus-visible+i{outline:2px solid var(--acc);outline-offset:2px}
.sub{font:700 12px var(--sans);text-transform:uppercase;letter-spacing:.5px;color:var(--ink3);margin:18px 0 10px}
.savebar{position:fixed;left:50%;bottom:18px;transform:translate(-50%,140%);z-index:30;display:flex;align-items:center;gap:12px;
 padding:10px 12px 10px 18px;border-radius:14px;background:var(--ink);color:var(--bg);box-shadow:0 10px 30px rgba(0,0,0,.25);transition:transform .2s}
.savebar.show{transform:translate(-50%,0)}.savebar .btn.ghost{color:var(--bg)}.savebar .btn.ghost:hover{background:rgba(127,127,127,.2)}
@media (prefers-reduced-motion:reduce){.savebar{transition:none}}
.toast{position:fixed;right:18px;bottom:18px;z-index:40;padding:12px 16px;border-radius:12px;background:var(--panel);border:1px solid var(--line);
 box-shadow:var(--shadow);font-weight:600;display:none;max-width:min(420px,calc(100vw - 36px))}
.toast.show{display:block}.toast.ok{border-color:var(--ok)}.toast.err{border-color:var(--err);color:var(--err)}
.cookie{display:flex;gap:14px;align-items:flex-start;flex-wrap:wrap}
.badge{display:inline-flex;align-items:center;gap:6px;padding:3px 10px;border-radius:999px;font-weight:700;font-size:12.5px}
.badge.ok{background:color-mix(in srgb,var(--ok) 16%,transparent);color:var(--ok)}
.badge.err{background:color-mix(in srgb,var(--err) 14%,transparent);color:var(--err)}
.badge.mut{background:var(--panel2);color:var(--ink3)}
@media (max-width:760px){main{padding:14px}.bar{padding:10px 14px 0}.card{padding:14px}nav.tabs{padding:4px 6px 0;gap:0}
 nav.tabs a{padding:8px 10px 10px}#ydaynote{display:none}.acct{display:none}}
</style></head><body>
<div class=top>
 <div class=bar><div class=brand><span class=mark><i></i></span>MakerMonitor</div><span class=acct id=acct></span>
  <span class=pill id=pill><span class=dot></span><span id=pilltx>Loading</span></span></div>
 <nav class=tabs id=tabs>
  <a href="#overview" data-v=overview>Overview</a><a href="#models" data-v=models>Models</a>
  <a href="#activity" data-v=activity>Activity<span class=ct id=ctact hidden></span></a>
  <a href="#health" data-v=health>Health<span class=ct id=cthl hidden></span></a><a href="#settings" data-v=settings>Settings</a></nav>
</div>
<main>
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
</div></section>

<section class=view id=v-settings><div class=set>
 <nav class=setnav><a href="#settings/mw">MakerWorld</a><a href="#settings/poll">Polling</a><a href="#settings/panels">Panels</a>
  <a href="#settings/notify">Notifications</a><a href="#settings/adv">Advanced</a></nav>
 <div class=form id=form><div class=card><span class=mut>Loading settings</span></div></div>
</div></section>
</main>
<div class=savebar id=savebar><span id=savetx>Unsaved changes</span>
 <button class="btn ghost sm" id=discard>Discard</button><button class="btn pri sm" id=save>Save changes</button></div>
<div class=toast id=toast></div>
<script>
"use strict";
const K=[["downloads","Downloads","d"],["prints","Prints","p"],["boosts","Boosts","b"],["likes","Likes","l"],["collects","Collects","c"]];
const KC={boosts:"b",likes:"l",collects:"c",downloads:"d",prints:"p",comment:"f"};
const WD=["Mon","Tue","Wed","Thu","Fri","Sat","Sun"];
const EV={health:"Health alerts",comment:"Comments",follower:"Followers",download:"Downloads",print:"Prints",boost:"Boosts",like:"Likes",collect:"Collects",points:"Points"};
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
function route(){const h=(location.hash||"#overview").slice(1),[v,sub]=h.split("/");const view=$("v-"+v)?v:"overview";
 document.querySelectorAll("section.view").forEach(s=>s.classList.toggle("on",s.id=="v-"+view));
 document.querySelectorAll("#tabs a").forEach(a=>a.classList.toggle("on",a.dataset.v==view));
 if(view=="settings"){if(!F)loadSettings();if(sub)setTimeout(()=>{const el=$("sec-"+sub);el&&el.scrollIntoView({behavior:"smooth"})},30)}
 if(view=="models")loadSparks()}
addEventListener("hashchange",route);

/* ---------- header, banner ---------- */
function renderHead(){const h=S.health||{},st=S.status||{};
 $("acct").textContent=S.account?"@"+S.account:"";
 const bad=h.auth=="expired",stale=h.stale,pill=$("pill");
 pill.className="pill "+(bad||st.ok===false?"err":stale||h.auth=="none"?"warn":"ok");
 $("pilltx").textContent=bad?"Cookie expired":stale?"Data stale":st.ok===false?"Last poll failed":"Updated "+ago(st.lastPoll);
 const bn=$("banner");
 if(bad){bn.className="banner err";bn.innerHTML=`MakerWorld rejected the cookie. Open makerworld.com so the extension refreshes it, or <a href="#settings/mw">paste one in Settings</a>.`}
 else if(stale){bn.className="banner warn";bn.textContent=`MakerWorld data hasn't updated since ${hm(h.since)}. Health has the details.`}
 else if(h.auth=="none"){bn.className="banner warn";bn.innerHTML=`No MakerWorld cookie yet, so only public stats show. <a href="#settings/mw">Add one in Settings</a>.`}
 else bn.className="banner";
 const fails=Object.values(h.src||{}).filter(x=>x.f).length;const ct=$("cthl");ct.hidden=!fails;ct.textContent=fails;
 const nf=(S.feed||[]).length;$("ctact").hidden=!nf;$("ctact").textContent=nf}

/* ---------- overview ---------- */
function renderOverview(){const t=S.today||{},y=S.yday||{},hr=S.hours||{};
 $("since").textContent=S.midnight?"since midnight":"since "+hm(S.since||0);
 $("ydaynote").textContent=y.date?"compared with all of yesterday":"";
 $("hero").innerHTML=K.map(([k,l,c])=>{const v=t[k]||0,yv=y[k];const hv=hr[k]||[];
  return `<div class=stat style="--k:var(--${c})"><div class=lab><span>${l}</span></div>
   <div class="val${v?"":" zero"}">${v?sg(v):"0"}</div><div class=cmp>${yv!=null?"yesterday "+fmt(yv):"&nbsp;"}</div>
   ${hv.length>1?spark(hv,c,120,28):"<svg></svg>"}</div>`}).join("");
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
  [`${a.fol7??"–"} / ${a.fol30??"–"}`,"followers from models, 7 / 30 days"],[`<a href="#activity" style="text-decoration:none">${a.unans??"–"}</a>`,"unanswered comments"]]
  .map(([v,l])=>`<div><b>${v}</b><span>${l}</span></div>`).join("")}

/* ---------- models ---------- */
const MCOLS=[["n","Model"],["sp","30 days"],["d30","Downloads"],["v30","Views"],["dpv","DL / view"],["p30","Prints"],["l30","Likes"],["c30","Collects"],["pt30","Points"],["mom","Trend"],["un","Unanswered"]];
function mrows(){return Object.values(C&&C.models||{}).map(r=>{const w=r.has?(mView=="7"?r.w||{}:r.m||{}):{};
 return {id:r.id,n:r.n,has:r.has,d30:w.d,v30:w.v,dpv:mView=="7"?r.dpv7:r.dpv30,p30:w.p,l30:w.l,c30:w.c,pt30:w.pt,
  mom:r.mom?(r.mom.dir=="up"?1:r.mom.dir=="down"?-1:0)+(r.mom.rising?1:0)-(r.mom.stalled?2:0):null,momr:r.mom,un:r.unans||0}})}
function renderModels(){if(!C){$("mtable").innerHTML=`<p class=mut>Waiting for creator stats</p>`;return}
 seg($("mseg"),[["7","7 days"],["30","30 days"]],mView,v=>mView=v);
 const rows=mrows().sort((a,b)=>{const x=a[mSort],y=b[mSort];if(mSort=="n")return mDir*String(x).localeCompare(String(y));return mDir*(((x??-1e9))-((y??-1e9)))});
 const nh=rows.filter(r=>!r.has).length;$("mcount").textContent=rows.length+" models"+(nh?`, ${nh} still waiting for MakerWorld analytics`:"");
 const head=MCOLS.map(([k,l])=>k=="sp"?`<th>${l}</th>`:`<th class="sort${mSort==k?" on":""}" data-k="${k}">${l}${mSort==k?(mDir<0?" ↓":" ↑"):""}</th>`).join("");
 const trend=m=>!m?"<span class=mut>–</span>":m.stalled?`<span class=down>stalled</span>`:m.rising?`<span class=up>rising fast</span>`:m.dir=="up"?`<span class=up>up</span>`:m.dir=="down"?`<span class=down>down</span>`:"<span class=mut>flat</span>";
 const nv=v=>v==null?"<span class=mut>–</span>":fmt(Math.round(v));
 $("mtable").innerHTML=`<table><thead><tr>${head}</tr></thead><tbody>`+(rows.map(r=>`<tr><td class=nm>${mwLink(r.id,r.n)}</td><td>${spark(SP[r.id],"d")}</td>
  <td>${nv(r.d30)}</td><td>${nv(r.v30)}</td><td>${pc(r.dpv)}</td><td>${nv(r.p30)}</td><td>${nv(r.l30)}</td><td>${nv(r.c30)}</td><td>${r.pt30==null?"<span class=mut>–</span>":nf(r.pt30)}</td>
  <td>${trend(r.momr)}</td><td>${r.un?`<b style="color:var(--warn)">${r.un}</b>`:"<span class=mut>0</span>"}</td></tr>`).join("")||`<tr><td colspan=11 class=empty>No models yet</td></tr>`)+"</tbody></table>";
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
  <span class=m><span class=mut>${esc(e.k=="comment"?"comment":e.k)}</span> · ${esc(e.m)}</span><span class=t>${ago(e.t)}</span></div>`}).join("")
  :`<p class=empty>Nothing yet today. New downloads, prints, boosts, likes and comments show here as they happen.</p>`;
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
  .map(([k,v])=>`<dt>${k}</dt><dd>${v}</dd>`).join("")}

function renderAll(){if(!S)return;renderHead();renderOverview();renderActivity();renderHealth();renderModels()}
async function load(){try{const c=await fetch("/v2/creator");if(c.ok)C=await c.json()}catch(e){}
 try{const r=await fetch("/api/dashboard",{credentials:"same-origin"});if(r.ok){S=await r.json();renderAll();loadSparks()}
  else{$("pill").className="pill err";$("pilltx").textContent="Unauthorized: reopen with ?token="}}
 catch(e){$("pill").className="pill err";$("pilltx").textContent="Poller unreachable"}}

/* ---------- settings ---------- */
let F=null,F0="",KEEP="__keep__",ERR={},open=new Set();
const api=(u,b)=>fetch(u,b?{method:"POST",credentials:"same-origin",headers:{"Content-Type":"application/json","X-MM":"1"},body:JSON.stringify(b)}:{credentials:"same-origin"});
function toast(msg,kind="ok"){const t=$("toast");t.textContent=msg;t.className="toast show "+kind;clearTimeout(toast.h);toast.h=setTimeout(()=>t.className="toast",kind=="err"?7000:3500)}
async function loadSettings(){const r=await api("/api/settings");if(!r.ok){$("form").innerHTML=`<div class=card>Settings need the dashboard token. Reopen this page with ?token=</div>`;return}
 F=await r.json();KEEP=F.keep;F.notifications.channels.forEach(c=>{c._set={};Object.keys(c).forEach(k=>{if(c[k]===KEEP)c._set[k]=1})});
 F0=JSON.stringify(F);ERR={};renderSettings();loadCookie()}
function dirty(){const d=F&&JSON.stringify(F)!=F0;$("savebar").classList.toggle("show",!!d);return d}
function fld(path,label,{type="text",hint="",wide=false,val,ph="",opts=null,min,max}={}){
 const e=ERR[path];const v=val??"";let input;
 if(opts)input=`<select data-p="${path}">${opts.map(([k,l])=>`<option value="${esc(k)}"${String(k)==String(v)?" selected":""}>${esc(l)}</option>`).join("")}</select>`;
 else if(type=="area")input=`<textarea data-p="${path}" placeholder="${esc(ph)}">${esc(v)}</textarea>`;
 else input=`<input type="${type}" data-p="${path}" value="${esc(v)}" placeholder="${esc(ph)}"${min!=null?` min=${min}`:""}${max!=null?` max=${max}`:""} autocomplete=off spellcheck=false>`;
 return `<label class="fld${wide?" wide":""}${e?" bad":""}"><span>${label}</span>${input}${e?`<span class=err>${esc(e)}</span>`:hint?`<small>${hint}</small>`:""}</label>`}
function evChips(path,list,all=Object.keys(EV)){const on=new Set(list||[]);
 return `<div class=chips data-ev="${path}">${all.map(k=>`<button type=button class="chip${on.has(k)?" on":""}" data-k="${k}" aria-pressed="${on.has(k)}">${EV[k]||k}</button>`).join("")}</div>`}
function renderSettings(){const t=F.top,n=F.notifications,d=n.defaults,eff=F.effective.poll_interval_sec,ro=F.readonly;
 const hosts=String(t.color_panel_host||"").split(",").map(s=>s.trim()).filter(Boolean);
 const dg=d.quiet_digest===undefined?true:d.quiet_digest;
 $("form").innerHTML=`
 <div class=card id=sec-mw><h2 class=h2>MakerWorld</h2><p class=desc>Your account and the browser cookie that unlocks creator stats.</p>
  <div class=fields>${fld("top.handle","Handle",{val:t.handle,hint:"Your MakerWorld username, without @"})}</div>
  <div class=sub>Cookie</div><div id=cookiebox class=mut>Checking</div>
  <label class="fld wide" style="margin-top:12px"><span>Paste a cookie</span><textarea id=cookiein placeholder="token=...; or the whole Cookie header from your browser's developer tools"></textarea>
  <small>Normally the Chrome extension sends this automatically. Pasting replaces the saved cookie and starts a poll.</small></label>
  <div class=row style="margin-top:10px"><button class="btn sm" id=cookiebtn type=button>Save cookie</button></div></div>

 <div class=card id=sec-poll><h2 class=h2>Polling</h2><p class=desc>How often MakerWorld is checked and how long each panel page stays up.</p>
  <div class=fields>${fld("top.poll_interval_sec","Poll every (seconds)",{type:"number",val:t.poll_interval_sec,min:60,max:3600,
   hint:eff!=t.poll_interval_sec?`Running at ${eff} s right now, set from a panel. Saving a new value here takes over.`:"60 to 3600. About 120 MakerWorld requests an hour are shared by everything."})}
  ${fld("top.page_cycle_sec","Panel page time (seconds)",{type:"number",val:t.page_cycle_sec,min:10,hint:"How long each e-paper page stays before it flips"})}</div></div>

 <div class=card id=sec-panels><h2 class=h2>Panels</h2><p class=desc>Displays the poller pushes stats to. Leave empty for none.</p>
  <div class=sub>Color panels</div><div class=hosts id=hosts>${hosts.map((h,i)=>hostRow(h,i)).join("")}</div>
  ${ERR["top.color_panel_host"]?`<div class=err style="color:var(--err)">${esc(ERR["top.color_panel_host"])}</div>`:""}
  <div class=row style="margin-top:10px"><button class="btn sm" id=addhost type=button>Add color panel</button>
   <span class=mut style="font-size:12.5px">The .local name (makermonitor-xxxxx.local) survives IP changes. Use host:port for a non-standard port.</span></div>
  <div class=fields style="margin-top:14px">${fld("top.color_panel_port","Default color panel port",{type:"number",val:t.color_panel_port})}</div>
  <div class=sub>E-paper panel</div>
  <div class=host><input type=text data-p="top.panel_host" value="${esc(t.panel_host)}" placeholder="192.168.1.50 (empty = none)" autocomplete=off>
   <input class=port type=number data-p="top.panel_port" value="${esc(t.panel_port)}"><button class="btn sm" type=button data-test=eink>Check</button><span class=res id=res-eink></span></div></div>

 <div class=card id=sec-notify><h2 class=h2>Notifications</h2><p class=desc>Push messages to your phone or chat when things happen. Each poll sends at most one message per channel.</p>
  <div class=sub>Defaults for every channel</div>
  <div class=fld><span>Events</span>${evChips("defaults.events",d.events)}${ERR["defaults.events"]?`<span class=err>${esc(ERR["defaults.events"])}</span>`:""}</div>
  <div class=fields style="margin-top:14px">
   ${fld("defaults.quiet_hours","Quiet hours",{val:q12(d.quiet_hours),ph:"11:00 PM-7:00 AM",hint:"Nothing is sent then, except urgent events. Empty = always on."})}
   ${fld("defaults.quiet_digest","When quiet hours end",{val:String(dg),opts:[["true","Send one catch-up message"],["always","Always send a catch-up, even after a quiet night"],["false","Drop what happened"]]})}</div>
  <div class=fld style="margin-top:14px"><span>Urgent (ignore quiet hours)</span>${evChips("defaults.urgent",d.urgent||["health"])}</div>
  <div class=fld style="margin-top:14px"><span>Wait until a message adds up to</span><div class=fields style="grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:8px">
   ${F.schema.grouped.map(k=>fld("defaults.min_count."+k,EV[k],{type:"number",val:(d.min_count||{})[k]||"",ph:"1",min:1})).join("")}</div>
   <small>For example 5 downloads: smaller amounts are held and added to the next message.</small></div>
  <div class=sub style="margin-top:24px">Channels</div><div id=chans>${n.channels.map((c,j)=>chanCard(c,j)).join("")||`<p class=mut>No channels yet. Add one to start getting messages.</p>`}</div>
  <div class=row><select id=newtype>${Object.keys(F.schema.types).map(k=>`<option value=${k}>${TYPE_LABEL[k]||k}</option>`).join("")}</select>
   <button class="btn sm" id=addch type=button>Add channel</button></div></div>

 <div class=card id=sec-adv><h2 class=h2>Advanced</h2><p class=desc>Rarely needed.</p>
  <div class=fields>${fld("top.user_agent","Browser user agent",{val:t.user_agent,wide:true,hint:"Should roughly match the browser the cookie came from"})}</div>
  <div class=sub>Set in config.yaml only (restart to change)</div>
  <dl class=kv><dt>Listen</dt><dd>${esc(ro.listen_host)}:${esc(ro.listen_port)}</dd><dt>Data folder</dt><dd>${esc(ro.data_dir)}</dd><dt>Dashboard token</dt><dd>api_token</dd><dt>Config file</dt><dd>${esc(F.path)}</dd></dl>
  <p class=mut style="margin-bottom:0">Every save keeps the previous file as config.yaml.bak-&lt;time&gt; next to it (the last 10).</p></div>`;
 bindSettings();dirty()}
const TYPE_LABEL={discord:"Discord",pushbullet:"Pushbullet",pushover:"Pushover",ntfy:"ntfy",telegram:"Telegram",homeassistant:"Home Assistant",apprise:"Apprise (Slack, Matrix, Gotify...)",webhook:"Webhook"};
function hostRow(h,i){return `<div class=host><input type=text data-host=${i} value="${esc(h)}" placeholder="makermonitor-xxxxx.local" autocomplete=off>
 <span class=port></span><button class="btn sm" type=button data-test=color data-i=${i}>Check</button>
 <span class=row><span class=res id=res-color-${i}></span><button class="btn ghost sm dan" type=button data-rmhost=${i} aria-label="Remove">Remove</button></span></div>`}
function chanCard(c,j){const fields=F.schema.types[c.type]||[],P=`channels.${j}`,isOpen=open.has(j)||Object.keys(ERR).some(k=>k.startsWith(P+"."));
 const errs=["form","env","type"].map(k=>ERR[P+"."+k]).filter(Boolean);const usesDef=c.events==null;
 return `<div class="ch${isOpen?" open":""}${c.enabled===false?" off":""}" data-j=${j}><header data-tog=${j}>
  <label class=sw onclick="event.stopPropagation()"><input type=checkbox data-en=${j}${c.enabled===false?"":" checked"} aria-label="Enabled"><i></i></label>
  <b>${esc(c.name||TYPE_LABEL[c.type]||c.type)}</b><span class=ty>${esc(c.type)}</span><span class=sp></span>
  <span class=mut style="font-size:12.5px">${usesDef?"default events":(c.events||[]).length+" events"}${c.quiet_hours?" · quiet "+esc(q12(c.quiet_hours)):""}</span><span aria-hidden=true>${isOpen?"▴":"▾"}</span></header>
  <div class=body>${errs.length?`<p style="color:var(--err);font-weight:600">${errs.map(esc).join("<br>")}</p>`:""}
  <div class=fields style="margin-top:12px">${fld(P+".name","Name",{val:c.name||"",ph:TYPE_LABEL[c.type]||c.type,hint:"Shown in the log; must be unique"})}
   ${fields.map(f=>{const sec=f.secret&&c._set&&c._set[f.key],v=c[f.key];
    if(f.kind=="bool")return `<div class=fld><span>${f.label}</span><label class=row style="font-weight:500"><input type=checkbox data-p="${P}.${f.key}"${v?" checked":""}> ${esc(f.hint)}</label></div>`;
    const area=f.kind=="lines"||f.kind=="headers";
    return fld(P+"."+f.key,f.label+(f.required?"":" (optional)"),{type:area?"area":f.secret?"password":f.kind=="int"?"number":"text",
     val:sec&&v===KEEP?"":v??"",ph:sec?"Saved. Type to replace.":f.hint,hint:sec?"Saved and hidden. Leave empty to keep it.":f.hint,wide:area})}).join("")}
   ${fld(P+".quiet_hours","Quiet hours (optional)",{val:q12(c.quiet_hours),ph:q12(F.notifications.defaults.quiet_hours)||"11:00 PM-7:00 AM",hint:"Empty = the default above"})}</div>
  <div class=fld style="margin-top:14px"><span>Events</span><label class=row style="font-weight:500"><input type=checkbox data-usedef=${j}${usesDef?" checked":""}> Use the default events</label>
   ${usesDef?"":evChips(P+".events",c.events)}${ERR[P+".events"]?`<span class=err>${esc(ERR[P+".events"])}</span>`:""}</div>
  <div class=row style="margin-top:16px"><button class="btn sm" type=button data-testch=${j}>Send test message</button><span class=res id=res-ch-${j}></span>
   <span class=sp style="flex:1"></span><button class="btn ghost sm dan" type=button data-rmch=${j}>Remove channel</button></div></div></div>`}
function setPath(path,val){const p=path.split(".");let o=p[0]=="top"?F.top:p[0]=="defaults"?F.notifications.defaults:F.notifications.channels;
 for(let i=1;i<p.length-1;i++){o[p[i]]=o[p[i]]??{};o=o[p[i]]}o[p[p.length-1]]=val}
function syncHosts(){F.top.color_panel_host=[...document.querySelectorAll("[data-host]")].map(i=>i.value.trim()).filter(Boolean).join(", ")}
function bindSettings(){const form=$("form");
 form.querySelectorAll("[data-p]").forEach(el=>{el.oninput=el.onchange=()=>{const p=el.dataset.p;let v=el.type=="checkbox"?el.checked:el.value;
  const m=p.match(/^channels\.(\d+)\.(\w+)$/);
  if(m){const c=F.notifications.channels[+m[1]];if(c._set&&c._set[m[2]]&&v==="")v=KEEP}
  if(p=="defaults.quiet_digest")v=v=="true"?true:v=="false"?false:"always";
  setPath(p,v);delete ERR[p];el.closest(".fld")&&el.closest(".fld").classList.remove("bad");dirty()}});
 form.querySelectorAll("[data-ev]").forEach(box=>box.querySelectorAll(".chip").forEach(b=>b.onclick=()=>{b.classList.toggle("on");b.setAttribute("aria-pressed",b.classList.contains("on"));
  setPath(box.dataset.ev,[...box.querySelectorAll(".chip.on")].map(x=>x.dataset.k));dirty()}));
 form.querySelectorAll("[data-host]").forEach(el=>el.oninput=()=>{syncHosts();dirty()});
 form.querySelectorAll("[data-rmhost]").forEach(b=>b.onclick=()=>{b.closest(".host").remove();syncHosts();renderSettings()});
 $("addhost").onclick=()=>{syncHosts();F.top.color_panel_host=(F.top.color_panel_host?F.top.color_panel_host+", ":"")+" ";renderSettings();
  const ins=document.querySelectorAll("[data-host]");ins[ins.length-1].focus()};
 form.querySelectorAll("[data-test]").forEach(b=>b.onclick=async()=>{const color=b.dataset.test=="color",res=$(color?"res-color-"+b.dataset.i:"res-eink");
  const host=color?document.querySelector(`[data-host="${b.dataset.i}"]`).value:F.top.panel_host;res.className="res";res.textContent="Checking";
  const r=await api("/api/settings/panel",{host,port:color?F.top.color_panel_port:F.top.panel_port,kind:color?"color":"eink"});const j=await r.json().catch(()=>({}));
  res.className="res "+(j.ok?"ok":"no");res.textContent=j.msg||"no answer"});
 form.querySelectorAll("[data-tog]").forEach(h=>h.onclick=()=>{const j=+h.dataset.tog;open.has(j)?open.delete(j):open.add(j);renderSettings()});
 form.querySelectorAll("[data-en]").forEach(el=>el.onchange=()=>{const c=F.notifications.channels[+el.dataset.en];if(el.checked)delete c.enabled;else c.enabled=false;renderSettings()});
 form.querySelectorAll("[data-usedef]").forEach(el=>el.onchange=()=>{const c=F.notifications.channels[+el.dataset.usedef];
  c.events=el.checked?null:[...(F.notifications.defaults.events||Object.keys(EV))];renderSettings()});
 form.querySelectorAll("[data-rmch]").forEach(b=>b.onclick=()=>{const j=+b.dataset.rmch,c=F.notifications.channels[j];
  if(!confirm(`Remove ${c.name||c.type}? It goes away when you save.`))return;F.notifications.channels.splice(j,1);open.clear();ERR={};renderSettings()});
 $("addch").onclick=()=>{const type=$("newtype").value;F.notifications.channels.push({type,events:null,_set:{}});
  open.add(F.notifications.channels.length-1);renderSettings()};
 form.querySelectorAll("[data-testch]").forEach(b=>b.onclick=async()=>{const j=+b.dataset.testch,res=$("res-ch-"+j);res.className="res";res.textContent="Sending";b.disabled=true;
  const r=await api("/api/settings/test",clean(F.notifications.channels[j]));const o=await r.json().catch(()=>({ok:false,errors:{"channel.form":"no answer"}}));b.disabled=false;
  res.className="res "+(o.ok?"ok":"no");res.textContent=o.ok?"Sent. Check the app or channel.":Object.values(o.errors||{}).join("; ")||"Failed"});
 $("cookiebtn").onclick=async()=>{const v=$("cookiein").value.trim();if(!v){toast("Paste a cookie first","err");return}
  const r=await api("/api/settings/cookie",{cookie:v});if(r.ok){$("cookiein").value="";const j=await r.json();renderCookie(j);toast(j.changed?"Cookie saved. Polling MakerWorld now.":"That cookie was already saved.")}
  else toast("Cookie not saved: "+((await r.json().catch(()=>({}))).detail||r.status),"err")}}
function clean(c){const o={};for(const[k,v]of Object.entries(c))if(k!="_set")o[k]=v;return o}
async function loadCookie(){const r=await api("/api/settings/cookie");if(r.ok)renderCookie(await r.json())}
function renderCookie(c){const a=c.auth,b=a=="ok"?`<span class="badge ok">Working</span>`:a=="expired"?`<span class="badge err">Rejected by MakerWorld</span>`:c.present?`<span class="badge mut">Not checked yet</span>`:`<span class="badge mut">Not set</span>`;
 $("cookiebox").innerHTML=`<div class=cookie>${b}<div><div class=ink2>${c.present?`Saved ${c.age!=null?ago(Date.now()/1e3-c.age):""} · ${c.names.length} cookies (${esc(c.names.slice(0,6).join(", "))}${c.names.length>6?", ...":""})`:"No cookie saved. Only public stats are polled."}</div>
  ${c.lastError&&!c.lastOk?`<div style="color:var(--err);font-size:12.5px;margin-top:4px">${esc(c.lastError)}</div>`:""}</div></div>`}
async function save(){const btn=$("save");btn.disabled=true;$("savetx").textContent="Saving";
 const body={rev:F.rev,top:F.top,notifications:{defaults:F.notifications.defaults,channels:F.notifications.channels.map(clean)}};
 let r,j;try{r=await api("/api/settings",body);j=await r.json()}catch(e){j={ok:false,detail:"poller unreachable"}}
 btn.disabled=false;$("savetx").textContent="Unsaved changes";
 if(r&&r.status==409){toast(j.detail+".","err");return}
 if(j.ok){toast(j.changed?"Saved and applied. Backup: "+j.backup:"No changes to save.");open.clear();await loadSettings();setTimeout(load,1500);return}
 ERR=j.errors||{};if(j.detail)toast("Not saved: "+j.detail,"err");else toast("Not saved. Fix the highlighted fields.","err");
 F.notifications.channels.forEach((c,i)=>{if(Object.keys(ERR).some(k=>k.startsWith(`channels.${i}.`)))open.add(i)});renderSettings();
 const first=document.querySelector(".bad,.ch.open .err");first&&first.scrollIntoView({behavior:"smooth",block:"center"})}
$("save").onclick=save;$("discard").onclick=()=>{F=JSON.parse(F0);ERR={};open.clear();renderSettings()};
addEventListener("beforeunload",e=>{if(dirty()){e.preventDefault();e.returnValue=""}});

route();load();setInterval(load,60000);
</script></body></html>"""
