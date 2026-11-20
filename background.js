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

/* MV3 service workers have no DOM: pull the dependency-free parser in
 * explicitly. On Firefox MV2 the background page loads it via
 * background.scripts in manifest.firefox.json, so importScripts does not
 * exist here and XVG is already global. */
if (typeof importScripts === "function") {
  try { importScripts("content/sniff-parse.js"); } catch (e) { /* already loaded */ }
}

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
/* global media store                                                  */
/*                                                                     */
/* Every x.com tab pushes the records its sniffer collected, so the    */
/* popup can offer ONE merged list across all tabs, link-fetch jobs    */
/* included. In-memory only (a browser restart clears it — same TTL    */
/* behaviour as the per-tab stores).                                   */
/* ------------------------------------------------------------------ */
const MEDIA_TTL = 60 * 60 * 1000;
const MEDIA_MAX = 800;

const mediaStore = new Map(); /* key -> compact record */
let mediaRev = 0;

function pruneMedia() {
  const cutoff = Date.now() - MEDIA_TTL;
  for (const [key, rec] of mediaStore) {
    if (rec.at < cutoff) mediaStore.delete(key);
  }
  while (mediaStore.size > MEDIA_MAX) {
    mediaStore.delete(mediaStore.keys().next().value);
  }
}

function compactRecord(rec) {
  return {
    key: rec.key,
    tweetId: rec.tweetId || null,
    author: rec.author || null,
    text: rec.text ? String(rec.text).slice(0, 280) : null,
    type: rec.type || "video",
    thumbnail: rec.thumbnail || null,
    mediaIds: Array.isArray(rec.mediaIds) ? rec.mediaIds.slice(0, 4) : [],
    sources: (rec.sources || []).map((s) => ({
      url: s.url,
      kind: s.kind,
      label: s.label || (XVG && XVG.qualityLabel ? XVG.qualityLabel(s) : null),
      bitrate: s.bitrate || 0
    })),
    at: rec.at || Date.now()
  };
}

/* merge items into the global store; returns how many NEW records landed */
function mergeGlobalRecords(items) {
  let added = 0;
  for (const raw of items || []) {
    if (!raw || !raw.key || !Array.isArray(raw.sources) || !raw.sources.length) continue;
    const rec = compactRecord(raw);
    if (!rec.sources.length) continue;
    const existing = mediaStore.get(rec.key);
    if (existing) {
      const seen = new Set(existing.sources.map((s) => s.url));
      for (const s of rec.sources) {
        if (!seen.has(s.url)) { existing.sources.push(s); seen.add(s.url); }
      }
      existing.sources.sort((a, b) => sourceRank(b) - sourceRank(a));
      existing.tweetId = existing.tweetId || rec.tweetId;
      existing.author = existing.author || rec.author;
      existing.text = existing.text || rec.text;
      existing.thumbnail = existing.thumbnail || rec.thumbnail;
      existing.at = Date.now();
    } else {
      mediaStore.set(rec.key, rec);
      added += 1;
    }
  }
  if (mediaStore.size || added) {
    pruneMedia();
    if (added) mediaRev += 1;
  }
  return added;
}

function globalListForPopup() {
  const videos = [];
  for (const rec of mediaStore.values()) {
    if (!rec.sources.length) continue;
    videos.push(rec);
    if (videos.length >= 500) break;
  }
  return videos;
}

/* background-side download (records from the global store / syndication) */
async function downloadRecordDirect(rec, src) {
  const filename = (XVG && XVG.fileNameFor)
    ? XVG.fileNameFor(rec, src)
    : "xvidgrab_" + Date.now() + ".mp4";
  const url = await API.downloads.download({
    url: src.url,
    filename,
    conflictAction: "uniquify",
    saveAs: false
  }).catch((err) => { throw new Error(String((err && err.message) || err)); });
  pushHistory({ filename, url: src.url, tweet: rec.tweetId ? "https://x.com/i/status/" + rec.tweetId : null, downloadId: url });
  startPoll(url, null);
  return { ok: true, downloadId: url };
}

function sourceRank(s) {
  if (!s) return 0;
  if (s.kind === "mp4") {
    const tier = s.label ? (parseInt(String(s.label), 10) || 0) : 0;
    return 2e9 + (s.bitrate || 0) + tier * 1e3;
  }
  return 1e9;
}

/* ------------------------------------------------------------------ */
/* link-fetch job queue                                                */
/*                                                                     */
/* The popup collects X links (profile / tweet / search / any page).   */
/* Jobs run one at a time in a background tab: the page loads with the */
/* sniffer active, the scroll-to-end collector walks the timeline,     */
/* records stream into the global store, and (optionally) each video   */
/* is downloaded the moment it is found — exactly the "Run" flow, but  */
/* on a link the user never had to open themselves.                    */
/* ------------------------------------------------------------------ */
let jobSeq = 0;
const jobs = [];              /* newest last; processed FIFO */
let jobActive = false;
let jobStopRequested = false;
let jobTabId = null;          /* tab owned by the currently running job */

