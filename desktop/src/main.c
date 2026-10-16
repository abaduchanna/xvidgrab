/* 3SVerse VideoGrabber v1.0.0 — free & open video/audio downloader GUI (yt-dlp engine)
 * MIT License — Copyright (c) 2026 Abad Umair Channa / 3S Verse
 * Engine: yt-dlp (https://github.com/yt-dlp/yt-dlp) — public domain (The Unlicense)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#define APP_TITLE   "3SVerse VideoGrabber v1.0.0"
#define WM_APP_DONE (WM_APP + 1)

#define IDC_URL      101
#define IDC_FMT      102
#define IDC_DIR      103
#define IDC_BROWSE   104
#define IDC_GO       105
#define IDC_CANCEL   106
#define IDC_OPEN     107
#define IDC_LOG      108
#define IDC_PROG     109

static const char *g_fmtArgs[] = {
    " -f \"bv*[ext=mp4]+ba[ext=m4a]/b[ext=mp4]/b\"",   /* 0: best MP4 */
    " -f \"bv*+ba/b\"",                                 /* 1: best quality */
    " -f \"ba[ext=m4a]/ba\"",                           /* 2: audio m4a */
    " -x --audio-format mp3 --audio-quality 0"          /* 3: audio mp3 (ffmpeg) */
};

static HWND g_hUrl, g_hFmt, g_hDir, g_hLog, g_hGo, g_hCancel, g_hOpen, g_hProg, g_hMain;
static HFONT g_hFont, g_hMono;

/* job state */
static PROCESS_INFORMATION g_pi;
static volatile LONG g_running = 0;
static HANDLE g_hReader = NULL;

/* pending log data (UTF-8) shared between reader thread and UI */
static CRITICAL_SECTION g_cs;
static char  *g_pending = NULL;   /* UTF-8 bytes not yet shown */
static size_t g_pendingLen = 0, g_pendingCap = 0;
static WCHAR *g_shown = NULL;     /* UTF-16 text currently in the log box */
static size_t g_shownLen = 0, g_shownCap = 0;
static int    g_percent = -1;     /* -1 = unknown */

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
        int n = GetWindowTextLengthW(g_hLog);
        SendMessageW(g_hLog, EM_SETSEL, n, n);
        SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
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
    char *p = strrchr(out, '\\');
    if (p) *p = 0; else out[0] = 0;
}

/* parse "[download]  42.3% of ..." -> percent */
static void TryParseProgress(const char *line)
{
    if (strncmp(line, "[download]", 10) != 0) return;
    const char *p = strchr(line, '%');
    if (!p || p == line) return;
    const char *q = p;
    while (q > line && (isdigit((unsigned char)q[-1]) || q[-1] == '.' || q[-1] == ' '))
        q--;
    while (*q == ' ') q++;
    if (!isdigit((unsigned char)*q)) return;
    double v = atof(q);
    if (v >= 0.0 && v <= 100.0) g_percent = (int)(v + 0.5);
}

