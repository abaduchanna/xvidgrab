/* 3SVerse VideoGrabber v1.3.0 — free & open video/audio downloader GUI (yt-dlp engine)
 * MIT License — Copyright (c) 2026 Abad Umair Channa / 3S Verse
 * Engine: yt-dlp (https://github.com/yt-dlp/yt-dlp) — public domain (The Unlicense)
 *
 * v1.3.0 — "Studio" brand system, cloned 1:1 from the 3SVerse xfer PC receiver
 * (owner order: "xfer 3sverse ki branding hai" — the xfer dashboard IS the brand):
 *   - near-black canvas #07060b, warm text #f5f1e6 (exact xfer values)
 *   - the real 3SVerse wordmark (logo.png, same file the Android apps use)
 *     centered in a stacked header: logo + mono letter-spaced title with the
 *     brand cyan highlight + tiny cyan eyebrow line
 *   - the hero ring spiral (top-right, 120s/turn + 16px/12s float) and the
 *     glossy orb (bottom-left, 140s/turn + 13s float) — the ACTUAL brand art
 *     from xfer (spiral/orb assets), rendered with GDI+ (alpha + rotation)
 *   - cards #0d0c14 / border #232130 / radius 14, fields #12101a
 *   - chip pill with lime status dot (cyan pulsing while downloading)
 *   - CTAs: cyan->magenta gradient (#6ee7ef -> #e44bd7) with dark text,
 *     ghost buttons with #232130 borders that light up cyan on hover
 *   - mono letter-spaced uppercase labels (#9b97b3)
 *   - the standard fixed 3SVerse footer strip: "DEVELOPED BY WWW.3SVERSE.COM"
 *     + version right (same as xfer PC / xfer Android / Studio)
 *   - dark & light themes (dark = xfer exact; light = same hues on warm paper),
 *     theme persisted in HKCU\Software\3SVerse\VideoGrabber, DWM dark title bar
 * Engine side unchanged from v1.2.0 (yt-dlp subprocess + live log + progress).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <objidl.h>
#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

/* GDI+ opaque types (flat API, declared before use) */
typedef struct GpGraphics GpGraphics;
typedef struct GpImage GpImage;
typedef struct GpImageAttributes GpImageAttributes;

#define APP_TITLEW  L"3SVerse VideoGrabber v1.3.0"
#define APP_CLASSW  L"3SVerseVideoGrabber"
#define BTN_CLASSW  L"3SVBtn"
#define WM_APP_DONE (WM_APP + 1)
#define APP_VER_A   L"v1.3.0"

/* embedded brand art (RCDATA in src/brand.rc) */
#define RES_LOGO    101
#define RES_SPIRAL  102
#define RES_ORB     103

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
#define BSTYLE_PRIMARY 0   /* cyan->magenta gradient, dark bold text (Download) */
#define BSTYLE_GHOST   1   /* card fill + #232130 border (Cancel / Browse / Open) */
#define BSTYLE_TOGGLE  2   /* ghost variant sitting on the canvas (theme switch) */

#define CLIENT_W  720
#define CLIENT_H  640
#define FOOTER_H  32

static const char *g_fmtArgs[] = {
    " -f \"bv*[ext=mp4]+ba[ext=m4a]/b[ext=mp4]/b\"",   /* 0: best MP4 */
    " -f \"bv*+ba/b\"",                                 /* 1: best quality */
    " -f \"ba[ext=m4a]/ba\"",                           /* 2: audio m4a */
    " -x --audio-format mp3 --audio-quality 0"          /* 3: audio mp3 (ffmpeg) */
};

/* ------------------------------------------------------------------ */
/* palette — DARK values are the exact xfer / 3sverse.com brand tokens */
/* ------------------------------------------------------------------ */
typedef struct {
    COLORREF bg, card, border, fieldBg;
    COLORREF txt, txtDim, txtMute;
    COLORREF cyan, mag, lime;
    COLORREF footTxt, footVer;
    COLORREF primTxt;
    double   artAlpha;     /* spiral/orb opacity: .90/.95 on dark (xfer) */
} PALETTE;

static PALETTE g_pal;
static BOOL    g_dark = TRUE;

