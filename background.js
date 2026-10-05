/*
 * XVidGrab — background page.
 *
 * Responsibilities:
 *  - capture X GraphQL auth headers (webRequest) so missed responses can be
 *    replayed by the page-world sniffer or fetched on demand
 *  - per-tab video badge
 *  - download relay (browser.downloads) + local history
 *  - popup <-> content script message relay
 *
 * Everything stays on-device: no network calls except re-fetching X's own
 * GraphQL endpoints with the user's already-present session.
 */
"use strict";

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
  browser.webRequest.onBeforeSendHeaders.addListener(captureHeaders, CAPTURE_FILTER, ["requestHeaders", "extraHeaders"]);
} catch (e) {
  /* older Firefox builds without extraHeaders support */
  browser.webRequest.onBeforeSendHeaders.addListener(captureHeaders, CAPTURE_FILTER, ["requestHeaders"]);
}

browser.webRequest.onCompleted.addListener((details) => {
  if (details.tabId < 0 || details.type !== "xmlhttprequest" || !looksGraphql(details.url)) return;
  const h = headerMap.get(details.requestId) || {};
  headerMap.delete(details.requestId);

  let m = tabCaptures.get(details.tabId);
  if (!m) { m = new Map(); tabCaptures.set(details.tabId, m); }
  m.delete(details.url);
  m.set(details.url, { url: details.url, auth: h.auth || null, csrf: h.csrf || null, at: Date.now() });
  pruneTab(details.tabId);

  /* let the content script replay this URL through the page sniffer */
  browser.tabs.sendMessage(details.tabId, {
    type: "xvg-replay-url",
    url: details.url,
    auth: h.auth || null,
    csrf: h.csrf || null
  }).catch(() => { /* no content script on that tab */ });
}, CAPTURE_FILTER);

browser.webRequest.onErrorOccurred.addListener((details) => {
  headerMap.delete(details.requestId);
}, CAPTURE_FILTER);

/* ------------------------------------------------------------------ */
/* badge                                                               */
/* ------------------------------------------------------------------ */
try { browser.browserAction.setBadgeBackgroundColor({ color: "#1d9bf0" }); } catch (e) { /* ignore */ }

function setBadge(tabId, count) {
  const n = Number(count) || 0;
  const text = n > 0 ? (n > 99 ? "99+" : String(n)) : "";
  browser.browserAction.setBadgeText({ tabId, text }).catch(() => { /* tab gone */ });
}

browser.tabs.onRemoved.addListener((tabId) => {
  tabCaptures.delete(tabId);
});

browser.tabs.onUpdated.addListener((tabId, info) => {
  if (info.status === "loading") setBadge(tabId, 0);
});

/* ------------------------------------------------------------------ */
/* history                                                             */
/* ------------------------------------------------------------------ */
async function pushHistory(entry) {
  try {
    const s = await browser.storage.local.get({ history: [] });
    const h = Array.isArray(s.history) ? s.history.slice() : [];
    h.unshift({
      filename: entry.filename || "",
      url: entry.url || "",
      tweet: entry.tweet || null,
      downloadId: entry.downloadId || null,
      when: Date.now()
    });
    await browser.storage.local.set({ history: h.slice(0, 50) });
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
/* download progress (poll downloads.search, push to the source tab)   */
/* ------------------------------------------------------------------ */
const activePolls = new Map(); /* downloadId -> { tabId, timer, last } */

function sendProgress(tabId, payload) {
  if (tabId == null) return;
  browser.tabs.sendMessage(tabId, payload).catch(() => { /* tab gone */ });
}

function stopPoll(downloadId) {
  const p = activePolls.get(downloadId);
  if (!p) return;
  clearInterval(p.timer);
  activePolls.delete(downloadId);
}

function startPoll(downloadId, tabId) {
  if (tabId == null) return;
  const p = { tabId, timer: null, last: -1 };
  p.timer = setInterval(async () => {
    try {
      const items = await browser.downloads.search({ id: downloadId });
      const d = items && items[0];
      if (!d) { stopPoll(downloadId); return; }
      if (d.totalBytes > 0 && d.bytesReceived !== p.last) {
        p.last = d.bytesReceived;
        sendProgress(tabId, {
          type: "xvg-progress",
          downloadId,
          state: "progress",
          received: d.bytesReceived,
          total: d.totalBytes
        });
      }
      /* hard stop after 30 min */
      if (Date.now() - p.startedAt > 30 * 60 * 1000) stopPoll(downloadId);
    } catch (e) {
      stopPoll(downloadId);
    }
  }, 500);
  p.startedAt = Date.now();
  activePolls.set(downloadId, p);
}

browser.downloads.onChanged.addListener((delta) => {
  const p = activePolls.get(delta.id);
  if (!p) return;
  if (delta.state && delta.state.current === "complete") {
    sendProgress(p.tabId, { type: "xvg-progress", downloadId: delta.id, state: "done" });
    stopPoll(delta.id);
  } else if (delta.state && delta.state.current === "interrupted") {
    sendProgress(p.tabId, { type: "xvg-progress", downloadId: delta.id, state: "failed" });
    stopPoll(delta.id);
  }
});

/* ------------------------------------------------------------------ */
/* message router                                                      */
/* ------------------------------------------------------------------ */
browser.runtime.onMessage.addListener((msg, sender) => {
  if (!msg || typeof msg.type !== "string") return;

  switch (msg.type) {
    case "xvg-badge": {
      if (sender.tab && sender.tab.id != null) setBadge(sender.tab.id, msg.count);
      return;
    }

    case "xvg-download": {
      const tabId = sender.tab && sender.tab.id;
      const p = browser.downloads.download({
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
        return { ok: true, downloadId };
      }).catch((err) => {
        return { ok: false, error: String((err && err.message) || err) };
      });
      return p;
    }

    case "xvg-get-captured": {
      const tabId = sender.tab && sender.tab.id;
      const m = tabId != null ? tabCaptures.get(tabId) : null;
      const captures = m ? Array.from(m.values()).map((c) => ({ url: c.url, auth: c.auth, csrf: c.csrf })) : [];
      return Promise.resolve({ captures });
    }

    case "xvg-fetch-tweet": {
      const tabId = sender.tab && sender.tab.id;
      if (tabId == null || !msg.tweetId) return Promise.resolve({ ok: false, reason: "bad-request" });
      return fetchTweetJson(tabId, msg.tweetId);
    }

    case "xvg-get-videos": {
      /* popup -> active tab content script */
      return browser.tabs.query({ active: true, lastFocusedWindow: true }).then((tabs) => {
        const tab = tabs && tabs[0];
        if (!tab || tab.id == null) return { videos: [], error: "No active tab." };
        return browser.tabs.sendMessage(tab.id, { type: "xvg-get-videos" }).then(
          (res) => res || { videos: [] },
          () => ({ videos: [], error: "Open x.com and scroll a little first." })
        );
      });
    }

    case "xvg-show-download": {
      if (!msg.downloadId) return Promise.resolve({ ok: false });
      return browser.downloads.show(Number(msg.downloadId)).then(
        () => ({ ok: true }),
        () => ({ ok: false })
      );
    }

    case "xvg-clear-history": {
      return browser.storage.local.set({ history: [] }).then(() => ({ ok: true }));
    }
  }
  /* async replies handled via returned promises above */
});
