# Components

Framework: none. One Python module (`poller/makermonitor/dashboard.py`) holds DASHBOARD_HTML: vanilla HTML, CSS custom properties, vanilla JS string templates. No component library, no Tailwind.

Shared primitives are CSS classes plus JS template helpers: `.card` panel with `h2` header strip, `.seg` segmented control, `.btn` (pri, ghost, dan, sm), `.badge`, `.chip`, `.sw` switch, `table` via `table(head,rows,empty)`, `spark()` sparkline, `pegs()` 24-hour peg strip, `brow()` Today board row.

```css
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
```

```js
function seg(el,items,cur,set){el.innerHTML=items.map(([k,l])=>`<button type=button class="${k==cur?"on":""}" aria-pressed="${k==cur}" data-k="${k}">${l}</button>`).join("");
 el.querySelectorAll("button").forEach(b=>b.onclick=()=>{set(b.dataset.k);renderAll()})}
function spark(vals,color,w=90,h=22,fill=true){if(!vals||!vals.length)return"";const mx=Math.max(1,...vals),n=vals.length,st=n>1?w/(n-1):0;
 const pts=vals.map((v,i)=>`${(i*st).toFixed(1)},${(h-2-(v/mx)*(h-4)).toFixed(1)}`).join(" ");
 return `<svg class=spark viewBox="0 0 ${w} ${h}" preserveAspectRatio=none aria-hidden=true>${fill?`<polygon points="0,${h} ${pts} ${w},${h}" fill="var(--${color})" opacity=.14 />`:""}<polyline points="${pts}" fill=none stroke="var(--${color})" stroke-width=1.6 stroke-linejoin=round vector-effect=non-scaling-stroke /></svg>`}
function table(head,rows,empty="Nothing yet"){return `<table><thead><tr>${head.map(h=>`<th>${h}</th>`).join("")}</tr></thead><tbody>`+
 (rows.length?rows.join(""):`<tr><td colspan=${head.length} class=empty>${empty}</td></tr>`)+"</tbody></table>"}

/* ---------- routing ---------- */
function spark(vals,color,w=90,h=22,fill=true){if(!vals||!vals.length)return"";const mx=Math.max(1,...vals),n=vals.length,st=n>1?w/(n-1):0;
 const pts=vals.map((v,i)=>`${(i*st).toFixed(1)},${(h-2-(v/mx)*(h-4)).toFixed(1)}`).join(" ");
 return `<svg class=spark viewBox="0 0 ${w} ${h}" preserveAspectRatio=none aria-hidden=true>${fill?`<polygon points="0,${h} ${pts} ${w},${h}" fill="var(--${color})" opacity=.14 />`:""}<polyline points="${pts}" fill=none stroke="var(--${color})" stroke-width=1.6 stroke-linejoin=round vector-effect=non-scaling-stroke /></svg>`}
function table(head,rows,empty="Nothing yet"){return `<table><thead><tr>${head.map(h=>`<th>${h}</th>`).join("")}</tr></thead><tbody>`+
 (rows.length?rows.join(""):`<tr><td colspan=${head.length} class=empty>${empty}</td></tr>`)+"</tbody></table>"}

/* ---------- routing ---------- */
function tabScroll(){const tb=$("tabs"),ta=tb.querySelector(".on");if(ta&&tb.scrollWidth>tb.clientWidth)tb.scrollLeft=ta.offsetLeft-tb.clientWidth+ta.offsetWidth+24}
function route(){const h=(location.hash||"#overview").slice(1),[v,sub]=h.split("/");const view=$("v-"+v)?v:"overview";document.body.dataset.view=view;
function table(head,rows,empty="Nothing yet"){return `<table><thead><tr>${head.map(h=>`<th>${h}</th>`).join("")}</tr></thead><tbody>`+
 (rows.length?rows.join(""):`<tr><td colspan=${head.length} class=empty>${empty}</td></tr>`)+"</tbody></table>"}

/* ---------- routing ---------- */
function tabScroll(){const tb=$("tabs"),ta=tb.querySelector(".on");if(ta&&tb.scrollWidth>tb.clientWidth)tb.scrollLeft=ta.offsetLeft-tb.clientWidth+ta.offsetWidth+24}
function route(){const h=(location.hash||"#overview").slice(1),[v,sub]=h.split("/");const view=$("v-"+v)?v:"overview";document.body.dataset.view=view;
 document.querySelectorAll("section.view").forEach(s=>s.classList.toggle("on",s.id=="v-"+view));
 document.querySelectorAll("#tabs a").forEach(a=>{const on=a.dataset.v==view;a.classList.toggle("on",on);on?a.setAttribute("aria-current","page"):a.removeAttribute("aria-current")});
 tabScroll();
const ARR=d=>`<svg width=10 height=10 viewBox="0 0 10 10" aria-hidden=true><path d="${d>0?"M5 1.2L9.2 7.6H.8z":"M5 8.8L.8 2.4h8.4z"}" fill=currentColor /></svg>`;
function pegs(td,yd,color,h,o={}){const W=240,H=o.big?24:20,cy=H/2-(o.big?0:1),R=o.big?4.8:4.5,r0=o.big?.9:1.4,t=td||[],y=yd||[];
 const mx=Math.max(1,...t.slice(0,h+1).map(v=>+v||0),...y.map(v=>+v||0)),rad=v=>r0+(R-r0)*(o.lin?Math.max(0,+v||0)/mx:Math.sqrt(Math.max(0,+v||0)/mx));
 let s=`<svg class=pegs viewBox="0 0 ${W} ${H}" aria-hidden=true>`;
 for(let i=0;i<24;i++){const x=i*10+5,tv=i<=h?+t[i]||0:0,yv=+y[i]||0;s+=o.tip?`<g><title>${esc(o.tip(i))}</title>`:"<g>";
  s+=`<circle cx=${x} cy=${cy} r=${r0} fill="var(--line)"/>`;
  if(yv>0)s+=`<circle cx=${x} cy=${cy} r=${rad(yv).toFixed(2)} fill=none stroke="var(--ink3)" stroke-width=.9 stroke-dasharray="1.5 1.1"/>`;
  if(tv>0)s+=`<circle cx=${x} cy=${cy} r=${rad(tv).toFixed(2)} style="fill:var(--${i===o.hi?"acc":color})"/>`;
  s+="</g>"}
function pegs(td,yd,color,h,o={}){const W=240,H=o.big?24:20,cy=H/2-(o.big?0:1),R=o.big?4.8:4.5,r0=o.big?.9:1.4,t=td||[],y=yd||[];
 const mx=Math.max(1,...t.slice(0,h+1).map(v=>+v||0),...y.map(v=>+v||0)),rad=v=>r0+(R-r0)*(o.lin?Math.max(0,+v||0)/mx:Math.sqrt(Math.max(0,+v||0)/mx));
 let s=`<svg class=pegs viewBox="0 0 ${W} ${H}" aria-hidden=true>`;
 for(let i=0;i<24;i++){const x=i*10+5,tv=i<=h?+t[i]||0:0,yv=+y[i]||0;s+=o.tip?`<g><title>${esc(o.tip(i))}</title>`:"<g>";
  s+=`<circle cx=${x} cy=${cy} r=${r0} fill="var(--line)"/>`;
  if(yv>0)s+=`<circle cx=${x} cy=${cy} r=${rad(yv).toFixed(2)} fill=none stroke="var(--ink3)" stroke-width=.9 stroke-dasharray="1.5 1.1"/>`;
  if(tv>0)s+=`<circle cx=${x} cy=${cy} r=${rad(tv).toFixed(2)} style="fill:var(--${i===o.hi?"acc":color})"/>`;
  s+="</g>"}
 if(o.now!==false&&h>=0&&h<24)s+=`<rect x=${h*10+1.5} y=${H-1.8} width=7 height=1.8 rx=.9 fill="var(--acc)"/>`;
function brow(l,c,val,zero,body,cmp){return `<div class="brow${zero?" zero":""}" style="--k:var(--${c})"><div class=lab><span><span class=kdot></span>${l}</span><div class="val${zero?" zero":""}">${val}</div></div>${body}<div class=cmp>${cmp}</div></div>`}
const PERIOD_ROWS=[...K,["views","Views","ink2"],["impressions","Impressions","ink3"],["points","Points","r"]];
function wk7(vals,color){const mx=Math.max(1,...vals),n=vals.length,bw=64/7;
 return `<svg class=wk7 viewBox="0 0 64 20" aria-hidden=true>${vals.map((v,i)=>`<rect x="${(i*bw+1).toFixed(1)}" y="${(20-Math.max(1.5,v/mx*19)).toFixed(1)}" width="${(bw-2).toFixed(1)}" height="${Math.max(1.5,v/mx*19).toFixed(1)}" rx=1.5 style="fill:var(--${color})" opacity="${i==n-1?1:.55}"/>`).join("")}</svg>`}
function renderOverview(){const t=S.today||{},y=S.yday||{},hr=S.hours||{},X=S.dash||{},yh=X.yhours,h=pHour();
 $("since").textContent=S.midnight?"since midnight":"since "+hm(S.since||0);
 $("ydaynote").innerHTML=`<span class=blegend aria-hidden=true><span><svg width=10 height=10><circle cx=5 cy=5 r=4 fill="var(--ink2)"/></svg>today</span>${yh?`<span><svg width=10 height=10><circle cx=5 cy=5 r=4 fill=none stroke="var(--ink3)" stroke-dasharray="1.6 1.2"/></svg>yesterday</span>`:""}<span><svg width=10 height=4><rect width=10 height=3 rx=1.5 fill="var(--acc)"/></svg>now</span></span>`;
 const tot=S.total||{},m=S.month||{},ra=+tot.ratingAvg||0,rv=t.ratings||0;
 $("hero").innerHTML=K.map(([k,l,c])=>{const v=t[k]||0,yv=y[k],yk=yh&&yh[k];
```
