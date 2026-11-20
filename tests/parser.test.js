/*
 * XVidGrab — parser unit tests (Node).
 * Run: node tests/parser.test.js
 */
"use strict";

const fs = require("fs");
const path = require("path");

const src = fs.readFileSync(path.join(__dirname, "..", "content", "sniff-parse.js"), "utf8");
(0, eval)(src); /* indirect eval -> global scope -> global XVG */

let pass = 0;
let fail = 0;
function ok(cond, label) {
  if (cond) { pass += 1; console.log("  PASS", label); }
  else { fail += 1; console.log("  FAIL", label); }
}

/* ------------------------------------------------------------------ */
/* fixture 1 — TweetDetail (snake_case, thread with a video + a GIF)   */
/* ------------------------------------------------------------------ */
const tweetDetail = {
  data: {
    threaded_conversation_with_injections_v2: {
      items: [
        {
          item: {
            itemContent: {
              __typename: "TimelineTimelineItem",
              tweet_results: {
                result: {
                  __typename: "Tweet",
                  rest_id: "1711111111111111111",
                  core: {
                    user_results: {
                      result: {
                        __typename: "User",
                        legacy: { screen_name: "nasa" },
                        core: { screen_name: "nasa" }
                      }
                    }
                  },
                  note_tweet: {
                    note_tweet_results: {
                      result: { text: "Artemis II progress render. https://x.com/nasa/status/1711111111111111111" }
                    }
                  },
                  legacy: {
                    id_str: "1711111111111111111",
                    full_text: "Artemis II progress render.",
                    extended_entities: {
                      media: [
                        {
                          id_str: "1722222222222222222",
                          type: "video",
                          media_url_https: "https://pbs.twimg.com/media/abc123?format=jpg&name=small",
                          video_info: {
                            variants: [
                              { bitrate: 288000, content_type: "video/mp4", url: "https://video.twimg.com/ext_tw_video/1722222222222222222/pu/vid/320x180/pl1.mp4?tag=12" },
                              { bitrate: 832000, content_type: "video/mp4", url: "https://video.twimg.com/ext_tw_video/1722222222222222222/pu/vid/640x360/pl2.mp4?tag=12" },
                              { bitrate: 2176000, content_type: "video/mp4", url: "https://video.twimg.com/ext_tw_video/1722222222222222222/pu/vid/1280x720/pl3.mp4?tag=12" },
                              { content_type: "application/x-mpegURL", url: "https://video.twimg.com/ext_tw_video/1722222222222222222/pu/pl/1722222222222222222.m3u8" }
                            ]
                          }
                        }
                      ]
                    }
                  }
                }
              }
            }
          }
        },
        {
          item: {
            itemContent: {
              __typename: "TimelineTimelineItem",
              tweet_results: {
                result: {
                  __typename: "Tweet",
                  rest_id: "1713333333333333333",
                  core: { user_results: { result: { core: { screen_name: "spacex" } } } },
                  legacy: {
                    id_str: "1713333333333333333",
                    full_text: "booster catch loop",
                    extended_entities: {
                      media: [
                        {
                          id_str: "1744444444444444444",
                          type: "animated_gif",
                          media_url_https: "https://pbs.twimg.com/media/gif1?format=jpg&name=small",
                          video_info: {
                            variants: [
                              { bitrate: 1024000, content_type: "video/mp4", url: "https://video.twimg.com/tweet_video/1744444444444444444.mp4" }
                            ]
                          }
                        }
                      ]
                    }
                  }
                }
              }
            }
          }
        }
      ]
    }
  }
};

/* ------------------------------------------------------------------ */
/* fixture 2 — TweetResultByRestId (newer camelCase unified shape)     */
/* ------------------------------------------------------------------ */
const byRestId = {
  data: {
    tweetResult: {
      result: {
        __typename: "Tweet",
        rest_id: "1795555555555555555",
        core: {
          user_results: {
            result: { core: { screen_name: "3sverse" }, legacy: { screen_name: "3sverse" } }
          }
        },
        media: {
          video_info: {
            variants: [
              { bitrate: 1280000, contentType: "video/mp4", url: "https://video.twimg.com/amplify_video/1796666666666666666/vid/1080x1920/v1.mp4?tag=14" },
              { bitrate: 256000, contentType: "video/mp4", url: "https://video.twimg.com/amplify_video/1796666666666666666/vid/270x480/v2.mp4?tag=14" }
            ]
          }
        },
        legacy: { id_str: "1795555555555555555", full_text: "landscape test" }
      }
    }
  }
};

