/* 3SVerse VideoGrabber v1.1.0 — free & open video/audio downloader GUI (yt-dlp engine)
 * MIT License — Copyright (c) 2026 Abad Umair Channa / 3S Verse
 * Engine: yt-dlp (https://github.com/yt-dlp/yt-dlp) — public domain (The Unlicense)
 *
 * v1.1.0 — full 3SVerse rebrand:
 *   - custom GDI skin with dark & light themes (one-click toggle, persisted
 *     in HKCU\Software\3SVerse\VideoGrabber)
 *   - header drawn with the 3SVerse brand: gradient spiral logo, glowing orb,
 *     wordmark + accent gradient bar
 *   - dark title bar (DWM immersive dark mode) + rounded corners on Win11
 *   - owner-drawn buttons with hover states, themed edit/combo/log controls
 *   - custom rounded progress bar with percent overlay
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#define APP_TITLEW  L"3SVerse VideoGrabber v1.1.0"
#define APP_CLASSW  L"3SVerseVideoGrabber"
#define WM_APP_DONE (WM_APP + 1)

#define IDC_URL      101
#define IDC_FMT      102
#define IDC_DIR      103
#define IDC_BROWSE   104
#define IDC_GO       105
#define IDC_CANCEL   106
#define IDC_OPEN     107
#define IDC_LOG      108
#define IDC_THEME    110

#define CLIENT_W  680
#define CLIENT_H  556
#define HEADER_H  92

static const char *g_fmtArgs[] = {
    " -f \"bv*[ext=mp4]+ba[ext=m4a]/b[ext=mp4]/b\"",   /* 0: best MP4 */
    " -f \"bv*+ba/b\"",                                 /* 1: best quality */
    " -f \"ba[ext=m4a]/ba\"",                           /* 2: audio m4a */
    " -x --audio-format mp3 --audio-quality 0"          /* 3: audio mp3 (ffmpeg) */
};

/* ------------------------------------------------------------------ */
/* palette                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    COLORREF bg, headerBg, txt, txtDim, txtMute;
    COLORREF editBg, editTxt, editBorder;
    COLORREF btnFace, btnHi, btnDn, btnTxt;
    COLORREF accent, accent2, primTxt;
    COLORREF progTrack, progFill;
    COLORREF logBg, logTxt;
} PALETTE;

static PALETTE g_pal;
static BOOL    g_dark = TRUE;

static void SetDarkPalette(PALETTE *p)
{
    p->bg         = RGB(23, 25, 31);
    p->headerBg   = RGB(28, 30, 38);
    p->txt        = RGB(232, 235, 242);
    p->txtDim     = RGB(150, 156, 170);
    p->txtMute    = RGB(104, 110, 124);
    p->editBg     = RGB(17, 19, 24);
    p->editTxt    = RGB(235, 238, 245);
    p->editBorder = RGB(55, 60, 72);
    p->btnFace    = RGB(38, 42, 51);
    p->btnHi      = RGB(48, 53, 64);
    p->btnDn      = RGB(28, 32, 40);
    p->btnTxt     = RGB(225, 229, 238);
    p->accent     = RGB(0, 176, 240);    /* 3SVerse cyan-blue */
    p->accent2    = RGB(124, 58, 237);   /* 3SVerse violet */
    p->primTxt    = RGB(255, 255, 255);
    p->progTrack  = RGB(30, 33, 41);
    p->progFill   = RGB(0, 160, 225);
    p->logBg      = RGB(16, 18, 23);
    p->logTxt     = RGB(190, 196, 208);
}

static void SetLightPalette(PALETTE *p)
{
    p->bg         = RGB(243, 245, 248);
    p->headerBg   = RGB(255, 255, 255);
    p->txt        = RGB(28, 32, 40);
    p->txtDim     = RGB(95, 102, 116);
    p->txtMute    = RGB(140, 147, 160);
    p->editBg     = RGB(255, 255, 255);
    p->editTxt    = RGB(28, 32, 40);
    p->editBorder = RGB(205, 210, 220);
    p->btnFace    = RGB(238, 241, 246);
    p->btnHi      = RGB(248, 250, 253);
    p->btnDn      = RGB(225, 229, 236);
    p->btnTxt     = RGB(40, 44, 54);
    p->accent     = RGB(0, 150, 214);
    p->accent2    = RGB(110, 52, 215);
    p->primTxt    = RGB(255, 255, 255);
    p->progTrack  = RGB(226, 230, 238);
    p->progFill   = RGB(0, 140, 200);
    p->logBg      = RGB(252, 253, 255);
    p->logTxt     = RGB(60, 66, 78);
}

