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
        (!!o.rest_id && (!!o.legacy || !!o.core));
      if (isTweet) {
        var id = str(o.rest_id) || (o.legacy && str(o.legacy.id_str));
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

      /* Standalone video_info nodes (media cards, newer unified shapes). */
      if (!mediaArr) {
        var sources = parseVariantsLoose(o);
        if (sources.length) {
          var mid = str(o.id_str) && String(o.id_str).length >= 8 ? str(o.id_str) : null;
          push({
            tweetId: ctx.id,
            author: ctx.author,
            text: ctx.text,
            type: o.type === "animated_gif" ? "gif" : "video",
            thumbnail: thumbOf(o),
            mediaIds: mid ? [mid] : [],
            key: mid || sources[0].url,
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

  return {
    parseMedia: parseMedia,
    parseVariants: parseVariants,
    parseVariantsLoose: parseVariantsLoose,
    sortSources: sortSources,
    bestSource: bestSource,
    qualityLabel: qualityLabel,
    fileNameFor: fileNameFor,
    classify: classify,
    dimsFromUrl: dimsFromUrl
  };
})();
