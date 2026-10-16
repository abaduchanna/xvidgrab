# 3SVerse VideoGrabber (Desktop)

> Windows desktop app of the [XVidGrab](../README.md) repo — the Firefox
> extension for X (Twitter) lives at the repository root.

Free and open-source video/audio downloader for Windows 10/11. Paste a URL, pick a
format, press **Download** — the file lands in the folder you chose, with a progress
bar and a full live log of what the engine is doing.

Built as a 100% legal product: the app itself is **MIT licensed** and the download
engine it drives is **yt-dlp** ([github.com/yt-dlp/yt-dlp](https://github.com/yt-dlp/yt-dlp)),
which is public-domain software (The Unlicense). Nothing in this project reuses or
rebrands anyone else's proprietary installer.

## Features

- Paste any video or audio page URL supported by yt-dlp (YouTube, Facebook, X/Twitter,
  Instagram, TikTok, Reddit, and hundreds more — whatever your yt-dlp build supports).
- Four formats in v1.0.0:
  - **Best MP4 (recommended)** — pre-merged MP4, works without ffmpeg
  - **Best quality (any format)** — highest available streams; merging separate
    video+audio streams needs `ffmpeg.exe` placed next to `VideoGrabber.exe`
  - **Audio only — M4A**
  - **Audio only — MP3** (needs `ffmpeg.exe` next to the app)
- Save-to folder picker (defaults to your `Downloads` folder), **Open folder** button
  when finished.
- Live progress bar parsed from yt-dlp's real `[download] 42.3%` lines, plus a
  complete engine log so failures always show the exact reason.
- Cancel button kills the running engine immediately.

## Install

### Setup (recommended)
1. Download `3SVerse_VideoGrabber_v1.0.0_Setup.exe` from
   [desktop releases](https://github.com/abaduchanna/xvidgrab/releases?q=desktop-v&expanded=true).
2. Run it — no administrator rights needed (installs into
   `%LOCALAPPDATA%\3SVerse\VideoGrabber`).
3. Optionally tick the desktop shortcut on the last page.

### Portable
Unzip `3SVerse_VideoGrabber_Portable.zip` anywhere and run `VideoGrabber.exe`.
Keep `yt-dlp.exe` in the same folder as `VideoGrabber.exe`.

## Using ffmpeg (optional)

`ffmpeg.exe` is not bundled. To unlock best-quality merging and MP3 conversion,
download a Windows build from <https://www.gyan.dev/ffmpeg/builds/> or
<https://github.com/BtbN/FFmpeg-Builds/releases>, and copy `bin\ffmpeg.exe` next to
`VideoGrabber.exe`. The app detects it automatically and passes
`--ffmpeg-location` to the engine.

## Usage

1. Copy the page URL of the video or audio you want.
2. Paste it into the URL box.
3. Pick a format.
4. Confirm the **Save to** folder.
5. Press **Download**. Watch the log; press **Cancel** any time.

Files are saved as `Title [id].ext` — clear names, no collisions.

## Fair use notice

This tool downloads content the yt-dlp engine supports. You are responsible for
respecting the terms of service of the platforms you use it with and the copyright
of the content you save. Download only what you have the right to keep.

## Build from source

```
windres src/icon.rc -O coff -o src/icon_res.o
gcc src/main.c src/icon_res.o -o VideoGrabber.exe -mwindows \
    -lcomctl32 -lshell32 -lole32 -lshlwapi -lgdi32 -luser32 -lkernel32 -s
```

CI (`.github/workflows/desktop.yml`, repo root) builds the exe + bundles the
latest yt-dlp.exe and compiles the Inno Setup installer on every push touching
`desktop/`; tags `desktop-v*` publish a GitHub release automatically.

## License

- App source and binaries: **MIT License** — see [LICENSE](LICENSE).
  Copyright (c) 2026 Abad Umair Channa / 3S Verse
- Bundled engine `yt-dlp.exe`: public domain (The Unlicense) — see
  [yt-dlp-UNLICENSE.txt](yt-dlp-UNLICENSE.txt). Downloaded at build time from the
  official yt-dlp releases, unmodified.