static void ApplyPalette(void)
{
    if (g_dark) SetDarkPalette(&g_pal); else SetLightPalette(&g_pal);
}

/* ------------------------------------------------------------------ */
/* globals                                                             */
/* ------------------------------------------------------------------ */
static HWND g_hUrl, g_hFmt, g_hDir, g_hLog, g_hGo, g_hCancel, g_hOpen,
            g_hBrowse, g_hTheme, g_hMain;
static HFONT g_hFont, g_hMono, g_hFontSm, g_hFontBrand;
static HBRUSH g_brBg, g_brEdit, g_brLog;
static RECT g_rcProg;

/* job state */
static PROCESS_INFORMATION g_pi;
static volatile LONG g_running = 0;
static HANDLE g_hReader = NULL;

/* pending log data (UTF-8) shared between reader thread and UI */
static CRITICAL_SECTION g_cs;
static char  *g_pending = NULL;
static size_t g_pendingLen = 0, g_pendingCap = 0;
static WCHAR *g_shown = NULL;
static size_t g_shownLen = 0, g_shownCap = 0;
static int    g_percent = -1;     /* -1 = unknown */

/* ------------------------------------------------------------------ */
/* hover-tracked owner-draw buttons (classic SetWindowLongPtr — no     */
/* comctl32 v6 dependency)                                             */
/* ------------------------------------------------------------------ */
typedef struct { WNDPROC old; HWND hwnd; int hover; } BTNCTX;
static BTNCTX g_ctx[8];
static int    g_ctxN = 0;

static LRESULT CALLBACK HoverBtnProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    BTNCTX *c = (BTNCTX *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (c) {
        if (m == WM_MOUSEMOVE && !c->hover) {
            TRACKMOUSEEVENT t;
            c->hover = 1;
            ZeroMemory(&t, sizeof(t));
            t.cbSize = sizeof(t);
            t.dwFlags = TME_LEAVE;
            t.hwndTrack = h;
            TrackMouseEvent(&t);
            InvalidateRect(h, NULL, TRUE);
        } else if (m == WM_MOUSELEAVE) {
            c->hover = 0;
            InvalidateRect(h, NULL, TRUE);
        }
    }
    return CallWindowProcW(c ? c->old : DefWindowProcW, h, m, w, l);
}

static void TrackHover(HWND h)
{
    BTNCTX *c;
    if (g_ctxN >= 8) return;
    c = &g_ctx[g_ctxN++];
    c->hover = 0;
    c->hwnd = h;
    c->old = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)c);
    SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)HoverBtnProc);
}

static int IsHover(HWND h)
{
    int i;
    for (i = 0; i < g_ctxN; i++)
        if (g_ctx[i].hwnd == h) return g_ctx[i].hover;
    return 0;
}

/* ------------------------------------------------------------------ */
/* small drawing helpers                                               */
/* ------------------------------------------------------------------ */
static COLORREF Lerp(COLORREF a, COLORREF b, double t)
{
    int r, g, bl;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    r  = GetRValue(a) + (int)((GetRValue(b) - GetRValue(a)) * t);
    g  = GetGValue(a) + (int)((GetGValue(b) - GetGValue(a)) * t);
    bl = GetBValue(a) + (int)((GetBValue(b) - GetBValue(a)) * t);
    if (r < 0) r = 0;  if (r > 255) r = 255;
    if (g < 0) g = 0;  if (g > 255) g = 255;
    if (bl < 0) bl = 0; if (bl > 255) bl = 255;
    return RGB(r, g, bl);
}

static COLORREF Shade(COLORREF c, double f)
{
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    if (f <= 1.0) {
        r = (int)(r * f); g = (int)(g * f); b = (int)(b * f);
    } else {
        double t = f - 1.0;
        r = (int)(r + (255 - r) * t);
        g = (int)(g + (255 - g) * t);
        b = (int)(b + (255 - b) * t);
    }
    if (r < 0) r = 0;  if (r > 255) r = 255;
    if (g < 0) g = 0;  if (g > 255) g = 255;
    if (b < 0) b = 0;  if (b > 255) b = 255;
    return RGB(r, g, b);
}