static DWORD WINAPI ReaderThread(LPVOID param)
{
    HANDLE hRead = (HANDLE)param;
    char buf[4096];
    DWORD n = 0;
    static char line[16384];
    size_t lineLen = 0;
    while (ReadFile(hRead, buf, sizeof(buf), &n, NULL) && n > 0) {
        for (DWORD i = 0; i < n; i++) {
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
}

static void StopJob(void)
{
    if (g_running && g_pi.hProcess) {
        TerminateProcess(g_pi.hProcess, (UINT)-1);
    }
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
    if (!url[0]) { MessageBoxA(hwnd, "Paste a video or audio URL first.", APP_TITLE, MB_ICONEXCLAMATION); return; }
    if (strchr(url, '\"')) { MessageBoxA(hwnd, "URL cannot contain quote characters.", APP_TITLE, MB_ICONEXCLAMATION); return; }

    GetWindowTextA(g_hDir, dir, sizeof(dir));
    if (!dir[0] || !FileExists(dir)) {
        MessageBoxA(hwnd, "Save-to folder does not exist.", APP_TITLE, MB_ICONEXCLAMATION);
        return;
    }

    ExeDir(exe, MAX_PATH);
    wsprintfA(engine, "%s\\yt-dlp.exe", exe);
    if (!FileExists(engine)) {
        MessageBoxA(hwnd, "yt-dlp.exe was not found next to VideoGrabber.exe.\nReinstall the setup or unzip the portable package completely.",
                    APP_TITLE, MB_ICONERROR);
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
        SendMessageA(g_hProg, PBM_SETPOS, 0, 0);
        InterlockedExchange(&g_running, 1);
        SetButtons(TRUE);

        {
            HANDLE t = CreateThread(NULL, 0, JobThread, (LPVOID)cmd, 0, NULL);
            if (t) {
                CloseHandle(t);
            } else {
                MessageBoxA(hwnd, "Could not start the download thread.", APP_TITLE, MB_ICONERROR);
                HeapFree(GetProcessHeap(), 0, cmd);
                InterlockedExchange(&g_running, 0);
                SetButtons(FALSE);
            }
        }
    }
}

static void BrowseFolder(HWND hwnd)
{
    BROWSEINFOA bi;
    char path[MAX_PATH];
    LPITEMIDLIST pidl;

    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = "Choose the folder to save downloads into:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        if (SHGetPathFromIDListA(pidl, path)) SetWindowTextA(g_hDir, path);
        CoTaskMemFree(pidl);
    }
}

static void OpenFolder(void)
{
    char dir[MAX_PATH];
    GetWindowTextA(g_hDir, dir, sizeof(dir));
    if (dir[0]) ShellExecuteA(g_hMain, "open", dir, NULL, NULL, SW_SHOWNORMAL);
}

typedef BOOL (*ENUMCHILDPROC)(HWND, LPARAM);

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE h = ((LPCREATESTRUCTA)lp)->hInstance;

        CreateWindowA("STATIC", "Video / audio URL:", WS_CHILD | WS_VISIBLE,
            12, 12, 220, 18, hwnd, NULL, h, NULL);
        g_hUrl = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            12, 34, 616, 24, hwnd, (HMENU)IDC_URL, h, NULL);

        CreateWindowA("STATIC", "Format:", WS_CHILD | WS_VISIBLE,
            12, 70, 60, 18, hwnd, NULL, h, NULL);
        g_hFmt = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            76, 66, 300, 200, hwnd, (HMENU)IDC_FMT, h, NULL);
        SendMessageA(g_hFmt, CB_ADDSTRING, 0, (LPARAM)"Best MP4 (recommended)");
        SendMessageA(g_hFmt, CB_ADDSTRING, 0, (LPARAM)"Best quality (any format)");
        SendMessageA(g_hFmt, CB_ADDSTRING, 0, (LPARAM)"Audio only - M4A");
        SendMessageA(g_hFmt, CB_ADDSTRING, 0, (LPARAM)"Audio only - MP3 (needs ffmpeg.exe)");
        SendMessageA(g_hFmt, CB_SETCURSEL, 0, 0);

        CreateWindowA("STATIC", "Save to:", WS_CHILD | WS_VISIBLE,
            12, 102, 60, 18, hwnd, NULL, h, NULL);
        g_hDir = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            76, 100, 444, 24, hwnd, (HMENU)IDC_DIR, h, NULL);
        CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            530, 99, 98, 26, hwnd, (HMENU)IDC_BROWSE, h, NULL);

        g_hGo = CreateWindowA("BUTTON", "Download", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            76, 134, 110, 30, hwnd, (HMENU)IDC_GO, h, NULL);
        g_hCancel = CreateWindowA("BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
            196, 134, 80, 30, hwnd, (HMENU)IDC_CANCEL, h, NULL);
        g_hOpen = CreateWindowA("BUTTON", "Open folder", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            286, 134, 110, 30, hwnd, (HMENU)IDC_OPEN, h, NULL);

        g_hProg = CreateWindowA(PROGRESS_CLASSA, "", WS_CHILD | WS_VISIBLE,
            12, 176, 616, 20, hwnd, (HMENU)IDC_PROG, h, NULL);
        SendMessageA(g_hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        g_hLog = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
                               ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            12, 208, 616, 226, hwnd, (HMENU)IDC_LOG, h, NULL);

        g_hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        g_hMono = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH | FF_MODERN, "Consolas");

        /* set fonts explicitly on every control */
        SendMessageA(g_hUrl, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageA(g_hFmt, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageA(g_hDir, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageA(g_hLog, WM_SETFONT, (WPARAM)g_hMono, TRUE);
        SendMessageA(g_hGo, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageA(g_hCancel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageA(g_hOpen, WM_SETFONT, (WPARAM)g_hFont, TRUE);

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
            if (g_percent >= 0) SendMessageA(g_hProg, PBM_SETPOS, (WPARAM)g_percent, 0);
        }
        return 0;
    case WM_APP_DONE:
        SetButtons(FALSE);
        EnableWindow(g_hOpen, TRUE);
        if (lp) {
            SendMessageA(g_hProg, PBM_SETPOS, 100, 0);
            LogLine("Saved into the folder above. Use Open folder to see the file.");
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_GO:     StartDownload(hwnd); return 0;
        case IDC_CANCEL: StopJob();          return 0;
        case IDC_BROWSE: BrowseFolder(hwnd); return 0;
        case IDC_OPEN:   OpenFolder();       return 0;
        }
        break;
    case WM_DESTROY:
        StopJob();
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow)
{
    WNDCLASSA wc;
    MSG msg;

    (void)hPrev; (void)cmdLine;
    InitializeCriticalSection(&g_cs);
    {
        INITCOMMONCONTROLSEX icc;
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icc);
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "3SVerseVideoGrabber";
    RegisterClassA(&wc);

    g_hMain = CreateWindowA("3SVerseVideoGrabber", APP_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 656, 502, NULL, NULL, hInst, NULL);
    if (!g_hMain) return 1;

    ShowWindow(g_hMain, nShow);
    UpdateWindow(g_hMain);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    CoUninitialize();
    DeleteCriticalSection(&g_cs);
    return (int)msg.wParam;
}
