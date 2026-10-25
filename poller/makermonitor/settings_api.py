"""Settings tab backend for the dashboard.

GET  /api/settings          config.yaml as form data (secrets masked)
POST /api/settings          validate, back up, save (comments kept), apply live
POST /api/settings/test     send a sample message to one channel from the form
POST /api/settings/panel    check that a panel host answers
GET  /api/settings/cookie   MakerWorld cookie status
POST /api/settings/cookie   paste a cookie by hand

All behind the dashboard token (same check as /api/dashboard). Secrets never
leave the poller: the form gets KEEP in their place and sends it back unchanged
to mean "keep what config.yaml has".
"""
from __future__ import annotations

import asyncio
import copy
import hashlib
import io
import logging
import os
import re
import time
from pathlib import Path
from typing import Any, Callable

import yaml
from fastapi import FastAPI, Header, HTTPException
from fastapi.responses import JSONResponse
from pydantic import ValidationError
from ruamel.yaml import YAML
from ruamel.yaml.comments import CommentedMap, CommentedSeq
from ruamel.yaml.scalarstring import DoubleQuotedScalarString

from .config import Settings
from .notify import CHANNELS, EVENT_TYPES, NotifyHub
from .notify.base import expand_env
from .panels import detect

log = logging.getLogger(__name__)

KEEP = "__keep__"
BACKUPS = 10  # config.yaml.bak-<time> files kept next to config.yaml

# Top-level fields the form edits. listen_*, data_dir and api_token stay
# file-only: changing them needs a restart and can lock the dashboard out.
TOP = {
    "handle": str, "poll_interval_sec": int, "page_cycle_sec": int, "panels": list, "user_agent": str,
}
# Replaced by `panels`; a save from the form moves them into it.
LEGACY_PANEL_KEYS = ("panel_host", "panel_port", "color_panel_host", "color_panel_port")
READONLY = ("listen_host", "listen_port", "data_dir")

# Per channel type: (key, label, kind, required, secret, hint). kind is text,
# int, bool, lines (list of strings) or headers (dict, "Name: value" lines).
F = tuple[str, str, str, bool, bool, str]
CHANNEL_FIELDS: dict[str, list[F]] = {
    "discord": [("url", "Webhook URL", "text", True, True, "Channel settings > Integrations > Webhooks"),
                ("mention", "Mention on health alerts", "text", False, False, "<@your-user-id>, optional"),
                ("username", "Bot name", "text", False, False, "default MakerMonitor")],
    "pushbullet": [("token", "Access token", "text", True, True, "pushbullet.com > Settings > Access Tokens"),
                   ("device", "Device iden", "text", False, False, "empty = all devices"),
                   ("channel", "Channel tag", "text", False, False, "optional")],
    "pushover": [("token", "App token", "text", True, True, "pushover.net > Create an Application"),
                 ("user", "User key", "text", True, True, "top of the pushover.net dashboard"),
                 ("priority", "Priority", "int", False, False, "-2 silent .. 1 high, default 0"),
                 ("alert_priority", "Health alert priority", "int", False, False, "default 1"),
                 ("sound", "Sound", "text", False, False, "optional, e.g. cashregister"),
                 ("device", "Device", "text", False, False, "optional")],
    "ntfy": [("topic", "Topic", "text", True, True, "pick something unguessable"),
             ("server", "Server", "text", False, False, "default https://ntfy.sh"),
             ("token", "Access token", "text", False, True, "optional"),
             ("username", "Username", "text", False, False, "optional"),
             ("password", "Password", "text", False, True, "optional"),
             ("priority", "Priority", "int", False, False, "1-5, default 3"),
             ("alert_priority", "Health alert priority", "int", False, False, "default 5")],
    "telegram": [("token", "Bot token", "text", True, True, "from @BotFather"),
                 ("chat_id", "Chat ID", "text", True, True, "your id, a group id or @channel"),
                 ("silent", "Silent", "bool", False, False, "deliver without sound")],
    "homeassistant": [("url", "Home Assistant URL", "text", True, False, "http://homeassistant.local:8123"),
                      ("token", "Long-lived token", "text", True, True, "Profile > Security"),
                      ("service", "Notify service", "text", False, False, "mobile_app_your_phone"),
                      ("event", "Event name", "text", False, False, "optional, for automations")],
    "apprise": [("urls", "Apprise URLs", "lines", True, True, "one per line: slack://..., gotify://...")],
    "webhook": [("url", "URL", "text", True, True, "receives a JSON POST"),
                ("headers", "Headers", "headers", False, True, "one per line: Name: value")],
}
COMMON = ("type", "name", "enabled", "events", "quiet_hours")
DEFAULT_KEYS = ("events", "quiet_hours", "quiet_digest", "urgent", "min_count")
GROUPED = ("download", "print", "boost", "like", "collect", "points")
_QUIET_T = re.compile(r"^(\d{1,2})(?::([0-5]\d))?\s*([ap])?\.?m?\.?$", re.I)