function jobPublic(j) {
  return {
    id: j.id,
    url: j.url,
    kind: j.kind,
    label: j.label,
    screenName: j.screenName || null,
    tweetId: j.tweetId || null,
    status: j.status,
    count: j.count || 0,
    error: j.error || null,
    autoDownload: !!j.autoDownload
  };
}

function jobsPublic() { return jobs.map(jobPublic); }

function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }

async function tabsSendMessageSafe(tabId, msg) {
  try { return await API.tabs.sendMessage(tabId, msg); }
  catch (e) { return null; }
}

/* wait until the tab's content script answers collector status calls */
async function waitForContent(tabId, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const r = await tabsSendMessageSafe(tabId, { type: "xvg-fetchall", action: "status" });
    if (r && r.ok) return true;
    await sleep(700);
  }
  return false;
}

/* one scroll-to-end collection pass on a page; returns videos collected */
async function collectPass(job, url, baseCount) {
  const tab = await API.tabs.create({ url, active: false });
  const tabId = tab && tab.id;
  if (tabId == null) throw new Error("could not open the page in a tab");
  job.tabId = tabId;
  jobTabId = tabId;
  try {
    const ready = await waitForContent(tabId, 45000);
    if (!ready) throw new Error("page did not load — log in to X or check the link");

    await tabsSendMessageSafe(tabId, {
      type: "xvg-fetchall", action: "start", autoDownload: !!job.autoDownload
    });

    const deadline = Date.now() + 13 * 60 * 1000;
    let st = null;
    let stopSent = false;
    while (Date.now() < deadline) {
      await sleep(1200);
      if (jobStopRequested && !stopSent) {
        stopSent = true;
        await tabsSendMessageSafe(tabId, { type: "xvg-fetchall", action: "stop" });
      }
      st = await tabsSendMessageSafe(tabId, { type: "xvg-fetchall", action: "status" });
      if (!st || !st.ok) throw new Error("collector lost — the page was closed or reloaded");
      job.count = baseCount + (Number(st.count) || 0);
      if (!st.running && st.finishedAt) break;
    }
    return Number(st && st.count) || 0;
  } finally {
    if (job.tabId === tabId) job.tabId = null;
    if (jobTabId === tabId) jobTabId = null;
    try { await API.tabs.remove(tabId); } catch (e) { /* user closed it */ }
  }
}

/* single-tweet jobs go through the public syndication embed API first —
 * no tab needed, instant. Falls back to a tab pass when it fails. */
async function tweetViaSyndication(job) {
  try {
    const n = Number(job.tweetId);
    const token = ((n / 1e15) * Math.PI).toString(36).substring(2, 12);
    const res = await fetch("https://cdn.syndication.twimg.com/tweet-result?id=" +
      encodeURIComponent(job.tweetId) + "&token=" + encodeURIComponent(token) + "&lang=en",
      { credentials: "omit" });
    if (!res.ok) return 0;
    const j = await res.json();
    const items = XVG.parseMedia({ tweetResult: { result: j } });
    if (!items.length) return 0;
    mergeGlobalRecords(items.map(compactRecord));
    if (job.autoDownload) {
      for (const rec of items) {
        const src = (rec.sources || []).filter((s) => s.kind === "mp4")[0] ||
          (rec.sources || [])[0];
        if (src) { try { await downloadRecordDirect(rec, src); } catch (e) { /* keep going */ } }
      }
    }
    return items.length;
  } catch (e) { return 0; }
}

async function runJob(job) {
  job.status = "running";
  job.startedAt = Date.now();
  job.error = null;
  job.foundCount = 0;
  try {
    if (job.kind === "tweet") {
      let n = await tweetViaSyndication(job);
      if (!n) n = await collectPass(job, job.url, 0);
      job.count = n;
    } else {
      const c1 = await collectPass(job, job.url, 0);
      let c2 = 0;
      if (!jobStopRequested && job.url2) {
        c2 = await collectPass(job, job.url2, c1);
      }
      job.count = c1 + c2;
    }
    /* prefer the exact number of NEW records this job contributed */
    if (job.foundCount > 0) job.count = job.foundCount;
    job.status = "done";
    if (!job.count) job.error = "no videos found on this page";
  } catch (e) {
    job.status = "failed";
    job.error = String((e && e.message) || e);
  } finally {
    if (job.tabId != null) {
      try { await API.tabs.remove(job.tabId); } catch (e) { /* gone */ }
      job.tabId = null;
    }
    jobTabId = null;
    job.finishedAt = Date.now();
  }
}

async function runJobsLoop() {
  if (jobActive) return;
  jobActive = true;
  jobStopRequested = false;
  try {
    while (!jobStopRequested) {
      const job = jobs.find((j) => j.status === "queued");
      if (!job) break;
      await runJob(job);
    }
  } finally {
    jobActive = false;
    jobStopRequested = false;
  }
}

