# Notifications

The poller can send MakerWorld activity and its own health alerts to other services. Notifications are optional, configured in `config.yaml` under `notifications:`, and work with or without any panel (leave `panel_host` and `color_panel_host` empty for a notifications-only setup).

Supported services:

| `type` | Service | What a message looks like |
|--------|---------|---------------------------|
| `discord` | Discord webhook | One colored embed per event type, model names link to MakerWorld, optional ping on alerts |
| `pushbullet` | Pushbullet | One note per poll: summary title, one block per event type |
| `pushover` | Pushover | HTML message (bold headings, italic comments), tap opens the model, alerts at high priority |
| `ntfy` | ntfy.sh or self-hosted ntfy | Markdown message with emoji tags per event type, tap opens the model, alerts at priority 5 |
| `telegram` | Telegram bot | HTML message, comments as quotes, model names as links |
| `homeassistant` | Home Assistant | Push to the HA companion app via a notify service, and/or an event for automations |
| `apprise` | [Apprise](https://github.com/caronc/apprise/wiki) | Markdown message to any of 100+ services (Slack, Matrix, Gotify, Signal, Teams, ...) |
| `webhook` | Any URL | JSON POST with a summary, a text version and every event as data |

## Events

| Event | When |
|-------|------|
| `download`, `print`, `boost`, `like`, `collect` | A model's count went up. Grouped per type, each model listed with `+N (total)` |
| `comment` | A new comment on one of your models (your own replies are skipped). Shows who and what |
| `follower` | Follower count changed (up or down) |
| `points` | MakerWorld points went up |
| `health` | The cookie expired, or there was no fresh data for 3 poll intervals, and again when each recovers |

Each poll sends **at most one message per channel**, with everything grouped by type. Every type has its own emoji and color. The first poll after startup, and the first poll after a long gap, only set a baseline, so old activity is never sent as new.

## Configuration

```yaml
notifications:
  # Defaults for every channel; any channel can override them.
  events: [download, print, boost, comment, follower, health]  # default: all types
  exclude: [points]          # or name what you don't want instead
  quiet_hours: "23:00-07:00"
  quiet_digest: true
  urgent: [health]
  min_count: {download: 5}

  channels:
    - type: discord
      url: "https://discord.com/api/webhooks/..."
    - type: pushover
      token: "a..."
      user: "u..."
      events: [comment, health]   # this channel only gets these
```

### Options for every channel

| Key | Default | Meaning |
|-----|---------|---------|
| `events` | all | Event types this channel gets. A list, or a comma-separated string |
| `exclude` | none | Event types to leave out |
| `quiet_hours` | off | `"HH:MM-HH:MM"` (can cross midnight). No messages during this window except `urgent` types |
| `quiet_digest` | `true` | When quiet hours end: `true` sends one "🌙 While you were away (since 11:00 PM)" message with everything held; `always` also sends "No new activity overnight" after a quiet night; `false` drops what happened during quiet hours |
| `urgent` | `[health]` | Types that are sent even during quiet hours |
| `min_count` | none | `{type: n}`: hold a grouped type until it adds up to `n`, e.g. `{download: 5}` |
| `name` | the type | Label used in the log (useful with two channels of the same type) |
| `enabled` | `true` | `false` turns the channel off without deleting it |

The digest after quiet hours goes out with the first poll after the window ends. Held events are kept in memory, so a poller restart during the night drops what was held until then.

### Per service

**Discord**: in the channel, Edit Channel > Integrations > Webhooks > New Webhook, copy the URL.

```yaml
    - type: discord
      url: "https://discord.com/api/webhooks/..."
      mention: "<@123456789>"   # optional: pinged on health warnings
      username: "MakerMonitor"  # optional
      avatar_url: ""            # optional
```

**Pushbullet**: pushbullet.com > Settings > Access Tokens.

```yaml
    - type: pushbullet
      token: "o.xxxxx"
      device: ""     # optional device_iden; empty = all devices
      channel: ""    # optional channel tag instead of a device
```

**Pushover**: create an application at pushover.net for the API token; the user key is on your dashboard.

```yaml
    - type: pushover
      token: "axxxxx"
      user: "uxxxxx"           # user or group key
      priority: 0              # -2 (silent) .. 1 (high)
      alert_priority: 1        # health warnings; 1 gets through Do Not Disturb
      sound: "cashregister"    # optional; alert_sound for warnings
      device: ""               # optional
```

Pushover messages are limited to 1,024 characters; long digests drop whole sections and end with "(+N more)".

**ntfy**: install the ntfy app and subscribe to your topic. On ntfy.sh anyone who knows the topic name can read it, so make it hard to guess.

```yaml
    - type: ntfy
      topic: "makermonitor-pick-something-unguessable"
      server: "https://ntfy.sh"   # or your own server
      token: ""                   # optional access token (or username/password)
      priority: 3                 # 1-5
      alert_priority: 5           # health warnings
```

**Telegram**: message @BotFather to create a bot and get its token, then send your bot a message. Your chat id comes from @userinfobot; group ids start with `-`, channels can use `@name`.

```yaml
    - type: telegram
      token: "123456:ABC..."
      chat_id: "123456789"
      silent: false              # true = no notification sound
```

**Home Assistant**: Profile > Security > Long-lived access tokens. Set `service` and/or `event`.

```yaml
    - type: homeassistant
      url: "http://homeassistant.local:8123"
      token: "${HA_TOKEN}"
      service: "mobile_app_your_phone"  # Developer Tools > Actions, notify.*
      event: "makermonitor"             # optional: fires an event for automations
```

The event's data has `summary`, `handle`, `types` (list) and `events` (same shape as the webhook below), so an automation can trigger on `event_type: makermonitor` and check, for example, `"comment" in trigger.event.data.types`.

**Apprise**: one URL per service, in the formats listed on the [Apprise wiki](https://github.com/caronc/apprise/wiki). Needs the `apprise` package (in `requirements.txt`).

```yaml
    - type: apprise
      urls:
        - "slack://T000/B000/XXXX"
        - "gotify://gotify.local/AbCdEf"
```

**Generic webhook**:

```yaml
    - type: webhook
      url: "https://example.com/hook"
      headers: {Authorization: "Bearer ${MY_TOKEN}"}
```

Body:

```json
{
  "source": "makermonitor",
  "handle": "your-handle",
  "time": 1792886901,
  "summary": "1 new comment, +4 downloads",
  "text": "plain-text version of the whole message",
  "events": [
    {"type": "comment", "count": 1, "model": "Model name", "model_id": "3477737",
     "url": "https://makerworld.com/en/models/3477737", "user": "Someone", "text": "Nice!", "t": 1792886901.0},
    {"type": "download", "count": 3, "total": 120, "model": "Model name", "model_id": "3477737",
     "url": "https://makerworld.com/en/models/3477737", "t": 1792886901.0},
    {"type": "health", "level": "warn", "title": "MakerWorld cookie expired", "text": "...", "t": 1792886901.0}
  ]
}
```

## Secrets

`config.yaml` is git-ignored, so tokens and webhook URLs can go straight in it. To keep them out of the file entirely, write `"${NAME}"` in any value and set that environment variable for the poller (for the systemd service, add `Environment=NAME=...` or an `EnvironmentFile=`). A channel whose variable is missing is disabled with a warning. Errors are logged without the request URL, so webhook URLs and bot tokens don't end up in `data/poller.log`.

## Testing

The dashboard's Settings view can add and edit channels and has a **Send test message** button per channel; saving there applies without a restart (see [README](README.md#settings)). From the command line:

```bash
.venv/bin/python -m makermonitor --test-notify
```

This sends a sample message (an alert, a comment, a follower, downloads and likes) to every enabled channel, ignoring quiet hours, and exits non-zero if any channel failed. Restart the poller afterwards so it picks up config changes. Each delivery or failure is logged as `notify <channel>: ...` in `data/poller.log`.

## Adding a service

Each service is one class in `makermonitor/notify/`. Copy `pushbullet.py` as a template:

```python
from .base import Channel, register
from .style import Digest, render_text

@register
class Example(Channel):
    """Docstring: the config keys this channel takes."""

    kind = "example"            # the `type:` in config.yaml
    required = ("url",)         # config keys that must be set

    async def send(self, digest: Digest) -> None:
        await self.post(self.cfg["url"], json={
            "title": f"MakerMonitor: {digest.summary}",
            "body": render_text(digest, markdown=True),
        })
```

Then add the module to the import list in `notify/hub.py`. What a channel gets:

- `self.cfg`: its config block, with `${NAME}` already expanded.
- `digest.summary`: one line, e.g. "+12 downloads, +3 likes, 1 new comment" (prefixed with the heading for the end-of-quiet digest).
- `digest.sections`: one per event type, comment or alert, each with `title` (emoji included), `lines`, `url`, `author` and `style` (`emoji`, `color`).
- `digest.events`: the raw events, for services that want data.
- `render_text(digest, markdown=...)`: the whole message as markdown or plain text.
- `self.post(url, ...)`: an httpx POST that retries once on HTTP 429 and raises on errors.

Filtering, quiet hours, digests and error logging are handled by the base class and the hub, so a channel only formats and sends.

Per-type wording, emoji and colors live in `notify/style.py` (`STYLES`); events are built in `notify/events.py`.