def _quiet_24h(spec: str) -> str | None:
    """"11:00 PM-7:00 AM", "11pm-7am" or "23:00-07:00" -> "23:00-07:00" (None if bad)."""
    parts = spec.split("-")
    if len(parts) != 2:
        return None
    out = []
    for p in parts:
        m = _QUIET_T.match(p.strip())
        if not m:
            return None
        h, mi, ap = int(m.group(1)), int(m.group(2) or 0), (m.group(3) or "").lower()
        if ap:
            if not 1 <= h <= 12:
                return None
            h = h % 12 + (12 if ap == "p" else 0)
        elif h > 23 or m.group(2) is None:
            return None
        out.append(f"{h:02d}:{mi:02d}")
    return "-".join(out)
_ENV = re.compile(r"^\$\{\w+\}$")


def _q(v: Any) -> Any:
    """New strings are written double-quoted, like the rest of config.yaml."""
    return DoubleQuotedScalarString(v) if isinstance(v, str) and v else v


def _yaml() -> YAML:
    y = YAML()
    y.preserve_quotes = True
    y.width = 4096  # keep long strings (user_agent) on one line
    y.indent(mapping=2, sequence=4, offset=2)
    return y


def _secret_keys(kind: str) -> set[str]:
    return {f[0] for f in CHANNEL_FIELDS.get(kind, []) if f[4]}


def _mask(value: Any) -> Any:
    """Environment references aren't secrets; everything else set is hidden."""
    if value in (None, "", [], {}):
        return ""
    if isinstance(value, str) and _ENV.match(value.strip()):
        return value
    return KEEP


def _to_form(kind: str, key: str, value: Any) -> Any:
    fields = {f[0]: f for f in CHANNEL_FIELDS.get(kind, [])}
    f = fields.get(key)
    if f and f[2] == "lines" and isinstance(value, (list, tuple)):
        return "\n".join(str(v) for v in value)
    if f and f[2] == "headers" and isinstance(value, dict):
        return "\n".join(f"{k}: {v}" for k, v in value.items())
    return value