/* ------------------------------------------------------------------ */
/* fixture 3 — quoted tweet inside a thread (context switching)        */
/* ------------------------------------------------------------------ */
const quoted = {
  data: {
    tweet: {
      result: {
        __typename: "Tweet",
        rest_id: "1880000000000000001",
        core: { user_results: { result: { core: { screen_name: "outer_user" } } } },
        legacy: {
          id_str: "1880000000000000001",
          full_text: "look at this https://x.com/inner_user/status/1880000000000000002",
          extended_entities: {
            media: [{ id_str: "1880000000000000010", type: "video", media_url_https: "https://pbs.twimg.com/media/o.jpg", video_info: { variants: [{ bitrate: 640000, content_type: "video/mp4", url: "https://video.twimg.com/tweet_video/o1.mp4" }] } }]
          }
        },
        quoted_status_result: {
          result: {
            __typename: "Tweet",
            rest_id: "1880000000000000002",
            core: { user_results: { result: { core: { screen_name: "inner_user" } } } },
            legacy: {
              id_str: "1880000000000000002",
              full_text: "inner video",
              extended_entities: {
                media: [{ id_str: "1880000000000000020", type: "video", media_url_https: "https://pbs.twimg.com/media/i.jpg", video_info: { variants: [{ bitrate: 2176000, content_type: "video/mp4", url: "https://video.twimg.com/tweet_video/i1.mp4" }] } }]
              }
            }
          }
        }
      }
    }
  }
};

/* ------------------------------------------------------------------ */
console.log("[1] TweetDetail fixture");
const r1 = XVG.parseMedia(tweetDetail);
ok(r1.length === 2, "two media records extracted, got " + r1.length);
const vid = r1.find((r) => r.tweetId === "1711111111111111111");
ok(!!vid, "video record has tweet id");
ok(vid.author === "nasa", "author resolved: " + vid.author);
ok(/Artemis II/.test(vid.text || ""), "text resolved from note_tweet");
ok(vid.sources.length === 4, "all 4 variants parsed, got " + vid.sources.length);
ok(vid.sources[0].kind === "mp4" && vid.sources[0].height === 720, "best source = 720p mp4, got " + JSON.stringify(vid.sources[0] && vid.sources[0].label));
ok(vid.sources[0].label === "720p", "quality label 720p, got " + vid.sources[0].label);
ok(vid.sources[vid.sources.length - 1].kind === "hls", "hls sorted last");
const gif = r1.find((r) => r.type === "gif");
ok(!!gif && gif.author === "spacex", "gif record + author (core.screen_name path)");
ok(XVG.fileNameFor(gif, gif.sources[0]) === "xvidgrab_spacex_1713333333333333333_480p.mp4" ||
   /xvidgrab_spacex_1713333333333333333/.test(XVG.fileNameFor(gif, gif.sources[0])), "filename pattern: " + XVG.fileNameFor(gif, gif.sources[0]));

console.log("[2] TweetResultByRestId (camelCase)");
const r2 = XVG.parseMedia(byRestId);
ok(r2.length === 1, "one record, got " + r2.length);
ok(r2[0].tweetId === "1795555555555555555", "tweetId from rest_id");
ok(r2[0].author === "3sverse", "author camelCase shape");
ok(r2[0].sources.length === 2, "camelCase contentType variants parsed");
ok(r2[0].sources[0].height === 1920 && r2[0].sources[0].label === "1080p", "portrait 1080p label: " + r2[0].sources[0].label);
ok(XVG.fileNameFor(r2[0], r2[0].sources[0]) === "xvidgrab_3sverse_1795555555555555555_1080p.mp4", "filename: " + XVG.fileNameFor(r2[0], r2[0].sources[0]));

