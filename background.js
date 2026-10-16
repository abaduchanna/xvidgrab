/*
 * XVidGrab — background (works as MV2 page in Firefox AND MV3 service
 * worker in Chromium browsers: Chrome, Edge, Brave, Opera, Vivaldi...).
 *
 * Responsibilities:
 *  - capture X GraphQL auth headers (webRequest) so missed responses can be
 *    replayed by the page-world sniffer or fetched on demand
 *  - per-tab video badge
 *  - download relay (downloads API) + local history
 *  - popup <-> content script message relay
 *
 * Everything stays on-device: no network calls except re-fetching X's own
 * GraphQL endpoints with the user's already-present session.
 */
"use strict";

const API = globalThis.browser || globalThis.chrome;
const BADGE_API = API.action || API.browserAction || API.action;

const GRAPHQL_TTL = 10 * 60 * 1000;   /* 10 min */
const GRAPHQL_MAX = 40;               /* per tab */
const CAPTURE_FILTER = { urls: ["https://x.com/i/api/*"] };

/** requestId -> { auth, csrf } */
const headerMap = new Map();
/** tabId -> Map(url -> { url, auth, csrf, at }) */
const tabCaptures = new Map();

function looksGraphql(url) {
  return typeof url === "string" && url.indexOf("/graphql/") !== -1;
}

function pruneTab(tabId) {
  const m = tabCaptures.get(tabId);
  if (!m) return;
  const cutoff = Date.now() - GRAPHQL_TTL;
  for (const [url, cap] of m) {
    if (cap.at < cutoff) m.delete(url);
  }
  while (m.size > GRAPHQL_MAX) {
    const first = m.keys().next().value;
    m.delete(first);
  }
}

function captureHeaders(details) {
  if (details.tabId < 0 || details.type !== "xmlhttprequest" || !looksGraphql(details.url)) return;
  const h = {};
  for (const hd of details.requestHeaders || []) {
    const n = hd.name.toLowerCase();
    if (n === "authorization") h.auth = hd.value;
    else if (n === "x-csrf-token") h.csrf = hd.value;
  }
  if (h.auth || h.csrf) headerMap.set(details.requestId, h);
}

try {
  API.webRequest.onBeforeSendHeaders.addListener(captureHeaders, CAPTURE_FILTER, ["requestHeaders", "extraHeaders"]);
} catch (e) {
  /* older builds without extraHeaders support */
  API.webRequest.onBeforeSendHeaders.addListener(captureHeaders, CAPTURE_FILTER, ["requestHeaders"]);
}

API.webRequest.onCompleted.addListener((details) => {
  if (details.tabId < 0 || details.type !== "xmlhttprequest" || !looksGraphql(details.url)) return;
  const h = headerMap.get(details.requestId) || {};
  headerMap.delete(details.requestId);

  let m = tabCaptures.get(details.tabId);
  if (!m) { m = new Map(); tabCaptures.set(details.tabId, m); }
  m.delete(details.url);
  m.set(details.url, { url: details.url, auth: h.auth || null, csrf: h.csrf || null, at: Date.now() });
  pruneTab(details.tabId);

  /* let the content script replay this URL through the page sniffer */
  API.tabs.sendMessage(details.tabId, {
    type: "xvg-replay-url",
    url: details.url,
    auth: h.auth || null,
    csrf: h.csrf || null
  }).catch(() => { /* no content script on that tab */ });
}, CAPTURE_FILTER);

API.webRequest.onErrorOccurred.addListener((details) => {
  headerMap.delete(details.requestId);
}, CAPTURE_FILTER);

/* ------------------------------------------------------------------ */
/* badge                                                               */
/* ------------------------------------------------------------------ */
try { BADGE_API.setBadgeBackgroundColor({ color: "#1d9bf0" }); } catch (e) { /* ignore */ }

function setBadge(tabId, count) {
  const n = Number(count) || 0;
  const text = n > 0 ? (n > 99 ? "99+" : String(n)) : "";
  try {
    const r = BADGE_API.setBadgeText({ tabId, text });
    if (r && r.catch) r.catch(() => { /* tab gone */ });
  } catch (e) { /* tab gone */ }
}

API.tabs.onRemoved.addListener((tabId) => {
  tabCaptures.delete(tabId);
});

API.tabs.onUpdated.addListener((tabId, info) => {
  if (info.status === "loading") setBadge(tabId, 0);
});

/* ------------------------------------------------------------------ */
/* history                                                             */
/* ------------------------------------------------------------------ */
async function pushHistory(entry) {
  try {
    const s = await API.storage.local.get({ history: [] });
    const h = Array.isArray(s.history) ? s.history.slice() : [];
    h.unshift({
      filename: entry.filename || "",
      url: entry.url || "",
      tweet: entry.tweet || null,
      downloadId: entry.downloadId || null,
      when: Date.now()
    });
    await API.storage.local.set({ history: h.slice(0, 50) });
  } catch (e) { /* storage may be unavailable on first run */ }
}