static void FillSolid(HDC dc, RECT r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void FillRound(HDC dc, RECT r, int rad, COLORREF fill, COLORREF outline)
{
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, outline);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

/* 3SVerse brand mark: archimedean spiral stroked with the brand gradient */
static void DrawSpiral(HDC dc, int cx, int cy, double scale, COLORREF c1, COLORREF c2)
{
    const int SEG = 12, PTS = 12;
    const double TWO_PI = 6.28318530717958647692;
    int s, i;
    for (s = 0; s < SEG; s++) {
        POINT pts[13];
        HPEN p, op;
        p = CreatePen(PS_SOLID, 2, Lerp(c1, c2, ((double)s + 0.5) / SEG));
        op = SelectObject(dc, p);
        for (i = 0; i <= PTS; i++) {
            int k = s * PTS + i;
            double t = k * (2.5 * TWO_PI) / (SEG * PTS);
            double r = 1.55 * t * scale;
            pts[i].x = (int)(cx + r * cos(t));
            pts[i].y = (int)(cy - r * sin(t));
        }
        MoveToEx(dc, pts[0].x, pts[0].y, NULL);
        for (i = 1; i <= PTS; i++) LineTo(dc, pts[i].x, pts[i].y);
        SelectObject(dc, op);
        DeleteObject(p);
    }
}

/* glowing orb: concentric discs lerping core->edge, fading into bg at rim */
static void DrawOrb(HDC dc, int cx, int cy, int R, COLORREF core, COLORREF edge, COLORREF bgc)
{
    int i;
    HPEN np = (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ op = SelectObject(dc, np);
    for (i = 18; i >= 1; i--) {
        double f = i / 18.0;
        COLORREF c;
        int r = (int)(R * f);
        HBRUSH b;
        HGDIOBJ ob;
        if (f > 0.62) c = Lerp(edge, bgc, (f - 0.62) / 0.38);
        else          c = Lerp(core, edge, f / 0.62);
        b = CreateSolidBrush(c);
        ob = SelectObject(dc, b);
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        SelectObject(dc, ob);
        DeleteObject(b);
    }
    SelectObject(dc, op);
}

/* ------------------------------------------------------------------ */
/* theme persistence (HKCU\Software\3SVerse\VideoGrabber)              */
/* ------------------------------------------------------------------ */
static void LoadTheme(void)
{
    HKEY k;
    g_dark = TRUE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\3SVerse\\VideoGrabber",
                      0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        WCHAR v[16] = L"";
        DWORD cb = sizeof(v), type = 0;
        if (RegQueryValueExW(k, L"Theme", NULL, &type, (BYTE *)v, &cb) == ERROR_SUCCESS
            && type == REG_SZ)
            g_dark = lstrcmpiW(v, L"light") != 0;
        RegCloseKey(k);
    }
}

static void SaveTheme(void)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\3SVerse\\VideoGrabber",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        const WCHAR *v = g_dark ? L"dark" : L"light";
        RegSetValueExW(k, L"Theme", 0, REG_SZ, (const BYTE *)v,
                       (DWORD)((lstrlenW(v) + 1) * sizeof(WCHAR)));
        RegCloseKey(k);
    }
}

typedef HRESULT (WINAPI *FN_DwmSetAttr)(HWND, DWORD, const void *, DWORD);

static void ApplyDwm(HWND w)
{
    HMODULE m = GetModuleHandleW(L"dwmapi.dll");
    FN_DwmSetAttr f;
    BOOL dark = g_dark;
    DWORD pref = 2; /* rounded corners */
    if (!m || !w) return;
    f = (FN_DwmSetAttr)(void *)GetProcAddress(m, "DwmSetWindowAttribute");
    if (!f) return;
    f(w, 20, &dark, sizeof(dark));   /* DWMWA_USE_IMMERSIVE_DARK_MODE */
    f(w, 19, &dark, sizeof(dark));   /* older builds */
    f(w, 33, &pref, sizeof(pref));   /* DWMWA_WINDOW_CORNER_PREFERENCE */
}

static void RebuildBrushes(void)
{
    if (g_brBg)   DeleteObject(g_brBg);
    if (g_brEdit) DeleteObject(g_brEdit);
    if (g_brLog)  DeleteObject(g_brLog);
    g_brBg   = CreateSolidBrush(g_pal.bg);
    g_brEdit = CreateSolidBrush(g_pal.editBg);
    g_brLog  = CreateSolidBrush(g_pal.logBg);
}

/* ------------------------------------------------------------------ */
/* log plumbing (unchanged engine side)                                */
/* ------------------------------------------------------------------ */
static void PushPending(const char *data, size_t len)
{
    EnterCriticalSection(&g_cs);
    if (g_pendingLen + len + 1 > g_pendingCap) {
        size_t nc = (g_pendingCap ? g_pendingCap : 8192);
        while (nc < g_pendingLen + len + 1) nc *= 2;
        char *nb = (char *)HeapReAlloc(GetProcessHeap(), 0, g_pending, nc);
        if (nb) { g_pending = nb; g_pendingCap = nc; }
    }
    if (g_pending && g_pendingLen + len + 1 <= g_pendingCap) {
        memcpy(g_pending + g_pendingLen, data, len);
        g_pendingLen += len;
        g_pending[g_pendingLen] = 0;
    }
    LeaveCriticalSection(&g_cs);
}