static void SetDarkPalette(PALETTE *p)
{
    p->bg      = RGB(0x07, 0x06, 0x0b);
    p->card    = RGB(0x0d, 0x0c, 0x14);
    p->border  = RGB(0x23, 0x21, 0x30);
    p->fieldBg = RGB(0x12, 0x10, 0x1a);
    p->txt     = RGB(0xf5, 0xf1, 0xe6);
    p->txtDim  = RGB(0xb9, 0xb5, 0xcc);
    p->txtMute = RGB(0x9b, 0x97, 0xb3);
    p->cyan    = RGB(0x6e, 0xe7, 0xef);
    p->mag     = RGB(0xe4, 0x4b, 0xd7);
    p->lime    = RGB(0xc7, 0xef, 0x70);
    p->footTxt = RGB(0xc7, 0xcb, 0xe0);
    p->footVer = RGB(0x8b, 0x87, 0xa0);
    p->primTxt = RGB(0x07, 0x06, 0x0b);
    p->artAlpha = 0.90;
}

static void SetLightPalette(PALETTE *p)
{
    p->bg      = RGB(0xf4, 0xf2, 0xec);
    p->card    = RGB(0xff, 0xff, 0xff);
    p->border  = RGB(0xe4, 0xdf, 0xd3);
    p->fieldBg = RGB(0xfa, 0xf8, 0xf3);
    p->txt     = RGB(0x2a, 0x27, 0x32);
    p->txtDim  = RGB(0x5c, 0x58, 0x68);
    p->txtMute = RGB(0x8b, 0x87, 0x98);
    p->cyan    = RGB(0x0e, 0x9f, 0xb6);
    p->mag     = RGB(0xc0, 0x2b, 0xa6);
    p->lime    = RGB(0x6f, 0x9c, 0x12);
    p->footTxt = RGB(0x4c, 0x48, 0x60);
    p->footVer = RGB(0x92, 0x8e, 0x9e);
    p->primTxt = RGB(0x07, 0x06, 0x0b);
    p->artAlpha = 0.25;
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
static HFONT g_hFont, g_hFontBtn, g_hMono, g_hH1, g_hSub, g_hLbl, g_hChip, g_hFoot;
static HBRUSH g_brBg, g_brEdit, g_brLog;
static WNDPROC g_oldUrl, g_oldDir, g_oldFmt;
static RECT g_rcCardSrc, g_rcCardOut, g_rcCardLog;
static RECT g_rcUrlF, g_rcFmtF, g_rcDirF, g_rcLogF;
static RECT g_rcChip, g_rcProg, g_rcFooter;
static double g_phase = 0.0;
static int    g_tick  = 0;

/* GDI+ state */
static ULONG_PTR         g_gdipToken = 0;
static GpImage          *g_imgLogo = NULL, *g_imgSpiral = NULL, *g_imgOrb = NULL;
static GpImageAttributes *g_attrDark = NULL, *g_attrLite = NULL;
static IStream          *g_streams[3] = { NULL, NULL, NULL };

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
/* GDI+ flat API (C-safe declarations; MinGW's gdiplus headers are     */
/* C++-oriented, so only the flat C entry points are declared here)    */
/* ------------------------------------------------------------------ */
typedef float REAL;
typedef int   GpStatus;      /* 0 = Ok */

typedef struct {
    UINT32 GdiplusVersion;
    void  *DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GpStartupInput;

#define WINGDIPAPI __stdcall
#define GPX(a)     ((GpStatus (WINGDIPAPI*)(a)))

/* enums (values from gdiplus flat headers) */
#define GPUNIT_PIXEL        2
#define MXORDER_PREPEND     0
#define MXORDER_APPEND      1
#define ITPL_HQ_BICUBIC     7
#define COLORTYPE_DEFAULT   0
#define CMFLAGS_DEFAULT     0

typedef struct { REAL m[5][5]; } GpColorMatrix;

static GpStatus (WINGDIPAPI *p_GdiplusStartup)(ULONG_PTR *, const GpStartupInput *, void *);
static VOID      (WINGDIPAPI *p_GdiplusShutdown)(ULONG_PTR);
static GpStatus  (WINGDIPAPI *p_GdipLoadImageFromStream)(IStream *, GpImage **);
static GpStatus  (WINGDIPAPI *p_GdipDisposeImage)(GpImage *);
static GpStatus  (WINGDIPAPI *p_GdipGetImageWidth)(GpImage *, UINT *);
static GpStatus  (WINGDIPAPI *p_GdipGetImageHeight)(GpImage *, UINT *);
static GpStatus  (WINGDIPAPI *p_GdipCreateFromHDC)(HDC, GpGraphics **);
static GpStatus  (WINGDIPAPI *p_GdipDeleteGraphics)(GpGraphics *);
static GpStatus  (WINGDIPAPI *p_GdipSetInterpolationMode)(GpGraphics *, int);
static GpStatus  (WINGDIPAPI *p_GdipRotateWorldTransform)(GpGraphics *, REAL, int);
static GpStatus  (WINGDIPAPI *p_GdipTranslateWorldTransform)(GpGraphics *, REAL, REAL, int);
static GpStatus  (WINGDIPAPI *p_GdipResetWorldTransform)(GpGraphics *);
static GpStatus  (WINGDIPAPI *p_GdipDrawImageRectRectI)(GpGraphics *, GpImage *,
                     INT, INT, INT, INT, INT, INT, INT, INT, INT,
                     const GpImageAttributes *, void *, void *);
static GpStatus  (WINGDIPAPI *p_GdipCreateImageAttributes)(GpImageAttributes **);
static GpStatus  (WINGDIPAPI *p_GdipDisposeImageAttributes)(GpImageAttributes *);
static GpStatus  (WINGDIPAPI *p_GdipSetImageAttributesColorMatrix)(GpImageAttributes *,
                     INT, BOOL, const GpColorMatrix *, const GpColorMatrix *, INT);

static BOOL LoadGdiPlus(void)
{
    HMODULE m = LoadLibraryW(L"gdiplus.dll");
    if (!m) return FALSE;
#define GPGET(f) p_##f = (void *)GetProcAddress(m, #f); if (!p_##f) return FALSE
    GPGET(GdiplusStartup);
    GPGET(GdiplusShutdown);
    GPGET(GdipLoadImageFromStream);
    GPGET(GdipDisposeImage);
    GPGET(GdipGetImageWidth);
    GPGET(GdipGetImageHeight);
    GPGET(GdipCreateFromHDC);
    GPGET(GdipDeleteGraphics);
    GPGET(GdipSetInterpolationMode);
    GPGET(GdipRotateWorldTransform);
    GPGET(GdipTranslateWorldTransform);
    GPGET(GdipResetWorldTransform);
    GPGET(GdipDrawImageRectRectI);
    GPGET(GdipCreateImageAttributes);
    GPGET(GdipDisposeImageAttributes);
    GPGET(GdipSetImageAttributesColorMatrix);
#undef GPGET
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* brand art loading + drawing                                         */
/* ------------------------------------------------------------------ */
static GpImage *ImageFromResource(int resId, IStream **keep)
{
    HRSRC hr = FindResourceW(NULL, MAKEINTRESOURCEW(resId), (LPCWSTR)RT_RCDATA);
    HGLOBAL res;
    const void *p;
    DWORD sz = 0;
    IStream *stm = NULL;
    GpImage *img = NULL;
    *keep = NULL;
    if (!hr) return NULL;
    res = LoadResource(NULL, hr);
    if (!res) return NULL;
    p = LockResource(res);
    sz = SizeofResource(NULL, hr);
    if (!p || !sz) return NULL;
    if (CreateStreamOnHGlobal(NULL, TRUE, &stm) != S_OK || !stm) return NULL;
    {
        ULONG written = 0;
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        if (stm->lpVtbl->Write(stm, p, sz, &written) != S_OK || written != sz) {
            stm->lpVtbl->Release(stm);
            return NULL;
        }
        stm->lpVtbl->Seek(stm, zero, STREAM_SEEK_SET, NULL);
        if (p_GdipLoadImageFromStream(stm, &img) != 0) {
            stm->lpVtbl->Release(stm);
            return NULL;
        }
    }
    *keep = stm;   /* stream kept alive for the process lifetime (safety) */
    return img;
}

static GpImageAttributes *MakeAlphaAttr(double alpha)
{
    GpImageAttributes *attr = NULL;
    GpColorMatrix cm;
    int i, j;
    for (i = 0; i < 5; i++)
        for (j = 0; j < 5; j++)
            cm.m[i][j] = (i == j) ? 1.0f : 0.0f;
    cm.m[3][3] = (REAL)alpha;
    if (p_GdipCreateImageAttributes(&attr) != 0 || !attr) return NULL;
    p_GdipSetImageAttributesColorMatrix(attr, COLORTYPE_DEFAULT, TRUE,
                                        &cm, NULL, CMFLAGS_DEFAULT);
    return attr;
}

/* draw image rotated about its center at (cx,cy), scaled into a w*h box */
static void DrawBrandImage(HDC hdc, GpImage *img, int cx, int cy,
                           int w, int h, double angleDeg,
                           GpImageAttributes *attr)
{
    GpGraphics *g = NULL;
    UINT iw = 0, ih = 0;
    if (!img || p_GdipCreateFromHDC(hdc, &g) != 0 || !g) { if (g) p_GdipDeleteGraphics(g); return; }
    p_GdipGetImageWidth(img, &iw);
    p_GdipGetImageHeight(img, &ih);
    if (iw && ih) {
        p_GdipSetInterpolationMode(g, ITPL_HQ_BICUBIC);
        p_GdipTranslateWorldTransform(g, (REAL)cx, (REAL)cy, MXORDER_APPEND);
        p_GdipRotateWorldTransform(g, (REAL)angleDeg, MXORDER_APPEND);
        p_GdipTranslateWorldTransform(g, (REAL)(-w / 2), (REAL)(-h / 2), MXORDER_APPEND);
        p_GdipDrawImageRectRectI(g, img, 0, 0, w, h, 0, 0, (INT)iw, (INT)ih,
                                 GPUNIT_PIXEL, attr, NULL, NULL);
        p_GdipResetWorldTransform(g);
    }
    p_GdipDeleteGraphics(g);
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
    if (b < 0) b = 0; if (b > 255) b = 255;
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
    cap = g_pal.bg;                  /* Win11: caption matches the canvas */
    f(w, 35, &cap, sizeof(cap));
}

static void RebuildBrushes(void)
{
    if (g_brBg)   DeleteObject(g_brBg);
    if (g_brEdit) DeleteObject(g_brEdit);
    if (g_brLog)  DeleteObject(g_brLog);
    g_brBg   = CreateSolidBrush(g_pal.bg);
    g_brEdit = CreateSolidBrush(g_pal.fieldBg);
    g_brLog  = CreateSolidBrush(g_pal.fieldBg);
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
        InvalidateRect(hwnd, &g_rcChip, FALSE);

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
/* 3SVBtn — custom self-painting button class (v1.2.0 pattern kept:    */
/* no BS_OWNERDRAW dependency, blank-out impossible).                  */
/* Restyled to the xfer tokens: gradient CTA + ghost cards.            */
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
    COLORREF txtc, fill, out;
    int bands = 24;

    dc = BeginPaint(h, &ps);
    GetClientRect(h, &r);
    rt = r;

    GetWindowTextW(h, t, 64);
    txtc = g_pal.txtDim;
    fill = g_pal.card;
    out  = g_pal.border;

    if (!IsWindowEnabled(h)) {
        FillRound(dc, r, 11, (st->style == BSTYLE_TOGGLE) ? g_pal.bg : g_pal.card, g_pal.border);
        txtc = g_pal.txtMute;
        bands = 1;
    } else if (st->style == BSTYLE_PRIMARY) {
        COLORREF a = st->down ? Shade(g_pal.cyan, 0.86) :
                     (st->hover ? Shade(g_pal.cyan, 1.12) : g_pal.cyan);
        COLORREF b = st->down ? Shade(g_pal.mag, 0.86) :
                     (st->hover ? Shade(g_pal.mag, 1.12) : g_pal.mag);
        {
            /* clip the gradient to the rounded shape (no square corners) */
            HRGN rg = CreateRoundRectRgn(r.left, r.top, r.right, r.bottom, 22, 22);
            SelectClipRgn(dc, rg);
            FillHGrad(dc, r, a, b, bands);
            SelectClipRgn(dc, NULL);
            DeleteObject(rg);
        }
        {
            HPEN p = CreatePen(PS_SOLID, 1, Shade(b, 0.72));
            HGDIOBJ op = SelectObject(dc, p);
            HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
            HGDIOBJ ob = SelectObject(dc, nb);
            RoundRect(dc, r.left, r.top, r.right, r.bottom, 22, 22);
            SelectObject(dc, ob);
            SelectObject(dc, op);
            DeleteObject(p);
        }
        txtc = g_pal.primTxt;
    } else { /* ghost + toggle */
        COLORREF fillc = (st->style == BSTYLE_TOGGLE) ? g_pal.bg : g_pal.card;
        if (st->down)      { fill = g_pal.fieldBg; }
        else if (st->hover){ fill = fillc; out = g_pal.cyan; txtc = g_pal.cyan; }
        else               { fill = fillc; }
        FillRound(dc, r, 11, fill, out);
    }

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, st->font ? st->font : g_hFont);
    SetTextColor(dc, txtc);
    DrawTextW(dc, t, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

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
        b = CreateSolidBrush(g_pal.fieldBg);
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
/* painting — xfer Studio layout                                       */
/* ------------------------------------------------------------------ */
static void DrawFrameRect(HDC dc, RECT r, COLORREF col, int w)
{
    HPEN p = CreatePen(PS_SOLID, w, col);
    HGDIOBJ op = SelectObject(dc, p);
    HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
    HGDIOBJ ob = SelectObject(dc, nb);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 20, 20);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(p);
}

static void PaintTextMono(HDC dc, const WCHAR *s, RECT r, HFONT f,
                          COLORREF c, int extra, UINT align)
{
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetTextCharacterExtra(dc, extra);
    DrawTextW(dc, s, -1, &r, align | DT_SINGLELINE | DT_VCENTER);
    SetTextCharacterExtra(dc, 0);
    SelectObject(dc, of);
}

/* status chip: pill with the lime (ready) / cyan (busy) dot, xfer .chip */
static void PaintChip(HDC dc)
{
    RECT r = g_rcChip;
    int cy = (r.top + r.bottom) / 2;
    double pulse = 0.5 + 0.5 * sin(g_phase * 2.6);
    COLORREF dotc;

    FillRound(dc, r, 13, g_pal.card, g_pal.border);

    if (g_running) {
        dotc = Lerp(Shade(g_pal.cyan, 0.45), g_pal.cyan, pulse);
        if (g_percent >= 0) {
            WCHAR t[48];
            wsprintfW(t, L"DOWNLOADING \x2022 %d%%", g_percent);
            PaintTextMono(dc, t, (RECT){ r.left + 30, r.top, r.right - 14, r.bottom },
                          g_hChip, g_pal.txtDim, 1, DT_LEFT);
        } else {
            PaintTextMono(dc, L"DOWNLOADING \x2022 SEE PROGRESS BELOW",
                          (RECT){ r.left + 30, r.top, r.right - 14, r.bottom },
                          g_hChip, g_pal.txtDim, 1, DT_LEFT);
        }
    } else {
        dotc = g_pal.lime;
        PaintTextMono(dc, L"READY \x2022 PASTE A LINK TO START",
                      (RECT){ r.left + 30, r.top, r.right - 14, r.bottom },
                      g_hChip, g_pal.txtDim, 1, DT_LEFT);
    }
    {
        HBRUSH b = CreateSolidBrush(dotc);
        HGDIOBJ ob = SelectObject(dc, b);
        HPEN np = (HPEN)GetStockObject(NULL_PEN);
        HGDIOBJ op = SelectObject(dc, np);
        Ellipse(dc, r.left + 18, cy - 4, r.left + 26, cy + 4);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(b);
    }
}

/* stacked header: wordmark + mono title with cyan highlight + eyebrow */
static void PaintHeader(HDC dc)
{
    SetBkMode(dc, TRANSPARENT);
    {
        UINT lw = 0, lh = 0;
        int logoH = 58, logoW = 190;
        if (g_imgLogo) {
            p_GdipGetImageWidth(g_imgLogo, &lw);
            p_GdipGetImageHeight(g_imgLogo, &lh);
            if (lw && lh) logoW = (int)((double)lh ? logoH * lw / lh : logoW);
        }
        if (g_imgLogo)
            DrawBrandImage(dc, g_imgLogo, CLIENT_W / 2, 10 + logoH / 2,
                           logoW, logoH, 0.0, NULL);
        else {
            SetBkMode(dc, TRANSPARENT);
            PaintTextMono(dc, L"3SVERSE",
                          (RECT){ 0, 18, CLIENT_W, 44 }, g_hH1, g_pal.txt, 3,
                          DT_CENTER | DT_NOPREFIX);
        }
    }
    SetBkMode(dc, TRANSPARENT);

    /* title: "3SVERSE " + "VIDEOGRABBER" (cyan), centered as one string */
    {
        const WCHAR *p1 = L"3SVERSE ";
        const WCHAR *p2 = L"VIDEOGRABBER";
        SIZE s1, s2;
        int x0, extra = 3;
        HDC tdc = dc;
        HGDIOBJ of = SelectObject(tdc, g_hH1);
        GetTextExtentPoint32W(tdc, p1, (int)wcslen(p1), &s1);
        GetTextExtentPoint32W(tdc, p2, (int)wcslen(p2), &s2);
        SelectObject(tdc, of);
        s2.cx += extra * (int)wcslen(p2);
        s1.cx += extra * (int)wcslen(p1);
        x0 = (CLIENT_W - (s1.cx + s2.cx)) / 2;
        PaintTextMono(dc, p1, (RECT){ x0, 74, x0 + s1.cx + 4, 92 },
                      g_hH1, g_pal.txt, extra, DT_LEFT | DT_NOPREFIX);
        PaintTextMono(dc, p2, (RECT){ x0 + s1.cx, 74, x0 + s1.cx + s2.cx + 8, 92 },
                      g_hH1, g_pal.cyan, extra, DT_LEFT | DT_NOPREFIX);
    }

    /* eyebrow line (xfer .sub): tiny cyan mono, letter-spaced */
    PaintTextMono(dc, L"PC DOWNLOADER \x2022 YT-DLP ENGINE \x2022 100% LOCAL \x2022 NO CLOUD",
                  (RECT){ 0, 96, CLIENT_W, 112 },
                  g_hSub, g_pal.cyan, 3, DT_CENTER | DT_NOPREFIX);
}

static void PaintCards(HDC dc)
{
    /* three brand cards (bg #0d0c14, border #232130, radius 14) */
    FillRound(dc, g_rcCardSrc, 14, g_pal.card, g_pal.border);
    FillRound(dc, g_rcCardOut, 14, g_pal.card, g_pal.border);
    FillRound(dc, g_rcCardLog, 14, g_pal.card, g_pal.border);

    SetBkMode(dc, TRANSPARENT);
    PaintTextMono(dc, L"VIDEO / AUDIO URL",
                  (RECT){ 40, 164, 400, 180 }, g_hLbl, g_pal.txtMute, 1,
                  DT_LEFT | DT_NOPREFIX);
    PaintTextMono(dc, L"FORMAT",
                  (RECT){ 40, 250, 300, 264 }, g_hLbl, g_pal.txtMute, 1,
                  DT_LEFT | DT_NOPREFIX);
    PaintTextMono(dc, L"SAVE FOLDER",
                  (RECT){ 40, 304, 300, 318 }, g_hLbl, g_pal.txtMute, 1,
                  DT_LEFT | DT_NOPREFIX);
    PaintTextMono(dc, L"ACTIVITY LOG",
                  (RECT){ 40, 474, 300, 490 }, g_hSub, g_pal.cyan, 2,
                  DT_LEFT | DT_NOPREFIX);
}

static void PaintFrames(HDC dc)
{
    COLORREF fr = g_pal.border;
    COLORREF frHi = g_pal.cyan;

    DrawFrameRect(dc, g_rcUrlF, (GetFocus() == g_hUrl) ? frHi : fr, (GetFocus() == g_hUrl) ? 2 : 1);
    DrawFrameRect(dc, g_rcFmtF, (GetFocus() == g_hFmt) ? frHi : fr, (GetFocus() == g_hFmt) ? 2 : 1);
    DrawFrameRect(dc, g_rcDirF, (GetFocus() == g_hDir) ? frHi : fr, (GetFocus() == g_hDir) ? 2 : 1);
    DrawFrameRect(dc, g_rcLogF, (GetFocus() == g_hLog) ? frHi : fr, (GetFocus() == g_hLog) ? 2 : 1);
}

static void PaintProgress(HDC dc)
{
    RECT r = g_rcProg;
    int pct = g_percent;
    WCHAR t[64];
    HGDIOBJ of;

    FillRound(dc, r, 8, g_pal.fieldBg, g_pal.border);
    if (pct > 100) pct = 100;
    if (pct >= 0 && g_running) {
        int w = (r.right - r.left - 6) * pct / 100;
        if (w > 0) {
            RECT f;
            HRGN rg;
            f.left = r.left + 3; f.right = r.left + 3 + w;
            f.top = r.top + 3;   f.bottom = r.bottom - 3;
            rg = CreateRoundRectRgn(f.left, f.top, f.right, f.bottom, 10, 10);
            SelectClipRgn(dc, rg);
            FillHGrad(dc, f, g_pal.cyan, g_pal.mag, 28);
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
    of = SelectObject(dc, g_hLbl);
    SetTextColor(dc, (g_running && pct > 0) ? g_pal.txt : g_pal.txtMute);
    DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

/* the standard fixed 3SVerse footer strip (xfer PC / xfer Android / Studio) */
static void PaintFooter(HDC dc)
{
    FillSolid(dc, g_rcFooter, g_pal.card);
    {
        RECT b = { 0, g_rcFooter.top, CLIENT_W, g_rcFooter.top + 1 };
        FillSolid(dc, b, g_pal.border);
    }
    PaintTextMono(dc, L"DEVELOPED BY WWW.3SVERSE.COM",
                  (RECT){ 0, g_rcFooter.top, CLIENT_W, g_rcFooter.bottom },
                  g_hFoot, g_pal.footTxt, 1, DT_CENTER | DT_NOPREFIX);
    PaintTextMono(dc, APP_VER_A,
                  (RECT){ CLIENT_W - 90, g_rcFooter.top, CLIENT_W - 10, g_rcFooter.bottom },
                  g_hFoot, g_pal.footVer, 0, DT_RIGHT | DT_NOPREFIX);
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

        /* near-black canvas (xfer #07060b) */
        FillSolid(mem, rc, g_pal.bg);

        /* the xfer background art: hero ring spiral top-right (120s/turn +
         * 16px/12s float), glossy orb bottom-left (140s/turn + 13s float) */
        if (g_imgSpiral) {
            double ang = g_tick * 0.135;                        /* 3 deg/s = 120s/turn */
            double dy  = 16.0 * sin(g_tick * (2.0 * 3.14159265358979) / 266.7);
            DrawBrandImage(mem, g_imgSpiral, CLIENT_W - 14, 42 + (int)dy,
                           338, 350, ang,
                           g_dark ? g_attrDark : g_attrLite);
        }
        if (g_imgOrb) {
            double ang2 = g_tick * 0.1157;                      /* 140s/turn */
            double dy2  = 12.0 * sin(g_tick * (2.0 * 3.14159265358979) / 288.9);
            DrawBrandImage(mem, g_imgOrb, 26, CLIENT_H - 44 + (int)dy2,
                           300, 300, ang2,
                           g_dark ? g_attrDark : g_attrLite);
        }

        PaintHeader(mem);
        PaintChip(mem);
        PaintCards(mem);
        PaintFrames(mem);
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

        /* cards + fields + frames + chip + footer geometry */
        g_rcCardSrc.left = 24; g_rcCardSrc.top = 156; g_rcCardSrc.right = 696; g_rcCardSrc.bottom = 234;
        g_rcCardOut.left = 24; g_rcCardOut.top = 244; g_rcCardOut.right = 696; g_rcCardOut.bottom = 364;
        g_rcCardLog.left = 24; g_rcCardLog.top = 468; g_rcCardLog.right = 696; g_rcCardLog.bottom = 606;

        g_rcUrlF.left = 38;  g_rcUrlF.top = 194; g_rcUrlF.right = 682; g_rcUrlF.bottom = 230;
        g_rcFmtF.left = 38;  g_rcFmtF.top = 268; g_rcFmtF.right = 372; g_rcFmtF.bottom = 300;
        g_rcDirF.left = 38;  g_rcDirF.top = 322; g_rcDirF.right = 542; g_rcDirF.bottom = 356;
        g_rcLogF.left = 38;  g_rcLogF.top = 494; g_rcLogF.right = 682; g_rcLogF.bottom = 594;

        g_rcChip.left = 170; g_rcChip.top = 120; g_rcChip.right = 550; g_rcChip.bottom = 146;
        g_rcProg.left = 24;  g_rcProg.top = 432; g_rcProg.right = 696; g_rcProg.bottom = 456;
        g_rcFooter.left = 0; g_rcFooter.top = CLIENT_H - FOOTER_H;
        g_rcFooter.right = CLIENT_W; g_rcFooter.bottom = CLIENT_H;

        g_hFont    = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hFontBtn = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_hMono    = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Consolas");
        g_hH1      = CreateFontW(-15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Consolas");
        g_hSub     = CreateFontW(-10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Consolas");
        g_hLbl     = CreateFontW(-9, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Consolas");
        g_hChip    = CreateFontW(-10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Consolas");
        g_hFoot    = CreateFontW(-9, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Consolas");

        /* URL row (inside SOURCE card) */
        g_hUrl = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            40, 196, 640, 32, hwnd, (HMENU)IDC_URL, h, NULL);

        /* Format row (inside OUTPUT card) */
        g_hFmt = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            40, 270, 330, 200, hwnd, (HMENU)IDC_FMT, h, NULL);
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Best MP4 (recommended)");
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Best quality (any format)");
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Audio only - M4A");
        SendMessageW(g_hFmt, CB_ADDSTRING, 0, (LPARAM)L"Audio only - MP3 (needs ffmpeg.exe)");
        SendMessageW(g_hFmt, CB_SETCURSEL, 0, 0);

        /* Save-to row (inside OUTPUT card) */
        g_hDir = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            40, 324, 500, 30, hwnd, (HMENU)IDC_DIR, h, NULL);

        /* action buttons (custom class — self-painting) */
        g_hGo = CreateWindowW(BTN_CLASSW, L"Download",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            24, 376, 200, 44, hwnd, (HMENU)IDC_GO, h, (LPVOID)(INT_PTR)BSTYLE_PRIMARY);
        g_hCancel = CreateWindowW(BTN_CLASSW, L"Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            236, 376, 110, 44, hwnd, (HMENU)IDC_CANCEL, h, (LPVOID)(INT_PTR)BSTYLE_GHOST);
        g_hOpen = CreateWindowW(BTN_CLASSW, L"Open folder",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            358, 376, 160, 44, hwnd, (HMENU)IDC_OPEN, h, (LPVOID)(INT_PTR)BSTYLE_GHOST);
        g_hBrowse = CreateWindowW(BTN_CLASSW, L"Browse...",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            550, 322, 114, 34, hwnd, (HMENU)IDC_BROWSE, h, (LPVOID)(INT_PTR)BSTYLE_GHOST);
        g_hTheme = CreateWindowW(BTN_CLASSW, g_dark ? L"Light mode" : L"Dark mode",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            20, 16, 118, 30, hwnd, (HMENU)IDC_THEME, h, (LPVOID)(INT_PTR)BSTYLE_TOGGLE);

        /* log console (inside ACTIVITY LOG card) */
        g_hLog = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            40, 496, 640, 96, hwnd, (HMENU)IDC_LOG, h, NULL);

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
        SendMessageW(g_hFmt, CB_SETITEMHEIGHT, 0, 20);
        itemH = (int)SendMessageW(g_hFmt, CB_GETITEMHEIGHT, 0, 0);
        if (itemH < 10) itemH = 20;
        (void)itemH;

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
        SetTimer(hwnd, 2, 45,  NULL);   /* brand animation (spiral + orb + chip) */
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
            g_tick++;
            g_phase += 0.045;
            if (!IsIconic(hwnd))
                InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_APP_DONE:
        SetButtons(FALSE);
        if (lp) {
            g_percent = 100;
            LogLine("Saved into the folder above. Use Open folder to see the file.");
        }
        InvalidateRect(hwnd, &g_rcProg, FALSE);
        InvalidateRect(hwnd, &g_rcChip, FALSE);
        return 0;
    case WM_PAINT:
        OnPaint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, g_pal.txt);
        SetBkColor(dc, g_pal.fieldBg);
        return (LRESULT)g_brEdit;
    }
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wp, g_pal.txt);
        SetBkColor((HDC)wp, g_pal.fieldBg);
        return (LRESULT)g_brEdit;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        if ((HWND)lp == g_hLog) {
            SetTextColor(dc, g_pal.txtDim);
            SetBkColor(dc, g_pal.fieldBg);
            return (LRESULT)g_brLog;
        }
        SetTextColor(dc, g_pal.txt);
        SetBkColor(dc, g_pal.fieldBg);
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
        if (g_hFont)    DeleteObject(g_hFont);
        if (g_hFontBtn) DeleteObject(g_hFontBtn);
        if (g_hMono)    DeleteObject(g_hMono);
        if (g_hH1)      DeleteObject(g_hH1);
        if (g_hSub)     DeleteObject(g_hSub);
        if (g_hLbl)     DeleteObject(g_hLbl);
        if (g_hChip)    DeleteObject(g_hChip);
        if (g_hFoot)    DeleteObject(g_hFoot);
        if (g_brBg)     DeleteObject(g_brBg);
        if (g_brEdit)   DeleteObject(g_brEdit);
        if (g_brLog)    DeleteObject(g_brLog);
        if (g_imgLogo)   { p_GdipDisposeImage(g_imgLogo);   g_imgLogo = NULL; }
        if (g_imgSpiral) { p_GdipDisposeImage(g_imgSpiral); g_imgSpiral = NULL; }
        if (g_imgOrb)    { p_GdipDisposeImage(g_imgOrb);    g_imgOrb = NULL; }
        if (g_attrDark) { p_GdipDisposeImageAttributes(g_attrDark); g_attrDark = NULL; }
        if (g_attrLite) { p_GdipDisposeImageAttributes(g_attrLite); g_attrLite = NULL; }
        {
            int i;
            for (i = 0; i < 3; i++) {
                if (g_streams[i]) { g_streams[i]->lpVtbl->Release(g_streams[i]); g_streams[i] = NULL; }
            }
        }
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
    GpStartupInput gsin;

    (void)hPrev; (void)cmdLine;
    InitializeCriticalSection(&g_cs);
    {
        INITCOMMONCONTROLSEX icc;
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icc);
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    /* GDI+ for the real brand art (logo / spiral / orb) */
    ZeroMemory(&gsin, sizeof(gsin));
    gsin.GdiplusVersion = 1;
    if (LoadGdiPlus() && p_GdiplusStartup(&g_gdipToken, &gsin, NULL) == 0) {
        g_imgLogo   = ImageFromResource(RES_LOGO,   &g_streams[0]);
        g_imgSpiral = ImageFromResource(RES_SPIRAL, &g_streams[1]);
        g_imgOrb    = ImageFromResource(RES_ORB,    &g_streams[2]);
        g_attrDark  = MakeAlphaAttr(0.90);
        g_attrLite  = MakeAlphaAttr(0.25);
    }

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

    if (g_gdipToken && p_GdiplusShutdown) p_GdiplusShutdown(g_gdipToken);
    CoUninitialize();
    DeleteCriticalSection(&g_cs);
    return (int)msg.wParam;
}
