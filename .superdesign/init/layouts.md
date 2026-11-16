# Layouts

App shell in `poller/makermonitor/dashboard.py`: sticky `header.top` (hardboard-brown band: brand mark, @account, status pill, Kiosk link; `nav.tabs` folder tabs Overview, Models, Activity, Export, Health, Settings; on phones the tabs become a fixed bottom bar), `main` (max 1280px) with one `section.view` per hash route, footer links.

```html
<a class=skip href="#main">Skip to content</a>
<header class=top>
 <div class=bar><div class=brand><svg class=mark width=24 height=24 viewBox="0 0 24 24" aria-hidden=true><rect width=24 height=24 rx=6 fill="var(--hb2)"/><circle cx=7 cy=7 r=2.6 fill="var(--d)"/><circle cx=17 cy=7 r=2.6 fill="var(--p)"/><circle cx=7 cy=17 r=2.6 fill="var(--l)"/><circle cx=17 cy=17 r=1.5 fill="none" stroke="var(--hb-ink2)" stroke-width=1.2/></svg>MakerMonitor</div><span class=acct id=acct></span>
  <span class=pill id=pill role=status><span class=dot></span><span id=pilltx>Loading</span></span>
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
```