class SettingsService:
    def __init__(self, ctx: Any, path: Path) -> None:
        self.ctx = ctx
        self.path = path
        self._lock = asyncio.Lock()

    # ---- reading -------------------------------------------------------
    def _raw(self) -> tuple[str, Any]:
        text = self.path.read_text()
        return text, _yaml().load(text) or CommentedMap()

    @staticmethod
    def _rev(text: str) -> str:
        return hashlib.sha256(text.encode()).hexdigest()[:16]

    def read(self) -> dict[str, Any]:
        text, doc = self._raw()
        st = self.ctx.settings
        top = {k: doc.get(k, getattr(st, k)) for k in TOP if k != "panels"}
        top["panels"] = st.panel_hosts  # `panels` plus anything still under the old keys
        n = doc.get("notifications") or {}
        defaults = {k: _plain(n.get(k)) for k in DEFAULT_KEYS if n.get(k) is not None}
        defaults["events"] = _effective(n.get("events"), n.get("exclude")) or list(EVENT_TYPES)
        channels = []
        for i, ch in enumerate(n.get("channels") or []):
            if not isinstance(ch, dict):
                continue
            kind = str(ch.get("type", "")).lower()
            secret = _secret_keys(kind)
            row: dict[str, Any] = {"_i": i}
            for k, v in ch.items():
                v = _plain(v)
                row[k] = _mask(v) if k in secret else _to_form(kind, k, v)
            row.pop("exclude", None)
            row["events"] = _effective(ch.get("events"), ch.get("exclude"), defaults["events"])
            channels.append(row)
        return {
            "rev": self._rev(text),
            "path": str(self.path),
            "top": _plain(top),
            "readonly": {k: getattr(st, k) for k in READONLY},
            "effective": {"poll_interval_sec": self.ctx.poll_interval},
            "panels_detected": self.ctx.panels_view(),
            "notifications": {"defaults": defaults, "channels": channels},
            "schema": {
                "events": list(EVENT_TYPES),
                "grouped": list(GROUPED),
                "types": {k: [dict(zip(("key", "label", "kind", "required", "secret", "hint"), f)) for f in v]
                          for k, v in CHANNEL_FIELDS.items() if k in CHANNELS},
            },
            "keep": KEEP,
        }

    # ---- building ------------------------------------------------------
    def _channel(self, form: dict[str, Any], old: dict[str, Any] | None, errors: dict[str, str],
                 where: str, target: CommentedMap | None = None) -> CommentedMap:
        """Form row -> config mapping. `target` is the existing mapping (edited in place)."""
        kind = str(form.get("type", "")).lower().strip()
        if kind not in CHANNELS:
            errors[f"{where}.type"] = f"unknown type {kind!r}"
        out = target if target is not None and str(target.get("type", "")).lower() == kind else CommentedMap()
        if out is not target and old:
            old = None  # type changed: nothing to inherit
        fields = {f[0]: f for f in CHANNEL_FIELDS.get(kind, [])}

        def put(key: str, value: Any) -> None:
            if value in (None, "", [], {}):
                out.pop(key, None)
            elif _plain(out.get(key)) != _plain(value):
                out[key] = _q(value)

        put("type", kind)
        put("name", str(form.get("name") or "").strip())
        if form.get("enabled") is False:
            out["enabled"] = False
        else:
            out.pop("enabled", None)
        for key, (_, label, fkind, required, secret, _h) in fields.items():
            v = form.get(key)
            if secret and v == KEEP:
                v = (old or {}).get(key)
                if v is None:
                    errors[f"{where}.{key}"] = f"{label} is required"
                    continue
                put(key, v)
                continue
            if fkind == "int":
                if v in (None, ""):
                    v = None
                else:
                    try:
                        v = int(v)
                    except (TypeError, ValueError):
                        errors[f"{where}.{key}"] = f"{label} must be a whole number"
                        continue
            elif fkind == "bool":
                v = True if v else None
            elif fkind == "lines":
                v = [s.strip() for s in str(v or "").splitlines() if s.strip()]
            elif fkind == "headers":
                h = {}
                for line in str(v or "").splitlines():
                    if line.strip():
                        if ":" not in line:
                            errors[f"{where}.{key}"] = "use Name: value, one per line"
                            break
                        a, b = line.split(":", 1)
                        h[a.strip()] = b.strip()
                v = h
            else:
                v = str(v or "").strip()
            if required and v in (None, "", [], {}):
                errors[f"{where}.{key}"] = f"{label} is required"
            put(key, v)
        events = [e for e in (form.get("events") or []) if e]
        bad = sorted(set(events) - set(EVENT_TYPES))
        if bad:
            errors[f"{where}.events"] = "unknown events " + ", ".join(bad)
        if form.get("events") is not None:
            if not events:
                errors[f"{where}.events"] = "pick at least one event, or use the defaults"
            out.pop("exclude", None)  # the form sends the effective list
        put("events", events)
        q = str(form.get("quiet_hours") or "").strip()
        if q:
            q = _quiet_24h(q) or q
            if not _quiet_24h(q):
                errors[f"{where}.quiet_hours"] = 'use "11:00 PM-7:00 AM"'
        put("quiet_hours", q)
        return out

    def _check_channel(self, cfg: dict[str, Any], defaults: dict[str, Any], errors: dict[str, str],
                       where: str) -> None:
        cls = CHANNELS.get(str(cfg.get("type", "")).lower())
        if cls is None or cfg.get("enabled") is False:
            return
        try:
            cls(expand_env(_plain(cfg)), defaults, self.ctx.settings.handle)
        except KeyError as exc:
            errors[f"{where}.env"] = f"environment variable {exc} is not set for the poller"
        except ValueError as exc:
            errors.setdefault(f"{where}.form", str(exc))

    def build(self, body: dict[str, Any]) -> tuple[str, Any, dict[str, str]]:
        """Apply the form to a copy of config.yaml. Returns (new text, doc, errors)."""
        _, doc = self._raw()
        errors: dict[str, str] = {}
        top = body.get("top") or {}
        for k, t in TOP.items():
            if k not in top:
                continue
            v = top[k]
            if t is list:  # panels: hosts or .local names, "host:port" for another port
                hosts = v.split(",") if isinstance(v, str) else list(v or [])
                clean: list[str] = []
                for h in hosts:
                    h = str(h).strip().removeprefix("http://").removeprefix("https://").rstrip("/")
                    if not h or h in clean:
                        continue
                    if " " in h or "/" in h:
                        errors["top.panels"] = f'"{h}" is not a host name or address'
                    clean.append(h)
                seq = CommentedSeq([_q(h) for h in clean])
                seq.fa.set_flow_style()
                if clean or "panels" in doc:
                    doc["panels"] = seq
                for old in LEGACY_PANEL_KEYS:
                    if old in doc:
                        del doc[old]
                continue
            try:
                v = int(v) if t is int else str(v if v is not None else "").strip()
            except (TypeError, ValueError):
                errors[f"top.{k}"] = "must be a whole number"
                continue
            if k not in doc and v == Settings.model_fields[k].default:
                continue
            if doc.get(k) != v:
                doc[k] = _q(v)
        poll = doc.get("poll_interval_sec", 600)
        if isinstance(poll, int) and not 60 <= poll <= 3600:
            errors["top.poll_interval_sec"] = "60 to 3600 seconds"
        cyc = doc.get("page_cycle_sec", 30)
        if isinstance(cyc, int) and cyc < 10:
            errors["top.page_cycle_sec"] = "at least 10 seconds"
        if not str(doc.get("handle") or "").strip():
            errors["top.handle"] = "required"

        nf = body.get("notifications")
        if nf is not None:
            n = doc.get("notifications")
            if not isinstance(n, dict):
                n = CommentedMap()
                doc["notifications"] = n
            d = nf.get("defaults") or {}
            q = str(d.get("quiet_hours") or "").strip()
            if q:
                q = _quiet_24h(q) or q
                if not _quiet_24h(q):
                    errors["defaults.quiet_hours"] = 'use "11:00 PM-7:00 AM"'
            events = [e for e in d.get("events") or [] if e in EVENT_TYPES]
            urgent = [e for e in d.get("urgent") or [] if e in EVENT_TYPES]
            mc = {}
            for t, v in (d.get("min_count") or {}).items():
                if t in GROUPED and v not in (None, "", 0, "0"):
                    try:
                        if int(v) > 1:
                            mc[t] = int(v)
                    except (TypeError, ValueError):
                        errors[f"defaults.min_count.{t}"] = "whole number"
            qd = d.get("quiet_digest", True)
            new_defaults = {
                "events": events if events and set(events) != set(EVENT_TYPES) else None,
                "quiet_hours": q or None,
                # [health] is the built-in default; only write it when it differs.
                "urgent": None if urgent == ["health"] else urgent,
                "quiet_digest": None if qd is True else qd,
                "min_count": mc or None,
            }
            if "events" in d and not events:
                errors["defaults.events"] = "pick at least one event"
            for k, v in new_defaults.items():
                if k not in d:
                    continue
                if v is None:
                    n.pop(k, None)
                elif k not in n:
                    n.insert(0, k, _q(v) if isinstance(v, str) else v)  # defaults sit above channels
                elif _plain(n.get(k)) != v:
                    n[k] = _q(v) if isinstance(v, str) else v
            if new_defaults["events"] is not None or "events" in d:
                n.pop("exclude", None)

            old_seq = n.get("channels")
            old_list = list(old_seq) if isinstance(old_seq, list) else []
            originals = [copy.deepcopy(_plain(c)) if isinstance(c, dict) else None for c in old_list]
            plain_defaults = {k: _plain(v) for k, v in n.items() if k != "channels"}
            new_items = []
            names = set()
            for j, row in enumerate(nf.get("channels") or []):
                where = f"channels.{j}"
                i = row.get("_i")
                i = i if isinstance(i, int) and 0 <= i < len(old_list) else None
                target = old_list[i] if i is not None and isinstance(old_list[i], CommentedMap) else None
                ch = self._channel(row, originals[i] if i is not None else None, errors, where, target)
                label = ch.get("name") or ch.get("type")
                if label in names:
                    errors[f"{where}.name"] = "two channels share this name; give one a name"
                names.add(label)
                self._check_channel(ch, plain_defaults, errors, where)
                new_items.append(ch)
            if isinstance(old_seq, CommentedSeq):
                # Edit the existing list so comments around it stay.
                del old_seq[len(new_items):]
                for k, ch in enumerate(new_items):
                    if k < len(old_seq):
                        if old_seq[k] is not ch:
                            old_seq[k] = ch
                    else:
                        old_seq.append(ch)
            elif new_items:
                n["channels"] = CommentedSeq(new_items)
            elif "channels" in n:
                n["channels"] = CommentedSeq()

        buf = io.StringIO()
        _yaml().dump(doc, buf)
        text = buf.getvalue()
        if not errors:
            try:
                Settings(**(yaml.safe_load(text) or {}))
            except (ValidationError, yaml.YAMLError) as exc:
                errors["form"] = str(exc)[:400]
        return text, doc, errors

    # ---- saving --------------------------------------------------------
    def _write(self, text: str) -> str:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        backup = self.path.with_name(f"{self.path.name}.bak-{stamp}")
        mode = self.path.stat().st_mode & 0o777
        backup.write_bytes(self.path.read_bytes())
        os.chmod(backup, 0o600)
        tmp = self.path.with_name(f".{self.path.name}.tmp")
        with open(tmp, "w") as fh:
            fh.write(text)
            fh.flush()
            os.fsync(fh.fileno())
        os.chmod(tmp, mode)
        try:
            os.replace(tmp, self.path)
        except OSError:  # a single-file bind mount can't be replaced; write in place
            tmp.unlink(missing_ok=True)
            self.path.write_text(text)
        old = sorted(self.path.parent.glob(f"{self.path.name}.bak-*"))
        for p in old[:-BACKUPS]:
            p.unlink(missing_ok=True)
        return backup.name

    async def save(self, body: dict[str, Any]) -> dict[str, Any]:
        async with self._lock:
            current = self.path.read_text()
            if body.get("rev") and body["rev"] != self._rev(current):
                raise HTTPException(409, "config.yaml changed since this page loaded; reload to see it")
            text, _, errors = self.build(body)
            if errors:
                return {"ok": False, "errors": errors}
            if text == current:
                return {"ok": True, "changed": False, "rev": self._rev(text)}
            backup = self._write(text)
            new = Settings(**(yaml.safe_load(text) or {}))
            self.ctx.apply_settings(new)
            log.info("settings saved from dashboard (backup %s)", backup)
            return {"ok": True, "changed": True, "backup": backup, "rev": self._rev(text)}

    async def test_channel(self, body: dict[str, Any]) -> dict[str, Any]:
        _, doc = self._raw()
        n = doc.get("notifications") or {}
        old_list = list(n.get("channels") or [])
        i = body.get("_i")
        old = _plain(old_list[i]) if isinstance(i, int) and 0 <= i < len(old_list) else None
        errors: dict[str, str] = {}
        ch = _plain(self._channel(body, old, errors, "channel"))
        defaults = {k: _plain(v) for k, v in n.items() if k != "channels"}
        ch.pop("enabled", None)
        self._check_channel(ch, defaults, errors, "channel")
        if errors:
            return {"ok": False, "errors": errors}
        hub = NotifyHub({**defaults, "channels": [ch]}, self.ctx.settings.handle)
        if not hub.channels:
            return {"ok": False, "errors": {"channel.form": "invalid channel settings (see poller.log)"}}
        seen = _Capture()  # the hub logs why a send failed; show that reason here
        logging.getLogger("makermonitor.notify.hub").addHandler(seen)
        try:
            results = await hub.test()
        finally:
            logging.getLogger("makermonitor.notify.hub").removeHandler(seen)
        ok = bool(results) and all(results.values())
        why = next((m.split(" failed: ", 1)[1].rsplit(" (", 1)[0] for m in seen.lines if " failed: " in m), "send failed")
        return {"ok": ok, "errors": {} if ok else {"channel.form": why[:300]}}

    async def test_panel(self, body: dict[str, Any]) -> dict[str, Any]:
        host = str(body.get("host") or "").strip().removeprefix("http://").rstrip("/")
        if not host:
            return {"ok": False, "msg": "no host"}
        base = f"http://{host}" if ":" in host else f"http://{host}:80"
        found = await detect(base)
        if not found:
            return {"ok": False, "msg": f"no answer from {host}"}
        kind = "Color panel" if found["type"] == "color" else "E-paper panel"
        fw = f", v{found['fw']}" if found["fw"] else ""
        return {"ok": True, "type": found["type"], "msg": f"{kind} ({found['board']}{fw})"}

    def cookie_status(self) -> dict[str, Any]:
        st = self.ctx.store.status
        cookie = self.ctx.store.get_cookie()
        names = [p.split("=", 1)[0].strip() for p in cookie.split(";") if "=" in p]
        return {
            "present": bool(cookie),
            "age": int(time.time() - st.cookie_updated_at) if st.cookie_updated_at else None,
            "auth": self.ctx.auth_state,
            "names": names[:20],
            "bytes": len(cookie),
            "lastError": st.last_error[:160],
            "lastPoll": int(st.last_poll_at),
            "lastOk": st.last_poll_ok,
        }


