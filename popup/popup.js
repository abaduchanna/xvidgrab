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

/* ---------------- video list ---------------- */
let currentVideos = [];

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
      type: "xvg-download-source",
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
      "No videos detected yet. Open x.com, scroll past a post with a video, then reopen this popup.";
    box.appendChild(empty);
    el("bulkRow").hidden = true;
    el("status").textContent = "Waiting for videos…";
    return;
  }

  el("status").textContent = currentVideos.length + " video" +
    (currentVideos.length > 1 ? "s" : "") + " detected on this tab";

  for (const v of currentVideos) box.appendChild(thumbRow(v));
  el("bulkRow").hidden = !currentVideos.some((v) => v.hasMp4);
}

api.runtime.sendMessage({ type: "xvg-get-videos" }).then(
  (res) => renderVideos(res && res.videos, res && res.error),
  () => renderVideos([], "Could not reach the active tab. Open x.com and try again.")
);

el("downloadAll").addEventListener("click", async () => {
  const btn = el("downloadAll");
  btn.disabled = true;
  const targets = currentVideos.filter((v) => v.hasMp4);
  let done = 0;
  for (const v of targets) {
    const best = (v.sources || []).filter((s) => s.kind === "mp4")[0];
    if (!best) continue;
    const res = await api.runtime.sendMessage({
      type: "xvg-download-source",
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
