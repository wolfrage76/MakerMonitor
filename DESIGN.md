---
name: MakerMonitor Dashboard
description: The LAN web dashboard for one MakerWorld creator, laid out as a workshop shadow board.
colors:
  hardboard: "#55402e"
  hardboard-raised: "#6b5440"
  hardboard-ink: "#fbf6ef"
  hardboard-ink-muted: "#e4d6c5"
  steel-ground: "#e9ecee"
  panel: "#ffffff"
  panel-sunk: "#f3f5f6"
  hairline: "#d3d8dc"
  hairline-soft: "#e5e9ec"
  ink: "#14191e"
  ink-secondary: "#424c56"
  ink-muted: "#58626c"
  shadow-paint: "#c2410c"
  shadow-paint-ink: "#ffffff"
  shadow-paint-soft: "#fbe8de"
  shadow-paint-bright: "#fb8c4a"
  shadow-paint-bright-ink: "#1c0d03"
  status-ok: "#07714d"
  status-warn: "#8a5a00"
  status-error: "#c42b40"
  metric-downloads: "#109e6a"
  metric-prints: "#7b5cf0"
  metric-boosts: "#d98a00"
  metric-likes: "#e0457b"
  metric-collects: "#2f80ed"
  metric-comments: "#0f8fb8"
  metric-ratings: "#c99a00"
typography:
  display:
    fontFamily: "system-ui, -apple-system, Segoe UI Variable Text, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "27px"
    fontWeight: 700
    lineHeight: 1.1
    letterSpacing: "-0.01em"
    fontFeature: "tnum"
  headline:
    fontFamily: "system-ui, -apple-system, Segoe UI Variable Text, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "17px"
    fontWeight: 650
    lineHeight: 1.3
  title:
    fontFamily: "system-ui, -apple-system, Segoe UI Variable Text, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "15px"
    fontWeight: 650
    lineHeight: 1.3
    fontFeature: "tnum"
  body:
    fontFamily: "system-ui, -apple-system, Segoe UI Variable Text, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "14px"
    fontWeight: 400
    lineHeight: 1.45
    fontFeature: "tnum"
  label:
    fontFamily: "system-ui, -apple-system, Segoe UI Variable Text, Segoe UI, Roboto, Helvetica Neue, sans-serif"
    fontSize: "12.5px"
    fontWeight: 500
    lineHeight: 1.35
  mono:
    fontFamily: "ui-monospace, SF Mono, Cascadia Mono, Menlo, Consolas, monospace"
    fontSize: "12.5px"
    fontWeight: 400
    lineHeight: 1.5
rounded:
  cell: "6px"
  panel: "8px"
  control: "9px"
  nested: "12px"
  dialog: "16px"
  pill: "999px"
spacing:
  hair: "2px"
  xs: "6px"
  sm: "10px"
  md: "16px"
  panel-x: "18px"
  page: "20px"
  section: "28px"
components:
  button-primary:
    backgroundColor: "{colors.shadow-paint}"
    textColor: "{colors.shadow-paint-ink}"
    rounded: "{rounded.control}"
    padding: "7px 14px"
    height: "36px"
  button-secondary:
    backgroundColor: "{colors.panel}"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "7px 14px"
    height: "36px"
  button-ghost:
    backgroundColor: "transparent"
    textColor: "{colors.ink-secondary}"
    rounded: "{rounded.control}"
    padding: "7px 14px"
  button-danger:
    backgroundColor: "{colors.status-error}"
    textColor: "{colors.panel}"
    rounded: "{rounded.control}"
    padding: "7px 14px"
  input:
    backgroundColor: "{colors.panel-sunk}"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "8px 11px"
    height: "40px"
  card:
    backgroundColor: "{colors.panel}"
    rounded: "{rounded.panel}"
    padding: "0 18px 16px"
  card-header-strip:
    typography: "{typography.headline}"
    padding: "11px 18px"
    height: "48px"
  tab:
    textColor: "{colors.hardboard-ink-muted}"
    rounded: "8px 8px 0 0"
    padding: "9px 14px 10px"
  tab-active:
    backgroundColor: "{colors.steel-ground}"
    textColor: "{colors.ink}"
  segmented-on:
    backgroundColor: "{colors.panel}"
    textColor: "{colors.ink}"
    rounded: "{rounded.cell}"
    padding: "4px 11px"
    height: "30px"
  chip:
    backgroundColor: "{colors.panel-sunk}"
    textColor: "{colors.ink-secondary}"
    rounded: "{rounded.pill}"
    padding: "5px 12px"
    height: "32px"
  chip-on:
    backgroundColor: "{colors.shadow-paint-soft}"
    textColor: "{colors.ink}"
  status-pill:
    textColor: "{colors.hardboard-ink}"
    rounded: "{rounded.pill}"
    padding: "5px 12px"
  badge-ok:
    textColor: "{colors.status-ok}"
    rounded: "{rounded.pill}"
    padding: "3px 10px"