class _Capture(logging.Handler):
    def __init__(self) -> None:
        super().__init__(logging.WARNING)
        self.lines: list[str] = []

    def emit(self, record: logging.LogRecord) -> None:
        self.lines.append(record.getMessage())


def _effective(events: Any, exclude: Any, base: list[str] | None = None) -> list[str] | None:
    """events minus exclude, as the channel sees it; None when neither is set."""
    if not events and not exclude:
        return None
    if isinstance(events, str):
        events = [e.strip() for e in events.split(",")]
    skip = set(exclude or [])
    return [e for e in (events or base or EVENT_TYPES) if e in EVENT_TYPES and e not in skip]


def _plain(v: Any) -> Any:
    """ruamel containers -> plain dict/list/str for JSON and validation."""
    if isinstance(v, dict):
        return {str(k): _plain(x) for k, x in v.items()}
    if isinstance(v, (list, tuple)):
        return [_plain(x) for x in v]
    if isinstance(v, str):
        return str(v)
    return v


def register(app: FastAPI, ctx: Any, path: Path, auth: Callable[[str | None, str | None], bool],
             set_cookie: Callable[[str], bool]) -> SettingsService:
    from fastapi import Cookie

    svc = SettingsService(ctx, path)

    def guard(token: str | None, mm_token: str | None, post: str | None = "x") -> None:
        if not auth(token, mm_token):
            raise HTTPException(401, "Unauthorized")
        if not post:  # every POST from the page sends X-MM; forms on other sites can't
            raise HTTPException(400, "missing X-MM header")

    @app.get("/api/settings")
    async def get_settings(token: str | None = None, mm_token: str | None = Cookie(default=None)) -> JSONResponse:
        guard(token, mm_token)
        return JSONResponse(svc.read())

    @app.post("/api/settings")
    async def post_settings(body: dict[str, Any], token: str | None = None,
                            mm_token: str | None = Cookie(default=None),
                            x_mm: str | None = Header(default=None, alias="X-MM")) -> JSONResponse:
        guard(token, mm_token, x_mm)
        out = await svc.save(body)
        return JSONResponse(out, status_code=200 if out["ok"] else 422)

    @app.post("/api/settings/test")
    async def post_test(body: dict[str, Any], token: str | None = None,
                        mm_token: str | None = Cookie(default=None),
                        x_mm: str | None = Header(default=None, alias="X-MM")) -> JSONResponse:
        guard(token, mm_token, x_mm)
        return JSONResponse(await svc.test_channel(body))

    @app.post("/api/settings/panel")
    async def post_panel(body: dict[str, Any], token: str | None = None,
                         mm_token: str | None = Cookie(default=None),
                         x_mm: str | None = Header(default=None, alias="X-MM")) -> JSONResponse:
        guard(token, mm_token, x_mm)
        return JSONResponse(await svc.test_panel(body))

    @app.get("/api/settings/cookie")
    async def get_cookie(token: str | None = None, mm_token: str | None = Cookie(default=None)) -> JSONResponse:
        guard(token, mm_token)
        return JSONResponse(svc.cookie_status())

    @app.post("/api/settings/cookie")
    async def post_cookie(body: dict[str, Any], token: str | None = None,
                          mm_token: str | None = Cookie(default=None),
                          x_mm: str | None = Header(default=None, alias="X-MM")) -> JSONResponse:
        guard(token, mm_token, x_mm)
        raw = str(body.get("cookie") or "").strip()
        if not raw:
            raise HTTPException(400, "paste the cookie first")
        changed = set_cookie(raw)
        return JSONResponse({"ok": True, "changed": changed, **svc.cookie_status()})

    return svc
