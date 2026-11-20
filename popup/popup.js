/* XVidGrab popup logic */
"use strict";

const api = globalThis.browser || globalThis.chrome;
const el = (id) => document.getElementById(id);

const manifest = api.runtime.getManifest();
el("version").textContent = "v" + manifest.version;

/* ---------------- settings ---------------- */
const SETTINGS_DEFAULTS = { quality: "best", showButtons: true };

api.storage.local.get(SETTINGS_DEFAULTS).then((s) => {
  el("quality").value = s.quality === "smallest" ? "smallest" : "best";
  el("showButtons").checked = s.showButtons !== false;
}).catch(() => {});

el("quality").addEventListener("change", () => {
  api.storage.local.set({ quality: el("quality").value }).catch(() => {});
});

el("showButtons").addEventListener("change", () => {
  api.storage.local.set({ showButtons: el("showButtons").checked }).catch(() => {});
});

/* ---------------- video list (global: all tabs + link jobs) ---------------- */
let currentVideos = [];
let lastMediaRev = -1;

async function refreshVideos(force) {
  let res = null;
  try {
    res = await api.runtime.sendMessage({ type: "xvg-get-videos-global" });
  } catch (e) { /* background unavailable */ }
  if (!res || !res.ok) {
    renderVideos([], "Could not reach the extension background — reopen the popup.");
    return;
  }
  if (!force && res.rev === lastMediaRev && currentVideos.length === (res.videos || []).length) return;
  lastMediaRev = res.rev;
  renderVideos(res.videos || []);
}

refreshVideos(true);
setInterval(() => refreshVideos(false), 1200);

function thumbRow(v) {
  const row = document.createElement("div");
  row.className = "video";

  const img = document.createElement("img");
  img.className = "video-thumb";
  img.alt = "";
  if (v.thumbnail) img.src = v.thumbnail;

  const body = document.createElement("div");
  body.className = "video-body";

  const author = document.createElement("div");
  author.className = "video-author";
  author.textContent = v.author ? "@" + v.author : (v.tweetId ? "post " + v.tweetId : "detected video");

  const text = document.createElement("div");
  text.className = "video-text";
  text.textContent = v.text || (v.type === "gif" ? "Animated GIF" : "Video");

  const foot = document.createElement("div");
  foot.className = "video-foot";

  const sel = document.createElement("select");
  const mp4s = (v.sources || []).filter((s) => s.kind === "mp4");
  for (const s of (mp4s.length ? mp4s : v.sources || [])) {
    const opt = document.createElement("option");
    opt.value = s.url;
    opt.textContent = s.label + (s.kind === "hls" ? " (stream)" : "");
    sel.appendChild(opt);
  }

  const btn = document.createElement("button");
  btn.className = "btn primary";
  btn.style.width = "auto";
  btn.style.padding = "6px 14px";
  btn.textContent = "Download";
  btn.disabled = !v.hasMp4;
  btn.addEventListener("click", async () => {
    btn.disabled = true;
    btn.textContent = "Saving…";
    const res = await api.runtime.sendMessage({
      type: "xvg-download-source-global",
      key: v.key,
      sourceUrl: sel.value || null
    }).catch((err) => ({ ok: false, error: String(err) }));
    btn.textContent = res && res.ok ? "Saved ✓" : "Failed";
    setTimeout(() => {
      btn.textContent = "Download";
      btn.disabled = !v.hasMp4;
    }, 2200);
  });

  const badge = document.createElement("span");
  badge.className = "badge";
  badge.textContent = v.type === "gif" ? "GIF" : "video";

  foot.appendChild(sel);
  foot.appendChild(btn);
  foot.appendChild(badge);
  body.appendChild(author);
  body.appendChild(text);
  body.appendChild(foot);
  row.appendChild(img);
  row.appendChild(body);
  return row;
}

function renderVideos(videos, error) {
  const box = el("videos");
  box.textContent = "";
  currentVideos = Array.isArray(videos) ? videos : [];

  if (!currentVideos.length) {
    const empty = document.createElement("div");
    empty.className = "empty";
    empty.textContent = error ||
      "No videos collected yet. Add links above, or open x.com and scroll past a post with a video.";
    box.appendChild(empty);
    el("bulkRow").hidden = true;
    el("status").textContent = "Waiting for videos…";
    return;
  }

  el("status").textContent = currentVideos.length + " video" +
    (currentVideos.length > 1 ? "s" : "") + " collected (all tabs + link jobs)";

  for (const v of currentVideos) box.appendChild(thumbRow(v));
  el("bulkRow").hidden = !currentVideos.some((v) => v.hasMp4);
}