console.log("[3] quoted tweet attribution");
const r3 = XVG.parseMedia(quoted);
ok(r3.length === 2, "outer + inner records, got " + r3.length);
const inner = r3.find((r) => r.tweetId === "1880000000000000002");
const outer = r3.find((r) => r.tweetId === "1880000000000000001");
ok(!!inner && inner.author === "inner_user", "inner tweet attributed to inner_user");
ok(!!outer && outer.author === "outer_user", "outer tweet attributed to outer_user");

console.log("[4] merge behaviour");
const a1 = XVG.parseMedia(tweetDetail);
const a2 = XVG.parseMedia(tweetDetail);
const merged = a1.concat(a2);
const seen = new Map();
for (const rec of merged) {
  const prev = seen.get(rec.key);
  if (prev) {
    const urls = new Set(prev.sources.map((s) => s.url));
    for (const s of rec.sources) if (!urls.has(s.url)) prev.sources.push(s);
  } else seen.set(rec.key, Object.assign({}, rec));
}
ok(seen.size === 2, "records deduped by key, got " + seen.size);
ok(Array.from(seen.values()).every((r) => r.sources.length === r.sources.filter((s, i, arr) => arr.findIndex((x) => x.url === s.url) === i).length), "no duplicate sources after merge");

console.log("[5] utils");
ok(XVG.classify("https://video.twimg.com/x.m3u8?tag=1", null) === "hls", "m3u8 by extension");
ok(XVG.classify("https://video.twimg.com/x.mp4?tag=1", null) === "mp4", "mp4 by extension");
ok(XVG.classify("https://x/y", "video/mp4;codecs=avc1") === "mp4", "mp4 by content-type with codecs");
ok(XVG.parseVariants([]).length === 0, "empty variants safe");
ok(XVG.parseMedia(null).length === 0, "null payload safe");
ok(XVG.parseMedia({ data: { thread: { a: [1, 2, 3] } } }).length === 0, "no-media payload safe");
const cyc = { self: null, media: { type: "video", video_info: { variants: [{ bitrate: 500, content_type: "video/mp4", url: "https://video.twimg.com/c.mp4" }] } } };
cyc.self = cyc;
ok(XVG.parseMedia(cyc).length === 1, "cyclic payload safe (WeakSet guard)");
ok(XVG.dimsFromUrl("https://video.twimg.com/a/720x1280/x.mp4").h === 1280, "portrait dims from URL");

console.log("[6] link parsing (parseXLink)");
let p;
p = XVG.parseXLink("https://x.com/Kazimirol2");
ok(p.ok && p.kind === "profile" && p.screenName === "Kazimirol2", "bare profile link -> profile, got " + JSON.stringify(p));
ok(p.url === "https://x.com/Kazimirol2" && p.url2 === "https://x.com/Kazimirol2/media", "profile fetch plan = root + media tab");

p = XVG.parseXLink("x.com/Kazimirol2");
ok(p.ok && p.kind === "profile", "scheme-less profile link ok");

p = XVG.parseXLink("https://x.com/Kazimirol2/media");
ok(p.ok && p.kind === "profile" && p.url === "https://x.com/Kazimirol2/media" && p.url2 === null, "explicit /media -> media pass only");

p = XVG.parseXLink("https://twitter.com/nasa/status/1711111111111111111/video/1");
ok(p.ok && p.kind === "tweet" && p.tweetId === "1711111111111111111", "twitter.com status + /video/1 -> tweet");

p = XVG.parseXLink("https://x.com/i/web/status/1795555555555555555?s=21");
ok(p.ok && p.kind === "tweet" && p.tweetId === "1795555555555555555" && p.url === "https://x.com/i/web/status/1795555555555555555", "i/web/status link normalized, tracking param dropped");

p = XVG.parseXLink("https://x.com/search?q=cat%20videos&src=typed_query");
ok(p.ok && p.kind === "search" && p.url === "https://x.com/search?q=cat%20videos", "search link keeps query, drops tracking");

p = XVG.parseXLink("https://x.com/explore");
ok(p.ok && p.kind === "page", "explore -> generic page");

