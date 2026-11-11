# MakerMonitor color panels: the 7" system, for other sizes

MakerMonitor is a small desk display (ESP32 + LVGL 8) that shows a 3D-print
designer's MakerWorld stats at a glance from ~1 m away. Mocks must be drawable
by the firmware:

- Exact pixel canvas per panel (no scrolling, no responsive reflow).
- One typeface: **Montserrat Medium (500) only**, sizes 12/14/16/20/22/26/32/40
  px built in; one custom digits-only Montserrat Medium 56 px for lead numbers.
  No bold, no italics, no other fonts.
- **Flat fills only** (RGB565 bands gradients): no gradients, glows, shadows,
  blur or transparency effects.
- Icons are simple 2.2-stroke line icons (bolt, heart, bookmark, download
  arrow, printer, coin "$", follower, star) at 20/32/40/48 px, recoloured.

## Tokens (exact hex)

| token | hex | use |
|---|---|---|
| ground | #06090E | screen background |
| plate | #0E141D | cards, radius 10, 1 px border #1E2A38 |
| ink | #E6EDF3 | numbers and primary text |
| ink2 | #B3C1CF | labels, secondary text |
| ink3 | #7E8C9B | captions, "yesterday" lines |
| dim | #2A3644 | zero values, empty bar tracks |
| accent | #22D3EE | controls only (pager, selected tab, buttons) |
| points gold | #F2C14E | Points icon and number; Points plate #28231A |
| boosts | #FFB020 · likes #FF4D7E · collects #3DA9FF · downloads #2EE59D · prints #A78BFA · followers #2DE2E6 · rating star #FFD23F | only on icons and chart bars |

## Rules from the shipped 7" Today (reference image)

- Header: "Today" sentence case 20 px ink, date beside it in ink3, clock
  centred, hamburger button top right. No tracked caps, no accent bar or rule.
- Lead stats: **Downloads, Prints, Points** matter most; Boosts shares the top
  row with Downloads and Prints. Each top card: large kind-coloured icon beside
  a big ink number; "yesterday +13" under it in ink3; the last 7 days as small
  bars (past days at ~28% of the kind colour, today full colour, weekday
  initials under, "Today" under the last).
- Middle bar: Likes | **Points** | Collects in one plate. No word labels: an
  icon beside each figure, hung from the top. Points is widest, on the gold
  plate, number in gold.
- Foot: Recent activity (icon, "+1", model name, "24m") and Followers (big
  number, "+0 today", star icon + "Ratings 108 • 4.94 avg").
- Numbers are ink; colour lives on icons and bars. Zeros are dim.
- Footer chrome stays: sync dot bottom left, pager dashes bottom centre
  (current one accent, wider), version bottom right in 12 px ink3.

## Sample data

Boosts 0 (yday 0), Downloads +13 (yday +13), Prints +4 (yday +9), Likes +3
(yday +37), Collects +13 (yday +22), Points +13.0 (yday +53.7), Followers 57
(+0 today), Ratings 108, 4.94 avg. Week Downloads 17,14,19,21,15,12,13; Prints
11,8,12,10,9,9,4. Activity: +1 collect "Funnel and Spout for Voisen 2LB
Silica" 24m; +1 collect "Desiccant Boxes for Sterilite Drybox" 24m; +1
download "Sterilite 19QT/54QT Filament Drybox" 34m.
