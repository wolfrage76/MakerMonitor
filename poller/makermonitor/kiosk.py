"""Kiosk: a full-screen wall/TV view of the dashboard data.

GET /kiosk opens with the same api_token as the dashboard (?token=... once,
then the mm_token cookie). The page reads /api/dashboard and /v2/creator every
minute and works everything else out in the browser, so it adds no endpoint.
"""

from __future__ import annotations

from typing import Callable

from fastapi import Cookie, FastAPI
from fastapi.responses import HTMLResponse

from .dashboard import LOGIN_HTML


def register(app: FastAPI, settings, dash_ok: Callable[[str | None, str | None], bool]) -> None:
    @app.get("/kiosk", response_class=HTMLResponse)
    async def kiosk(token: str | None = None, mm_token: str | None = Cookie(default=None)) -> HTMLResponse:
        if not dash_ok(token, mm_token):
            return HTMLResponse(LOGIN_HTML, status_code=401)
        resp = HTMLResponse(KIOSK_HTML, headers={"Cache-Control": "no-store"})
        if token == settings.api_token:
            resp.set_cookie("mm_token", token, max_age=180 * 86400, httponly=True, samesite="strict")
        return resp


KIOSK_HTML = r"""<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>MakerMonitor Kiosk</title>
<style>
:root{
--bg:#05070a;--panel:#0b0f14;--panel2:#10161d;--line:#18212b;
--ink:#e6ecf2;--ink2:#9aa7b4;--ink3:#64717e;
--acc:#38c6f0;--acc-dim:#1b5f75;--ok:#34d399;--warn:#f5b13d;--err:#ff6b7d;
--sans:ui-sans-serif,"Inter","Segoe UI Variable","Segoe UI",system-ui,-apple-system,Roboto,sans-serif;
--display:ui-rounded,"SF Pro Rounded","Nunito","Segoe UI Variable Display","Segoe UI",system-ui,sans-serif;
/* One design unit = 1px of a 1920x1080 layout, scaled to fit any 16:9 screen. */
--u:min(calc((100vw - 24px) / 1920),calc((100vh - 24px) / 1080));
color-scheme:dark}
*{box-sizing:border-box}
html,body{margin:0;height:100%;overflow:hidden;background:var(--bg);color:var(--ink);font-family:var(--sans);
 -webkit-font-smoothing:antialiased;font-variant-numeric:tabular-nums}
body.idle,body.idle *{cursor:none!important}
.num{font-variant-numeric:tabular-nums;font-feature-settings:"tnum"}
#stage{position:absolute;left:50%;top:50%;width:calc(1920*var(--u));height:calc(1080*var(--u));
 transform:translate(calc(-50% + var(--sx,0px)),calc(-50% + var(--sy,0px)));transition:transform 4s ease-in-out;
 padding:calc(40*var(--u)) calc(48*var(--u)) calc(28*var(--u));display:grid;
 grid-template-rows:calc(56*var(--u)) 1fr calc(232*var(--u)) calc(36*var(--u));row-gap:calc(26*var(--u));font-size:calc(22*var(--u))}
h2{margin:0;font:600 calc(19*var(--u))/1 var(--sans);letter-spacing:.14em;text-transform:uppercase;color:var(--ink3);
 display:flex;align-items:baseline;gap:calc(14*var(--u))}
h2 .ct{letter-spacing:0;text-transform:none;font-weight:600;color:var(--ink2)}
/* header */
header{display:flex;align-items:center;justify-content:space-between}
.brand{display:flex;align-items:baseline;gap:calc(18*var(--u))}
.brand b{font:700 calc(34*var(--u))/1 var(--display);letter-spacing:-.01em}
.brand span{color:var(--ink3);font-size:calc(22*var(--u))}
.clock{font:600 calc(44*var(--u))/1 var(--display);color:var(--ink);letter-spacing:-.01em}
.clock small{font-size:.5em;color:var(--ink3);margin-left:.3em;font-weight:600;letter-spacing:.04em}
/* main */
main{display:grid;grid-template-columns:calc(600*var(--u)) calc(700*var(--u)) 1fr;column-gap:calc(40*var(--u));min-height:0}
section{min-height:0;display:flex;flex-direction:column;gap:calc(20*var(--u))}
.sep{border-left:1px solid var(--line);padding-left:calc(40*var(--u));margin-left:calc(-20*var(--u))}
/* attention */
#att{background:var(--panel);border:1px solid var(--line);border-radius:calc(18*var(--u));padding:calc(26*var(--u)) calc(28*var(--u));gap:calc(16*var(--u));
 position:relative;overflow:hidden}
#att::before{content:"";position:absolute;left:0;top:0;bottom:0;width:calc(5*var(--u));background:var(--st,var(--ink3))}
#att.alert{--st:var(--err)}#att.warn{--st:var(--warn)}#att.calm{--st:var(--ok)}
.alerts{display:flex;flex-direction:column;gap:calc(10*var(--u))}
.al{display:flex;gap:calc(14*var(--u));align-items:flex-start;padding:calc(14*var(--u)) calc(18*var(--u));border-radius:calc(12*var(--u));
 background:color-mix(in srgb,var(--c) 12%,transparent);color:var(--ink)}
.al i{flex:none;width:calc(12*var(--u));height:calc(12*var(--u));border-radius:50%;background:var(--c);margin-top:calc(9*var(--u))}
.al b{display:block;font-size:calc(26*var(--u));font-weight:650}
.al span{color:var(--ink2);font-size:calc(19*var(--u))}
.al.err{--c:var(--err)}.al.warn{--c:var(--warn)}
.unans{display:flex;align-items:baseline;gap:calc(16*var(--u))}
.unans .big{font:700 calc(76*var(--u))/.9 var(--display);color:var(--warn);letter-spacing:-.02em}
.unans .lbl{font-size:calc(26*var(--u));color:var(--ink);line-height:1.2}
.unans .lbl small{display:block;color:var(--ink3);font-size:calc(18*var(--u))}
.ulist{flex:none;display:flex;flex-direction:column;gap:calc(8*var(--u))}
.ulist div{display:grid;grid-template-columns:1fr calc(110*var(--u)) calc(40*var(--u));align-items:center;gap:calc(14*var(--u));font-size:calc(20*var(--u))}
.ulist .n{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:var(--ink2)}
.ulist .bar{height:calc(8*var(--u));border-radius:9px;background:var(--line);overflow:hidden}
.ulist .bar i{display:block;height:100%;background:color-mix(in srgb,var(--warn) 70%,transparent);border-radius:9px}
.ulist b{text-align:right;font-weight:650}
.cms{flex:1 1 0;display:flex;flex-direction:column;gap:calc(14*var(--u));min-height:0;overflow:hidden}
.cm{border-left:2px solid var(--line);padding-left:calc(16*var(--u));transition:border-color 1s,background 1s}
.cm .who{font-size:calc(18*var(--u));color:var(--ink3);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.cm .who b{color:var(--ink2);font-weight:600}
.cm p{margin:calc(4*var(--u)) 0 0;font-size:calc(21*var(--u));line-height:1.35;color:var(--ink);
 display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden}
.minor{flex:none;margin-top:auto;display:flex;flex-wrap:wrap;gap:calc(8*var(--u)) calc(22*var(--u));font-size:calc(18*var(--u));color:var(--ink3)}
.minor span{display:inline-flex;align-items:center;gap:calc(8*var(--u))}
.minor i{width:calc(9*var(--u));height:calc(9*var(--u));border-radius:50%;background:var(--c,var(--ink3))}
.calmst{flex:1;display:flex;flex-direction:column;justify-content:center;align-items:flex-start;gap:calc(12*var(--u))}
.calmst .ck{width:calc(64*var(--u));height:calc(64*var(--u));border-radius:50%;border:calc(3*var(--u)) solid var(--ok);display:grid;place-items:center;color:var(--ok)}
.calmst b{font:700 calc(48*var(--u))/1.05 var(--display)}
.calmst span{color:var(--ink2);font-size:calc(22*var(--u))}
/* milestones */
.ms{display:flex;flex-direction:column;gap:calc(13*var(--u))}
.mr{display:grid;grid-template-columns:1fr auto;row-gap:calc(6*var(--u));transition:background 1s}
.mr .k{font-size:calc(22*var(--u));color:var(--ink2);font-weight:550}
.mr .v{font-size:calc(23*var(--u));color:var(--ink3);text-align:right}
.mr .v b{color:var(--ink);font-weight:650;font-size:calc(26*var(--u))}
.mr .v em{font-style:normal;color:var(--ink2)}
.track{grid-column:1/-1;height:calc(8*var(--u));background:var(--line);border-radius:99px;overflow:hidden}
.track i{display:block;height:100%;border-radius:99px;background:var(--acc-dim);transition:width 1.5s cubic-bezier(.2,.7,.2,1);position:relative}
.track i::after{content:"";position:absolute;right:0;top:0;bottom:0;width:calc(4*var(--u));background:var(--acc);border-radius:99px}
.mr .eta{grid-column:1/-1;display:flex;justify-content:space-between;font-size:calc(17*var(--u));color:var(--ink3)}
.mr .eta b{color:var(--acc);font-weight:600}
.mr.near .eta b{color:var(--ok)}
.sub{display:flex;flex-direction:column;gap:calc(14*var(--u))}
.mm{display:grid;grid-template-columns:1fr auto;row-gap:calc(6*var(--u))}
.mm .n{grid-column:1/-1;font-size:calc(20*var(--u));white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:var(--ink)}
.mm .v{font-size:calc(17*var(--u));color:var(--ink3)}
.mm .track{height:calc(6*var(--u))}
.mm .eta{font-size:calc(17*var(--u));color:var(--ink3);text-align:right}.mm .eta b{color:var(--acc);font-weight:600}
.crossed{display:flex;flex-direction:column;gap:calc(10*var(--u))}
.cx{display:grid;grid-template-columns:calc(26*var(--u)) 1fr auto;align-items:baseline;gap:calc(10*var(--u));font-size:calc(20*var(--u));
 padding:calc(4*var(--u)) calc(8*var(--u));margin:0 calc(-8*var(--u));border-radius:calc(8*var(--u));transition:background 1.5s}
.cx .f{color:var(--ok);font-size:calc(16*var(--u))}
.cx .t{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:var(--ink2)}.cx .t b{color:var(--ink);font-weight:650}
.cx .d{color:var(--ink3);font-size:calc(17*var(--u))}
.empty{color:var(--ink3);font-size:calc(19*var(--u))}
.fresh{background:color-mix(in srgb,var(--acc) 16%,transparent)!important}
.cm.fresh{border-color:var(--acc)}
/* stats strip */
.strip{display:grid;grid-template-columns:repeat(6,1fr);border-top:1px solid var(--line);padding-top:calc(24*var(--u))}
.st{padding:0 calc(26*var(--u));border-left:1px solid var(--line);display:flex;flex-direction:column;gap:calc(6*var(--u));min-width:0}
.st:first-child{border-left:0;padding-left:0}
.st .l{font-size:calc(18*var(--u));letter-spacing:.12em;text-transform:uppercase;color:var(--ink3);font-weight:600}
.st .v{font:700 calc(62*var(--u))/1 var(--display);letter-spacing:-.02em;white-space:nowrap}
.st .d{font-size:calc(21*var(--u));color:var(--ink3)}.st .d b{color:var(--acc);font-weight:650}
.st svg{display:block;width:100%;height:calc(48*var(--u));margin-top:auto;overflow:visible}
.st .ax{display:flex;justify-content:space-between;font-size:calc(14*var(--u));color:var(--ink3);margin-top:calc(2*var(--u))}
/* footer */
footer{display:flex;align-items:center;gap:calc(28*var(--u));font-size:calc(18*var(--u));color:var(--ink3)}
footer .dot{display:inline-block;width:calc(10*var(--u));height:calc(10*var(--u));border-radius:50%;background:var(--c,var(--ink3));margin-right:calc(8*var(--u))}
footer .sp{flex:1}
#conn{display:none;color:var(--warn)}#conn.on{display:inline}
/* fullscreen button */
#fs{position:fixed;right:24px;bottom:24px;z-index:5;font:600 15px var(--sans);color:var(--ink);background:var(--panel2);
 border:1px solid var(--line);border-radius:999px;padding:10px 18px;display:flex;gap:10px;align-items:center;cursor:pointer;
 opacity:1;transition:opacity .4s}
#fs:hover{border-color:var(--acc)}#fs kbd{font:600 12px var(--sans);color:var(--ink3);border:1px solid var(--line);border-radius:5px;padding:1px 6px}
body.idle #fs{opacity:0;pointer-events:none}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
</style></head><body>
<div id=stage>
 <header>
  <div class=brand><b>MakerMonitor</b><span id=acct></span></div>
  <div class="clock num" id=clock></div>
 </header>
 <main>
  <section id=att aria-live=polite><h2>Needs attention <span class=ct id=attct></span></h2><div id=attb style="display:contents"></div></section>
  <section class=sep><h2>Next mile markers</h2><div class=ms id=ms></div></section>
  <section class=sep id=right>
   <div class=sub><h2>Models next up</h2><div class=sub id=mm></div></div>
   <div class=sub style="margin-top:calc(14*var(--u))"><h2>Crossed this week</h2><div class=crossed id=cx></div></div>
  </section>
 </main>
 <div class=strip id=strip></div>
 <footer>
  <span id=fresh></span><span id=conn>Reconnecting…</span><span class=sp></span><span id=pnl></span>
 </footer>
</div>
<button id=fs type=button aria-label="Toggle full screen"><span id=fstx>Full screen</span><kbd>F</kbd></button>
<script>
"use strict";
const $=id=>document.getElementById(id);
const esc=s=>String(s??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const nf=(n,d=0)=>Number(n||0).toLocaleString("en-US",{maximumFractionDigits:d,minimumFractionDigits:0});
const T12={hour:"numeric",minute:"2-digit",hour12:true};
const tm=t=>new Date(t*1000).toLocaleTimeString("en-US",T12);
const DAY=86400;
function ago(t){const s=Math.max(0,Date.now()/1000-t);
 if(s<90)return"just now";if(s<3600)return Math.round(s/60)+" min ago";if(s<DAY)return Math.round(s/3600)+" h ago";
 const d=Math.round(s/DAY);return d==1?"yesterday":d+" days ago"}
const QS=new URLSearchParams(location.search),TOK=QS.get("token");
const api=p=>p+(TOK?(p.includes("?")?"&":"?")+"token="+encodeURIComponent(TOK):"");

// Headline metrics: key in total/today/days, label, decimals.
const M=[["downloads","Downloads"],["prints","Prints"],["likes","Likes"],["boosts","Boosts"],["points","Points"],["followers","Followers"]];
const ML={downloads:"downloads",prints:"prints",likes:"likes",boosts:"boosts",points:"points",followers:"followers"};

// Round-number ladder: the next milestone is the next multiple of step(v).
function step(v){v=Math.abs(v);if(v<100)return 10;if(v<1000)return 50;if(v<10000)return 250;if(v<100000)return 2500;
 return Math.pow(10,Math.floor(Math.log10(v)))/4}
const nextMs=v=>{const s=step(v);return{prev:Math.floor(v/s)*s,next:(Math.floor(v/s)+1)*s}};

function etaText(rem,rate){
 if(!(rate>0))return{t:"no recent pace",d:""};
 const days=rem/rate;
 if(days<1)return{t:"likely today",d:"",near:1};
 const at=new Date(Date.now()+days*DAY*1000);
 const ds=at.toLocaleDateString("en-US",{weekday:days<7?"short":undefined,month:"short",day:"numeric"});
 if(days>365)return{t:"over a year",d:""};
 if(days<14)return{t:"~"+Math.round(days)+(Math.round(days)==1?" day":" days"),d:ds,near:days<3};
 if(days<90)return{t:"~"+Math.round(days/7)+" weeks",d:ds};
 return{t:"~"+Math.round(days/30)+" months",d:ds};
}

let S=null,C=null,prev={cm:null,cx:null},first=true,lastOk=0;

// Numbers ease from their old value to the new one.
function tween(el,to,dec){
 const from=parseFloat(el.dataset.v);el.dataset.v=to;
 if(isNaN(from)||from===to||matchMedia("(prefers-reduced-motion:reduce)").matches){el.textContent=nf(to,dec);return}
 const t0=performance.now(),dur=1200;
 (function f(t){const k=Math.min(1,(t-t0)/dur),e=1-Math.pow(1-k,3);el.textContent=nf(from+(to-from)*e,dec);if(k<1)requestAnimationFrame(f)})(t0);
}

// Daily deltas (oldest first, today last) for a metric.
function series(k){
 if(k=="followers")return((S.followers||{}).series30||[]).slice(-7);
 return(S.days||[]).map(d=>+d[k]||0);
}
function dayLabel(i,n){const d=(S.days||[])[i];if(i==n-1)return"today";if(i==n-2)return"yesterday";
 const full={Sun:"Sunday",Mon:"Monday",Tue:"Tuesday",Wed:"Wednesday",Thu:"Thursday",Fri:"Friday",Sat:"Saturday"};
 return d&&full[d.d]?full[d.d]:(n-1-i)+" days ago"}
// Average of the closed days (today is partial), else the 30-day month figure.
function rate(k){
 const s=series(k);const closed=s.slice(0,-1);
 const r=closed.length?closed.reduce((a,b)=>a+b,0)/closed.length:0;
 if(r>0)return r;
 const mk={collects:"saves"}[k]||k,m=S.month||{};return m[mk]?m[mk]/30:0;
}

function render(){
 if(!S)return;
 const tot=S.total||{},td=S.today||{},h=S.health||{},st=S.status||{};
 $("acct").textContent=S.account||"";
 renderAttention(h,st);
 renderMilestones(tot);
 renderModels();
 renderStrip(tot,td);
 renderFooter(h,st);
 first=false;
}

function renderAttention(h,st){
 const al=[],acct=(C&&C.acct)||(S.creator||{}).acct||{},cr=S.creator||{};
 if(h.loopStalled)al.push(["err","Not polling","The poll loop stopped"+(h.loopTick?" at "+tm(h.loopTick):"")+". Restart the poller."]);
 if(h.auth=="expired")al.push(["err","MakerWorld cookie expired","Paste a fresh cookie in the dashboard Settings."]);
 else if(h.auth=="none")al.push(["warn","No MakerWorld cookie","Only public stats are showing."]);
 if(!h.loopStalled&&h.stale)al.push(["warn","Data is stale","No fresh MakerWorld data since "+(h.since?tm(h.since):"start-up")+"."]);
 else if(st.ok===false)al.push(["warn","Last poll failed",st.msg||""]);
 const un=cr.unans??acct.unans??0,top=(cr.unansTop||[]).slice(0,4);
 const recent=(S.comments||[]).filter(c=>c.t&&Date.now()-Date.parse(c.t)<3*DAY*1000).slice(0,2);
 if(al.length&&un){top.splice(3);recent.splice(1)}
 const pl=h.panels||[],down=pl.filter(p=>p.up===false);
 const att=$("att"),b=$("attb");
 att.className=al.some(a=>a[0]=="err")?"alert":al.length||un?"warn":"calm";
 const n=al.length+(un?1:0);
 $("attct").textContent=n?n+(n==1?" item":" items"):"";
 let html="";
 if(al.length)html+=`<div class=alerts>${al.map(([c,t,s])=>`<div class="al ${c}"><i></i><div><b>${esc(t)}</b><span>${esc(s)}</span></div></div>`).join("")}</div>`;
 if(un){
  const nm=Object.values((C&&C.models)||{}).filter(m=>m.unans>0).length;
  const mx=Math.max(1,...top.map(r=>r.u));
  html+=`<div class=unans><span class="big num" data-v id=unn>${nf(un)}</span><span class=lbl>comments awaiting your reply${nm?`<small>on ${nf(nm)} model${nm==1?"":"s"} · most waiting below</small>`:""}</span></div>`;
  html+=`<div class=ulist>${top.map(r=>`<div><span class=n>${esc(r.n)}</span><span class=bar><i style="width:${Math.round(r.u/mx*100)}%"></i></span><b class=num>${nf(r.u)}</b></div>`).join("")}</div>`;
 }
 if(!al.length&&!un)html+=`<div class=calmst><div class=ck><svg width="60%" height="60%" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.5l4.5 4.5L19 7.5"/></svg></div><b>All caught up</b><span>No unanswered comments and MakerWorld data is flowing.</span></div>`;
 if(recent.length){
  if(!al.length||!un)html+=`<h2 style="margin-top:calc(6*var(--u))">Latest comments</h2>`;
  html+=`<div class=cms>${recent.map(c=>`<div class=cm data-k="${esc(c.u+"|"+c.t)}"><div class=who><b>${esc(c.u)}</b> on ${esc(c.m)} · ${ago(Date.parse(c.t)/1000)}</div><p>${esc(c.x)}</p></div>`).join("")}</div>`;
 }
 if(pl.length)html+=`<div class=minor><span style="--c:${down.length==pl.length?"var(--err)":down.length?"var(--warn)":"var(--ok)"}"><i></i>${pl.length-down.length} of ${pl.length} panels answering</span>${down.length&&down.length<=3?`<span>Offline: ${down.map(p=>esc(p.board||p.host)).join(", ")}</span>`:""}</div>`;
 b.innerHTML=html;
 const u=$("unn");if(u){u.dataset.v=prev.un??"";tween(u,un,0);prev.un=un}
 // Flash comments that were not there on the last fetch.
 const keys=[...b.querySelectorAll(".cm")].map(e=>e.dataset.k);
 if(prev.cm)b.querySelectorAll(".cm").forEach(e=>{if(!prev.cm.includes(e.dataset.k))flash(e)});
 prev.cm=keys;
}

function flash(el){el.classList.add("fresh");setTimeout(()=>el.classList.remove("fresh"),6000)}

function renderMilestones(tot){
 const rows=M.filter(([k])=>tot[k]!=null).map(([k,l])=>{
  const v=+tot[k],{prev:p,next:nx}=nextMs(v),r=rate(k),rem=nx-v,e=etaText(rem,r),pct=Math.max(2,Math.min(100,(v-p)/(nx-p)*100));
  return `<div class="mr${e.near?" near":""}" data-k=${k}><span class=k>${l}</span><span class="v num"><b data-v>${nf(v,k=="points"?0:0)}</b> <em>→ ${nf(nx)}</em></span>
  <span class=track><i style="width:${pct.toFixed(1)}%"></i></span>
  <span class="eta num"><span>${nf(rem,k=="points"?0:0)} to go · ${r>0?nf(r,r<10?1:0)+"/day":"–"}</span><span><b>${e.t}</b>${e.d?" · "+e.d:""}</span></span></div>`});
 const ms=$("ms"),old=[...ms.querySelectorAll(".mr .v b")].map(x=>x.dataset.v);
 ms.innerHTML=rows.join("")||`<p class=empty>No totals yet.</p>`;
 ms.querySelectorAll(".mr").forEach((r,i)=>{const b=r.querySelector(".v b"),k=r.dataset.k;b.dataset.v=old[i]??"";tween(b,+tot[k],0)});
}

function crossings(){
 const out=[],tot=S.total||{};
 for(const [k,l] of M){
  if(tot[k]==null)continue;
  const s=series(k),n=s.length;let end=+tot[k];
  for(let i=n-1;i>=0;i--){const start=end-s[i];
   const st=step(Math.max(start,1)),a=Math.floor(start/st),b=Math.floor(end/st);
   if(b>a&&start>0)out.push({k:k+i,txt:`<b>${nf(b*st)}</b> ${ML[k]}`,when:dayLabel(i,n),age:n-1-i});
   end=start}
 }
 const mods=Object.values((C&&C.models)||{});
 for(const m of mods){const life=m.life||{},w=m.w||{};
  for(const [k,wk,l] of [["d","d","downloads"],["p","p","prints"]]){
   const end=+life[k]||0,start=end-(+w[wk]||0);if(start<=0||end<=start)continue;
   const st=step(start),a=Math.floor(start/st),b=Math.floor(end/st);
   if(b>a)out.push({k:m.id+k,txt:`<b>${nf(b*st)}</b> ${l} · ${esc(m.n)}`,when:"this week",age:7})}}
 return out.sort((a,b)=>a.age-b.age);
}

function renderModels(){
 const mods=Object.values((C&&C.models)||{}),cand=[];
 for(const m of mods){const life=m.life||{},w=m.w||{};
  for(const [k,l] of [["d","downloads"],["p","prints"]]){
   const v=+life[k]||0,r=(+w[k]||0)/7;if(!v||!(r>0))continue;
   const {prev:p,next:nx}=nextMs(v),rem=nx-v;cand.push({m,k,l,v,p,nx,rem,r,days:rem/r})}}
 cand.sort((a,b)=>a.days-b.days);
 const seen=new Set(),pick=[];for(const c of cand){if(seen.has(c.m.id))continue;seen.add(c.m.id);pick.push(c);if(pick.length==3)break}
 $("mm").innerHTML=pick.length?pick.map(c=>{const e=etaText(c.rem,c.r),pct=Math.max(2,(c.v-c.p)/(c.nx-c.p)*100);
  return `<div class=mm><span class=n>${esc(c.m.n)}</span><span class=track><i style="width:${pct.toFixed(1)}%"></i></span>
  <span class="v num">${nf(c.v)} → ${nf(c.nx)} ${c.l}</span><span class="eta num"><b>${e.t}</b>${e.d?" · "+e.d:""}</span></div>`}).join("")
  :`<p class=empty>${C?"No model is moving fast enough to call yet.":"Model stats are still loading."}</p>`;
 const cx=crossings().slice(0,6),box=$("cx");
 box.innerHTML=cx.length?cx.map(x=>`<div class=cx data-k="${esc(x.k)}"><span class=f>◆</span><span class="t num">${x.txt}</span><span class=d>${x.when}</span></div>`).join("")
  :`<p class=empty>No milestones crossed in the last 7 days.</p>`;
 const keys=cx.map(x=>x.k);
 if(prev.cx)box.querySelectorAll(".cx").forEach(e=>{if(!prev.cx.includes(e.dataset.k))flash(e)});
 prev.cx=keys;
}

function spark(s){
 // Daily bars; today's bar is partial, so it is drawn as an outline.
 if(!s.length)return"";
 const W=100,H=40,n=s.length,gap=3,bw=(W-gap*(n-1))/n,mx=Math.max(1,...s);
 return `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio=none aria-hidden=true>${s.map((v,i)=>{
  const h=Math.max(1.5,v/mx*H),x=i*(bw+gap),last=i==n-1;
  return last?`<rect x="${x+.6}" y="${H-h+.6}" width="${bw-1.2}" height="${Math.max(.1,h-1.2)}" rx="1.5" fill="none" stroke="var(--acc)" stroke-width="1.2" vector-effect=non-scaling-stroke />`
   :`<rect x="${x}" y="${H-h}" width="${bw}" height="${h}" rx="1.5" fill="var(--acc-dim)"/>`}).join("")}</svg>`;
}

function renderStrip(tot,td){
 const box=$("strip"),old={};box.querySelectorAll(".st").forEach(e=>old[e.dataset.k]=e.querySelector(".v").dataset.v);
 box.innerHTML=M.filter(([k])=>tot[k]!=null).map(([k,l])=>{const d=k=="followers"?(S.followers||{}).today??td[k]:td[k],s=series(k);
  return `<div class=st data-k=${k}><span class=l>${l}</span><span class="v num" data-v>${nf(tot[k])}</span>
  <span class="d num">${d>0?`<b>+${nf(d,k=="points"?1:0)}</b> today`:"none yet today"}</span>${spark(s)}<span class=ax><span>${s.length} days</span><span>today</span></span></div>`}).join("");
 box.querySelectorAll(".st").forEach(e=>{const v=e.querySelector(".v");v.dataset.v=old[e.dataset.k]??"";tween(v,+tot[e.dataset.k],0)});
}

function renderFooter(h,st){
 const t=st.lastPoll||h.since||S.t,ps=st.pollSec||600,age=Date.now()/1000-t;
 const c=h.loopStalled||age>3*ps?"var(--err)":age>1.5*ps||h.stale?"var(--warn)":"var(--ok)";
 $("fresh").innerHTML=t?`<span class=dot style="--c:${c}"></span>Updated ${tm(t)} · ${ago(t)}`:"Waiting for the first poll";
 const lm=h.limiter||{};$("pnl").textContent=lm.budget?`MakerWorld requests ${nf(lm.requests_last_hour)} / ${nf(lm.budget)} this hour`:"";
}

function clock(){const d=new Date(),s=d.toLocaleTimeString("en-US",T12),[hm,ap]=s.split(" ");
 $("clock").innerHTML=`${hm}<small>${ap||""}</small>`;
 if(S)renderFooter(S.health||{},S.status||{})}

async function load(){
 try{
  const [r1,r2]=await Promise.all([fetch(api("/api/dashboard"),{cache:"no-store",credentials:"same-origin"}),
   fetch("/v2/creator",{cache:"no-store"}).catch(()=>null)]);
  if(!r1.ok)throw new Error(r1.status==401?"Sign-in needed":"HTTP "+r1.status);
  S=await r1.json();
  if(r2&&r2.ok){try{C=await r2.json()}catch(e){}}
  lastOk=Date.now();$("conn").className="";render();
 }catch(e){
  $("conn").textContent=String(e.message).startsWith("Sign")?"Sign-in needed: open /kiosk?token=…":"Reconnecting…";
  $("conn").className="on";
 }
}

// Burn-in care: drift the whole layout a few pixels every few minutes.
const SHIFT=[[0,0],[6,3],[3,-5],[-5,-3],[-4,5],[5,-1],[-2,-6],[0,6]];let si=0;
setInterval(()=>{si=(si+1)%SHIFT.length;const s=document.getElementById("stage").style;s.setProperty("--sx",SHIFT[si][0]+"px");s.setProperty("--sy",SHIFT[si][1]+"px")},180000);

// Full screen: button and F key; the button and the cursor hide when idle.
function toggleFs(){if(document.fullscreenElement)document.exitFullscreen();else document.documentElement.requestFullscreen().catch(()=>{})}
$("fs").addEventListener("click",toggleFs);
addEventListener("keydown",e=>{if((e.key=="f"||e.key=="F")&&!e.ctrlKey&&!e.metaKey&&!e.altKey)toggleFs()});
document.addEventListener("fullscreenchange",()=>{$("fstx").textContent=document.fullscreenElement?"Exit full screen":"Full screen"});
let idleT;function wake(){document.body.classList.remove("idle");clearTimeout(idleT);idleT=setTimeout(()=>document.body.classList.add("idle"),3000)}
["mousemove","mousedown","touchstart","keydown"].forEach(ev=>addEventListener(ev,wake,{passive:true}));wake();

clock();setInterval(clock,5000);
load();setInterval(load,60000);
</script></body></html>"""
