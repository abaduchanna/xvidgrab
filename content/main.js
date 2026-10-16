/*
 * XVidGrab — content script (isolated world).
 *
 *  - keeps a per-tab store of media records published by the page sniffer
 *  - injects a native-feeling download button into tweet action rows
 *  - downloads via the downloads API (background relay) with live progress
 *  - replays GraphQL URLs the background captured while we were not yet
 *    injected (late install / reload / bfcache)
 *  - answers popup queries for the active tab's video list
 */
(function () {
  "use strict";

  const api = globalThis.browser || globalThis.chrome;

  /* ---------------------------------------------------------------- */
  /* state                                                            */
  /* ---------------------------------------------------------------- */
  const RECORD_TTL = 15 * 60 * 1000;
  const RECORD_MAX = 150;

  const store = new Map();          /* key -> record */
  const btnByArticle = new WeakMap();
  const replaySent = new Set();     /* urls already handed to the sniffer */
  const resolving = new Set();      /* tweetIds currently being resolved */

  const settings = { quality: "best", showButtons: true };

  function pruneStore() {
    const cutoff = Date.now() - RECORD_TTL;
    for (const [key, rec] of store) {
      if (rec.at < cutoff) store.delete(key);
    }
    while (store.size > RECORD_MAX) {
      store.delete(store.keys().next().value);
    }
  }

  function ingest(items) {
    let changed = false;
    for (const rec of items || []) {
      if (!rec || !rec.key || !Array.isArray(rec.sources) || !rec.sources.length) continue;
      const existing = store.get(rec.key);
      if (existing) {
        const seen = new Set(existing.sources.map((s) => s.url));
        for (const s of rec.sources) if (!seen.has(s.url)) existing.sources.push(s);
        existing.sources = XVG.sortSources(existing.sources.slice());
        existing.tweetId = existing.tweetId || rec.tweetId;
        existing.author = existing.author || rec.author;
        existing.text = existing.text || rec.text;
        existing.thumbnail = existing.thumbnail || rec.thumbnail;
        existing.at = Date.now();
      } else {
        store.set(rec.key, Object.assign({ at: Date.now() }, rec));
      }
      changed = true;
    }
    if (changed) {
      pruneStore();
      updateBadge();
      scheduleScan();
    }
  }

  function updateBadge() {
    try {
      api.runtime.sendMessage({ type: "xvg-badge", count: store.size }).catch(() => {});
    } catch (e) { /* extension reloading */ }
  }

  /* ---------------------------------------------------------------- */
  /* settings                                                         */
  /* ---------------------------------------------------------------- */
  const DEFAULTS = { quality: "best", showButtons: true };
  api.storage.local.get(DEFAULTS).then((s) => {
    settings.quality = s.quality === "smallest" ? "smallest" : "best";
    settings.showButtons = s.showButtons !== false;
    applySettings();
  }).catch(() => {});

  api.storage.onChanged.addListener((changes, area) => {
    if (area !== "local") return;
    if (changes.quality) settings.quality = changes.quality.newValue === "smallest" ? "smallest" : "best";
    if (changes.showButtons) settings.showButtons = changes.showButtons.newValue !== false;
    applySettings();
  });

  function applySettings() {
    if (!settings.showButtons) removeAllButtons();
    else scheduleScan();
  }

  /* pick the source a button click downloads */
  function pickSource(sources) {
    const mp4 = sources.filter((s) => s.kind === "mp4");
    if (!mp4.length) return null;
    if (settings.quality === "smallest") return mp4[mp4.length - 1];
    return mp4[0]; /* sources are sorted best-first */
  }

  /* ---------------------------------------------------------------- */
  /* sniffer bridge (page world <-> isolated world)                   */
  /* ---------------------------------------------------------------- */
  document.addEventListener("xvg:media", (ev) => {
    try {
      const data = JSON.parse(String(ev.detail || "{}"));
      if (data && Array.isArray(data.items)) ingest(data.items);
    } catch (e) { /* ignore malformed events */ }
  });

  function dispatchReplay(cap) {
    if (!cap || !cap.url || replaySent.has(cap.url)) return;
    replaySent.add(cap.url);
    while (replaySent.size > 80) replaySent.delete(replaySent.values().next().value);
    try {
      document.dispatchEvent(new CustomEvent("xvg:replay", {
        detail: JSON.stringify({ url: cap.url, auth: cap.auth || null, csrf: cap.csrf || null })
      }));
    } catch (e) { /* ignore */ }
  }

  /* ---------------------------------------------------------------- */
  /* DOM helpers                                                      */
  /* ---------------------------------------------------------------- */
  const BTN_SIZE = "34.75px";

  function makeIcon() {
    const SVGNS = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(SVGNS, "svg");
    svg.setAttribute("viewBox", "0 0 24 24");
    svg.setAttribute("width", "18.75");
    svg.setAttribute("height", "18.75");
    svg.setAttribute("aria-hidden", "true");
    const p1 = document.createElementNS(SVGNS, "path");
    p1.setAttribute("fill", "currentColor");
    p1.setAttribute("d", "M12 2.75c-.55 0-1 .45-1 1v10.34l-3.3-3.3a1 1 0 1 0-1.41 1.42l5 5a1 1 0 0 0 1.42 0l5-5a1 1 0 1 0-1.41-1.42l-3.3 3.3V3.75c0-.55-.45-1-1-1z");
    const p2 = document.createElementNS(SVGNS, "path");
    p2.setAttribute("fill", "currentColor");
    p2.setAttribute("d", "M4.5 19.25h15v2h-15z");
    svg.appendChild(p1);
    svg.appendChild(p2);
    return svg;
  }

  function baseBtnStyle(btn) {
    btn.style.cssText =
      "cursor:pointer;background:transparent;border:none;border-radius:9999px;" +
      "width:" + BTN_SIZE + ";height:" + BTN_SIZE + ";padding:0;margin:0;" +
      "display:flex;align-items:center;justify-content:center;" +
      "color:#71767b;transition:color .2s,background-color .2s;flex:0 0 auto;";
  }

  function tint(btn, color, ms) {
    baseBtnStyle(btn);
    btn.style.color = color;
    if (ms) setTimeout(() => { if (btn.isConnected) baseBtnStyle(btn); }, ms);
  }

  function pulse(btn) {
    try {
      btn.animate(
        [{ opacity: 0.45, transform: "scale(0.9)" }, { opacity: 1, transform: "scale(1.08)" }],
        { duration: 700, direction: "alternate", iterations: Infinity, easing: "ease-in-out" }
      );
    } catch (e) { /* Web Animations unavailable */ }
  }

  function stopPulse(btn) {
    try { btn.getAnimations().forEach((a) => a.cancel()); } catch (e) { /* ignore */ }
  }

  function tweetIdFromArticle(art) {
    if (!art) return null;
    const timeEl = art.querySelector("time");
    const tLink = timeEl && timeEl.closest("a[href*='/status/']");
    if (tLink) {
      const m = /\/status\/(\d+)/.exec(tLink.getAttribute("href") || "");
      if (m) return m[1];
    }
    for (const link of art.querySelectorAll("a[href*='/status/']")) {
      const href = link.getAttribute("href") || "";
      if (/\/(analytics|photo|video)\//.test(href)) continue;
      const m = /\/status\/(\d+)/.exec(href);
      if (m) return m[1];
    }
    return null;
  }

  /* ---------------------------------------------------------------- */
  /* button injection                                                 */
  /* ---------------------------------------------------------------- */
  function removeAllButtons() {
    for (const b of document.querySelectorAll("[data-xvg-btn]")) b.remove();
  }

  function scan() {
    if (!settings.showButtons) return;
    const articles = document.querySelectorAll("article[data-testid='tweet']");
    for (const art of articles) {
      try {
        if (!art.querySelector("[data-testid='videoPlayer']")) continue;
        const group = art.querySelector("div[role='group']");
        if (!group || group.querySelector("[data-xvg-btn]")) continue;
        const btn = document.createElement("button");
        btn.type = "button";
        btn.setAttribute("data-xvg-btn", "1");
        btn.setAttribute("aria-label", "Download video with XVidGrab");
        btn.title = "Download video (XVidGrab)";
        btn.appendChild(makeIcon());
        baseBtnStyle(btn);
        btn.addEventListener("mouseenter", () => { if (!btn.__busy) btn.style.color = "#1d9bf0"; });
        btn.addEventListener("mouseleave", () => { if (!btn.__busy) btn.style.color = "#71767b"; });
        btn.addEventListener("click", (ev) => {
          ev.preventDefault();
          ev.stopPropagation();
          handleButtonClick(btn, art);
        }, true);
        group.appendChild(btn);
        btnByArticle.set(art, btn);
      } catch (e) { /* DOM surprises — never break the page */ }
    }
  }

  let scanPending = false;
  function scheduleScan() {
    if (scanPending) return;
    scanPending = true;
    setTimeout(() => { scanPending = false; scan(); }, 200);
  }

  const observer = new MutationObserver(() => scheduleScan());
  observer.observe(document.documentElement, { childList: true, subtree: true });

  /* ---------------------------------------------------------------- */
  /* on-demand resolution                                             */
  /* ---------------------------------------------------------------- */
  function recordsForTweet(tweetId) {
    const out = [];
    for (const rec of store.values()) {
      if (rec.tweetId === tweetId && rec.sources.some((s) => s.kind === "mp4")) out.push(rec);
    }
    return out;
  }

  async function resolveTweet(tweetId) {
    const res = await api.runtime.sendMessage({
      type: "xvg-fetch-tweet",
      tweetId
    }).catch(() => ({ ok: false }));
    if (res && res.ok && res.json) {
      try { ingest(XVG.parseMedia(res.json)); } catch (e) { /* ignore */ }
    }
    /* syndication fallback (public embed API, no auth) */
    if (!recordsForTweet(tweetId).length) {
      try {
        const n = Number(tweetId);
        const token = ((n / 1e15) * Math.PI).toString(36).substring(2, 12);
        const r = await fetch("https://cdn.syndication.twimg.com/tweet-result?id=" +
          encodeURIComponent(tweetId) + "&token=" + encodeURIComponent(token) + "&lang=en", {
          credentials: "omit"
        });
        if (r.ok) {
          const j = await r.json();
          try { ingest(XVG.parseMedia({ tweetResult: { result: j } })); } catch (e) { /* ignore */ }
        }
      } catch (e) { /* offline / gone */ }
    }
    return recordsForTweet(tweetId);
  }

  /* ---------------------------------------------------------------- */
  /* toast + progress panel                                           */
  /* ---------------------------------------------------------------- */
  const FONT = "-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif";

  let toastEl = null;
  let toastTimer = null;
  function toast(msg) {
    if (!toastEl) {
      toastEl = document.createElement("div");
      toastEl.style.cssText =
        "position:fixed;left:50%;bottom:28px;transform:translateX(-50%);" +
        "z-index:2147483647;background:rgba(15,20,25,0.94);color:#fff;" +
        "font:13px/1.45 " + FONT + ";padding:10px 16px;border-radius:12px;" +
        "box-shadow:0 6px 24px rgba(0,0,0,0.4);pointer-events:none;" +
        "opacity:0;transition:opacity .2s;max-width:70vw;";
      (document.body || document.documentElement).appendChild(toastEl);
    }
    toastEl.textContent = msg;
    toastEl.style.opacity = "1";
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { if (toastEl) toastEl.style.opacity = "0"; }, 3200);
  }

  const progressRows = new Map(); /* downloadId -> {row, fill, pct} */
  let panelEl = null;

  function ensurePanel() {
    if (panelEl && panelEl.isConnected) return panelEl;
    panelEl = document.createElement("div");
    panelEl.style.cssText =
      "position:fixed;right:20px;bottom:20px;z-index:2147483647;width:300px;" +
      "background:#000;border:1px solid #333639;border-radius:16px;padding:14px 16px;" +
      "box-shadow:0 4px 16px rgba(255,255,255,0.06);font:13px/1.45 " + FONT + ";color:#e7e9ea;" +
      "display:none;flex-direction:column;gap:10px;";
    (document.body || document.documentElement).appendChild(panelEl);
    return panelEl;
  }

  function refreshPanel() {
    if (!panelEl) return;
    panelEl.style.display = progressRows.size ? "flex" : "none";
  }

  function progressRow(downloadId, filename) {
    const panel = ensurePanel();
    const row = document.createElement("div");
    row.style.cssText = "position:relative;";
    const title = document.createElement("div");
    title.textContent = filename;
    title.style.cssText = "font-weight:700;margin-bottom:6px;padding-right:24px;" +
      "white-space:nowrap;overflow:hidden;text-overflow:ellipsis;";
    const bar = document.createElement("div");
    bar.style.cssText = "background:#333639;border-radius:2px;height:4px;overflow:hidden;";
    const fill = document.createElement("div");
    fill.style.cssText = "background:#1d9bf0;width:0%;height:100%;transition:width .3s;";
    bar.appendChild(fill);
    const pct = document.createElement("div");
    pct.textContent = "0%";
    pct.style.cssText = "text-align:right;color:#71767b;font-size:11px;margin-top:4px;";
    const close = document.createElement("button");
    close.textContent = "x";
    close.style.cssText = "position:absolute;top:-2px;right:0;background:none;border:none;" +
      "color:#71767b;cursor:pointer;font:700 12px " + FONT + ";";
    close.addEventListener("click", () => { row.remove(); progressRows.delete(downloadId); refreshPanel(); });
    row.appendChild(title);
    row.appendChild(bar);
    row.appendChild(pct);
    row.appendChild(close);
    panel.appendChild(row);
    progressRows.set(downloadId, { row, fill, pct });
    refreshPanel();
  }

  function progressUpdate(downloadId, state, received, total) {
    const row = progressRows.get(downloadId);
    if (!row) return;
    if (state === "progress" && total > 0) {
      const p = Math.min(100, Math.round((received / total) * 100));
      row.fill.style.width = p + "%";
      row.pct.textContent = p + "%";
    } else if (state === "done") {
      row.fill.style.width = "100%";
      row.fill.style.background = "#00ba7c";
      row.pct.textContent = "Saved";
      setTimeout(() => {
        row.row.remove();
        progressRows.delete(downloadId);
        refreshPanel();
      }, 2500);
    } else if (state === "failed") {
      row.fill.style.background = "#f4212e";
      row.pct.textContent = "Failed";
      setTimeout(() => {
        row.row.remove();
        progressRows.delete(downloadId);
        refreshPanel();
      }, 4000);
    }
  }

  /* ---------------------------------------------------------------- */
  /* downloads                                                        */
  /* ---------------------------------------------------------------- */
  async function downloadRecord(btn, record, src) {
    const filename = XVG.fileNameFor(record, src);
    if (btn) {
      btn.__busy = true;
      tint(btn, "#1d9bf0");
      pulse(btn);
    }
    try {
      const res = await api.runtime.sendMessage({
        type: "xvg-download",
        url: src.url,
        filename,
        tweet: record.tweetId ? "https://x.com/i/status/" + record.tweetId : null
      }).catch((err) => ({ ok: false, error: String(err) }));

      if (res && res.ok) {
        if (btn) { stopPulse(btn); tint(btn, "#00ba7c", 1400); }
        toast("XVidGrab: saving " + filename);
      } else {
        if (btn) { stopPulse(btn); tint(btn, "#f4212e", 1600); }
        toast("XVidGrab: download failed — " + ((res && res.error) || "unknown error"));
      }
    } finally {
      if (btn) { btn.__busy = false; stopPulse(btn); if (btn.isConnected) baseBtnStyle(btn); }
    }
  }

  async function handleButtonClick(btn, art) {
    const tweetId = tweetIdFromArticle(art);
    let recs = tweetId ? recordsForTweet(tweetId) : [];

    if (!recs.length && tweetId && !resolving.has(tweetId)) {
      resolving.add(tweetId);
      try { recs = await resolveTweet(tweetId); }
      finally { resolving.delete(tweetId); }
    }

    if (!recs.length) {
      tint(btn, "#f4212e", 1600);
      toast("XVidGrab: no video stream captured yet — scroll a little and try again");
      return;
    }

    const rec = recs[0];
    const src = pickSource(rec.sources);
    if (!src) {
      tint(btn, "#f4212e", 1600);
      toast("XVidGrab: no MP4 variant available for this post");
      return;
    }
    downloadRecord(btn, rec, src);
  }

  /* ---------------------------------------------------------------- */
  /* popup + background messaging                                     */
  /* ---------------------------------------------------------------- */
  function listForPopup() {
    const videos = [];
    for (const rec of store.values()) {
      if (!rec.sources.length) continue;
      videos.push({
        key: rec.key,
        tweetId: rec.tweetId || null,
        author: rec.author || null,
        text: rec.text ? String(rec.text).slice(0, 280) : null,
        type: rec.type || "video",
        thumbnail: rec.thumbnail || null,
        sources: rec.sources.map((s) => ({
          url: s.url,
          kind: s.kind,
          label: s.label || XVG.qualityLabel(s),
          bitrate: s.bitrate || 0
        })),
        hasMp4: rec.sources.some((s) => s.kind === "mp4")
      });
      if (videos.length >= 40) break;
    }
    return videos;
  }

  api.runtime.onMessage.addListener((msg) => {
    if (!msg || typeof msg.type !== "string") return;

    switch (msg.type) {
      case "xvg-replay-url":
        dispatchReplay(msg);
        return Promise.resolve({ ok: true });

      case "xvg-get-videos":
        return Promise.resolve({ videos: listForPopup() });

      case "xvg-download-source": {
        /* popup picked a specific record + source */
        const rec = store.get(msg.key);
        if (!rec) return Promise.resolve({ ok: false, error: "record expired — refresh the page" });
        const src = (rec.sources || []).find((s) => s.url === msg.sourceUrl) ||
          (msg.sourceUrl ? null : pickSource(rec.sources));
        if (!src) return Promise.resolve({ ok: false, error: "source not found" });
        return downloadRecord(null, rec, src).then(() => ({ ok: true }));
      }

      case "xvg-progress":
        progressUpdate(msg.downloadId, msg.state, msg.received || 0, msg.total || 0);
        return;
    }
  });

  /* replay URLs the background captured before this content script ran */
  (async () => {
    try {
      const res = await api.runtime.sendMessage({ type: "xvg-get-captured" })
        .catch(() => ({ captures: [] }));
      (res && res.captures || []).slice(0, 12).forEach(dispatchReplay);
    } catch (e) { /* ignore */ }
  })();

  /* first scan + badge */
  scheduleScan();
  updateBadge();
})();