/* ---------------- fetch all / run (auto-scroll to the end) ---------------- */
let fetchTimer = null;
let fetchLastRendered = -1;
let runLogShown = 0; /* how many log lines already rendered, so we only append */

function setFetchButtons(running) {
  el("runAll").disabled = running;
  el("fetchAll").disabled = running;
  el("fetchStop").hidden = !running;
}

function renderRunLog(lines) {
  const box = el("runLog");
  if (!lines || !lines.length) { box.hidden = true; return; }
  box.hidden = false;
  /* only append new lines - keeps the log from re-flowing/jumping scroll
   * position on every 800ms poll while a run is going */
  for (let i = runLogShown; i < lines.length; i++) {
    const line = lines[i];
    const div = document.createElement("div");
    if (/^\S+\s+FAILED /.test(line)) div.className = "fail";
    else if (/^\S+\s+Saved /.test(line)) div.className = "ok";
    div.textContent = line;
    box.appendChild(div);
  }
  runLogShown = lines.length;
  box.scrollTop = box.scrollHeight;
}

function startFetchPolling() {
  setFetchButtons(true);
  runLogShown = 0;
  el("runLog").textContent = "";
  if (fetchTimer) clearInterval(fetchTimer);
  fetchTimer = setInterval(pollFetchAll, 800);
  pollFetchAll();
}

function stopFetchPolling(note) {
  if (fetchTimer) { clearInterval(fetchTimer); fetchTimer = null; }
  setFetchButtons(false);
  if (note) el("fetchStatus").textContent = note;
}

async function pollFetchAll() {
  let res = null;
  try {
    res = await api.runtime.sendMessage({ type: "xvg-fetchall", action: "status" });
  } catch (e) { /* popup closing */ }
  if (!res || !res.ok) { stopFetchPolling("Could not reach the page — is x.com open?"); return; }
  if (res.autoDownload) renderRunLog(res.log);
  if (res.running) {
    el("fetchStatus").textContent = res.autoDownload
      ? "Running… " + res.count + " found, " + res.downloadedCount + " downloaded" +
        (res.failedCount ? ", " + res.failedCount + " failed" : "") +
        " · scroll round " + res.round
      : "Collecting… " + res.count + " videos · scroll round " + res.round +
        " · height " + res.scrollHeight + "px";
    if (res.count !== fetchLastRendered && res.count > 0) {
      fetchLastRendered = res.count;
      refreshVideos();
    }
    return;
  }
  /* finished (or idle) */
  if (res.finishedAt && res.autoDownload) {
    stopFetchPolling("Run finished — " + res.downloadedCount + " downloaded" +
      (res.failedCount ? ", " + res.failedCount + " failed" : "") + ".");
  } else {
    stopFetchPolling(res.finishedAt
      ? "Done — " + res.count + " videos collected. Download below, or click Run next time to auto-download."
      : "Run scrolls the page and downloads every video it finds, as it finds them. You can still click any video's own Download button at any time, during or after a run.");
  }
  if (res.finishedAt) refreshVideos();
}

el("runAll").addEventListener("click", async () => {
  el("fetchStatus").textContent = "Starting… keep this tab in the foreground.";
  let res = null;
  try {
    res = await api.runtime.sendMessage({ type: "xvg-fetchall", action: "start", autoDownload: true });
  } catch (e) { /* ignore */ }
  if (res && res.ok) startFetchPolling();
  else el("fetchStatus").textContent = "Could not start — open x.com first.";
});

el("fetchAll").addEventListener("click", async () => {
  el("fetchStatus").textContent = "Starting… keep this tab in the foreground.";
  let res = null;
  try {
    res = await api.runtime.sendMessage({ type: "xvg-fetchall", action: "start" });
  } catch (e) { /* ignore */ }
  if (res && res.ok) startFetchPolling();
  else el("fetchStatus").textContent = "Could not start — open x.com first.";
});

el("fetchStop").addEventListener("click", async () => {
  try { await api.runtime.sendMessage({ type: "xvg-fetchall", action: "stop" }); } catch (e) { /* ignore */ }
  el("fetchStatus").textContent = "Stopping after the current round…";
});

