/* 3SVerse VideoGrabber v1.2.0 — free & open video/audio downloader GUI (yt-dlp engine)
 * MIT License — Copyright (c) 2026 Abad Umair Channa / 3S Verse
 * Engine: yt-dlp (https://github.com/yt-dlp/yt-dlp) — public domain (The Unlicense)
 *
 * v1.2.0 — "3SVerse Halo" full visual overhaul (fixes v1.1.0 blank-button bug):
 *   - root cause of v1.1.0 blank buttons: BS_OWNERDRAW + WM_DRAWITEM plumbing
 *     failed to render on real machines -> buttons rendered as blank boxes.
 *     v1.2.0 removes that entire dependency: all buttons are a custom
 *     self-painting window class ("3SVBtn") that cannot blank out.
 *   - labels are painted directly by the parent (no STATIC child controls)
 *   - animated 3SVerse brand header: rotating gradient spiral + pulsing orb
 *   - gradient Download button (cyan -> violet), ghost-style secondary buttons
 *   - focus-accented rounded frames around edits / combo / log
 *   - faint rotating spiral watermark on the canvas, gradient progress bar
 *   - dark & light themes, both fully branded; theme persisted in
 *     HKCU\Software\3SVerse\VideoGrabber; DWM dark title bar + caption color
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

#define APP_TITLEW  L"3SVerse VideoGrabber v1.2.0"
#define APP_CLASSW  L"3SVerseVideoGrabber"
#define BTN_CLASSW  L"3SVBtn"
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

/* button styles for the 3SVBtn class */
#define BSTYLE_PRIMARY 0   /* gradient fill, white bold text (Download) */
#define BSTYLE_GHOST   1   /* panel fill + border (Cancel / Browse / Open) */
#define BSTYLE_TOGGLE  2   /* ghost + top-right theme switch */

#define CLIENT_W  700
#define CLIENT_H  612
#define HEADER_H  108

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
    p->bg         = RGB(21, 23, 29);
    p->headerBg   = RGB(27, 30, 38);
    p->txt        = RGB(234, 237, 244);
    p->txtDim     = RGB(148, 155, 170);
    p->txtMute    = RGB(102, 108, 122);
    p->editBg     = RGB(16, 18, 23);
    p->editTxt    = RGB(235, 238, 245);
    p->editBorder = RGB(58, 63, 76);
    p->btnFace    = RGB(38, 42, 52);
    p->btnHi      = RGB(50, 55, 67);
    p->btnDn      = RGB(28, 32, 40);
    p->btnTxt     = RGB(226, 230, 239);
    p->accent     = RGB(0, 176, 240);    /* 3SVerse cyan-blue */
    p->accent2    = RGB(124, 58, 237);   /* 3SVerse violet */
    p->primTxt    = RGB(255, 255, 255);
    p->progTrack  = RGB(30, 33, 41);
    p->progFill   = RGB(0, 160, 225);
    p->logBg      = RGB(14, 16, 21);
    p->logTxt     = RGB(188, 194, 206);
}

