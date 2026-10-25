"""Optional push notifications (Discord, Pushbullet, Pushover, ntfy, Telegram,
Home Assistant, Apprise, generic webhook).

Add a channel: subclass base.Channel in a new module, decorate it with
@register, set `kind`, implement `send(digest)`, and import the module in hub.py.
"""
from .base import CHANNELS, Channel, register
from .events import EVENT_TYPES, Event
from .hub import NotifyHub

__all__ = ["CHANNELS", "Channel", "EVENT_TYPES", "Event", "NotifyHub", "register"]