function addJobs(urls, autoDownload) {
  const added = [];
  for (const raw of urls || []) {
    const plan = XVG.parseXLink(raw);
    if (!plan || !plan.ok) {
      added.push({ ok: false, input: String(raw), reason: (plan && plan.reason) || "invalid" });
      continue;
    }
    const kindLabel = plan.kind === "tweet" ? "Post"
      : plan.kind === "profile" ? "Profile"
      : plan.kind === "search" ? "Search" : "Page";
    const label = plan.kind === "profile" ? "@" + plan.screenName
      : plan.kind === "tweet" ? "post " + plan.tweetId
      : kindLabel;
    const dup = jobs.find((j) => j.url === plan.url && (j.status === "queued" || j.status === "running"));
    if (dup) { added.push({ ok: true, duplicate: true, job: jobPublic(dup) }); continue; }
    const job = {
      id: ++jobSeq,
      url: plan.url,
      url2: plan.url2 || null,
      kind: plan.kind,
      label,
      screenName: plan.screenName || null,
      tweetId: plan.tweetId || null,
      status: "queued",
      count: 0,
      error: null,
      autoDownload: autoDownload !== false,
      tabId: null,
      startedAt: 0,
      finishedAt: 0
    };
    jobs.push(job);
    while (jobs.length > 60) jobs.shift();
    added.push({ ok: true, job: jobPublic(job) });
  }
  return added;
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

    case "xvg-fetchall": {
      /* popup -> active tab content script: start/stop/status of the
       * scroll-to-end collector (search pages, timelines, ...). */
      API.tabs.query({ active: true, lastFocusedWindow: true }).then((tabs) => {
        const tab = tabs && tabs[0];
        if (!tab || tab.id == null) { sendResponse({ ok: false, error: "No active tab." }); return; }
        API.tabs.sendMessage(tab.id, { type: "xvg-fetchall", action: msg.action || "status" }).then(
          (res) => sendResponse(res || { ok: false }),
          () => sendResponse({ ok: false, error: "Open x.com first." })
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

    /* ---- global store + link jobs (v1.4.0) ---- */

    case "xvg-media-push": {
      /* content script -> background: new/updated records for the
       * one-merged-list popup view */
      const added = mergeGlobalRecords(msg.items);
      if (sender.tab && sender.tab.id != null && sender.tab.id === jobTabId) {
        const job = jobs.find((j) => j.status === "running");
        if (job) job.foundCount = (job.foundCount || 0) + added;
      }
      sendResponse({ ok: true, added });
      return;
    }

    case "xvg-get-videos-global": {
      sendResponse({ ok: true, videos: globalListForPopup(), rev: mediaRev });
      return;
    }

    case "xvg-download-source-global": {
      const rec = mediaStore.get(msg.key);
      if (!rec) { sendResponse({ ok: false, error: "record not found" }); return; }
      const src = (rec.sources || []).find((s) => s.url === msg.sourceUrl) ||
        (rec.sources || []).filter((s) => s.kind === "mp4")[0] ||
        (rec.sources || [])[0];
      if (!src) { sendResponse({ ok: false, error: "no source" }); return; }
      downloadRecordDirect(rec, src).then(sendResponse, (err) =>
        sendResponse({ ok: false, error: String((err && err.message) || err) }));
      return true;
    }

    case "xvg-jobs": {
      const action = msg.action || "status";
      if (action === "add") {
        const results = addJobs(msg.urls, msg.autoDownload);
        sendResponse({ ok: true, results, jobs: jobsPublic(), active: jobActive });
        if (results.some((r) => r.ok && !r.duplicate)) runJobsLoop();
        return;
      }
      if (action === "stop") {
        jobStopRequested = true;
        if (jobTabId != null) tabsSendMessageSafe(jobTabId, { type: "xvg-fetchall", action: "stop" });
        sendResponse({ ok: true, jobs: jobsPublic(), active: jobActive });
        return;
      }
      if (action === "clear") {
        for (let i = jobs.length - 1; i >= 0; i--) {
          if (jobs[i].status === "done" || jobs[i].status === "failed") jobs.splice(i, 1);
        }
        sendResponse({ ok: true, jobs: jobsPublic(), active: jobActive });
        return;
      }
      if (action === "remove") {
        const idx = jobs.findIndex((j) => j.id === Number(msg.id) && j.status === "queued");
        if (idx !== -1) jobs.splice(idx, 1);
        sendResponse({ ok: true, jobs: jobsPublic(), active: jobActive });
        return;
      }
      if (action === "resume") {
        sendResponse({ ok: true, jobs: jobsPublic(), active: jobActive });
        runJobsLoop();
        return;
      }
      /* status */
      sendResponse({ ok: true, jobs: jobsPublic(), active: jobActive });
      return;
    }
  }
  /* async replies handled via sendResponse + return true above */
});