static void AppendShown(const char *utf8)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)strlen(utf8), NULL, 0);
    if (wlen < 0) return;
    if (g_shownLen + (size_t)wlen + 1 > 130000 && g_shown) { /* cap: drop oldest half */
        size_t keep = g_shownLen / 2;
        memmove(g_shown, g_shown + (g_shownLen - keep), keep * sizeof(WCHAR));
        g_shownLen = keep;
    }
    if (g_shownLen + (size_t)wlen + 1 > g_shownCap) {
        size_t nc = (g_shownCap ? g_shownCap : 16384);
        while (nc < g_shownLen + (size_t)wlen + 1) nc *= 2;
        WCHAR *nb = (WCHAR *)HeapReAlloc(GetProcessHeap(), 0, g_shown, nc * sizeof(WCHAR));
        if (!nb) return;
        g_shown = nb; g_shownCap = nc;
    }
    if (g_shown) {
        MultiByteToWideChar(CP_UTF8, 0, utf8, (int)strlen(utf8), g_shown + g_shownLen, wlen);
        g_shownLen += (size_t)wlen;
        g_shown[g_shownLen] = 0;
        SetWindowTextW(g_hLog, g_shown);
        {
            int n = GetWindowTextLengthW(g_hLog);
            SendMessageW(g_hLog, EM_SETSEL, n, n);
            SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
        }
    }
}

static void LogLine(const char *s)
{
    PushPending(s, strlen(s));
    PushPending("\r\n", 2);
}

static BOOL FileExists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void ExeDir(char *out, DWORD cap)
{
    DWORD n = GetModuleFileNameA(NULL, out, cap);
    if (n == 0 || n >= cap) { out[0] = 0; return; }
    {
        char *p = strrchr(out, '\\');
        if (p) *p = 0; else out[0] = 0;
    }
}

/* parse "[download]  42.3% of ..." -> percent */
static void TryParseProgress(const char *line)
{
    if (strncmp(line, "[download]", 10) != 0) return;
    {
        const char *p = strchr(line, '%');
        if (!p || p == line) return;
        {
            const char *q = p;
            while (q > line && (isdigit((unsigned char)q[-1]) || q[-1] == '.' || q[-1] == ' '))
                q--;
            while (*q == ' ') q++;
            if (!isdigit((unsigned char)*q)) return;
            {
                double v = atof(q);
                if (v >= 0.0 && v <= 100.0) g_percent = (int)(v + 0.5);
            }
        }
    }
}

static DWORD WINAPI ReaderThread(LPVOID param)
{
    HANDLE hRead = (HANDLE)param;
    char buf[4096];
    DWORD n = 0;
    static char line[16384];
    size_t lineLen = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &n, NULL) && n > 0) {
        DWORD i;
        for (i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                if (lineLen >= sizeof(line)) lineLen = sizeof(line) - 1;
                line[lineLen] = 0;
                TryParseProgress(line);
                PushPending(line, lineLen);
                PushPending("\r\n", 2);
                lineLen = 0;
            } else if (c != '\r') {
                if (lineLen + 1 < sizeof(line)) line[lineLen++] = c;
            }
        }
    }
    CloseHandle(hRead);
    return 0;
}

static void SetButtons(BOOL running)
{
    EnableWindow(g_hGo, !running);
    EnableWindow(g_hCancel, running);
    if (running) EnableWindow(g_hOpen, FALSE);
    else         EnableWindow(g_hOpen, TRUE);
}

static void StopJob(void)
{
    if (g_running && g_pi.hProcess)
        TerminateProcess(g_pi.hProcess, (UINT)-1);
}