static void SetLightPalette(PALETTE *p)
{
    p->bg         = RGB(238, 241, 246);
    p->headerBg   = RGB(255, 255, 255);
    p->txt        = RGB(29, 34, 44);
    p->txtDim     = RGB(90, 98, 112);
    p->txtMute    = RGB(140, 147, 160);
    p->editBg     = RGB(255, 255, 255);
    p->editTxt    = RGB(29, 34, 44);
    p->editBorder = RGB(201, 207, 218);
    p->btnFace    = RGB(232, 236, 243);
    p->btnHi      = RGB(244, 247, 251);
    p->btnDn      = RGB(221, 227, 236);
    p->btnTxt     = RGB(40, 46, 58);
    p->accent     = RGB(0, 144, 216);
    p->accent2    = RGB(109, 52, 216);
    p->primTxt    = RGB(255, 255, 255);
    p->progTrack  = RGB(226, 230, 238);
    p->progFill   = RGB(0, 134, 204);
    p->logBg      = RGB(255, 255, 255);
    p->logTxt     = RGB(74, 81, 96);
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
static HFONT g_hFont, g_hMono, g_hFontSm, g_hFontBtn, g_hFontWord, g_hFontSub;
static HBRUSH g_brBg, g_brEdit, g_brLog;
static WNDPROC g_oldUrl, g_oldDir, g_oldFmt;
static RECT g_rcHeader, g_rcProg, g_rcLog, g_rcUrlF, g_rcFmtF, g_rcDirF;
static int  g_fmtFieldH = 30;
static double g_phase = 0.0;

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

/* horizontal gradient strip via lerped bands (pure GDI, no msimg32) */
static void FillHGrad(HDC dc, RECT r, COLORREF c1, COLORREF c2, int bands)
{
    int i;
    int w = r.right - r.left;
    if (w <= 0 || bands < 1) return;
    for (i = 0; i < bands; i++) {
        RECT s = r;
        s.left  = r.left + (int)((long long)w * i / bands);
        s.right = r.left + (int)((long long)w * (i + 1) / bands);
        if (s.right <= s.left) s.right = s.left + 1;
        FillSolid(dc, s, Lerp(c1, c2, (double)(i + 0.5) / bands));
    }
}

/* vertical gradient via lerped bands */
static void FillVGrad(HDC dc, RECT r, COLORREF c1, COLORREF c2, int bands)
{
    int i;
    int h = r.bottom - r.top;
    if (h <= 0 || bands < 1) return;
    for (i = 0; i < bands; i++) {
        RECT s = r;
        s.top    = r.top + (int)((long long)h * i / bands);
        s.bottom = r.top + (int)((long long)h * (i + 1) / bands);
        if (s.bottom <= s.top) s.bottom = s.top + 1;
        FillSolid(dc, s, Lerp(c1, c2, (double)(i + 0.5) / bands));
    }
}

/* 3SVerse brand mark: archimedean spiral stroked with the brand gradient.
 * phase rotates the whole spiral so the logo slowly spins. */
static void DrawSpiral(HDC dc, int cx, int cy, double scale, double phase,
                       COLORREF c1, COLORREF c2)
{
    const int SEG = 12, PTS = 14;
    const double TWO_PI = 6.28318530717958647692;
    int s, i;
    for (s = 0; s < SEG; s++) {
        POINT pts[15];
        HPEN p, op;
        p = CreatePen(PS_SOLID, 2, Lerp(c1, c2, ((double)s + 0.5) / SEG));
        op = SelectObject(dc, p);
        for (i = 0; i <= PTS; i++) {
            int k = s * PTS + i;
            double t = k * (2.6 * TWO_PI) / (SEG * PTS);
            double a = t - phase;
            double r = 1.55 * t * scale;
            pts[i].x = (int)(cx + r * cos(a));
            pts[i].y = (int)(cy - r * sin(a));
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

/* soft glow rings (fake alpha via lerped solid discs) */
static void DrawGlow(HDC dc, int cx, int cy, int R, COLORREF glow, COLORREF bgc)
{
    int i;
    HPEN np = (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ op = SelectObject(dc, np);
    for (i = 6; i >= 1; i--) {
        int r = R * i / 6;
        COLORREF c = Lerp(glow, bgc, 0.82);
        HBRUSH b = CreateSolidBrush(c);
        HGDIOBJ ob = SelectObject(dc, b);
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
    COLORREF cap;
    if (!m || !w) return;
    f = (FN_DwmSetAttr)(void *)GetProcAddress(m, "DwmSetWindowAttribute");
    if (!f) return;
    f(w, 20, &dark, sizeof(dark));   /* DWMWA_USE_IMMERSIVE_DARK_MODE */
    f(w, 19, &dark, sizeof(dark));   /* older builds */
    f(w, 33, &pref, sizeof(pref));   /* DWMWA_WINDOW_CORNER_PREFERENCE */
    cap = g_pal.headerBg;            /* Win11: caption matches the header */
    f(w, 35, &cap, sizeof(cap));
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
/* 3SVBtn — custom self-painting button class.                         */
/* No BS_OWNERDRAW / WM_DRAWITEM dependency: the button paints itself, */
/* so it can never render as a blank box (v1.1.0 regression root fix). */
/* ------------------------------------------------------------------ */
typedef struct {
    int  style;        /* BSTYLE_PRIMARY / BSTYLE_GHOST / BSTYLE_TOGGLE */
    BOOL hover;
    BOOL down;
    HFONT font;
} BTNSTATE;

static void BtnPaint(HWND h, BTNSTATE *st)
{
    PAINTSTRUCT ps;
    RECT r, rt;
    HDC dc;
    WCHAR t[64];
    COLORREF fill, txtc, out;
    int id = GetDlgCtrlID(h);
    int bands = 20;

    dc = BeginPaint(h, &ps);
    GetClientRect(h, &r);
    rt = r;

    GetWindowTextW(h, t, 64);
    txtc = g_pal.btnTxt;
    fill = g_pal.btnFace;
    out  = g_pal.editBorder;

    if (!IsWindowEnabled(h)) {
        FillRound(dc, r, 8, g_pal.btnFace, g_pal.editBorder);
        txtc = g_pal.txtMute;
        bands = 1;
        fill = g_pal.btnFace;
    } else if (st->style == BSTYLE_PRIMARY) {
        COLORREF a = st->down ? Shade(g_pal.accent, 0.82) :
                     (st->hover ? Shade(g_pal.accent, 1.10) : g_pal.accent);
        COLORREF b = st->down ? Shade(g_pal.accent2, 0.82) :
                     (st->hover ? Shade(g_pal.accent2, 1.10) : g_pal.accent2);
        {
            /* clip the gradient to the rounded shape (no square corners) */
            HRGN rg = CreateRoundRectRgn(r.left, r.top, r.right, r.bottom, 16, 16);
            SelectClipRgn(dc, rg);
            FillHGrad(dc, r, a, b, bands);
            SelectClipRgn(dc, NULL);
            DeleteObject(rg);
        }
        {
            HPEN p = CreatePen(PS_SOLID, 1, Shade(b, 0.7));
            HGDIOBJ op = SelectObject(dc, p);
            HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
            HGDIOBJ ob = SelectObject(dc, nb);
            RoundRect(dc, r.left, r.top, r.right, r.bottom, 16, 16);
            SelectObject(dc, ob);
            SelectObject(dc, op);
            DeleteObject(p);
        }
        txtc = g_pal.primTxt;
    } else { /* ghost + toggle */
        if (st->down)      { fill = g_pal.btnDn; }
        else if (st->hover){ fill = g_pal.btnHi; out = Lerp(g_pal.editBorder, g_pal.accent, 0.55); }
        else               { fill = g_pal.btnFace; }
        FillRound(dc, r, 8, fill, out);
    }

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, st->font ? st->font : g_hFont);
    SetTextColor(dc, txtc);
    DrawTextW(dc, t, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    /* subtle bottom edge on ghost buttons */
    if (st->style != BSTYLE_PRIMARY && IsWindowEnabled(h)) {
        RECT e = r;
        e.bottom -= 1; e.top = e.bottom - 1;
        e.left += 6; e.right -= 6;
        FillSolid(dc, e, Lerp(fill, g_pal.bg, 0.55));
    }
    (void)id;
    EndPaint(h, &ps);
}

static LRESULT CALLBACK BtnProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    BTNSTATE *st = (BTNSTATE *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)l;
        st = (BTNSTATE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BTNSTATE));
        if (!st) return -1;
        st->style = (int)(INT_PTR)cs->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
        return 0;
    }
    case WM_SETFONT:
        if (st) st->font = (HFONT)w;
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_MOUSEMOVE:
        if (st && !st->hover) {
            TRACKMOUSEEVENT t;
            st->hover = 1;
            ZeroMemory(&t, sizeof(t));
            t.cbSize = sizeof(t);
            t.dwFlags = TME_LEAVE;
            t.hwndTrack = h;
            TrackMouseEvent(&t);
            InvalidateRect(h, NULL, TRUE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (st) { st->hover = 0; InvalidateRect(h, NULL, TRUE); }
        return 0;
    case WM_LBUTTONDOWN:
        if (st) { st->down = 1; SetCapture(h); InvalidateRect(h, NULL, TRUE); }
        return 0;
    case WM_LBUTTONUP:
        if (st && st->down) {
            POINT pt;
            RECT r;
            st->down = 0;
            if (GetCapture() == h) ReleaseCapture();
            GetClientRect(h, &r);
            pt.x = (short)LOWORD(l); pt.y = (short)HIWORD(l);
            if (PtInRect(&r, pt))
                SendMessageW(GetParent(h), WM_COMMAND,
                             MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), (LPARAM)h);
            InvalidateRect(h, NULL, TRUE);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (st) { st->down = 0; InvalidateRect(h, NULL, TRUE); }
        return 0;
    case WM_ENABLE:
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (st) BtnPaint(h, st);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_BUTTON;
    case WM_NCDESTROY:
        if (st) { HeapFree(GetProcessHeap(), 0, st); SetWindowLongPtrW(h, GWLP_USERDATA, 0); }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* edit/combo subclass: guarantees the dark fill even if the classic
 * WM_CTLCOLOREDIT path is skipped, and repaints focus frames */
static LRESULT CALLBACK FieldSubProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    WNDPROC old = (h == g_hUrl) ? g_oldUrl : (h == g_hDir) ? g_oldDir : g_oldFmt;
    if (m == WM_ERASEBKGND) {
        HDC dc = (HDC)w;
        RECT r;
        HBRUSH b;
        GetClientRect(h, &r);
        b = CreateSolidBrush(g_pal.editBg);
        FillRect(dc, &r, b);
        DeleteObject(b);
        return 1;
    }
    if (m == WM_SETFOCUS || m == WM_KILLFOCUS) {
        RECT *fr = (h == g_hUrl) ? &g_rcUrlF : (h == g_hDir) ? &g_rcDirF : &g_rcFmtF;
        InvalidateRect(g_hMain, fr, FALSE);
    }
    return CallWindowProcW(old, h, m, w, l);
}

/* ------------------------------------------------------------------ */
/* painting                                                            */
/* ------------------------------------------------------------------ */
static void DrawFrameRect(HDC dc, RECT r, COLORREF col, int w)
{
    HPEN p = CreatePen(PS_SOLID, w, col);
    HGDIOBJ op = SelectObject(dc, p);
    HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
    HGDIOBJ ob = SelectObject(dc, nb);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 10, 10);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(p);
}

static void PaintHeader(HDC dc)
{
    RECT rc = g_rcHeader;
    int cy = HEADER_H / 2;
    int fadeX;

    FillVGrad(dc, rc, g_pal.headerBg, Lerp(g_pal.headerBg, g_pal.bg, 0.6), 16);

    /* ambient brand glow behind the logo */
    DrawGlow(dc, 46, cy, 46, g_pal.accent2, g_pal.headerBg);
    DrawGlow(dc, 46, cy, 30, g_pal.accent, g_pal.headerBg);

    /* rotating gradient spiral + pulsing core orb */
    DrawOrb(dc, 46, cy, (int)(5.5 + 1.4 * sin(g_phase * 1.6)),
            Lerp(g_pal.accent, RGB(255, 255, 255), 0.72),
            g_pal.accent, g_pal.headerBg);
    DrawSpiral(dc, 46, cy, 0.50, g_phase, g_pal.accent, g_pal.accent2);

    /* wordmark */
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_hFontWord);
    SetTextColor(dc, g_pal.txt);
    SetTextCharacterExtra(dc, 2);
    {
        RECT rw = { 84, 22, 400, 58 };
        DrawTextW(dc, L"3SVERSE", -1, &rw, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    SelectObject(dc, g_hFontSub);
    SetTextColor(dc, g_pal.txtDim);
    SetTextCharacterExtra(dc, 6);
    {
        RECT rs = { 86, 57, 400, 76 };
        DrawTextW(dc, L"VIDEOGRABBER", -1, &rs, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    SetTextCharacterExtra(dc, 0);

    /* brand gradient bar */
    {
        RECT gb = { 86, 82, 266, 85 };
        FillHGrad(dc, gb, g_pal.accent, g_pal.accent2, 24);
    }

    /* separator + gradient accent line fading to the right */
    {
        RECT sep = { 0, HEADER_H - 1, CLIENT_W, HEADER_H };
        FillSolid(dc, sep, Lerp(g_pal.headerBg, g_pal.editBorder, 0.85));
        fadeX = (int)(CLIENT_W * 0.68);
        {
            RECT g1 = { 0, HEADER_H - 3, fadeX, HEADER_H - 1 };
            RECT g2 = { fadeX, HEADER_H - 3, CLIENT_W, HEADER_H - 1 };
            FillHGrad(dc, g1, g_pal.accent, g_pal.accent2, 26);
            FillHGrad(dc, g2, g_pal.accent2, g_pal.headerBg, 12);
        }
    }
}

static void PaintBodyLabels(HDC dc)
{
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_hFont);
    SetTextColor(dc, g_pal.txtDim);
    {
        RECT r1 = { 16, 124, 420, 142 };
        RECT r2 = { 16, 190, 90, 208 };
        RECT r3 = { 16, 236, 90, 254 };
        DrawTextW(dc, L"Video / audio URL:", -1, &r1, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawTextW(dc, L"Format:",            -1, &r2, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawTextW(dc, L"Save to:",           -1, &r3, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

static void PaintBodyFrames(HDC dc)
{
    COLORREF fr = g_pal.editBorder;
    COLORREF frHi = g_pal.accent;

    DrawFrameRect(dc, g_rcUrlF, (GetFocus() == g_hUrl) ? frHi : fr, (GetFocus() == g_hUrl) ? 2 : 1);
    DrawFrameRect(dc, g_rcFmtF, (GetFocus() == g_hFmt) ? frHi : fr, (GetFocus() == g_hFmt) ? 2 : 1);
    DrawFrameRect(dc, g_rcDirF, (GetFocus() == g_hDir) ? frHi : fr, (GetFocus() == g_hDir) ? 2 : 1);
    DrawFrameRect(dc, g_rcLog,  (GetFocus() == g_hLog) ? frHi : fr, (GetFocus() == g_hLog) ? 2 : 1);
}

static void PaintProgress(HDC dc)
{
    RECT r = g_rcProg;
    int pct = g_percent;
    WCHAR t[64];
    HGDIOBJ of;

    FillRound(dc, r, 8, g_pal.progTrack, g_pal.editBorder);
    if (pct > 100) pct = 100;
    if (pct >= 0 && g_running) {
        int w = (r.right - r.left - 6) * pct / 100;
        if (w > 0) {
            RECT f;
            HRGN rg;
            f.left = r.left + 3; f.right = r.left + 3 + w;
            f.top = r.top + 3;   f.bottom = r.bottom - 3;
            rg = CreateRoundRectRgn(f.left, f.top, f.right, f.bottom, 6, 6);
            SelectClipRgn(dc, rg);
            FillHGrad(dc, f, g_pal.accent, g_pal.accent2, 24);
            SelectClipRgn(dc, NULL);
            DeleteObject(rg);
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
    RECT r1, r2;
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_hFontSm);
    SetTextColor(dc, g_pal.txtMute);
    r1.left = 16;  r1.top = CLIENT_H - 30; r1.right = 520;     r1.bottom = CLIENT_H - 8;
    r2.left = 520; r2.top = CLIENT_H - 30; r2.right = CLIENT_W - 16; r2.bottom = CLIENT_H - 8;
    DrawTextW(dc,
        L"Powered by the yt-dlp engine - runs 100% locally - MIT License",
        -1, &r1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(dc, Lerp(g_pal.accent, g_pal.txtDim, 0.25));
    DrawTextW(dc, L"3SVerse - 3sverse.com", -1, &r2, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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

        /* canvas + ambient tint glows (fake alpha, pure GDI) */
        FillSolid(mem, rc, g_pal.bg);
        DrawGlow(mem, CLIENT_W + 60, CLIENT_H + 60, 300, g_pal.accent2, g_pal.bg);
        DrawGlow(mem, -40, CLIENT_H + 90, 230, g_pal.accent, g_pal.bg);

        /* faint rotating brand watermark spiral, bottom-right */
        DrawSpiral(mem, CLIENT_W - 130, CLIENT_H - 60, 2.1, 0.9,
                   Lerp(g_pal.bg, g_pal.accent, 0.10),
                   Lerp(g_pal.bg, g_pal.accent2, 0.10));

        PaintHeader(mem);
        PaintBodyLabels(mem);
        PaintBodyFrames(mem);
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
/* wndproc                                                             */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE h = ((LPCREATESTRUCTW)lp)->hInstance;
        int itemH;

        g_rcHeader.left = 0; g_rcHeader.top = 0;
        g_rcHeader.right = CLIENT_W; g_rcHeader.bottom = HEADER_H;

        g_hFont     = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hFontBtn  = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hMono     = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  FIXED_PITCH | FF_MODERN, L"Consolas");
        g_hFontSm   = CreateFontW(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hFontWord = CreateFontW(-26, 0, 0, 0, FW_HEAVY, 0, 0, 0, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hFontSub  = CreateFontW(-10, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        /* URL row */
        g_hUrl = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            14, 146, 672, 30, hwnd, (HMENU)IDC_URL, h, NULL);

        /* Format row */
        g_hFmt = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            90, 186, 320, 200, hwnd, (HMENU)IDC_FMT, h, NULL);
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Best MP4 (recommended)");
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Best quality (any format)");
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Audio only - M4A");
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Audio only - MP3 (needs ffmpeg.exe)");
        SendMessageW(g_hFmt, CB_SETCURSEL, 0, 0);

        /* Save-to row */
        g_hDir = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            90, 232, 470, 30, hwnd, (HMENU)IDC_DIR, h, NULL);

        /* action buttons (custom class — self-painting) */
        g_hGo = CreateWindowW(BTN_CLASSW, L"Download",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            14, 280, 190, 44, hwnd, (HMENU)IDC_GO, h, (LPVOID)(INT_PTR)BSTYLE_PRIMARY);
        g_hCancel = CreateWindowW(BTN_CLASSW, L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            214, 280, 104, 44, hwnd, (HMENU)IDC_CANCEL, h, (LPVOID)(INT_PTR)BSTYLE_GHOST);
        g_hOpen = CreateWindowW(BTN_CLASSW, L"Open folder",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            328, 280, 150, 44, hwnd, (HMENU)IDC_OPEN, h, (LPVOID)(INT_PTR)BSTYLE_GHOST);
        g_hBrowse = CreateWindowW(BTN_CLASSW, L"Browse...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            572, 230, 114, 34, hwnd, (HMENU)IDC_BROWSE, h, (LPVOID)(INT_PTR)BSTYLE_GHOST);
        g_hTheme = CreateWindowW(BTN_CLASSW, g_dark ? L"Light mode" : L"Dark mode",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            566, 26, 120, 34, hwnd, (HMENU)IDC_THEME, h, (LPVOID)(INT_PTR)BSTYLE_TOGGLE);

        /* log console */
        g_hLog = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            14, 382, 672, 192, hwnd, (HMENU)IDC_LOG, h, NULL);

        /* fonts onto controls */
        SendMessageW(g_hUrl, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hFmt, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hDir, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hLog, WM_SETFONT, (WPARAM)g_hMono, TRUE);
        SendMessageW(g_hGo,     WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);
        SendMessageW(g_hCancel, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);
        SendMessageW(g_hOpen,   WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);
        SendMessageW(g_hBrowse, WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);
        SendMessageW(g_hTheme,  WM_SETFONT, (WPARAM)g_hFontBtn, TRUE);

        /* combo field metrics -> frame rect */
        SendMessageW(g_hFmt, CB_SETITEMHEIGHT, 0, 22);
        itemH = (int)SendMessageW(g_hFmt, CB_GETITEMHEIGHT, 0, 0);
        if (itemH < 10) itemH = 22;
        g_fmtFieldH = itemH + 2 * GetSystemMetrics(SM_CYEDGE);

        /* field frame rects (parent-painted rounded borders) */
        g_rcUrlF.left = 13;  g_rcUrlF.top = 145; g_rcUrlF.right = 687; g_rcUrlF.bottom = 177;
        g_rcFmtF.left = 89;  g_rcFmtF.top = 185; g_rcFmtF.right = 411; g_rcFmtF.bottom = 186 + g_fmtFieldH + 2;
        g_rcDirF.left = 89;  g_rcDirF.top = 231; g_rcDirF.right = 561; g_rcDirF.bottom = 263;
        g_rcProg.left = 14;  g_rcProg.top = 340; g_rcProg.right = 686; g_rcProg.bottom = 368;
        g_rcLog.left  = 13;  g_rcLog.top  = 381; g_rcLog.right  = 687; g_rcLog.bottom  = 575;

        /* subclass fields: guaranteed dark fill + focus frame repaints */
        g_oldUrl = (WNDPROC)SetWindowLongPtrW(g_hUrl, GWLP_WNDPROC, (LONG_PTR)FieldSubProc);
        g_oldFmt = (WNDPROC)SetWindowLongPtrW(g_hFmt, GWLP_WNDPROC, (LONG_PTR)FieldSubProc);
        g_oldDir = (WNDPROC)SetWindowLongPtrW(g_hDir, GWLP_WNDPROC, (LONG_PTR)FieldSubProc);

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

        SetTimer(hwnd, 1, 250, NULL);   /* log drain + progress */
        SetTimer(hwnd, 2, 45,  NULL);   /* brand animation */
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
        } else if (wp == 2) {
            g_phase += 0.05;
            if (!IsIconic(hwnd))
                InvalidateRect(hwnd, &g_rcHeader, FALSE);
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
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
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
        SetTextColor(dc, g_pal.editTxt);
        SetBkColor(dc, g_pal.editBg);
        return (LRESULT)g_brEdit;
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
        KillTimer(hwnd, 2);
        if (g_hFont)     DeleteObject(g_hFont);
        if (g_hFontBtn)  DeleteObject(g_hFontBtn);
        if (g_hMono)     DeleteObject(g_hMono);
        if (g_hFontSm)   DeleteObject(g_hFontSm);
        if (g_hFontWord) DeleteObject(g_hFontWord);
        if (g_hFontSub)  DeleteObject(g_hFontSub);
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
    WNDCLASSW wc, bc;
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

    /* custom self-painting button class (v1.1.0 blank-button root fix) */
    ZeroMemory(&bc, sizeof(bc));
    bc.lpfnWndProc = BtnProc;
    bc.hInstance = hInst;
    bc.hCursor = LoadCursor(NULL, IDC_HAND);
    bc.hbrBackground = NULL;
    bc.lpszClassName = BTN_CLASSW;
    RegisterClassW(&bc);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hbrBackground = NULL;
    wc.lpszClassName = APP_CLASSW;
    RegisterClassW(&wc);

    wr.left = 0; wr.top = 0; wr.right = CLIENT_W; wr.bottom = CLIENT_H;
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    g_hMain = CreateWindowW(APP_CLASSW, APP_TITLEW,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
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