/* popup may reopen mid-run: resume the live counter */
(async () => {
  let res = null;
  try {
    res = await api.runtime.sendMessage({ type: "xvg-fetchall", action: "status" });
  } catch (e) { /* ignore */ }
  if (res && res.ok && res.running) startFetchPolling();
})();

el("downloadAll").addEventListener("click", async () => {
  const btn = el("downloadAll");
  btn.disabled = true;
  const targets = currentVideos.filter((v) => v.hasMp4);
  let done = 0;
  for (const v of targets) {
    const best = (v.sources || []).filter((s) => s.kind === "mp4")[0];
    if (!best) continue;
    const res = await api.runtime.sendMessage({
      type: "xvg-download-source-global",
      key: v.key,
      sourceUrl: best.url
    }).catch(() => ({ ok: false }));
    if (res && res.ok) done += 1;
  }
  btn.textContent = "Started " + done + " download" + (done === 1 ? "" : "s");
  setTimeout(() => {
    btn.textContent = "Download all";
    btn.disabled = false;
  }, 2600);
});

/* ---------------- link jobs (fetch from links) ---------------- */
const KIND_CHIP = { profile: "PROFILE", tweet: "POST", search: "SEARCH", page: "PAGE" };

async function pollJobs() {
  let res = null;
  try {
    res = await api.runtime.sendMessage({ type: "xvg-jobs", action: "status" });
  } catch (e) { /* background unavailable */ }
  renderQueue(res);
}

function renderQueue(res) {
  const list = el("queue");
  const jobs = (res && res.jobs) || [];
  const active = !!(res && res.active);
  const hasDone = jobs.some((j) => j.status === "done" || j.status === "failed");
  const hasQueued = jobs.some((j) => j.status === "queued");

  el("queueEmpty").hidden = jobs.length > 0;
  list.hidden = jobs.length === 0;
  list.textContent = "";

  for (const j of jobs) {
    const li = document.createElement("li");
    li.className = "q-row st-" + j.status;

    const chip = document.createElement("span");
    chip.className = "q-chip";
    chip.textContent = KIND_CHIP[j.kind] || "PAGE";

    const name = document.createElement("span");
    name.className = "q-name";
    name.textContent = j.label || j.url;
    name.title = j.url + (j.error ? " — " + j.error : "");

    const st = document.createElement("span");
    st.className = "q-status";
    if (j.status === "queued") st.textContent = "waiting";
    else if (j.status === "running") st.textContent = "fetching… " + (j.count || 0) + " found";
    else if (j.status === "done") st.textContent = (j.error ? "done — " + j.error : "done — " + j.count + " video" + (j.count === 1 ? "" : "s"));
    else st.textContent = "failed — " + (j.error || "error");

    li.appendChild(chip);
    li.appendChild(name);
    li.appendChild(st);

    if (j.status === "queued") {
      const rm = document.createElement("button");
      rm.className = "q-remove";
      rm.textContent = "x";
      rm.title = "Remove from queue";
      rm.addEventListener("click", async () => {
        try { await api.runtime.sendMessage({ type: "xvg-jobs", action: "remove", id: j.id }); } catch (e) { /* ignore */ }
        pollJobs();
      });
      li.appendChild(rm);
    }

    list.appendChild(li);
  }

  const running = jobs.filter((j) => j.status === "running").length;
  const queued = jobs.filter((j) => j.status === "queued").length;
  const foundTotal = jobs.reduce((n, j) => n + (j.status === "done" ? (j.count || 0) : 0), 0);

  let statusText = "";
  if (active && running) statusText = "Fetching " + (running + queued) + " page" + ((running + queued) === 1 ? "" : "s") + " in the background…";
  else if (queued) statusText = queued + " link" + (queued === 1 ? "" : "s") + " waiting — press Fetch";
  else if (hasDone) statusText = "Done — " + foundTotal + " video" + (foundTotal === 1 ? "" : "s") + " collected in total";
  el("queueStatus").textContent = statusText;

  el("queueStop").hidden = !active;
  el("queueFetch").hidden = active || !hasQueued;
  el("queueClear").hidden = !hasDone || active;
}

el("queueStop").addEventListener("click", async () => {
  try { await api.runtime.sendMessage({ type: "xvg-jobs", action: "stop" }); } catch (e) { /* ignore */ }
  el("queueStatus").textContent = "Stopping after the current page…";
  pollJobs();
});

