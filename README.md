# XVidGrab — X (Twitter) Video Downloader

![License](https://img.shields.io/badge/license-MIT-green)
![Firefox](https://img.shields.io/badge/Firefox-140%2B-1d9bf0)
![Manifest](https://img.shields.io/badge/Manifest%20V2-webextension-blue)
![Tracking](https://img.shields.io/badge/tracking-none-success)

**XVidGrab** is a free, open-source Firefox extension that downloads videos and
animated GIFs from X (formerly Twitter) with one click. Browse x.com as usual —
XVidGrab passively detects video streams as they load and lets you save them
from the post itself or from a popup list with quality selection.

It is a clean-room, fully local alternative to freemium downloaders such as
*App Pass's "X (Twitter) Video Downloader"*: the same core workflow
(page-level stream detection + post button + popup list), but **without
subscriptions, download limits, accounts, or telemetry**. No code was copied
from any proprietary extension — this project is written from scratch and
released under the MIT license.

## Features

- **One-click download** — a native-feeling download button is injected into
  the action row of every post containing a video or animated GIF.
- **Best quality automatically** — progressive MP4 variants are ranked by
  bitrate and resolution (`720p`, `1080p`…) with standard
  shorter-side resolution tiers. `Smallest file` mode is available in the popup.
- **Popup video list** — every video detected on the active tab, with
  thumbnail, author, post text, per-video quality selector and a
  **free "Download all"** button (a paid feature in App Pass).
- **Live progress panel** — per-download progress bar on the page, driven by
  the browser's real download state (bytes received / total).
- **Missed-response recovery** — the background page captures X GraphQL
  request URLs (with your already-present session headers) so posts that
  loaded before the extension was injected can still be resolved by replay or
  on-demand fetch.
- **Toolbar badge** — shows how many videos the current tab has detected.
- **GIF support** — animated GIFs are saved as the MP4 loops X actually
  stores (GIF files on X are MP4 without audio).
- **No limits, no nagging, no servers** — every byte flows directly from X's
  CDN to your Downloads folder.

## Install (Firefox desktop)

### Temporary (any Firefox, no signing)

1. Download the latest `XVidGrab-<version>.zip` (or build it yourself, below).
2. Open `about:debugging#/runtime/this-firefox`.
3. Click **Load Temporary Add-on…** and pick `manifest.json` inside the zip
   (unzip first).
4. Open [x.com](https://x.com) and scroll past any post with a video.

Temporary add-ons are removed when Firefox closes.

### Permanent

Firefox requires extensions to be cryptographically signed (by
[addons.mozilla.org](https://addons.mozilla.org)) before normal installation.
Options:

- Self-distribute: sign the built zip as an **unlisted add-on** on AMO, or
- Use **Firefox Developer Edition** / **Nightly** / **ESR** and set
  `xpinstall.signatures.required` to `false` in `about:config`, then load the
  zip via `about:addons` → gear icon → *Install Add-on From File…*.

## How it works

```
x.com page
├── content/sniffer.js   (page world, document_start)
│     wraps window.fetch + XMLHttpRequest
│     → parses GraphQL JSON for video_info / videoInfo variants
│     → emits xvg:media CustomEvents (string detail, page-local)
├── content/main.js      (isolated sandbox)
│     stores records · injects action-row buttons · progress panel
│     → downloads via background relay
└── background.js
      webRequest captures GraphQL auth headers (recovery/replay)
      browser.downloads.download + progress polling + history
```

Media extraction walks X's GraphQL responses (`TweetDetail`,
`TweetResultByRestId`, timeline and profile responses — current and legacy
schemas) for `video_info.variants` / `videoInfo.variants`. Progressive MP4
variants are always preferred because they are single files with muxed audio;
HLS master playlists are listed for information but not downloaded.

## Privacy

- No analytics, no telemetry, no remote servers, no accounts.
- The only network requests XVidGrab ever makes are to X's own endpoints with
  your existing logged-in session (recovery fetches) and X's public embed
  fallback — all from your browser, on your machine.
- The extension declares `data_collection_permissions: none`.
- Download history is stored locally (`storage.local`) and never leaves the
  browser; the popup can clear it anytime.

## Limitations

- Posts whose media exists **only** as HLS (some live broadcasts/replays)
  cannot be saved as MP4 by a purely local tool — those rows are marked
  "no MP4 variant".
- Very old tweets never re-fetched after installation may need a manual page
  reload (or one extra scroll) before the stream metadata appears.
- Firefox for Android is not supported (buttons rely on desktop UI hooks).

## Build from source

```bash
git clone https://github.com/abaduchanna/xvidgrab.git
cd xvidgrab
npm install --global web-ext   # once
web-ext lint --ignore-files tests
web-ext build --ignore-files tests --artifacts-dir ../dist
node tests/parser.test.js      # 29 parser assertions
```

Requires Node.js 18+ for the test-suite. No bundler, no build step — the
extension ships plain JavaScript.

## Disclaimer

This project is for personal, educational use. Respect content creators'
rights and X's Terms of Service — do not redistribute copyrighted media
without permission. XVidGrab is not affiliated with X Corp. or App Pass.

## License

[MIT](LICENSE) © 2026 3SVerse
