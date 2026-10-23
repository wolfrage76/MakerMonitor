const COOKIE_NAMES = [
  "token",
  "cf_clearance",
  "JWTtoken",
  "jwt",
  "session",
  "sid",
];

const DEFAULTS = {
  pollerUrl: "http://127.0.0.1:8787",
  apiToken: "change-me",
  enabled: true,
  lastHash: "",
  lastPushAt: 0,
  lastPushOk: false,
  lastError: "",
};

async function getConfig() {
  const stored = await chrome.storage.sync.get(Object.keys(DEFAULTS));
  return { ...DEFAULTS, ...stored };
}

function hashString(s) {
  let h = 0;
  for (let i = 0; i < s.length; i++) {
    h = (Math.imul(31, h) + s.charCodeAt(i)) | 0;
  }
  return String(h);
}

async function collectMakerWorldCookies() {
  const all = await chrome.cookies.getAll({ domain: "makerworld.com" });
  const byName = {};
  for (const c of all) {
    byName[c.name] = c.value;
  }
  // Prefer known names first, then include the rest (CF / session variants).
  const ordered = [];
  const seen = new Set();
  for (const name of COOKIE_NAMES) {
    if (byName[name]) {
      ordered.push(`${name}=${byName[name]}`);
      seen.add(name);
    }
  }
  for (const [name, value] of Object.entries(byName)) {
    if (!seen.has(name)) ordered.push(`${name}=${value}`);
  }
  return {
    cookieHeader: ordered.join("; "),
    map: byName,
    hasToken: Boolean(byName.token || byName.JWTtoken || byName.jwt),
  };
}

async function pushCookieIfNeeded(force = false) {
  const cfg = await getConfig();
  if (!cfg.enabled) {
    return { skipped: true, reason: "disabled" };
  }

  const { cookieHeader, map, hasToken } = await collectMakerWorldCookies();
  if (!cookieHeader || !hasToken) {
    const err = "No MakerWorld login cookie (token) found — log in on makerworld.com";
    await chrome.storage.sync.set({ lastError: err, lastPushOk: false });
    return { ok: false, error: err };
  }

  const h = hashString(cookieHeader);
  if (!force && h === cfg.lastHash) {
    return { ok: true, changed: false };
  }

  const url = cfg.pollerUrl.replace(/\/$/, "") + "/auth/cookie";
  try {
    const res = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${cfg.apiToken}`,
      },
      body: JSON.stringify({
        cookie: cookieHeader,
        cookies: map,
        source: "extension",
      }),
    });
    const text = await res.text();
    let body = {};
    try {
      body = JSON.parse(text);
    } catch {
      body = { raw: text };
    }
    if (!res.ok) {
      const err = `Poller HTTP ${res.status}: ${text.slice(0, 160)}`;
      await chrome.storage.sync.set({ lastError: err, lastPushOk: false });
      return { ok: false, error: err };
    }
    await chrome.storage.sync.set({
      lastHash: h,
      lastPushAt: Date.now(),
      lastPushOk: true,
      lastError: "",
    });
    return { ok: true, changed: true, body };
  } catch (e) {
    const err = `Poller unreachable: ${e.message}`;
    await chrome.storage.sync.set({ lastError: err, lastPushOk: false });
    return { ok: false, error: err };
  }
}

async function fetchPollerStatus() {
  const cfg = await getConfig();
  const url = cfg.pollerUrl.replace(/\/$/, "") + "/status";
  const res = await fetch(url, {
    headers: { Authorization: `Bearer ${cfg.apiToken}` },
  });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  (async () => {
    if (msg?.action === "pushNow") {
      sendResponse(await pushCookieIfNeeded(true));
      return;
    }
    if (msg?.action === "getStatus") {
      const cfg = await getConfig();
      let poller = null;
      try {
        poller = await fetchPollerStatus();
      } catch (e) {
        poller = { error: e.message };
      }
      sendResponse({ config: cfg, poller });
      return;
    }
    sendResponse({ error: "unknown action" });
  })();
  return true;
});

let debounceTimer = null;
chrome.cookies.onChanged.addListener((changeInfo) => {
  const c = changeInfo.cookie;
  if (!c || !c.domain || !c.domain.includes("makerworld.com")) return;
  if (debounceTimer) clearTimeout(debounceTimer);
  // Short in-memory debounce; periodic alarm covers SW restarts.
  debounceTimer = setTimeout(() => pushCookieIfNeeded(false), 1500);
});

chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name === "periodicPush") {
    pushCookieIfNeeded(false);
  }
});

chrome.alarms.create("periodicPush", { periodInMinutes: 30 });

// Push once on install/startup if already logged in.
chrome.runtime.onInstalled.addListener(() => pushCookieIfNeeded(true));
chrome.runtime.onStartup.addListener(() => pushCookieIfNeeded(false));
