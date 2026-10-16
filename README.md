# XVidGrab — X (Twitter) Video Downloader

![License](https://img.shields.io/badge/license-MIT-green)
![Firefox](https://img.shields.io/badge/Firefox-140%2B-1d9bf0)
![Chromium](https://img.shields.io/badge/Chrome%20%2F%20Edge%20%2F%20Brave-111%2B-4285f4)
![Manifest](https://img.shields.io/badge/Manifest-V2%20%2B%20V3-blue)
![Tracking](https://img.shields.io/badge/tracking-none-success)

**XVidGrab** is a free, open-source **cross-browser extension** that downloads
videos and animated GIFs from X (formerly Twitter) with one click. It runs in
**every major browser**: Firefox, Chrome, Edge, Brave, Opera, Vivaldi and
Yandex. Browse x.com as usual — XVidGrab passively detects video streams as
they load and lets you save them from the post itself or from a popup list
with quality selection.

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

## Install — Chrome / Edge / Brave / Opera / Vivaldi / Yandex (Chromium)

Since v1.1.0 the extension ships as Manifest V3 for every Chromium-based
browser. One package works in all of them:

1. Download `XVidGrab_Extension_Chrome.zip` from the
   [releases page](https://github.com/abaduchanna/xvidgrab/releases/latest)
   and unzip it into a folder you keep (e.g. `C:\XVidGrab`).
2. Open the extensions page:
   `chrome://extensions` (Chrome), `edge://extensions` (Edge),
   `brave://extensions` (Brave), `opera://extensions` (Opera),
   `vivaldi://extensions` (Vivaldi), `browser://tune` → extensions (Yandex).
3. Enable **Developer mode** (toggle, usually top-right).
4. Click **Load unpacked** and select the unzipped folder.
5. Pin the XVidGrab icon, open [x.com](https://x.com) and scroll past any
   post with a video — download buttons appear in the post action rows.

The extension stays installed until you remove it or delete the folder.
(Chromium blocks one-click installs of non-store extensions — that is a
browser security policy, not a bug in XVidGrab. "Load unpacked" is the
documented, supported route.)

## Install — Firefox

### Temporary (any Firefox, no signing — works right now)

1. Download `XVidGrab_Extension_Firefox.xpi` from the
   [releases page](https://github.com/abaduchanna/xvidgrab/releases/latest).
2. Open `about:debugging#/runtime/this-firefox`.
3. Click **Load Temporary Add-on…** and pick the `.xpi` file (or the
   `manifest.json` inside the unzipped folder).
4. Open [x.com](https://x.com) and scroll past any post with a video.

Temporary add-ons are removed when Firefox closes.

### "This add-on could not be verified" — why, and the permanent fix

Normal ("release") Firefox only installs extensions that are
cryptographically signed by Mozilla. Installing the unsigned build
permanently fails with *"could not be verified"* — that is Firefox's
signature enforcement, not a broken file. Three ways to make it
permanent:

**Route A — sign it on AMO (recommended; keeps your current Firefox).**

1. Create a free account at [addons.mozilla.org/developers](https://addons.mozilla.org/developers/).
2. Either upload `XVidGrab-<version>.zip` yourself at
   *Submit a New Add-on → on your own (self-distribution)* — AMO signs
   it and gives you a permanent `.xpi` — **or** run the bundled signer:
   - generate your API keys at
     [addons.mozilla.org/developers/addon/api/key](https://addons.mozilla.org/developers/addon/api/key/)
   - `./sign_amo.sh` (Linux/macOS) or `sign_amo.bat` (Windows; needs Node.js)
3. Install the signed `.xpi` via `about:addons` → gear icon →
   *Install Add-on From File…*. It then survives restarts and updates.

**Route B — Developer Edition / Nightly / ESR.** In those channels set
`xpinstall.signatures.required` to `false` in `about:config`, then
install the unsigned file via `about:addons` → gear icon →
*Install Add-on From File…*.

**Route C — temporary load** (above) whenever you need it; it expires
when Firefox closes.

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
└── background.js  (MV2 page in Firefox / MV3 service worker in Chromium)
      webRequest captures GraphQL auth headers (recovery/replay)
      downloads.download + onChanged-driven progress + history
```

The same codebase ships two manifests: `manifest.json` (Manifest V3,
service worker — Chromium) and `manifest.firefox.json` (Manifest V2,
persistent background — Firefox). CI packages both on every `v*` tag.
Download progress is driven purely by `downloads.onChanged` deltas, so it
behaves identically in MV2 pages and MV3 service workers (which may kill
timers, but never extension events).

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
- In Chromium browsers, Chromium's own store policy means the extension is
  loaded via "Load unpacked" (see install steps above) rather than a
  one-click web-store install.

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
