# Dashboard critique (before the redesign)

Method: a heuristic design review plus the impeccable detector run on the page source and the live page. Target: `poller/makermonitor/dashboard.py`, live at the poller's `/`. Screenshots of every view are in `before/`.

## Design health: 25/40 (Acceptable)

| # | Heuristic | Score | Key issue |
|---|---|---|---|
| 1 | Visibility of system status | 3 | "Updated 3 min ago" is good; the 60 s refresh is silent and the Activity count badge is unexplained. |
| 2 | Match with the real world | 3 | To-do and funnel prose is plain; tables fall back to DL, COLL., DL/VIEW, "×9.9". |
| 3 | User control and freedom | 3 | Settings has Discard, confirm dialogs, Ctrl+S. Overview ranges are per card only. |
| 4 | Consistency and standards | 2 | Range toggles differ card to card; "DL" vs "Downloads"; mono vs sans numbers. |
| 5 | Error prevention | 3 | Validated inputs, confirmed removals. |
| 6 | Recognition rather than recall | 2 | Model names truncate to identical prefixes; one model's facts are spread over 12 cards. |
| 7 | Flexibility and efficiency | 2 | No per-model view, sorting is mouse-only, no view shortcuts. |
| 8 | Aesthetic and minimalist design | 2 | Models is a wall: a 13-column table plus 12 analysis cards. |
| 9 | Error recovery | 3 | Settings errors are actionable; a failed main load only turns the pill red. |
| 10 | Help and documentation | 2 | No explanation of the ratios, points sources or badge counts. |

## Design specificity

The content is specific to a MakerWorld creator (pacing against "by now yesterday", the impressions to prints funnel, the to-do copy with evidence). The chrome is not: white rounded cards, a coloured dot before every label, segmented pills, a 12-column card grid. Rename it and it is any SaaS metrics page. Type is split three ways (rounded display numerals, sans labels, monospace table cells).

Detector: 1 real AA failure (the highlighted day label in Best times, 4.0:1 at 11.5 px), 1 false positive (tab-bar inset), 1 advisory (card titles at 15 px against 13 to 14 px body rely on weight alone).

## What works

1. The To do list: each item names the model, the evidence and the next step. This is the best thing in the product.
2. Honest comparisons: today against yesterday at the same time, and a funnel that names its weakest step.
3. Settings craft: save bar, Discard, confirm dialogs, plain errors, skip link, focus rings, live regions.

## Priority issues

- **[P1] Every card has the same weight.** The To do list, the product's real value, sits in the second row and looks exactly like the Followers card. Zero-count tiles (Boosts 0, Ratings 0 with five gold stars that are really the lifetime average) get the same space as Downloads.
- **[P1] Models is a data dump.** 13 columns plus 12 cards; Pages to improve, Print problems, Rising and Stalled repeat the To do list; nothing lets you look at one model.
- **[P1] Phone layout breaks.** Tabs run off the right edge (Health clipped, Settings and Kiosk unreachable without a hidden scroll), the models table shows 4 of 13 columns, and names cut to identical prefixes.
- **[P1] Sorting needs a mouse.** Sortable headers are plain `<th>` with click handlers: no button, no `aria-sort`.
- **[P2] Meaning rides on colour alone.** Red funnel rates, red "down" trends, red and green deltas carry no arrow or word.
- **[P2] Contrast and size.** The Best times highlight label fails AA; lots of 11 to 12 px muted text (axes, column heads, subtitles).

## Persona red flags

- **Power user:** no per-model page, no view shortcuts, ranges per card, feed not groupable.
- **Keyboard and screen reader:** sort headers unreachable, colour-only deltas, charts with no text alternative.
- **Phone:** off-screen tabs, a 6,500 px tall Models page, clipped tables, 28 px segment buttons.

## Minor

- The Today footer packs five stats into one run-on line.
- The Funnel card is 40% empty below its steps.
- Best-times day values have no unit.
- "Kiosk ↗" sits in the main nav as a peer of the views.