---

# Design System: MakerMonitor Dashboard

Scope: the web dashboard the poller serves at `/` (Overview, Models, Activity, Export, Health, Settings) and its login page, which share `_BASE_CSS`. The desk panels run ESP32 firmware with their own LVGL UI and are not governed here. The `/kiosk` view shares only the metric colours.

## Overview

**Creative North Star: "The Shadow Board"**

The dashboard is a workshop pegboard. Every metric keeps a fixed slot and every hour of the day is a peg hole: yesterday's count is the painted outline, today's peg fills it, so a gap or a surplus reads at a glance without a chart legend. A hardboard-brown band with folder tabs sits over a cool galvanized-steel ground, and flat white panels hang on hairlines with a header strip in place of a floating title.

Density is workbench-dense, not airy: rows of numbers, tables and short sentences, all in one system sans with tabular figures. Colour does two jobs only. The metric colours, shared with the desk panels, are terrain: downloads are always green, likes always pink. Shadow-paint orange marks what needs the creator and the current hour, and nothing else.

The world refuses the row of identical white stat tiles with sparklines; the Today board is one panel of slotted rows instead.

**Key Characteristics:**
- Hardboard band, steel ground, flat hairline panels with a 48px header strip.
- The 24-peg day strip as the signature mark, reused for best hours.
- Fixed metric colours as terrain; one reserved orange for "needs you" and "now".
- One system sans for everything, tabular figures everywhere, 12-hour times.
- Light and dark from the OS, no theme switch, no external assets.

## Colors

A cool steel-grey working surface under a warm brown band, with one hot orange and seven fixed metric hues. Every token has a dark-theme counterpart (listed in the sidecar); in dark mode the two oranges swap roles so the brighter one carries the accent.

### Primary
- **Shadow-Paint Orange** (shadow-paint): the reserved colour. The filled dot on a "Needs you" item, the outlined dot on a "fix this" item, the now-hour tick under every peg strip, the active tab icon, sort arrows, focus rings, caret, text-field focus border, the primary button, and the selected chip. **Shadow-Paint Bright** (shadow-paint-bright) is the same paint on dark ground: the phone tab bar's active bar and icon, the save bar's primary button, and focus inside the band.
- **Shadow-Paint Soft** (shadow-paint-soft): a wash for selected chips; never a fill for whole panels.

### Secondary
- **Hardboard** (hardboard / hardboard-raised): the top band and the phone bottom tab bar, and the brand mark's tile. Its inks (hardboard-ink, hardboard-ink-muted) are the only text colours used on it.

### Tertiary: metric terrain
- **Downloads green, Prints violet, Boosts amber, Likes pink, Collects blue, Comments teal, Ratings gold** (metric-*): one hue per metric, identical to the desk panels. Used for the metric dot beside a label, the today pegs, table header dots, sparklines and 7-day bars, and feed tags (15% tint behind a 78% ink mix). Views use ink-secondary and impressions ink-muted when they need a dot.

### Neutral
- **Steel Ground** (steel-ground): the page behind panels and the active tab, so the tab reads as cut out of the band.
- **Panel** (panel) and **Panel Sunk** (panel-sunk): panel faces, and the recessed fill of inputs, segmented controls, chips, nested cards and meters.
- **Hairline** (hairline) for panel borders, input strokes and table headers; **Hairline Soft** (hairline-soft) for row dividers, the header-strip rule and peg holes.
- **Ink / Ink Secondary / Ink Muted** (ink, ink-secondary, ink-muted): figures and names; labels and supporting sentences; windows, axes, units and yesterday outlines. Ink Muted holds AA on panel and ground in both themes.
- **Status** (status-ok, status-warn, status-error): up/down deltas, banners, badges, Health source dots, switch-on, form errors. Never used for metrics.

