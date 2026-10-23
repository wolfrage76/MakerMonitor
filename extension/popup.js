const $ = (id) => document.getElementById(id);

async function load() {
  const cfg = await chrome.storage.sync.get([
    "pollerUrl",
    "apiToken",
    "enabled",
    "lastPushAt",
    "lastPushOk",
    "lastError",
  ]);
  $("pollerUrl").value = cfg.pollerUrl || "http://127.0.0.1:8787";
  $("apiToken").value = cfg.apiToken || "change-me";
  $("enabled").checked = cfg.enabled !== false;
  await refreshStatus();
}

async function save() {
  await chrome.storage.sync.set({
    pollerUrl: $("pollerUrl").value.trim().replace(/\/$/, ""),
    apiToken: $("apiToken").value,
    enabled: $("enabled").checked,
  });
  $("status").textContent = "Saved.";
  await refreshStatus();
}

function fmtTime(ts) {
  if (!ts) return "never";
  return new Date(ts).toLocaleString();
}

async function refreshStatus() {
  $("status").textContent = "Checking…";
  chrome.runtime.sendMessage({ action: "getStatus" }, (resp) => {
    if (!resp) {
      $("status").innerHTML = '<span class="bad">No response from background</span>';
      return;
    }
    const cfg = resp.config || {};
    const p = resp.poller || {};
    const lines = [];
    lines.push(`Last push: ${fmtTime(cfg.lastPushAt)} (${cfg.lastPushOk ? "ok" : "fail"})`);
    if (cfg.lastError) lines.push(`Push error: ${cfg.lastError}`);
    if (p.error) {
      lines.push(`Poller: unreachable (${p.error})`);
    } else {
      lines.push(`Poller cookie: ${p.cookie_present ? "yes" : "no"} age=${p.cookie_age_sec ?? "?"}s`);
      lines.push(`Auth OK: ${p.auth_ok}`);
      lines.push(`Last poll OK: ${p.last_poll_ok} age=${p.last_poll_age_sec ?? "?"}s`);
      if (p.last_error) lines.push(`Poll error: ${p.last_error}`);
      if (p.totals) {
        lines.push(
          `Totals DL=${p.totals.downloads} PR=${p.totals.prints} BST=${p.totals.boosts} PTS=${p.totals.points}`
        );
      }
      lines.push(`Panel reachable: ${p.panel_reachable}`);
    }
    $("status").textContent = lines.join("\n");
  });
}

$("save").addEventListener("click", save);
$("refresh").addEventListener("click", refreshStatus);
$("push").addEventListener("click", () => {
  $("status").textContent = "Pushing cookie…";
  chrome.runtime.sendMessage({ action: "pushNow" }, (resp) => {
    if (!resp) {
      $("status").innerHTML = '<span class="bad">No response</span>';
      return;
    }
    if (resp.ok) {
      $("status").textContent = resp.changed
        ? "Cookie pushed (changed)."
        : "Cookie pushed (unchanged / forced).";
    } else {
      $("status").textContent = resp.error || JSON.stringify(resp);
    }
    setTimeout(refreshStatus, 500);
  });
});

load();