p = XVG.parseXLink("https://x.com/i/lists/12345");
ok(p.ok && p.kind === "page", "list page -> generic page");

p = XVG.parseXLink("https://google.com/nasa");
ok(!p.ok, "non-X host rejected");

p = XVG.parseXLink("not a link at all");
ok(!p.ok, "garbage rejected");

p = XVG.parseXLink("https://mobile.twitter.com/NASA");
ok(p.ok && p.kind === "profile" && p.screenName === "NASA", "mobile.twitter.com normalized");

console.log("[7] extractLinks");
const found = XVG.extractLinks("Check https://x.com/Kazimirol2 and https://x.com/nasa/status/123, plus t.co/xyz!");
ok(found.length === 3, "three links found in text, got " + found.length);
ok(found[0] === "https://x.com/Kazimirol2", "first link clean");
ok(found[1] === "https://x.com/nasa/status/123", "trailing comma stripped");

console.log("[8] flight-stream salvage (parseFlightText)");
const flight = '1b:["$","div",null,{"props":"x"}]2a:["$","$L3",null,{"tweet":{"rest_id":"1711111111111111111","video_info":{"variants":[{"bitrate":2176000,"content_type":"video/mp4","url":"https://video.twimg.com/f1.mp4"},{"content_type":"application/x-mpegURL","url":"https://video.twimg.com/f1.m3u8"}]}}}] 4d:["$","p",null,"end"]';
const fs1 = XVG.parseFlightText(flight);
ok(fs1.length === 1, "one video recovered from flight text, got " + fs1.length);
ok(fs1[0].sources.length === 2 && fs1[0].sources[0].kind === "mp4", "variants parsed from salvage island");
/* parseFlightText harvests any islands it finds; the sniffer only calls it
 * AFTER JSON.parse fails, so valid JSON payloads never reach this path. */
ok(XVG.parseFlightText(JSON.stringify(tweetDetail)).length === 2, "islands inside normal JSON also harvestable (fallback contract)");
ok(XVG.parseFlightText("").length === 0, "empty text safe");
const escapedFlight = 'chunk:["x",{\\"video_info\\":{\\"variants\\":[{\\"bitrate\\":640000,\\"content_type\\":\\"video/mp4\\",\\"url\\":\\"https://video.twimg.com/e1.mp4\\"}]}}]';
const fs2 = XVG.parseFlightText(escapedFlight);
ok(fs2.length === 1 && fs2[0].sources.length === 1, "escaped JSON-in-JSON island recovered");

console.log("[9] flight salvage keeps thumbnails + ids (media_entities2 island)");
/* Real-world shape: x.com embeds timeline data as escaped JSON-in-JSON;
 * each media entity carries the poster (media_url_https), the media id
 * and an expanded_url with the tweet context. v1.4.0 dropped all of it
 * (thumbnail: null) because only the bare video_info object was harvested. */
const mediaEntity = {
  additional_media_info: { monetizable: false },
  expanded_url: "https://x.com/NASA/status/2123798314843787300/video/1",
  ext_media_availability: { status: "Available" },
  id_str: "2123798254152208384",
  indices: [255, 278],
  media_url_https: "https://pbs.twimg.com/media/HXk_T7cWcAAEUL9.jpg",
  original_info: { height: 405, width: 720 },
  type: "video",
  video_info: {
    duration_millis: 100714,
    variants: [
      { content_type: "application/x-mpegURL", url: "https://video.twimg.com/amplify_video/2123798254152208384/pl/master.m3u8?tag=16" },
      { bitrate: 2176000, content_type: "video/mp4", url: "https://video.twimg.com/amplify_video/2123798254152208384/vid/1080x720/m1.mp4?tag=16" },
      { bitrate: 632000, content_type: "video/mp4", url: "https://video.twimg.com/amplify_video/2123798254152208384/vid/640x360/m2.mp4?tag=16" }
    ]
  }
};
const pageFlight = '<script>requestAnimationFrame(function(){ver=1})</script>' +
  'a1:["$","div",null,{"props":"x"}]2b:["$","$L5",null,{"timeline":' +
  JSON.stringify({ media_entities2: [mediaEntity] }).replace(/"/g, '\\"') + '}]';