### Named Rules
**The Shadow-Paint Rule.** Orange means "this needs you" or "now". It never colours a metric, a chart series, a heading or decoration; if an element is not actionable and not the current hour, it is not orange.

**The Fixed Slot Rule.** A metric's colour is its identity across web, kiosk and desk panels. Never reassign, reorder or reuse a metric hue for status or emphasis.

## Typography

**Display Font:** the system UI sans (system-ui, with Segoe UI / Roboto / Helvetica Neue fallbacks)
**Body Font:** the same system sans
**Label/Mono Font:** the system monospace, only for code, tokens, secrets and error detail

**Character:** one plain workshop face for every word and number; hierarchy comes from weight (500 / 600 / 650 / 700) and size, not from a second family.

### Hierarchy
- **Display** (700, 27px, 1.1, -0.01em; 24px on phone): the count in each Today board slot. A zero drops to 600 in ink-muted.
- **Headline** (650, 17px, 1.3): panel header strips; Settings section titles use 700 at 18px.
- **Title** (650, 15–16px): secondary figures in facts rows, the herofoot, weekday cells; 21px at 700 for the follower minis.
- **Body** (400, 14px, 1.45): base text; tables run at 13.5px, supporting sentences at 13px.
- **Label** (500–600, 12.5px): time windows in header strips, column headers, units, axis labels (12px), help text.

### Named Rules
**The One Face Rule.** Every number uses the system sans with tabular figures, set on `body`. Monospace is never used for metrics.

**The 12-Hour Rule.** Every time the dashboard shows is 12-hour with AM/PM: clock times as "11:00 AM", axes as "12 AM, 6 AM, 12 PM, 6 PM, 11 PM", ranges as "9:00 PM-7:00 AM". No 24-hour time anywhere.

## Layout

Content sits in a 1280px column with 20px page padding (14px by 12px on phone) under the sticky band. Overview is a two-column board at 7fr / 5fr with 16px gaps: Today, Top models, Best times and Followers on the left, Needs you, Funnel and 7 and 30 days on the right, and Creator at a glance full width below. Other views use a 12-column grid (spans 12/8/7/6/5/4) that collapses to one column under 1000px; on Overview the columns dissolve and panels follow a fixed reading order (Today, Needs you, Top models, Funnel, ...).

Panel padding is 18px (14px phone); rows inside are divided by soft hairlines with 9–12px vertical padding. The Today board row is a three-track grid: 140px label and count, the peg strip, 160px comparison; on phone the strip drops beneath the label. Settings is a 188px sticky section nav beside the form (max 1080px), which becomes a horizontal pill strip under 900px.

Breakpoints: 1000px (grid collapse), 900px (settings and export stack), 760px (phone: bottom tab bar, hidden columns), 560px, 420px.

## Elevation & Depth

The system is flat. Panels sit on the ground with a 1px hairline and no shadow; depth comes from the band, the ground and the sunk fills. Shadows exist only on things that float above the board or that are physically "pressed in".

### Shadow Vocabulary
- **Pressed** (`0 1px 2px rgba(20,25,30,.08)`; `rgba(0,0,0,.4)` dark): the selected segment of a segmented control, the current Settings nav item, the login box.
- **Floating** (`0 10px 30px rgba(0,0,0,.28)` save bar, `.18` toast): transient bars over content.
- **Modal** (`0 20px 50px rgba(0,0,0,.3)`, backdrop `rgba(8,12,18,.5)`): the confirm dialog.

### Named Rules
**The Hairline Rule.** Panels are flat on a 1px hairline. A panel never gets a drop shadow; only floating and modal layers do.

## Shapes