static DWORD WINAPI JobThread(LPVOID param)
{
    char *cmdline = (char *)param;
    SECURITY_ATTRIBUTES sa;
    HANDLE hRead = NULL, hWrite = NULL;
    STARTUPINFOA si;
    BOOL ok;

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        LogLine("ERROR: could not create output pipe.");
        goto done_fail;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;

    LogLine(cmdline);
    LogLine("---");

    EnterCriticalSection(&g_cs);
    ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &g_pi);
    LeaveCriticalSection(&g_cs);
    if (!ok) {
        LogLine("ERROR: could not start yt-dlp.exe (is it next to VideoGrabber.exe?)");
        CloseHandle(hWrite);
        CloseHandle(hRead);
        goto done_fail;
    }
    CloseHandle(hWrite); /* parent copy must close so ReadFile sees EOF */

    g_hReader = CreateThread(NULL, 0, ReaderThread, (LPVOID)hRead, 0, NULL);

    WaitForSingleObject(g_pi.hProcess, INFINITE);
    {
        DWORD exitCode = 0;
        GetExitCodeProcess(g_pi.hProcess, &exitCode);

        if (g_hReader) { WaitForSingleObject(g_hReader, 5000); CloseHandle(g_hReader); g_hReader = NULL; }
        CloseHandle(g_pi.hProcess); CloseHandle(g_pi.hThread);
        ZeroMemory(&g_pi, sizeof(g_pi));

        {
            char tail[128];
            if (exitCode == 0) wsprintfA(tail, "=== DONE (exit 0) ===");
            else               wsprintfA(tail, "=== FAILED (exit %u) ===", exitCode);
            LogLine(tail);
        }
        PostMessage(g_hMain, WM_APP_DONE, 0, (LPARAM)(exitCode == 0 ? 1 : 0));
    }
    HeapFree(GetProcessHeap(), 0, cmdline);
    InterlockedExchange(&g_running, 0);
    return 0;

done_fail:
    HeapFree(GetProcessHeap(), 0, cmdline);
    PostMessage(g_hMain, WM_APP_DONE, 0, (LPARAM)0);
    InterlockedExchange(&g_running, 0);
    return 0;
}

static void StartDownload(HWND hwnd)
{
    char url[2048], dir[MAX_PATH], exe[MAX_PATH], engine[MAX_PATH], ffmpeg[MAX_PATH + 32];
    int fmt;

    GetWindowTextA(g_hUrl, url, sizeof(url));
    {
        char *s = url;
        while (*s == ' ' || *s == '\t') s++;
        memmove(url, s, strlen(s) + 1);
        {
            size_t L = strlen(url);
            while (L && (url[L-1] == ' ' || url[L-1] == '\t')) url[--L] = 0;
        }
    }
    if (!url[0]) { MessageBoxW(hwnd, L"Paste a video or audio URL first.", APP_TITLEW, MB_ICONEXCLAMATION); return; }
    if (strchr(url, '\"')) { MessageBoxW(hwnd, L"URL cannot contain quote characters.", APP_TITLEW, MB_ICONEXCLAMATION); return; }

    GetWindowTextA(g_hDir, dir, sizeof(dir));
    if (!dir[0] || !FileExists(dir)) {
        MessageBoxW(hwnd, L"Save-to folder does not exist.", APP_TITLEW, MB_ICONEXCLAMATION);
        return;
    }

    ExeDir(exe, MAX_PATH);
    wsprintfA(engine, "%s\\yt-dlp.exe", exe);
    if (!FileExists(engine)) {
        MessageBoxW(hwnd,
            L"yt-dlp.exe was not found next to VideoGrabber.exe.\n"
            L"Reinstall the setup or unzip the portable package completely.",
            APP_TITLEW, MB_ICONERROR);
        return;
    }

    fmt = (int)SendMessageA(g_hFmt, CB_GETCURSEL, 0, 0);
    if (fmt < 0) fmt = 0;

    {
        char *cmd = (char *)HeapAlloc(GetProcessHeap(), 0, 8192);
        char fmtArgs[256];
        if (!cmd) return;
        lstrcpynA(fmtArgs, g_fmtArgs[fmt], sizeof(fmtArgs));
        wsprintfA(cmd, "\"%s\" --newline --no-playlist -N 4 %s", engine, fmtArgs);

        wsprintfA(ffmpeg, "%s\\ffmpeg.exe", exe);
        if (FileExists(ffmpeg)) {
            lstrcatA(cmd, " --ffmpeg-location \"");
            lstrcatA(cmd, ffmpeg);
            lstrcatA(cmd, "\"");
        }
        lstrcatA(cmd, " -P \"");
        lstrcatA(cmd, dir);
        lstrcatA(cmd, "\" -o \"%(title)s [%(id)s].%(ext)s\" -- \"");
        lstrcatA(cmd, url);
        lstrcatA(cmd, "\"");

        g_percent = 0;
        InterlockedExchange(&g_running, 1);
        SetButtons(TRUE);
        InvalidateRect(hwnd, &g_rcProg, FALSE);

        {
            HANDLE t = CreateThread(NULL, 0, JobThread, (LPVOID)cmd, 0, NULL);
            if (t) {
                CloseHandle(t);
            } else {
                MessageBoxW(hwnd, L"Could not start the download thread.", APP_TITLEW, MB_ICONERROR);
                HeapFree(GetProcessHeap(), 0, cmd);
                InterlockedExchange(&g_running, 0);
                SetButtons(FALSE);
            }
        }
    }
}