const fs3 = XVG.parseFlightText(pageFlight);
ok(fs3.length === 1, "media entity recovered from escaped page flight, got " + fs3.length);
ok(fs3.length && !!fs3[0].thumbnail, "thumbnail kept (media_url_https)");
ok(fs3.length && fs3[0].thumbnail === "https://pbs.twimg.com/media/HXk_T7cWcAAEUL9.jpg", "thumbnail URL exact");
ok(fs3.length && fs3[0].author === "NASA", "author from expanded_url, got " + (fs3[0] && fs3[0].author));
ok(fs3.length && fs3[0].tweetId === "2123798314843787300", "tweetId from expanded_url");
ok(fs3.length && fs3[0].mediaIds.indexOf("2123798254152208384") !== -1, "media id kept");
ok(fs3.length && fs3[0].key === "2123798254152208384", "keyed by media id so the JSON path merges into one row");

/* unescaped variant of the same payload */
const fs3b = XVG.parseFlightText(JSON.stringify({ media_entities2: [mediaEntity] }) + " and some junk that breaks JSON.parse on purpose");
ok(fs3b.length === 1 && !!fs3b[0].thumbnail, "unescaped media_entities2 island harvested");

/* bare video_info island: metadata recovered from the NEIGHBOURING entity fields */
const bareFlight = '{"m":[' +
  '{"id_str":"2123798254152208384","media_url_https":"https://pbs.twimg.com/media/Near.jpg","type":"video",' +
  '"expanded_url":"https://x.com/nasa/status/2123798314843787300/video/1",' +
  '"video_info":{"variants":[{"bitrate":640000,"content_type":"video/mp4","url":"https://video.twimg.com/ext_tw_video/2123798254152208384/pu/vid/b1.mp4"}]}}]}x';
const fs4 = XVG.parseFlightText(bareFlight);
ok(fs4.length === 1, "bare island recovered");
ok(fs4.length && !!fs4[0].thumbnail && fs4[0].author === "nasa" && fs4[0].tweetId === "2123798314843787300",
  "bare island enriched with nearby thumbnail + author + tweetId");

/* same video from BOTH paths must merge on one key, not duplicate */
const bothPaths = XVG.parseMedia({ media: mediaEntity });
ok(bothPaths.length === 1 && bothPaths[0].key === "2123798254152208384", "JSON path uses the same media-id key");

console.log("[10] syndication embed shape (cdn.syndication.twimg.com/tweet-result)");
const synd = XVG.parseMedia({
  tweetResult: {
    result: {
      __typename: "Tweet",
      id_str: "2123435938848067841",
      text: "Artemis II wakeup call",
      user: { id_str: "19999991", screen_name: "NASA", name: "NASA" },
      mediaDetails: [
        {
          type: "video",
          media_url_https: "https://pbs.twimg.com/amplify_video_thumb/2123435905390100480/img/UAjjfTl4lSQubpUI.jpg",
          video_info: {
            variants: [
              { bitrate: 1280000, content_type: "video/mp4", url: "https://video.twimg.com/amplify_video/2123435905390100480/vid/avc1/1280x720/s1.mp4?tag=16" },
              { bitrate: 320000, content_type: "video/mp4", url: "https://video.twimg.com/amplify_video/2123435905390100480/vid/avc1/320x180/s2.mp4?tag=16" }
            ]
          }
        }
      ]
    }
  }
});
ok(synd.length === 1, "syndication tweet parsed, got " + synd.length);
ok(synd.length && synd[0].tweetId === "2123435938848067841", "tweetId from flat id_str + user");
ok(synd.length && synd[0].author === "NASA", "author from flat user.screen_name");
ok(synd.length && synd[0].text === "Artemis II wakeup call", "text from flat text field");
ok(synd.length && !!synd[0].thumbnail, "thumbnail from mediaDetails media_url_https");
ok(synd.length && synd[0].key === "2123435905390100480", "key from stream-URL media id");

console.log("");
console.log("RESULT: " + pass + " passed, " + fail + " failed");
process.exit(fail ? 1 : 0);