Softly squared, never pill-shaped panels. Panels are 8px; controls (buttons, inputs, affix groups, Settings nav items) are 9px; nested containers inside a panel (panel rows, channel cards) 12px; the dialog 16px. Pills (999px) are reserved for small status and count elements: the status pill, badges, chips, tab counts, feed summary chips, meters and the switch. Folder tabs round only their top corners (8px 8px 0 0). Dots are circles: 8px metric dots, 10px action dots, 9px source dots.

## Components

### Buttons
Plain workshop buttons, 600 weight, 36px tall (32px small).
- **Shape:** gently rounded (9px).
- **Primary:** shadow-paint fill with its ink; hover brightens 8%. Inside the save bar it uses shadow-paint-bright.
- **Secondary:** panel face with a hairline; hover darkens the border to ink-muted.
- **Ghost:** no fill, ink-secondary; hover gets the sunk fill.
- **Danger:** status-error fill, used only in the confirm dialog; the quieter "remove" is error-coloured text.
- **Focus:** a 2px shadow-paint outline at 2px offset, everywhere.

### Chips and segmented controls
- **Segmented control:** a sunk 8px track with 2px inset; the selected segment is the panel face with the Pressed shadow and a hairline ring. Used in header strips to change a panel's window or metric.
- **Chips:** pill, sunk fill, hairline border, 13px 600. Selected: shadow-paint-soft wash with a shadow-paint border.

### Cards / Containers
- **Corner Style:** 8px.
- **Background:** panel on steel ground.
- **Shadow Strategy:** none (see The Hairline Rule).
- **Border:** 1px hairline.
- **Header strip:** every panel opens with a 48px strip (11px by 18px, soft hairline below) carrying the title at left and, at right, either a segmented control or a 12.5px muted note stating the time window ("30 days, and yesterday", "last 30 days").

### Inputs / Fields
- **Style:** sunk fill, 1px hairline, 9px, 40px tall (search 36px).
- **Focus:** shadow-paint border plus a 3px ring of shadow-paint at 24%.
- **Error:** status-error border and ring; message in 12.5px 600 error text below.
- **Switch:** 44 by 26 pill, status-ok when on.

### Navigation
- **Desktop:** folder tabs cut into the hardboard band; inactive tabs in hardboard-ink-muted, hover a 9% ink wash, the active tab takes the steel ground colour with ink text and a shadow-paint icon. Counts sit in small pills.
- **Phone (under 760px):** tabs move to a fixed six-column bottom bar on hardboard, 60px tall, icon over 12px label; the active tab gets a 3px shadow-paint-bright bar at the top edge.

### The Peg Strip (signature)
A 240 by 20 SVG row of 24 holes, one per hour. Each hole is a hairline-soft dot; yesterday's count is a solid ink-muted outline (1.3px stroke); today's count up to now is a filled peg in the metric colour, radius scaled by square root of the value; the current hour gets a 7px shadow-paint tick beneath. Reused at 24px tall for Best times, where the busiest hour gets an ink ring and there is no now-tick. A shared 12-hour axis labels it once per board.

### Needs-you list
A plain list with soft dividers. Each item leads with a 10px dot: filled shadow-paint for act now, shadow-paint ring for fix the page, ink-muted ring otherwise; a bold linked headline, then a 13px ink-secondary sentence with the numbers behind it.

### Status pill
In the band: pill with a 10% hardboard-ink wash and an 8px dot (green ok, amber warn, red error).

## Do's and Don'ts

### Do:
- **Do** give every metric its fixed slot and its fixed colour, the same as the desk panels.
- **Do** use the peg strip for anything that is "per hour of a day".
- **Do** state the time window of every number in the header strip or beside the figure.
- **Do** put deltas in status-ok / status-error with a small triangle, and "even" in ink when there is no change.
- **Do** write every time in 12-hour format with AM/PM.
- **Do** keep each new panel flat: 8px, 1px hairline, 48px header strip.

### Don't:
- **Don't** use shadow-paint orange for a metric, a chart series or decoration (The Shadow-Paint Rule).
- **Don't** build rows of identical stat tiles with sparklines; put metrics in one slotted panel.
- **Don't** add drop shadows to panels, or a second typeface for display or numbers.
- **Don't** load fonts, scripts or images from another host; the page is self-contained.
- **Don't** add a manual theme toggle; light and dark follow the OS.