el("queueFetch").addEventListener("click", async () => {
  try { await api.runtime.sendMessage({ type: "xvg-jobs", action: "resume" }); } catch (e) { /* ignore */ }
  pollJobs();
});

el("queueClear").addEventListener("click", async () => {
  try { await api.runtime.sendMessage({ type: "xvg-jobs", action: "clear" }); } catch (e) { /* ignore */ }
  pollJobs();
});

/* ---------------- add-links dialog ---------------- */
function openLinksModal() {
  el("addLinksModal").hidden = false;
  el("linksError").hidden = true;
  el("linksInput").value = "";
  el("linksInput").focus();
}

function closeLinksModal() {
  el("addLinksModal").hidden = true;
}

function showLinksError(msg) {
  const box = el("linksError");
  box.textContent = msg;
  box.hidden = false;
}

el("addLinksBtn").addEventListener("click", openLinksModal);
el("linksCancel").addEventListener("click", closeLinksModal);

api.storage.local.get({ jobAutoDownload: true }).then((s) => {
  el("jobAutoDl").checked = s.jobAutoDownload !== false;
}).catch(() => {});

async function submitLinks() {
  const raw = el("linksInput").value || "";
  const urls = (window.XVG && XVG.extractLinks) ? XVG.extractLinks(raw) : [];
  if (!urls.length) {
    showLinksError("No x.com links found — paste links like https://x.com/username");
    return;
  }
  const autoDownload = el("jobAutoDl").checked;
  api.storage.local.set({ jobAutoDownload: autoDownload }).catch(() => {});

  let res = null;
  try {
    res = await api.runtime.sendMessage({
      type: "xvg-jobs", action: "add", urls, autoDownload
    });
  } catch (e) { /* ignore */ }

  if (!res || !res.ok) {
    showLinksError("Could not add links — reopen the popup and try again.");
    return;
  }

  const bad = (res.results || []).filter((r) => !r.ok);
  if (bad.length) {
    showLinksError("Skipped " + bad.length + " link" + (bad.length === 1 ? "" : "s") +
      " (not x.com links): " + bad.slice(0, 3).map((b) => b.input).join(", "));
    setTimeout(closeLinksModal, 1800);
    pollJobs();
    return;
  }

  closeLinksModal();
  el("queueStatus").textContent = "Added " + (res.results || []).length +
    " link" + ((res.results || []).length === 1 ? "" : "s") + " — fetching in the background…";
  pollJobs();
}

el("linksAddFetch").addEventListener("click", submitLinks);
el("linksInput").addEventListener("keydown", (ev) => {
  if ((ev.ctrlKey || ev.metaKey) && ev.key === "Enter") submitLinks();
});

pollJobs();
setInterval(pollJobs, 900);

/* ---------------- history ---------------- */
function timeAgo(ts) {
  const s = Math.max(1, Math.round((Date.now() - ts) / 1000));
  if (s < 60) return s + "s ago";
  if (s < 3600) return Math.round(s / 60) + "m ago";
  if (s < 86400) return Math.round(s / 3600) + "h ago";
  return Math.round(s / 86400) + "d ago";
}

function renderHistory(items) {
  const ul = el("history");
  ul.textContent = "";
  if (!items || !items.length) {
    const li = document.createElement("li");
    li.className = "none";
    li.textContent = "Nothing downloaded yet.";
    ul.appendChild(li);
    return;
  }
  for (const it of items.slice(0, 8)) {
    const li = document.createElement("li");
    const name = document.createElement("span");
    name.className = "h-name";
    name.textContent = it.filename || it.url;
    name.title = (it.filename || it.url) + (it.when ? " — " + timeAgo(it.when) : "");
    const show = document.createElement("button");
    show.className = "h-show";
    show.textContent = it.when ? timeAgo(it.when) : "Show";
    if (it.downloadId) {
      show.addEventListener("click", () => {
        api.runtime.sendMessage({ type: "xvg-show-download", downloadId: it.downloadId }).catch(() => {});
      });
    } else {
      show.disabled = true;
    }
    li.appendChild(name);
    li.appendChild(show);
    ul.appendChild(li);
  }
}

api.storage.local.get({ history: [] }).then((s) => renderHistory(s.history)).catch(() => {});