static void BrowseFolder(HWND hwnd)
{
    BROWSEINFOW bi;
    WCHAR path[MAX_PATH];
    LPITEMIDLIST pidl;

    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Choose the folder to save downloads into:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, path)) SetWindowTextW(g_hDir, path);
        CoTaskMemFree(pidl);
    }
}

static void OpenFolder(void)
{
    char dir[MAX_PATH];
    GetWindowTextA(g_hDir, dir, sizeof(dir));
    if (dir[0]) {
        WCHAR wdir[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, dir, -1, wdir, MAX_PATH);
        ShellExecuteW(g_hMain, L"open", wdir, NULL, NULL, SW_SHOWNORMAL);
    }
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */
static void PaintHeader(HDC dc, RECT rc)
{
    int cy = (rc.top + rc.bottom) / 2;
    HFONT f1, f2;
    HGDIOBJ of;
    RECT line;

    FillSolid(dc, rc, g_pal.headerBg);
    line.left = rc.left; line.right = rc.right;
    line.top = rc.bottom - 1; line.bottom = rc.bottom;
    FillSolid(dc, line, g_pal.editBorder);

    /* spiral logo + orb core */
    DrawSpiral(dc, rc.left + 40, cy, 0.60, g_pal.accent, g_pal.accent2);
    DrawOrb(dc, rc.left + 40, cy, 6, g_pal.accent, g_pal.accent2, g_pal.headerBg);

    /* wordmark */
    f1 = CreateFontW(-21, 0, 0, 0, FW_HEAVY, 0, 0, 0, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    f2 = CreateFontW(-10, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    of = SelectObject(dc, f1);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, g_pal.txt);
    SetTextCharacterExtra(dc, 1);
    {
        RECT rw = { rc.left + 76, cy - 24, rc.left + 340, cy };
        DrawTextW(dc, L"3SVERSE", -1, &rw, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    SelectObject(dc, f2);
    SetTextColor(dc, g_pal.txtDim);
    SetTextCharacterExtra(dc, 4);
    {
        RECT rs = { rc.left + 78, cy + 2, rc.left + 340, cy + 18 };
        DrawTextW(dc, L"VIDEOGRABBER", -1, &rs, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    SetTextCharacterExtra(dc, 0);

    /* brand gradient bar */
    {
        int i, x0 = rc.left + 78, y0 = cy + 22;
        RECT seg;
        for (i = 0; i < 18; i++) {
            seg.left = x0 + i * 5; seg.right = seg.left + 5;
            seg.top = y0; seg.bottom = y0 + 3;
            FillSolid(dc, seg, Lerp(g_pal.accent, g_pal.accent2, (double)i / 17.0));
        }
    }

    SelectObject(dc, of);
    DeleteObject(f1);
    DeleteObject(f2);
}

static void PaintProgress(HDC dc)
{
    RECT r = g_rcProg;
    int pct = g_percent;
    WCHAR t[64];
    HGDIOBJ of;

    FillRound(dc, r, 7, g_pal.progTrack, g_pal.editBorder);
    if (pct > 100) pct = 100;
    if (pct >= 0 && g_running) {
        int w = (r.right - r.left - 4) * pct / 100;
        if (w > 0) {
            RECT f;
            f.left = r.left + 2; f.right = r.left + 2 + w;
            f.top = r.top + 2;   f.bottom = r.bottom - 2;
            FillRound(dc, f, 5, g_pal.progFill, g_pal.progFill);
        }
    }
    if (g_running) {
        if (pct >= 0) wsprintfW(t, L"%d%%", pct);
        else          lstrcpynW(t, L"Downloading...", 64);
    } else {
        lstrcpynW(t, L"Ready", 64);
    }
    SetBkMode(dc, TRANSPARENT);
    of = SelectObject(dc, g_hFontSm);
    SetTextColor(dc, (g_running && pct > 0) ? RGB(255, 255, 255) : g_pal.txtDim);
    DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

static void PaintFooter(HDC dc)
{
    RECT r;
    r.left = 14; r.top = CLIENT_H - 26; r.right = CLIENT_W - 14; r.bottom = CLIENT_H - 8;
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_hFontSm);
    SetTextColor(dc, g_pal.txtMute);
    DrawTextW(dc,
        L"Powered by the yt-dlp engine - runs 100% locally - MIT License - 3SVerse",
        -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);

        FillSolid(mem, rc, g_pal.bg);
        {
            RECT hdr = { 0, 0, rc.right, HEADER_H };
            PaintHeader(mem, hdr);
        }
        PaintProgress(mem);
        PaintFooter(mem);

        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
    }
    EndPaint(hwnd, &ps);
}

/* ------------------------------------------------------------------ */
/* owner-draw buttons                                                  */
/* ------------------------------------------------------------------ */
static void DrawButton(HDC dc, DRAWITEMSTRUCT *d)
{
    RECT r = d->rcItem;
    int id = (int)d->CtlID;
    COLORREF fill = g_pal.btnFace, txt = g_pal.btnTxt, out = g_pal.editBorder;
    WCHAR t[64];
    HGDIOBJ of;

    if (id == IDC_GO) {
        fill = g_pal.accent; txt = g_pal.primTxt; out = g_pal.accent;
        if (IsHover(d->hwndItem)) fill = Shade(g_pal.accent, 1.12);
    } else {
        if (IsHover(d->hwndItem)) fill = g_pal.btnHi;
    }
    if (d->itemState & ODS_SELECTED) {
        if (id == IDC_GO) fill = Shade(g_pal.accent, 0.82);
        else              fill = g_pal.btnDn;
    }
    if (d->itemState & ODS_DISABLED) {
        fill = g_pal.btnFace; txt = g_pal.txtMute;
    }

    FillRound(dc, r, 7, fill, out);
    GetWindowTextW(d->hwndItem, t, 64);
    SetBkMode(dc, TRANSPARENT);
    of = SelectObject(dc, g_hFont);
    SetTextColor(dc, txt);
    DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

/* ------------------------------------------------------------------ */
/* wndproc                                                             */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE h = ((LPCREATESTRUCTW)lp)->hInstance;

        CreateWindowW(L"STATIC", L"Video / audio URL:", WS_CHILD | WS_VISIBLE,
            14, 102, 220, 16, hwnd, NULL, h, NULL);
        g_hUrl = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            14, 120, CLIENT_W - 28, 26, hwnd, (HMENU)IDC_URL, h, NULL);

        CreateWindowW(L"STATIC", L"Format:", WS_CHILD | WS_VISIBLE,
            14, 160, 70, 16, hwnd, NULL, h, NULL);
        g_hFmt = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            90, 156, 300, 200, hwnd, (HMENU)IDC_FMT, h, NULL);
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Best MP4 (recommended)");
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Best quality (any format)");
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Audio only - M4A");
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Audio only - MP3 (needs ffmpeg.exe)");
        SendMessageW(g_hFmt, CB_SETCURSEL, 0, 0);

        CreateWindowW(L"STATIC", L"Save to:", WS_CHILD | WS_VISIBLE,
            14, 198, 70, 16, hwnd, NULL, h, NULL);
        g_hDir = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
            90, 194, 430, 26, hwnd, (HMENU)IDC_DIR, h, NULL);
        g_hBrowse = CreateWindowW(L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            530, 192, 136, 28, hwnd, (HMENU)IDC_BROWSE, h, NULL);
        TrackHover(g_hBrowse);

        g_hGo = CreateWindowW(L"BUTTON", L"Download",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_DEFPUSHBUTTON | WS_TABSTOP,
            90, 232, 150, 34, hwnd, (HMENU)IDC_GO, h, NULL);
        g_hCancel = CreateWindowW(L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            250, 232, 90, 34, hwnd, (HMENU)IDC_CANCEL, h, NULL);
        g_hOpen = CreateWindowW(L"BUTTON", L"Open folder",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            350, 232, 130, 34, hwnd, (HMENU)IDC_OPEN, h, NULL);
        TrackHover(g_hGo);
        TrackHover(g_hCancel);
        TrackHover(g_hOpen);

        g_hTheme = CreateWindowW(L"BUTTON", g_dark ? L"Light mode" : L"Dark mode",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            CLIENT_W - 122, 20, 106, 30, hwnd, (HMENU)IDC_THEME, h, NULL);
        TrackHover(g_hTheme);

        /* progress bar region (custom-painted by the parent) */
        g_rcProg.left = 14; g_rcProg.top = 282;
        g_rcProg.right = CLIENT_W - 14; g_rcProg.bottom = 304;

        g_hLog = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            14, 316, CLIENT_W - 28, 206, hwnd, (HMENU)IDC_LOG, h, NULL);

        g_hFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hMono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH | FF_MODERN, L"Consolas");
        g_hFontSm = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        SendMessageW(g_hUrl,  WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hFmt,  WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hDir,  WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hLog,  WM_SETFONT, (WPARAM)g_hMono, TRUE);
        SendMessageW(g_hGo,     WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hCancel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hOpen,   WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hBrowse, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hTheme,  WM_SETFONT, (WPARAM)g_hFont, TRUE);

        /* default save-to = Downloads */
        {
            char dl[MAX_PATH];
            char home[MAX_PATH];
            if (GetEnvironmentVariableA("USERPROFILE", home, MAX_PATH)) {
                lstrcpynA(dl, home, MAX_PATH);
                lstrcatA(dl, "\\Downloads");
                SetWindowTextA(g_hDir, dl);
            }
        }

        SetTimer(hwnd, 1, 250, NULL);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            char *chunk = NULL;
            EnterCriticalSection(&g_cs);
            if (g_pendingLen) {
                chunk = (char *)HeapAlloc(GetProcessHeap(), 0, g_pendingLen + 1);
                if (chunk) {
                    memcpy(chunk, g_pending, g_pendingLen);
                    chunk[g_pendingLen] = 0;
                }
                g_pendingLen = 0;
                if (g_pending) g_pending[0] = 0;
            }
            LeaveCriticalSection(&g_cs);
            if (chunk) {
                AppendShown(chunk);
                HeapFree(GetProcessHeap(), 0, chunk);
            }
            InvalidateRect(hwnd, &g_rcProg, FALSE);
        }
        return 0;
    case WM_APP_DONE:
        SetButtons(FALSE);
        if (lp) {
            g_percent = 100;
            LogLine("Saved into the folder above. Use Open folder to see the file.");
        }
        InvalidateRect(hwnd, &g_rcProg, FALSE);
        return 0;
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)lp)->CtlType == ODT_BUTTON) {
            DrawButton((HDC)wp, (DRAWITEMSTRUCT *)lp);
            return TRUE;
        }
        break;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        if ((HWND)lp == g_hLog) {
            SetTextColor(dc, g_pal.logTxt);
            SetBkColor(dc, g_pal.logBg);
            return (LRESULT)g_brLog;
        }
        SetTextColor(dc, g_pal.editTxt);
        SetBkColor(dc, g_pal.editBg);
        return (LRESULT)g_brEdit;
    }
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wp, g_pal.editTxt);
        SetBkColor((HDC)wp, g_pal.editBg);
        return (LRESULT)g_brEdit;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        if ((HWND)lp == g_hLog) {
            SetTextColor(dc, g_pal.logTxt);
            SetBkColor(dc, g_pal.logBg);
            return (LRESULT)g_brLog;
        }
        SetTextColor(dc, g_pal.txtDim);
        SetBkColor(dc, g_pal.bg);
        return (LRESULT)g_brBg;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_GO:     StartDownload(hwnd); return 0;
        case IDC_CANCEL: StopJob();          return 0;
        case IDC_BROWSE: BrowseFolder(hwnd); return 0;
        case IDC_OPEN:   OpenFolder();       return 0;
        case IDC_THEME:
            g_dark = !g_dark;
            SaveTheme();
            ApplyPalette();
            RebuildBrushes();
            ApplyDwm(hwnd);
            SetWindowTextW(g_hTheme, g_dark ? L"Light mode" : L"Dark mode");
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        break;
    case WM_DESTROY:
        StopJob();
        KillTimer(hwnd, 1);
        if (g_hFont)     DeleteObject(g_hFont);
        if (g_hMono)     DeleteObject(g_hMono);
        if (g_hFontSm)   DeleteObject(g_hFontSm);
        if (g_brBg)      DeleteObject(g_brBg);
        if (g_brEdit)    DeleteObject(g_brEdit);
        if (g_brLog)     DeleteObject(g_brLog);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow)
{
    WNDCLASSW wc;
    MSG msg;
    RECT wr;

    (void)hPrev; (void)cmdLine;
    InitializeCriticalSection(&g_cs);
    {
        INITCOMMONCONTROLSEX icc;
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icc);
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    LoadTheme();
    ApplyPalette();
    RebuildBrushes();

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = APP_CLASSW;
    RegisterClassW(&wc);

    wr.left = 0; wr.top = 0; wr.right = CLIENT_W; wr.bottom = CLIENT_H;
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    g_hMain = CreateWindowW(APP_CLASSW, APP_TITLEW,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, hInst, NULL);
    if (!g_hMain) return 1;

    ApplyDwm(g_hMain);
    ShowWindow(g_hMain, nShow);
    UpdateWindow(g_hMain);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(g_hMain, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    DeleteCriticalSection(&g_cs);
    return (int)msg.wParam;
}
