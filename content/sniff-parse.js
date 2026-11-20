/*
 * XVidGrab — media parser (pure functions, no DOM/browser APIs).
 *
 * Extracts video / animated-GIF media records from X (Twitter) GraphQL JSON
 * responses and classifies stream sources (progressive MP4 variants and HLS
 * master playlists). Shared by the page-world sniffer, the content script and
 * the Node test-suite (tests/parser.test.js).
 *
 * This file must stay dependency-free so it can run in three environments:
 *   - MAIN-world content script (page context)
 *   - isolated content script (sandbox context)
 *   - Node.js (eval'd by tests/parser.test.js)
 */
var XVG = (function () {
  "use strict";

  var HLS_TYPES = [
    "application/x-mpegurl",
    "application/vnd.apple.mpegurl",
    "audio/mpegurl",
    "audio/x-mpegurl"
  ];

  function toNum(v) {
    var n = typeof v === "number" ? v : Number(v);
    return Number.isFinite(n) && n > 0 ? n : 0;
  }

  function str(v) {
    return typeof v === "string" && v.trim() ? v.trim() : null;
  }

  /* twimg stream URLs embed capture dimensions, e.g. /720x1280/ or /1200x675/ */
  function dimsFromUrl(url) {
    var m = /(?:^|[/_-])(\d{2,5})x(\d{2,5})(?:[/_.?-]|$)/i.exec(url || "");
    return m ? { w: toNum(m[1]), h: toNum(m[2]) } : { w: 0, h: 0 };
  }

  /* "video/mp4" content type or .mp4 extension, .m3u8 for HLS playlists */
  function classify(url, contentType) {
    var ct = str(contentType) || "";
    ct = ct.split(";")[0].trim().toLowerCase();
    var isHls = HLS_TYPES.indexOf(ct) !== -1 || /\.m3u8([?#]|$)/i.test(url);
    if (isHls) return "hls";
    if (ct === "video/mp4" || /\.mp4([?#]|$)/i.test(url)) return "mp4";
    return null;
  }

  function qualityLabel(src) {
    if (!src) return "video";
    var d = dimsFromUrl(src.url);
    /* Standard resolution tiers use the shorter side: both 1280x720 and
     * 1080x1920 (portrait) are commonly called "720p" / "1080p". */
    var tier = d.w && d.h ? Math.min(d.w, d.h) : (d.h || d.w);
    if (tier) return tier + "p";
    if (src.bitrate) return Math.round(src.bitrate / 1000) + " kbps";
    return src.kind === "hls" ? "HLS" : "MP4";
  }

  /* Normalize raw video_info.variants[] entries into source objects. */
  function parseVariants(raw) {
    if (!Array.isArray(raw)) return [];
    var seen = Object.create(null);
    var out = [];
    for (var i = 0; i < raw.length; i++) {
      var v = raw[i];
      if (!v || typeof v !== "object") continue;
      var url = str(v.url);
      if (!url || !/^https:\/\//i.test(url)) continue;
      var kind = classify(url, v.content_type || v.contentType);
      if (!kind) continue;
      if (seen[url]) continue;
      seen[url] = 1;
      var d = dimsFromUrl(url);
      var bitrate = toNum(v.bitrate);
      out.push({
        url: url,
        kind: kind,
        bitrate: bitrate,
        width: d.w,
        height: d.h,
        contentType: kind === "hls" ? "application/vnd.apple.mpegurl" : "video/mp4",
        label: qualityLabel({ url: url, kind: kind, bitrate: bitrate })
      });
    }
    return sortSources(out);
  }

  /*
   * Progressive MP4 variants are single files with muxed audio — always the
   * preferred download. HLS master playlists rank after MP4 (XVidGrab keeps
   * downloads 100% local, and MP4 covers every regular X video).
   */
  function sourceRank(s) {
    if (s.kind === "mp4") return 2e9 + (s.bitrate || 0) + s.height * 1e3;
    return 1e9 + (s.height || 0);
  }

  function sortSources(list) {
    return list.sort(function (a, b) {
      return sourceRank(b) - sourceRank(a);
    });
  }

  /* Best downloadable source: highest-bitrate MP4, else master HLS. */
  function bestSource(sources) {
    if (!Array.isArray(sources) || !sources.length) return null;
    var mp4 = sources.filter(function (s) { return s.kind === "mp4"; });
    if (mp4.length) return mp4[0];
    return sources[0];
  }

  function parseVariantsLoose(obj) {
    if (!obj || typeof obj !== "object") return [];
    var vi = obj.video_info || obj.videoInfo;
    if (vi && Array.isArray(vi.variants)) return parseVariants(vi.variants);
    var vr = obj.video_results || obj.videoResults;
    if (vr && vr.result) {
      vi = vr.result.video_info || vr.result.videoInfo;
      if (vi && Array.isArray(vi.variants)) return parseVariants(vi.variants);
    }
    return [];
  }

  function authorOf(tweetNode) {
    /* syndication embed shape (cdn.syndication.twimg.com/tweet-result):
     * the user object sits flat on the result node */
    var flat = tweetNode.user;
    if (flat && str(flat.screen_name)) return str(flat.screen_name);
    var core = tweetNode.core || tweetNode.Core;
    var ur = core && (core.user_results || core.userResults);
    var r = ur && ur.result;
    if (!r) return null;
    var c = r.core;
    if (c && str(c.screen_name)) return str(c.screen_name); /* newer schema */
    var lg = r.legacy;
    return (lg && str(lg.screen_name)) || null;
  }

  function textOf(tweetNode) {
    /* syndication embed shape: plain text field on the result node */
    var flatText = str(tweetNode.text);
    if (flatText) return flatText;
    var note = tweetNode.note_tweet || tweetNode.noteTweet;
    var nres = note && (note.note_tweet_results || note.noteTweetResults);
    var nt = nres && nres.result;
    var t = nt && (str(nt.text) || str(nt.full_text));
    if (t) return t;
    var lg = tweetNode.legacy;
    return (lg && str(lg.full_text)) || null;
  }

  function mediaArrayOf(o) {
    var lg = o.legacy;
    if (lg) {
      if (lg.extended_entities && Array.isArray(lg.extended_entities.media)) {
        return lg.extended_entities.media;
      }
      if (lg.entities && Array.isArray(lg.entities.media)) {
        return lg.entities.media;
      }
    }
    return null;
  }

  function mediaIdOf(m) {
    return str(m && (m.id_str || m.idStr || m.media_key || m.mediaKey));
  }

  function thumbOf(m) {
    return str(m && (m.media_url_https || m.mediaUrlHttps || m.media_url || m.mediaUrl));
  }

  /*
   * Walk any GraphQL response and return video/gif records:
   * [{ tweetId, author, text, type, thumbnail, mediaIds, sources[] }]
   * Records without a resolvable tweet id are kept (keyed by media ids)
   * so the popup can still offer them.
   */
  function parseMedia(root) {
    var records = [];
    if (!root || typeof root !== "object") return records;

    var visited = new WeakSet();
    var budget = { nodes: 0 };

    function push(rec) {
      for (var i = 0; i < records.length; i++) {
        var r = records[i];
        /* Same media entity seen again (timeline + detail + replay):
         * key = mediaId, or first source URL when no media id exists. */
        if (r.key !== rec.key) continue;
        var seen = Object.create(null);
        r.sources.forEach(function (s) { seen[s.url] = 1; });
        rec.sources.forEach(function (s) {
          if (!seen[s.url]) { r.sources.push(s); seen[s.url] = 1; }
        });
        r.sources = sortSources(r.sources);
        r.tweetId = r.tweetId || rec.tweetId;
        r.author = r.author || rec.author;
        r.text = r.text || rec.text;
        r.thumbnail = r.thumbnail || rec.thumbnail;
        r.mediaIds = r.mediaIds.concat(rec.mediaIds.filter(function (id) {
          return r.mediaIds.indexOf(id) === -1;
        }));
        return;
      }
      records.push(rec);
    }

    function collectFromMediaArray(arr, ctx) {
      for (var i = 0; i < arr.length; i++) {
        var m = arr[i];
        if (!m || typeof m !== "object") continue;
        var type = m.type;
        var sources = parseVariantsLoose(m);
        if (!sources.length && !(type === "video" || type === "animated_gif")) continue;
        if (!sources.length) continue;
        var mid = mediaIdOf(m);
        push({
          tweetId: ctx.id,
          author: ctx.author,
          text: ctx.text,
          type: type === "animated_gif" ? "gif" : "video",
          thumbnail: thumbOf(m),
          mediaIds: mid ? [mid] : [],
          key: mid || sources[0].url,
          sources: sources
        });
      }
    }

    function walk(o, ctx) {
      if (!o || typeof o !== "object") return;
      if (++budget.nodes > 250000) return;
      if (visited.has(o)) return;
      try { visited.add(o); } catch (e) { /* frozen exotic object */ }

      /* Does this node represent a tweet result? */
      var tn = o.__typename;
      var isTweet =
        tn === "Tweet" ||
        tn === "TweetWithVisibilityResults" ||
        (!!o.rest_id && (!!o.legacy || !!o.core)) ||
        /* syndication tweet-result shape: flat id_str + user + text/mediaDetails
         * (media entities also carry id_str, so "user" is required to match) */
        (!!o.id_str && !!o.user &&
          (typeof o.text === "string" || Array.isArray(o.mediaDetails)));
      if (isTweet) {
        var id = str(o.rest_id) || (o.legacy && str(o.legacy.id_str)) ||
          (o.user ? str(o.id_str) : null);
        if (id) {
          ctx = {
            id: id,
            author: authorOf(o) || ctx.author,
            text: textOf(o) || ctx.text
          };
        }
      }

      var mediaArr = mediaArrayOf(o);
      if (mediaArr) collectFromMediaArray(mediaArr, ctx);

      /* Standalone video_info nodes (media cards, newer unified shapes,
       * syndication mediaDetails). Key on the media id baked into the
       * stream URL when the entity carries no id_str — the same id the
       * flight-salvage path uses, so both paths merge into one row. */
      if (!mediaArr) {
        var sources = parseVariantsLoose(o);
        if (sources.length) {
          var mid = str(o.id_str) && String(o.id_str).length >= 8 ? str(o.id_str) : null;
          var urlId = mid ? null : mediaIdFromStreamUrl(sources[0].url);
          push({
            tweetId: ctx.id,
            author: ctx.author,
            text: ctx.text,
            type: o.type === "animated_gif" ? "gif" : "video",
            thumbnail: thumbOf(o),
            mediaIds: mid || urlId ? [mid || urlId] : [],
            key: mid || urlId || sources[0].url,
            sources: sources
          });
        }
      }

      if (Array.isArray(o)) {
        var n = Math.min(o.length, 500);
        for (var i = 0; i < n; i++) walk(o[i], ctx);
      } else {
        var keys;
        try { keys = Object.keys(o); } catch (e) { return; }
        for (var k = 0; k < keys.length; k++) {
          var val;
          try { val = o[keys[k]]; } catch (e) { continue; }
          if (val && typeof val === "object") walk(val, ctx);
        }
      }
    }

    walk(root, { id: null, author: null, text: null });
    return records;
  }

  /* xvidgrab_<author>_<tweetId>_<label>.mp4 — flat, filesystem-safe */
  function fileNameFor(record, src) {
    var author = String((record && record.author) || "x")
      .replace(/^@/, "")
      .replace(/[^A-Za-z0-9_]/g, "") || "x";
    var id = (record && (record.tweetId || record.mediaIds[0])) || String(Date.now());
    var label = "mp4";
    if (src) {
      var d = dimsFromUrl(src.url);
      var tier = d.w && d.h ? Math.min(d.w, d.h) : (d.h || d.w);
      if (tier) label = tier + "p";
      else if (src.bitrate) label = Math.round(src.bitrate / 1000) + "k";
    }
    return ("xvidgrab_" + author + "_" + id + "_" + label + ".mp4")
      .replace(/[\\/:*?"<>|]/g, "_");
  }

  /*
   * Extract every URL-looking token from a blob of text (one or more
   * links, any whitespace/newline/comma separation). Returns raw strings;
   * callers validate with parseXLink().
   */
  function extractLinks(text) {
    var out = [];
    if (!text || typeof text !== "string") return out;
    var re = /(?:https?:\/\/|(?:(?:x|twitter)\.com|t\.co)\/)[^\s"'<>]+/gi;
    var m;
    while ((m = re.exec(text)) !== null) {
      var u = m[0].replace(/[.,;:)\]]+$/, "");
      if (u) out.push(u);
    }
    return out;
  }

  /*
   * Classify an X (Twitter) link into a fetch plan:
   *   { ok, kind, url, url2, screenName, tweetId }
   *   kind "tweet"   — one status post (fetched directly, no tab when possible)
   *   kind "profile" — a user timeline; url = profile root, url2 = /media tab
   *   kind "search"  — a search results page (scroll collected)
   *   kind "page"    — any other x.com page (list, explore, hashtag...)
   * Hosts: x.com, twitter.com, mobile.x.com, m.twitter.com. t.co links are
   * opened as "page" (the tab follows the redirect to their real target).
   */
  var RESERVED_SEGMENTS = [
    "home", "i", "explore", "search", "notifications", "messages",
    "settings", "intent", "compose", "bookmarks", "tos",
    "privacy", "help", "logout", "signup", "login", "register"
  ];

  function parseXLink(raw) {
    var input = typeof raw === "string" ? raw.trim() : "";
    if (!input) return { ok: false, reason: "empty" };
    if (!/^https?:\/\//i.test(input)) input = "https://" + input;
    var u;
    try { u = new URL(input); } catch (e) { return { ok: false, reason: "not-a-url" }; }
    if (u.protocol !== "https:" && u.protocol !== "http:") return { ok: false, reason: "bad-protocol" };

    var host = String(u.hostname || "").toLowerCase();
    if (host === "t.co" || host === "www.t.co") {
      return { ok: true, kind: "page", url: "https://" + host + u.pathname + u.search, screenName: null, tweetId: null };
    }
    if (!/^(?:www\.|mobile\.|m\.)?(?:twitter\.com|x\.com)$/.test(host)) {
      return { ok: false, reason: "not-an-x-link" };
    }

    var path = String(u.pathname || "/").replace(/\/+$/, "") || "/";
    var segs = path.split("/").filter(Boolean);
    var base = "https://x.com";

    /* 1. a status post — /<user>/status/<id>[...], /i/web/status/<id> */
    var sm = /\/status\/(\d+)/.exec(path);
    if (sm) {
      return { ok: true, kind: "tweet", url: base + "/i/web/status/" + sm[1], screenName: null, tweetId: sm[1] };
    }

    /* 2. search — keep the query, drop tracking params */
    if (segs[0] === "search") {
      var q = u.searchParams.get("q");
      if (!q) return { ok: true, kind: "page", url: base + "/explore", screenName: null, tweetId: null };
      return { ok: true, kind: "search", url: base + "/search?q=" + encodeURIComponent(q), screenName: null, tweetId: null };
    }

    /* 3. profile — a single handle, optionally with a timeline sub-tab */
    var PROFILE_SUBTABS = ["media", "with_replies", "highlights", "likes", "superfollows"];
    if (segs.length === 1 && RESERVED_SEGMENTS.indexOf(segs[0]) === -1 &&
        /^[A-Za-z0-9_]{1,15}$/.test(segs[0])) {
      var s = segs[0];
      return {
        ok: true, kind: "profile",
        url: base + "/" + s,
        url2: base + "/" + s + "/media",
        screenName: s, tweetId: null
      };
    }
    if (segs.length === 2 && RESERVED_SEGMENTS.indexOf(segs[0]) === -1 &&
        /^[A-Za-z0-9_]{1,15}$/.test(segs[0]) &&
        PROFILE_SUBTABS.indexOf(segs[1]) !== -1) {
      var s2 = segs[0];
      var want = segs[1];
      return {
        ok: true, kind: "profile",
        url: want === "media" ? base + "/" + s2 + "/media" : base + "/" + s2,
        url2: want === "media" ? null : base + "/" + s2 + "/media",
        screenName: s2, tweetId: null
      };
    }

    /* 4. everything else (lists, hashtags, explore, events...) — open and
     * scroll-collect whatever the page loads */
    return { ok: true, kind: "page", url: base + (path === "/" ? "/home" : path) + (u.search || ""), screenName: null, tweetId: null };
  }

  /*
   * Flight-stream salvage.
   *
   * Some X responses are react-server "flight" payloads (JSON islands
   * embedded inside string chunks) rather than pure JSON — JSON.parse
   * throws on them and the sniffer would miss every video on the page.
   * Worse, the timeline data is often JSON-in-JSON (escaped once more),
   * so only raw text harvesting works there.
   *
   * Harvesting strategy, best-first:
   *   1. Complete MEDIA-ENTITY islands — keys whose value is an array of
   *      media entities ("media_entities2" — page flight payload,
   *      "mediaDetails" — syndication shape, "extended_entities" — legacy
   *      GraphQL). These keep the poster thumbnail (media_url_https), the
   *      media id and the tweet context (expanded_url) — no more gray rows.
   *   2. Bare "video_info" islands (old behaviour), enriched with the
   *      nearest preceding metadata (thumbnail / media id / expanded_url /
   *      screen_name) from the same media entity — serialization order puts
   *      those fields before video_info, so proximity is reliable here.
   * Records recovered this way key on the media id baked into the stream
   * URL, which is the SAME id the JSON path uses — so a salvaged record
   * and a later full-JSON record for the same video MERGE instead of
   * appearing twice in the popup.
   */
  function balancedSpan(text, open) {
    var openCh = text.charAt(open);
    var closeCh = openCh === "{" ? "}" : "]";
    var depth = 0, inStr = false, esc = false;
    for (var i = open; i < text.length; i++) {
      var ch = text.charAt(i);
      if (inStr) {
        if (esc) esc = false;
        else if (ch === "\\") esc = true;
        else if (ch === "\"") inStr = false;
        continue;
      }
      if (ch === "\"") { inStr = true; continue; }
      if (ch === openCh) depth++;
      else if (ch === closeCh) {
        depth--;
        if (depth === 0) return i;
      }
    }
    return -1;
  }

  /* first { or [ after a JSON key ("name":  <value>) */
  function valueStartAfterKey(text, from) {
    for (var i = from; i < text.length; i++) {
      var ch = text.charAt(i);
      if (ch === ":" || ch === " " || ch === "\t" || ch === "\n" || ch === "\r") continue;
      return (ch === "{" || ch === "[") ? i : -1;
    }
    return -1;
  }

  /* media id baked into twimg stream URLs:
   * video.twimg.com/{ext_tw_video|amplify_video|...}/<media id>/vid/... */
  function mediaIdFromStreamUrl(url) {
    var m = /(?:ext_tw_video|amplify_video|creator_studio_video|legacy_video|vms)\/(\d{10,25})\//.exec(url || "");
    return m ? m[1] : null;
  }

  function lastMatch(re, s) {
    var m = null, mm;
    while ((mm = re.exec(s)) !== null) m = mm;
    return m;
  }

  /* nearest metadata BEFORE a bare video_info island (same media entity —
   * these fields serialize before video_info), never crossing lastEnd */
  function metaBefore(text, from, to) {
    var win = text.slice(from, Math.max(from, to));
    var thumb = lastMatch(/"media_url_https"\s*:\s*"([^"]+)"/g, win);
    var eu = lastMatch(/"expanded_url"\s*:\s*"([^"]+)"/g, win);
    var sn = lastMatch(/"screen_name"\s*:\s*"([A-Za-z0-9_]{1,15})"/g, win);
    var tweetId = null, author = null;
    if (eu) {
      var sm = /([A-Za-z0-9_]{1,15})\/status\/(\d{10,25})/.exec(eu[1]);
      if (sm) { author = sm[1]; tweetId = sm[2]; }
    }
    return {
      thumbnail: thumb ? thumb[1] : null,
      tweetId: tweetId,
      author: author || (sn ? sn[1] : null)
    };
  }

  /* build a record from a complete media entity (thumbnail + id intact) */
  function recordFromMediaEntity(m, ctx) {
    ctx = ctx || { id: null, author: null, text: null };
    var sources = parseVariantsLoose(m);
    var type = m.type;
    if (!sources.length) return null;
    var mid = mediaIdOf(m);
    /* tweet context from the entity's own expanded_url when present */
    var link = str(m.expanded_url) || "";
    var sm = link ? /(?:^|\/)(?:x\.com|twitter\.com)?\/?([A-Za-z0-9_]{1,15})\/status\/(\d{10,25})/.exec(link) : null;
    return {
      tweetId: (sm && sm[2]) || ctx.id,
      author: (sm && sm[1]) || ctx.author,
      text: ctx.text,
      type: type === "animated_gif" ? "gif" : "video",
      thumbnail: thumbOf(m),
      mediaIds: mid ? [mid] : (mediaIdFromStreamUrl(sources[0].url) ? [mediaIdFromStreamUrl(sources[0].url)] : []),
      key: mid || mediaIdFromStreamUrl(sources[0].url) || sources[0].url,
      sources: sources
    };
  }

  function harvestFlight(text) {
    var records = [];
    var covered = Object.create(null); /* stream URLs already in a full entity */
    var spans = [];                    /* harvested [start,end) ranges     */
    var pushed = 0;

    function overlaps(start, end) {
      for (var i = 0; i < spans.length; i++) {
        if (start < spans[i][1] && end > spans[i][0]) return true;
      }
      return false;
    }

    function pushRecord(rec) {
      if (!rec || !rec.sources.length) return;
      for (var i = 0; i < records.length; i++) {
        var r = records[i];
        if (r.key !== rec.key) continue;
        var seen = Object.create(null);
        r.sources.forEach(function (s) { seen[s.url] = 1; });
        rec.sources.forEach(function (s) {
          if (!seen[s.url]) { r.sources.push(s); seen[s.url] = 1; }
        });
        r.sources = sortSources(r.sources);
        r.tweetId = r.tweetId || rec.tweetId;
        r.author = r.author || rec.author;
        r.thumbnail = r.thumbnail || rec.thumbnail;
        r.mediaIds = r.mediaIds.concat(rec.mediaIds.filter(function (id) {
          return r.mediaIds.indexOf(id) === -1;
        }));
        return;
      }
      records.push(rec);
    }

    /* pass 1 — complete media-entity islands (thumbnail + ids intact) */
    var KEYS = ["\"media_entities2\"", "\"mediaDetails\"", "\"extended_entities\""];
    for (var k = 0; k < KEYS.length; k++) {
      var idx = 0;
      while (pushed < 500) {
        var p = text.indexOf(KEYS[k], idx);
        if (p === -1) break;
        idx = p + KEYS[k].length;
        var vs = valueStartAfterKey(text, idx);
        if (vs === -1) continue;
        var ve = balancedSpan(text, vs);
        if (ve === -1) { idx = text.length; break; }
        idx = ve + 1;
        if (overlaps(vs, ve + 1)) continue;
        var chunk;
        try { chunk = JSON.parse(text.slice(vs, ve + 1)); } catch (e) { continue; }
        var list = Array.isArray(chunk) ? chunk
          : (chunk && Array.isArray(chunk.media) ? chunk.media : []);
        var added = 0;
        for (var j = 0; j < list.length; j++) {
          var m = list[j];
          if (!m || typeof m !== "object") continue;
          var rec = recordFromMediaEntity(m, null);
          if (!rec) continue;
          pushRecord(rec);
          pushed++;
          added++;
          for (var s = 0; s < rec.sources.length; s++) covered[rec.sources[s].url] = 1;
        }
        if (added) spans.push([vs, ve + 1]);
      }
    }

    /* pass 2 — bare video_info islands, enriched with nearby metadata */
    var KEY = "video_info";
    var idx2 = 0;
    var lastEnd = 0;
    while (pushed < 600) {
      var k2 = text.indexOf(KEY, idx2);
      if (k2 === -1) break;
      idx2 = k2 + KEY.length;
      var open = text.indexOf("{", idx2);
      if (open === -1) break;
      if (open - idx2 > 4) continue; /* key must be followed by its object */
      var end = balancedSpan(text, open);
      if (end === -1) break;
      idx2 = end + 1;
      var island;
      try { island = JSON.parse(text.slice(open, end + 1)); } catch (e) { continue; }
      if (!island || !Array.isArray(island.variants)) continue;
      var sources = parseVariants(island.variants);
      if (!sources.length) continue;
      for (var s2 = 0; s2 < sources.length; s2++) {
        if (covered[sources[s2].url]) { sources = []; break; }
      }
      if (!sources.length) continue; /* a complete entity already covers this video */
      var meta = metaBefore(text, lastEnd, k2);
      lastEnd = end + 1;
      var urlId = mediaIdFromStreamUrl(sources[0].url);
      pushRecord({
        tweetId: meta.tweetId,
        author: meta.author,
        text: null,
        type: "video",
        thumbnail: meta.thumbnail,
        mediaIds: urlId ? [urlId] : [],
        key: urlId || sources[0].url,
        sources: sources
      });
      pushed++;
    }
    return records;
  }

  function parseFlightText(text) {
    if (!text || typeof text !== "string" || text.indexOf("video_info") === -1) return [];
    var records = harvestFlight(text);
    if (!records.length && text.indexOf("\\\"") !== -1) {
      /* JSON-in-JSON escaped payload: unescape one level and retry */
      records = harvestFlight(text.replace(/\\\\\"/g, "\\\"").replace(/\\"/g, "\""));
    }
    return records;
  }

  return {
    parseMedia: parseMedia,
    parseVariants: parseVariants,
    parseVariantsLoose: parseVariantsLoose,
    sortSources: sortSources,
    bestSource: bestSource,
    qualityLabel: qualityLabel,
    fileNameFor: fileNameFor,
    classify: classify,
    dimsFromUrl: dimsFromUrl,
    extractLinks: extractLinks,
    parseXLink: parseXLink,
    parseFlightText: parseFlightText
  };
})();
