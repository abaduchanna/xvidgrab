/*
 * XVidGrab — page-world network sniffer (content script, world: "MAIN").
 *
 * Runs at document_start in the page context, BEFORE X's bundles execute, and
 * passively wraps window.fetch + XMLHttpRequest so every GraphQL response the
 * site loads is parsed for video / GIF media. Results are published to the
 * isolated content script as string-detail CustomEvents on `document`:
 *
 *   xvg:media   { items: [record...] }            — parsed media records
 *   xvg:replay  { url, auth, csrf }               — re-fetch a missed URL
 *
 * Nothing is ever uploaded anywhere: parsing happens locally and the page
 * behaves exactly as without XVidGrab.
 */
(function () {
  "use strict";
  if (window.__XVG_SNIFFER__) return;
  window.__XVG_SNIFFER__ = true;

  var REPLAY_SEEN_MAX = 60;
  var replaySeen = [];
  var replaySeenSet = Object.create(null);

  function emit(type, payload) {
    try {
      document.dispatchEvent(new CustomEvent(type, { detail: JSON.stringify(payload) }));
    } catch (e) { /* page unloading etc. */ }
  }

  function isGraphQLUrl(url) {
    return typeof url === "string" && url.indexOf("/graphql/") !== -1;
  }

  function isTwimgStreamUrl(url) {
    return typeof url === "string" &&
      /^https:\/\/video(?:-[^.]+)?\.twimg\.com\//i.test(url) &&
      /\.m3u8([?#]|$)/i.test(url);
  }

  function twimgRecord(url) {
    var rec = XVG.parseMedia({
      media: {
        type: "video",
        id_str: null,
        video_info: {
          variants: [{ url: url, content_type: "application/x-mpegurl", bitrate: 0 }]
        }
      }
    });
    if (!rec.length) return null;
    rec[0].tweetId = null;
    return rec[0];
  }

  function handleJson(json) {
    if (!json || typeof json !== "object") return;
    var items;
    try { items = XVG.parseMedia(json); } catch (e) { return; }
    if (items && items.length) emit("xvg:media", { items: items });
  }

  function handleResponseUrl(url) {
    if (!isTwimgStreamUrl(url)) return;
    if (replaySeenSet[url]) return;
    rememberUrl(url);
    var rec = twimgRecord(url);
    if (rec) emit("xvg:media", { items: [rec] });
  }

  function rememberUrl(url) {
    if (replaySeenSet[url]) return;
    replaySeenSet[url] = 1;
    replaySeen.push(url);
    if (replaySeen.length > REPLAY_SEEN_MAX) {
      var drop = replaySeen.shift();
      delete replaySeenSet[drop];
    }
  }

  /* ------------------------------------------------------------------ */
  /* fetch hook                                                          */
  /* ------------------------------------------------------------------ */
  var origFetch = window.fetch;
  if (typeof origFetch === "function") {
    window.fetch = function () {
      var args = arguments;
      var p = origFetch.apply(this, args);
      try {
        var input = args[0];
        var url = typeof input === "string" ? input : (input && input.url) || "";
        if (isGraphQLUrl(url)) {
          p.then(function (res) {
            try {
              if (res && typeof res.clone === "function") {
                res.clone().text().then(function (txt) {
                  try { handleJson(JSON.parse(txt)); } catch (e) { /* not JSON */ }
                }).catch(function () { /* body unavailable */ });
              }
            } catch (e) { /* detached */ }
          }).catch(function () { /* network error */ });
        }
        handleResponseUrl(url);
        /* resolved response URL can differ (redirects) */
        p.then(function (res) {
          try { handleResponseUrl(res && res.url); } catch (e) { /* ignore */ }
        }).catch(function () { /* ignore */ });
      } catch (e) { /* never break the page */ }
      return p;
    };
  }

  /* ------------------------------------------------------------------ */
  /* XHR hook                                                            */
  /* ------------------------------------------------------------------ */
  var XHR = window.XMLHttpRequest;
  var origOpen = XHR.prototype.open;
  var origSend = XHR.prototype.send;

  XHR.prototype.open = function (method, url) {
    try { this.__xvgUrl = String(url || ""); } catch (e) { /* ignore */ }
    return origOpen.apply(this, arguments);
  };

  XHR.prototype.send = function () {
    var xhr = this;
    try {
      var url = xhr.__xvgUrl || "";
      if (isGraphQLUrl(url) || isTwimgStreamUrl(url)) {
        xhr.addEventListener("load", function () {
          try {
            var rt = xhr.responseType;
            if (rt === "" || rt === "text") {
              handleResponseUrl(url);
              try { handleJson(JSON.parse(xhr.responseText)); } catch (e) { /* not JSON */ }
            } else if (rt === "json") {
              handleJson(xhr.response);
            }
          } catch (e) { /* ignore */ }
        });
      }
    } catch (e) { /* ignore */ }
    return origSend.apply(this, arguments);
  };

  /* ------------------------------------------------------------------ */
  /* replay: re-fetch GraphQL URLs the background captured before this   */
  /* sniffer existed (late install / reload / bfcache restores).         */
  /* ------------------------------------------------------------------ */
  document.addEventListener("xvg:replay", function (ev) {
    var msg;
    try { msg = JSON.parse(String(ev.detail || "{}")); } catch (e) { return; }
    var url = msg && typeof msg.url === "string" ? msg.url : null;
    if (!url || !isGraphQLUrl(url)) return;
    if (replaySeenSet[url]) return;
    rememberUrl(url);

    var headers = { "x-twitter-active-user": "yes", accept: "application/json" };
    if (msg.auth) headers.authorization = msg.auth;
    if (msg.csrf) headers["x-csrf-token"] = msg.csrf;

    var init = { headers: headers, credentials: "include" };
    var p = origFetch.call(window, url, init);
    p.then(function (res) {
      try { return res.json(); } catch (e) { return null; }
    }).then(handleJson).catch(function () { /* ignore */ });
  });
})();