/* ------------------------------------------------------------------ */
/* on-demand tweet fetch (fallback when the sniffer missed a post)     */
/* ------------------------------------------------------------------ */
async function fetchTweetJson(tabId, tweetId) {
  const m = tabCaptures.get(tabId);
  if (!m || !m.size) return { ok: false, reason: "no-captured-urls" };

  let target = null;
  for (const cap of m.values()) {
    if (/\/TweetResultByRestId/i.test(cap.url)) { target = cap; break; }
  }
  if (!target) {
    for (const cap of m.values()) {
      if (/\/TweetDetail/i.test(cap.url)) { target = cap; break; }
    }
  }
  if (!target) return { ok: false, reason: "no-relevant-captured-urls" };

  let url = target.url;
  try {
    const u = new URL(target.url);
    if (/\/TweetResultByRestId/i.test(u.pathname)) {
      let features = null;
      try { features = JSON.parse(u.searchParams.get("features") || "null"); } catch (e) { /* none */ }
      const vars = {
        tweetId: String(tweetId),
        withCommunity: false,
        includePromotedContent: false,
        withVoice: false
      };
      const q = new URLSearchParams();
      q.set("variables", JSON.stringify(vars));
      if (features) q.set("features", JSON.stringify(features));
      url = u.origin + u.pathname + "?" + q.toString();
    }
  } catch (e) { /* fall back to raw captured URL */ }

  const headers = { "x-twitter-active-user": "yes", accept: "application/json" };
  if (target.auth) headers.authorization = target.auth;
  if (target.csrf) headers["x-csrf-token"] = target.csrf;

  try {
    const res = await fetch(url, { headers, credentials: "include" });
    if (!res.ok) return { ok: false, reason: "http-" + res.status };
    const json = await res.json();
    return { ok: true, json };
  } catch (e) {
    return { ok: false, reason: "fetch-failed" };
  }
}

/* ------------------------------------------------------------------ */
/* download progress                                                   */
/*                                                                   */
/* MV3 service workers can kill setInterval timers, so progress is    */
/* driven purely by downloads.onChanged (bytesReceived deltas), which */
/* fires in BOTH MV2 pages and MV3 service workers.                   */
/* ------------------------------------------------------------------ */
const activePolls = new Map(); /* downloadId -> { tabId, startedAt, last } */

function sendProgress(tabId, payload) {
  if (tabId == null) return;
  try {
    const r = API.tabs.sendMessage(tabId, payload);
    if (r && r.catch) r.catch(() => { /* tab gone */ });
  } catch (e) { /* tab gone */ }
}

function stopPoll(downloadId) {
  activePolls.delete(downloadId);
}

function startPoll(downloadId, tabId) {
  if (tabId == null) return;
  activePolls.set(downloadId, { tabId, startedAt: Date.now(), last: 0 });
}

API.downloads.onChanged.addListener((delta) => {
  const p = activePolls.get(delta.id);
  if (!p) return;
  if (delta.state && delta.state.current === "complete") {
    sendProgress(p.tabId, { type: "xvg-progress", downloadId: delta.id, state: "done" });
    stopPoll(delta.id);
    return;
  }
  if (delta.state && delta.state.current === "interrupted") {
    sendProgress(p.tabId, { type: "xvg-progress", downloadId: delta.id, state: "failed" });
    stopPoll(delta.id);
    return;
  }
  if (delta.bytesReceived && delta.bytesReceived.current !== p.last) {
    p.last = delta.bytesReceived.current;
    sendProgress(p.tabId, {
      type: "xvg-progress",
      downloadId: delta.id,
      state: "progress",
      received: p.last,
      total: delta.totalBytes ? delta.totalBytes.current : 0
    });
  }
  /* hard stop after 30 min */
  if (Date.now() - p.startedAt > 30 * 60 * 1000) stopPoll(delta.id);
});

/* ------------------------------------------------------------------ */
/* message router (callback-style sendResponse: identical behaviour in */
/* Firefox MV2 and Chromium MV3)                                       */
/* ------------------------------------------------------------------ */
API.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (!msg || typeof msg.type !== "string") return;

  switch (msg.type) {
    case "xvg-badge": {
      if (sender.tab && sender.tab.id != null) setBadge(sender.tab.id, msg.count);
      return;
    }

    case "xvg-download": {
      const tabId = sender.tab && sender.tab.id;
      API.downloads.download({
        url: msg.url,
        filename: msg.filename || undefined,
        conflictAction: "uniquify",
        saveAs: false
      }).then((downloadId) => {
        pushHistory({
          filename: msg.filename || "",
          url: msg.url,
          tweet: msg.tweet || null,
          downloadId
        });
        startPoll(downloadId, tabId);
        sendResponse({ ok: true, downloadId });
      }).catch((err) => {
        sendResponse({ ok: false, error: String((err && err.message) || err) });
      });
      return true;
    }

    case "xvg-get-captured": {
      const tabId = sender.tab && sender.tab.id;
      const m = tabId != null ? tabCaptures.get(tabId) : null;
      const captures = m ? Array.from(m.values()).map((c) => ({ url: c.url, auth: c.auth, csrf: c.csrf })) : [];
      sendResponse({ captures });
      return;
    }

    case "xvg-fetch-tweet": {
      const tabId = sender.tab && sender.tab.id;
      if (tabId == null || !msg.tweetId) { sendResponse({ ok: false, reason: "bad-request" }); return; }
      fetchTweetJson(tabId, msg.tweetId).then(sendResponse);
      return true;
    }

    case "xvg-get-videos": {
      /* popup -> active tab content script */
      API.tabs.query({ active: true, lastFocusedWindow: true }).then((tabs) => {
        const tab = tabs && tabs[0];
        if (!tab || tab.id == null) { sendResponse({ videos: [], error: "No active tab." }); return; }
        API.tabs.sendMessage(tab.id, { type: "xvg-get-videos" }).then(
          (res) => sendResponse(res || { videos: [] }),
          () => sendResponse({ videos: [], error: "Open x.com and scroll a little first." })
        );
      });
      return true;
    }

    case "xvg-show-download": {
      if (!msg.downloadId) { sendResponse({ ok: false }); return; }
      API.downloads.show(Number(msg.downloadId)).then(
        () => sendResponse({ ok: true }),
        () => sendResponse({ ok: false })
      );
      return true;
    }

    case "xvg-clear-history": {
      API.storage.local.set({ history: [] }).then(() => sendResponse({ ok: true }));
      return true;
    }
  }
  /* async replies handled via sendResponse + return true above */
});
