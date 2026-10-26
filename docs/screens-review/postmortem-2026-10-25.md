# Post-mortem: "design API 502" and a cookie warning (Oct 25, 2026)

## Summary

From about 12:13 PM to 2:18 PM, MakerWorld's servers kept answering some requests with HTTP 502. Five polls failed on that. Each was retried and recovered within 2 to 5 minutes, and no counts went wrong. But every failed poll told the panels the poll status was `error` with the text "design API: HTTP Error 502". The color panel shows that text in its **Cookie** row (and the e-paper in its status line), so it looked like a cookie problem. It wasn't: the cookie was accepted the whole time.

## Timeline

- **12:13 PM**: model list 502, poll failed, retried in 2 min.
- **12:15 PM**: you opened MakerWorld; the extension sent the cookie and the poll was OK.
- **12:16 PM**: another 502, poll failed, retried in 5 min.
- **12:21 PM**: OK.
- **12:21 – 1:22 PM**: all polls OK. The extension sent the cookie again at 12:35 PM and 12:46 PM.
- **1:31 PM**: 502, failed, retried in 2 min.
- **1:34 PM**: OK.
- **1:44 PM**: OK.
- **1:53 PM**: the extension sent the cookie; the immediate poll got a 502 and failed.
- **1:54 PM**: the next poll was OK.
- **2:03 PM**: 502, failed, retried in 2 min.
- **2:06 PM**: OK.
- **2:15 PM**: 502, failed, retried in 2 min.
- **2:18 PM**: OK. No failures since.

Over the window there were 28 HTTP 502 errors in total, across the model list, profile, comments and points requests. There were also some HTTP 403s on background requests: the 30-day reach report, per-model analytics and launch windows. Those don't affect the cookie state, and counts still updated.

## Root cause

- MakerWorld had an intermittent server-side outage (502s).
- On a failed poll that isn't an auth failure, the poller pushed `auth: "error"` with the error text to the color panels, and `status("error", …)` to the e-paper panel.
- The color panel's Status page decides its **Cookie** row from that `auth` field. Anything other than `ok` shows amber with the message, so a server error read as "cookie trouble".

## Why 09370ef didn't prevent it

It did what it was built for, but this was a different case:

- **No profile fallback**: the 502s failed the poll cleanly, with no partial data and no extra requests.
- **Fast retry**: every failure recovered within 2 to 5 minutes.
- **Cookie rule**: never triggered, because no poll was refused (401/403) on the models or points requests. There was no "poll auth failed" at all, and `health.auth` stayed `ok` throughout.

The warning came from how a plain failed poll is shown on the panels, which 09370ef didn't change.

## Discord

No bad messages. The only messages in the window were normal activity: +1 collect (12:35 PM and 12:47 PM), +0.9 points (12:48 PM and 1:22 PM). No health alert was sent: the data never went stale, which needs 3 poll intervals without fresh data.

## Proposed fixes (not implemented)

1. **Don't call a server error a cookie problem.** On a non-auth failure, keep sending the real cookie state (`ok`) with a plain message such as "MakerWorld is having trouble (HTTP 502), trying again in 2 min". That's a poller-only change, in the generic failure path of `server.py`. The Cookie row then stays "auth OK" and the Poller row explains the retry.
2. **Or quieter still: leave the panels alone for a short blip.** Keep showing the last good state while a retry is pending, and only report a problem once data is actually stale. Stale already turns the status dot amber and shows the "data stale since 2:03 PM" banner.
3. **E-paper:** send the same friendlier message, or nothing, instead of `ERR design API: HTTP Error 502`.

I recommend 1 together with 3; 2 is optional on top.
