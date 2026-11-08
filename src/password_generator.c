/*
 * password_generator.c
 *
 * 随机密码生成器 —— Windows (Win10+) 可视化工具，纯 C + Win32 原生 API。
 *
 * 功能：
 *   - 长度 4~256，默认 16；快捷档位 8/16/32/64/128/256
 *   - 默认字符集：数字 + 小写字母 + 大写字母；可选特殊字符
 *   - 可选：排除易混淆字符 (0 O o 1 l I)
 *   - 每种勾选字符至少含一个（保底抽取 + Fisher-Yates 洗牌）
 *   - 熵(bit)实时估算 + 强度条（弱/中/强/极强）
 *   - 选项/长度变化自动重新生成；一键复制到剪贴板
 *   - 窗口可缩放，PerMonitorV2 DPI 感知
 *   - 使用 BCryptGenRandom 系统级 CSPRNG，安全随机（非 rand()）
 *
 * 构建：见 build.bat；逻辑单元测试：见 tests/test_logic.c
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   /* Windows 10 */
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <wchar.h>

/* ---- 控件 ID ---- */
#define IDC_LEN_EDIT     1001
#define IDC_LEN_SPIN     1002
#define IDC_CHK_DIGITS   1003
#define IDC_CHK_LOWER    1004
#define IDC_CHK_UPPER    1005
#define IDC_CHK_SPECIAL  1006
#define IDC_CHK_NOAMBIG  1007
#define IDC_PWD_EDIT     1008
#define IDC_BTN_GENERATE 1009
#define IDC_BTN_COPY     1010
#define IDC_BTN_P8      1011
#define IDC_BTN_P16     1012
#define IDC_BTN_P20     1013
#define IDC_BTN_P24     1014
#define IDC_BTN_P32     1015
#define IDC_BTN_P64     1016
#define IDC_BTN_P128    1017
#define IDC_BTN_P256    1018
#define IDC_BAR         1019
#define IDC_LBL_ENTROPY 1020
#define IDC_BTN_BATCH   1021

/* ---- 参数 ---- */
#define MIN_LEN     4
#define MAX_LEN     256
#define DEFAULT_LEN 16

/* ---- 窗口（设计像素，96dpi 基准） ---- */
#define CLIENT_W     640
#define CLIENT_H     440
#define MIN_CLIENT_W 560
#define MIN_CLIENT_H 360

/* ---- 字符集 ---- */
static const char g_digits[]  = "0123456789";
static const char g_lower[]   = "abcdefghijklmnopqrstuvwxyz";
static const char g_upper[]   = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char g_special[] = "!@#$%^&*()-_=+[]{};:,.<>?/";

/* 排除易混淆字符时剔除：0 O o 1 l I */
static const char g_ambiguous[] = "0Oo1lI";

/* ---- 全局控件 ----（UI 专用，测试编译时排除以消除未使用告警） */
#ifndef CIPHER_TESTING
static HINSTANCE g_hInst;
static HWND g_hLblLen, g_hLenEdit, g_hLenSpin, g_hLblRange;
static HWND g_hChkDigits, g_hChkLower, g_hChkUpper, g_hChkSpecial, g_hChkNoAmbig;
static HWND g_hPwdEdit, g_hBtnGen, g_hBtnCopy, g_hBtnBatch;

/* 快捷档位按钮数（与 g_presetLens 元素个数一致） */
#define PRESET_COUNT 8
static HWND g_hPresets[PRESET_COUNT];
static HWND g_hBar, g_hLblEntropy;
static BOOL g_ready = FALSE;                 /* 控件初始化完成前不自动生成 */
static const int g_presetLens[PRESET_COUNT] = { 8, 16, 20, 24, 32, 64, 128, 256 };
static HFONT g_hFontUI, g_hFontMono;
#endif

/* ============ 安全随机 ============ */

/* 用系统级 CSPRNG 填充 buf，成功返回 TRUE */
static BOOL gen_random_bytes(uint8_t *buf, size_t n)
{
    NTSTATUS st = BCryptGenRandom(NULL, buf, (ULONG)n,
                                  BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return BCRYPT_SUCCESS(st);
}

/* 从 [0, range) 中返回无偏随机索引；失败返回 -1。
 * 采用拒绝采样消除模运算偏差。 */
static int unbiased_index(int range)
{
    if (range <= 0) return -1;
    int limit = 256 - (256 % range);   /* 最大的、被 range 整除的上界 */
    for (;;) {
        uint8_t r;
        if (!gen_random_bytes(&r, 1)) return -1;
        if (r < limit) return r % range;
    }
}

/* ============ 字符集与生成 ============ */

/* 字符池信息：全池 + 各勾选集合（均已完成易混淆过滤） */
typedef struct {
    char pool[128];        /* 全池（各集合拼接，'\0' 结尾） */
    int  poolLen;
    char sets[4][32];      /* 各勾选集合，顺序：数字/小写/大写/特殊（按勾选裁剪） */
    int  setLens[4];
    int  setCount;
} CharsetInfo;

/* 过滤单个集合：按 noAmbig 剔除易混淆字符后拷入 out，返回长度 */
static int filter_set(const char *src, BOOL noAmbig, char *out, size_t outSize)
{
    size_t n = 0;
    for (size_t i = 0; src[i] && n + 1 < outSize; i++) {
        BOOL amb = FALSE;
        if (noAmbig) {
            for (size_t j = 0; g_ambiguous[j]; j++)
                if (src[i] == g_ambiguous[j]) { amb = TRUE; break; }
        }
        if (!amb) out[n++] = src[i];
    }
    out[n] = '\0';
    return (int)n;
}

/* 按勾选项构建 CharsetInfo：全池 + 各集合（过滤后） */
static void build_charset(BOOL digits, BOOL lower, BOOL upper,
                          BOOL special, BOOL noAmbig, CharsetInfo *ci)
{
    const char *src[4];
    int cnt = 0;
    if (digits)  src[cnt++] = g_digits;
    if (lower)   src[cnt++] = g_lower;
    if (upper)   src[cnt++] = g_upper;
    if (special) src[cnt++] = g_special;

    ci->setCount = cnt;
    ci->poolLen  = 0;
    for (int s = 0; s < cnt; s++) {
        ci->setLens[s] = filter_set(src[s], noAmbig,
                                    ci->sets[s], sizeof ci->sets[s]);
        for (int i = 0; i < ci->setLens[s]
                        && ci->poolLen + 1 < (int)sizeof ci->pool; i++)
            ci->pool[ci->poolLen++] = ci->sets[s][i];
    }
    ci->pool[ci->poolLen] = '\0';
}

/* 生成密码：每个勾选集合保底 1 个字符，其余从全池均匀抽取，
 * 最后 Fisher–Yates 洗牌（CSPRNG）消除位置规律。 */
static BOOL generate_password(const CharsetInfo *ci, int length,
                              char *out, size_t outSize)
{
    if (ci->poolLen <= 0 || length <= 0) return FALSE;
    if (length < ci->setCount) return FALSE;
    if ((size_t)length + 1 > outSize) return FALSE;

    int n = 0;
    for (int s = 0; s < ci->setCount; s++) {   /* 每类保底 1 个 */
        int idx = unbiased_index(ci->setLens[s]);
        if (idx < 0) return FALSE;
        out[n++] = ci->sets[s][idx];
    }
    for (; n < length; n++) {                  /* 其余从全池抽取 */
        int idx = unbiased_index(ci->poolLen);
        if (idx < 0) return FALSE;
        out[n] = ci->pool[idx];
    }
    for (int i = length - 1; i > 0; i--) {     /* Fisher–Yates 洗牌 */
        int j = unbiased_index(i + 1);
        if (j < 0) return FALSE;
        char t = out[i]; out[i] = out[j]; out[j] = t;
    }
    out[length] = '\0';
    return TRUE;
}

/* 熵估算：log2(池大小) × 长度（bit） */
static double estimate_entropy_bits(int poolLen, int length)
{
    if (poolLen <= 1 || length <= 0) return 0.0;
    return log2((double)poolLen) * (double)length;
}

/* 批量生成 count 个密码并逐行（CRLF）写入 path 文本文件。
 * 参数非法、无法创建文件或生成失败返回 FALSE（失败不留半截文件）。 */
static BOOL write_passwords_file(const wchar_t *path, int count,
                                 const CharsetInfo *ci, int length)
{
    if (!path || !*path || !ci || ci->poolLen <= 0) return FALSE;
    if (count <= 0) return FALSE;
    if (length < ci->setCount || length > MAX_LEN) return FALSE;

    FILE *f = _wfopen(path, L"wb");
    if (!f) return FALSE;

    char pwd[MAX_LEN + 1];
    BOOL ok = TRUE;
    for (int i = 0; i < count; i++) {
        if (!generate_password(ci, length, pwd, sizeof pwd)) { ok = FALSE; break; }
        fputs(pwd, f);
        fputs("\r\n", f);
    }
    if (fclose(f) != 0) ok = FALSE;
    if (!ok) _wremove(path);
    return ok;
}

/* 定义 CIPHER_TESTING 时以下 UI 区不编译（供 tests/test_logic.c 单测逻辑层） */
#ifndef CIPHER_TESTING

/* ============ UI ============ */

static double g_scale = 1.0;   /* 96dpi 基准的 DPI 缩放系数 */

/* 设计像素 → 实际像素 */
static int S(int v) { return (int)(v * g_scale + 0.5); }

static void create_fonts(void)
{
    if (g_hFontUI)   DeleteObject(g_hFontUI);
    if (g_hFontMono) DeleteObject(g_hFontMono);
    g_hFontUI = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            FF_DONTCARE, L"Segoe UI");
    g_hFontMono = CreateFontW(-S(17), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FF_DONTCARE, L"Consolas");
}

/* 把当前字体重新发给所有子控件（DPI 变化后调用） */
static void apply_fonts(void)
{
    struct { HWND *ph; HFONT *pf; } map[] = {
        { &g_hLblLen, &g_hFontUI }, { &g_hLenEdit, &g_hFontUI },
        { &g_hLblRange, &g_hFontUI }, { &g_hLenSpin, &g_hFontUI },
        { &g_hChkDigits, &g_hFontUI }, { &g_hChkLower, &g_hFontUI },
        { &g_hChkUpper, &g_hFontUI }, { &g_hChkSpecial, &g_hFontUI },
        { &g_hChkNoAmbig, &g_hFontUI }, { &g_hPwdEdit, &g_hFontMono },
        { &g_hBtnGen, &g_hFontUI }, { &g_hBtnCopy, &g_hFontUI },
        { &g_hBtnBatch, &g_hFontUI },
        { &g_hLblEntropy, &g_hFontUI },
    };
    for (int i = 0; i < PRESET_COUNT; i++)
        if (g_hPresets[i])
            SendMessageW(g_hPresets[i], WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (*map[i].ph)
            SendMessageW(*map[i].ph, WM_SETFONT, (WPARAM)*map[i].pf, TRUE);
}

/* ---- 强度条（自绘子窗口） ---- */
#define BAR_MSG_SETBITS (WM_USER + 1)   /* wp = bits*10 */

/* 强度分档与配色（阈值 40/60/128 bit） */
static const wchar_t *tier_name(double bits)
{
    if (bits < 40)  return L"弱";
    if (bits < 60)  return L"中";
    if (bits < 128) return L"强";
    return L"极强";
}
static COLORREF tier_color(double bits)
{
    if (bits < 40)  return RGB(232, 17, 35);    /* #E81123 */
    if (bits < 60)  return RGB(255, 140, 0);    /* #FF8C00 */
    if (bits < 128) return RGB(0, 120, 215);    /* #0078D7 */
    return RGB(16, 124, 16);                    /* #107C10 */
}

static LRESULT CALLBACK bar_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;                              /* 自绘，避免闪烁 */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        double bits = (double)GetWindowLongPtrW(h, GWLP_USERDATA) / 10.0;
        double frac = bits / 128.0;            /* 128 bit 封顶 */
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        /* 圆角胶囊条：半径 = 半高；空画笔避免圆角外描黑边 */
        int r = (rc.bottom - rc.top) / 2;
        HBRUSH bg = CreateSolidBrush(RGB(233, 233, 233));
        HBRUSH fg = CreateSolidBrush(tier_color(bits));
        HPEN nullPen = (HPEN)GetStockObject(NULL_PEN);
        HGDIOBJ oldPen  = SelectObject(dc, nullPen);
        HGDIOBJ oldBrush = SelectObject(dc, bg);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 2 * r, 2 * r);
        if (frac > 0) {
            int right = rc.left + (int)((rc.right - rc.left) * frac);
            SelectObject(dc, fg);
            RoundRect(dc, rc.left, rc.top, right, rc.bottom, 2 * r, 2 * r);
        }
        SelectObject(dc, oldBrush);           /* 恢复原对象后再销毁自建画刷 */
        SelectObject(dc, oldPen);             /* 库存画笔不可删除 */
        DeleteObject(bg);
        DeleteObject(fg);
        EndPaint(h, &ps);
        return 0;
    }
    case BAR_MSG_SETBITS:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)(int)(wp));
        InvalidateRect(h, NULL, TRUE);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void register_bar_class(void)
{
    WNDCLASSEXW bc;
    memset(&bc, 0, sizeof bc);
    bc.cbSize       = sizeof bc;
    /* 绘制内容依赖窗口宽度/高度：尺寸一变必须整体失效重绘，
     * 否则 WM_PAINT 被剪辑到新增暴露区，旧像素保留旧宽度的填充，
     * 拖动改宽时强度条会出现断层（填充边界冻结在上次全量绘制处）。 */
    bc.style        = CS_HREDRAW | CS_VREDRAW;
    bc.lpfnWndProc   = bar_wndproc;
    bc.hInstance     = g_hInst;
    bc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    bc.lpszClassName = L"CrtStrengthBar";
    RegisterClassExW(&bc);
}

static HWND create_child(const wchar_t *cls, const wchar_t *text,
                         DWORD style, DWORD exStyle,
                         int x, int y, int w, int h,
                         HWND parent, int id, HFONT font)
{
    HWND hChild = CreateWindowExW(exStyle, cls, text,
                                  WS_CHILD | WS_VISIBLE | style,
                                  x, y, w, h, parent,
                                  (HMENU)(INT_PTR)id, g_hInst, NULL);
    if (hChild && font) SendMessageW(hChild, WM_SETFONT, (WPARAM)font, TRUE);
    return hChild;
}

/* 依据当前客户区尺寸全量重排控件（自底向上：先定底行与强度条，再给密码框剩余空间） */
static void layout_controls(HWND hwnd)
{
    if (!g_hPwdEdit) return;                  /* 控件未建全时跳过 */
    RECT rc; GetClientRect(hwnd, &rc);
    int cx = rc.right, cy = rc.bottom;
    int m = S(20);                            /* 四周边距 */

    /* 行 1：长度 */
    int y = S(18);
    MoveWindow(g_hLblLen,   m, y + S(3), S(84), S(20), TRUE);
    MoveWindow(g_hLenEdit,  m + S(90), y, S(64), S(26), TRUE);
    /* 编辑框就位后重挂 buddy：UDS_ALIGNRIGHT 使箭头贴住编辑框右缘
     * （控件创建于 0,0，布局移动后必须重新定位，含 DPI 变化场景）。
     * 箭头与编辑框右端重叠且绘制在编辑框之下，需抬升 z 序使其可见 */
    SendMessageW(g_hLenSpin, UDM_SETBUDDY, (WPARAM)g_hLenEdit, 0);
    SetWindowPos(g_hLenSpin, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    MoveWindow(g_hLblRange, m + S(164), y + S(5), S(90), S(20), TRUE);

    /* 行 2：快捷档位（窗口够宽时拉开间距，封顶 S(24)） */
    y = S(54);
    int pw = S(56), ph = S(26), gap = S(8);
    if (cx - 2 * m > PRESET_COUNT * pw + (PRESET_COUNT - 1) * gap) {
        gap = (cx - 2 * m - PRESET_COUNT * pw) / (PRESET_COUNT - 1);
        if (gap > S(24)) gap = S(24);
    }
    for (int i = 0; i < PRESET_COUNT; i++)
        MoveWindow(g_hPresets[i], m + i * (pw + gap), y, pw, ph, TRUE);

    /* 行 3~4：字符集复选框 */
    y = S(96);
    MoveWindow(g_hChkDigits, m, y, S(180), S(22), TRUE);
    MoveWindow(g_hChkLower,  m + S(200), y, S(200), S(22), TRUE);
    y = S(124);
    MoveWindow(g_hChkUpper,   m, y, S(180), S(22), TRUE);
    MoveWindow(g_hChkSpecial, m + S(200), y, S(200), S(22), TRUE);
    y = S(152);
    MoveWindow(g_hChkNoAmbig, m, y, S(420), S(22), TRUE);

    /* 底部：按钮行（批量生成 + 生成密码 + 复制，右下角） */
    int bh = S(34);
    int by = cy - m - bh;
    MoveWindow(g_hBtnCopy, cx - m - S(96), by, S(96), bh, TRUE);
    MoveWindow(g_hBtnGen,  cx - m - S(96) - S(10) - S(140), by, S(140), bh, TRUE);
    MoveWindow(g_hBtnBatch, cx - m - S(96) - S(10) - S(140) - S(10) - S(110),
               by, S(110), bh, TRUE);

    /* 强度条 + 熵值（密码框与按钮行之间，自底向上定位） */
    int ey = by - S(8) - S(20);                    /* 熵值文本行 */
    int sy = ey - S(6) - S(14);                    /* 强度条行 */
    MoveWindow(g_hLblEntropy, m, ey, cx - 2 * m, S(20), TRUE);
    MoveWindow(g_hBar, m, sy, cx - 2 * m, S(14), TRUE);

    /* 中部：密码框 = 剩余全部空间（弹性区） */
    y = S(188);
    int pwdBottom = sy - S(10);
    int pwdH = pwdBottom - y;
    if (pwdH < S(48)) pwdH = S(48);
    MoveWindow(g_hPwdEdit, m, y, cx - 2 * m, pwdH, TRUE);
}

static void create_controls(HWND hwnd)
{
    create_fonts();

    /* 长度行（位置由 layout_controls 统一计算） */
    g_hLblLen = create_child(L"Static", L"密码长度：", SS_LEFT, 0,
                             0, 0, 0, 0, hwnd, -1, g_hFontUI);
    g_hLenEdit = create_child(L"Edit", L"", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 0,
                              0, 0, 0, 0, hwnd, IDC_LEN_EDIT, g_hFontUI);
    g_hLblRange = create_child(L"Static", L"(4 ~ 256)", SS_LEFT, 0,
                               0, 0, 0, 0, hwnd, -1, g_hFontUI);

    g_hLenSpin = CreateWindowExW(0, UPDOWN_CLASSW, L"",
                        WS_CHILD | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT |
                        UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_LEN_SPIN,
                        g_hInst, NULL);
    /* UDS_AUTOBUDDY 选 z 序相邻控件，因标签在编辑框之后创建会误选 g_hLblRange
     * （并把初始值 0 写进标签）；显式指定编辑框为 buddy 并恢复标签文本，
     * UDS_ALIGNRIGHT 随之把箭头定位到编辑框右缘 */
    SendMessageW(g_hLenSpin, UDM_SETBUDDY, (WPARAM)g_hLenEdit, 0);
    SetWindowTextW(g_hLblRange, L"(4 ~ 256)");
    SendMessageW(g_hLenSpin, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
    SendMessageW(g_hLenSpin, UDM_SETRANGE32, MIN_LEN, MAX_LEN);
    SendMessageW(g_hLenSpin, UDM_SETPOS32, 0, DEFAULT_LEN);

    /* 字符集复选框 */
    g_hChkDigits = create_child(L"Button", L"数字 (0-9)", BS_AUTOCHECKBOX, 0,
                                0, 0, 0, 0, hwnd, IDC_CHK_DIGITS, g_hFontUI);
    g_hChkLower  = create_child(L"Button", L"小写字母 (a-z)", BS_AUTOCHECKBOX, 0,
                                0, 0, 0, 0, hwnd, IDC_CHK_LOWER, g_hFontUI);
    g_hChkUpper  = create_child(L"Button", L"大写字母 (A-Z)", BS_AUTOCHECKBOX, 0,
                                0, 0, 0, 0, hwnd, IDC_CHK_UPPER, g_hFontUI);
    g_hChkSpecial= create_child(L"Button", L"特殊字符 (!@#…)", BS_AUTOCHECKBOX, 0,
                                0, 0, 0, 0, hwnd, IDC_CHK_SPECIAL, g_hFontUI);
    g_hChkNoAmbig= create_child(L"Button", L"排除易混淆字符 (0 O 1 l I)",
                                BS_AUTOCHECKBOX, 0,
                                0, 0, 0, 0, hwnd, IDC_CHK_NOAMBIG, g_hFontUI);

    /* 快捷档位按钮 */
    wchar_t txt[8];
    for (int i = 0; i < PRESET_COUNT; i++) {
        swprintf(txt, 8, L"%d", g_presetLens[i]);
        g_hPresets[i] = create_child(L"Button", txt, BS_PUSHBUTTON, 0,
                                     0, 0, 0, 0, hwnd, IDC_BTN_P8 + i, g_hFontUI);
    }

    /* 密码显示（多行只读自动换行 + 垂直滚动）+ 强度条 + 熵值 */
    g_hPwdEdit = create_child(L"Edit", L"",
                              WS_BORDER | ES_MULTILINE | ES_READONLY |
                              WS_VSCROLL | ES_AUTOVSCROLL, 0,
                              0, 0, 0, 0, hwnd, IDC_PWD_EDIT, g_hFontMono);
    g_hBar = CreateWindowExW(0, L"CrtStrengthBar", L"",
                             WS_CHILD | WS_VISIBLE,
                             0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_BAR,
                             g_hInst, NULL);
    g_hLblEntropy = create_child(L"Static", L"", SS_LEFT, 0,
                                 0, 0, 0, 0, hwnd, IDC_LBL_ENTROPY, g_hFontUI);
    g_hBtnBatch = create_child(L"Button", L"批量生成", BS_PUSHBUTTON, 0,
                               0, 0, 0, 0, hwnd, IDC_BTN_BATCH, g_hFontUI);
    g_hBtnGen  = create_child(L"Button", L"生成密码", BS_PUSHBUTTON, 0,
                              0, 0, 0, 0, hwnd, IDC_BTN_GENERATE, g_hFontUI);
    g_hBtnCopy = create_child(L"Button", L"复制", BS_PUSHBUTTON, 0,
                              0, 0, 0, 0, hwnd, IDC_BTN_COPY, g_hFontUI);

    /* 默认勾选：数字 + 小写 + 大写 */
    SendMessageW(g_hChkDigits, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_hChkLower,  BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_hChkUpper,  BM_SETCHECK, BST_CHECKED, 0);

    g_ready = TRUE;

    layout_controls(hwnd);
}

/* 读取设置并生成 */
static void on_generate(HWND hwnd)
{
    int length = (int)SendMessageW(g_hLenSpin, UDM_GETPOS32, 0, 0);
    if (length < MIN_LEN) length = MIN_LEN;
    if (length > MAX_LEN) length = MAX_LEN;

    BOOL digits  = SendMessageW(g_hChkDigits,  BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL lower   = SendMessageW(g_hChkLower,   BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL upper   = SendMessageW(g_hChkUpper,   BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL special = SendMessageW(g_hChkSpecial, BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL noAmbig = SendMessageW(g_hChkNoAmbig, BM_GETCHECK, 0, 0) == BST_CHECKED;

    CharsetInfo ci;
    build_charset(digits, lower, upper, special, noAmbig, &ci);
    if (ci.poolLen <= 0) {
        MessageBoxW(hwnd, L"请至少选择一种字符组成。", L"提示",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    char pwd[MAX_LEN + 1];
    if (!generate_password(&ci, length, pwd, sizeof pwd)) {
        MessageBoxW(hwnd, L"生成失败：无法获取安全随机数。", L"错误",
                    MB_OK | MB_ICONERROR);
        return;
    }

    /* 密码均为 ASCII，转 wchar 显示 */
    wchar_t wpwd[MAX_LEN + 1];
    for (int i = 0; i < length; i++) wpwd[i] = (wchar_t)(unsigned char)pwd[i];
    wpwd[length] = L'\0';
    SetWindowTextW(g_hPwdEdit, wpwd);

    /* 更新强度条与熵值文本 */
    double bits = estimate_entropy_bits(ci.poolLen, length);
    SendMessageW(g_hBar, BAR_MSG_SETBITS, (WPARAM)(int)(bits * 10), 0);
    wchar_t ebuf[64];
    swprintf(ebuf, 64, L"熵 ≈ %.1f bit · %ls", bits, tier_name(bits));
    SetWindowTextW(g_hLblEntropy, ebuf);
}

/* 复制到剪贴板（CF_UNICODETEXT） */
static void on_copy(HWND hwnd)
{
    int len = GetWindowTextLengthW(g_hPwdEdit);
    if (len <= 0) {
        MessageBoxW(hwnd, L"还没有可复制的密码，请先生成。", L"提示",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }

    wchar_t *buf = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));
    if (!buf) {
        MessageBoxW(hwnd, L"复制失败：内存分配失败。", L"错误",
                    MB_OK | MB_ICONERROR);
        return;
    }
    GetWindowTextW(g_hPwdEdit, buf, len + 1);

    size_t bytes = (size_t)(len + 1) * sizeof(wchar_t);
    HGLOBAL hG = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hG) {
        free(buf);
        MessageBoxW(hwnd, L"复制失败：内存分配失败。", L"错误",
                    MB_OK | MB_ICONERROR);
        return;
    }
    void *lp = GlobalLock(hG);
    if (!lp) {
        GlobalFree(hG);
        free(buf);
        MessageBoxW(hwnd, L"复制失败：内存锁定失败。", L"错误",
                    MB_OK | MB_ICONERROR);
        return;
    }
    memcpy(lp, buf, bytes);
    GlobalUnlock(hG);
    free(buf);

    if (!OpenClipboard(hwnd)) {
        GlobalFree(hG);
        MessageBoxW(hwnd, L"打开剪贴板失败。", L"错误",
                    MB_OK | MB_ICONERROR);
        return;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, hG)) {
        GlobalFree(hG);                    /* 所有权未转移，需自行释放 */
        CloseClipboard();
        MessageBoxW(hwnd, L"复制失败：写入剪贴板失败。", L"错误",
                    MB_OK | MB_ICONERROR);
        return;
    }
    CloseClipboard();                      /* 成功后 hG 归剪贴板所有 */
}

/* ============ 批量生成弹窗 ============ */

/* 弹窗内部控件 ID（独立窗口，与主窗口 ID 空间互不影响） */
#define IDC_BD_COUNT_EDIT 1101
#define IDC_BD_COUNT_SPIN 1102
#define IDC_BD_PATH_EDIT  1103
#define IDC_BD_BROWSE     1104
#define IDC_BD_GO         1105
#define IDC_BD_CANCEL     1106

#define BD_MIN_COUNT 1
#define BD_MAX_COUNT 10000

static HWND g_hBdCountEdit, g_hBdCountSpin, g_hBdPathEdit, g_hBdBrowse,
           g_hBdGo, g_hBdCancel, g_hBdLblCount, g_hBdLblPath, g_hBdHint;
static HWND g_hBdOwner;                      /* 打开弹窗的主窗口 */
static HFONT g_hBdFont;
static double g_bdScale = 1.0;               /* 弹窗自身 DPI 缩放（不干扰主窗口 g_scale） */
static wchar_t g_bdHint[128];                /* 打开时按主界面设置生成 */

/* 弹窗设计像素 → 实际像素 */
static int BS(int v) { return (int)(v * g_bdScale + 0.5); }

/* 预填默认路径：文档目录\passwords_YYYYMMDD_HHMM.txt（时间戳防覆盖） */
static void make_default_batch_path(wchar_t *out, size_t cch)
{
    wchar_t dir[MAX_PATH];
    wcscpy(dir, L".");
    SHGetFolderPathW(NULL, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, NULL, 0, dir);
    SYSTEMTIME st;
    GetLocalTime(&st);
    swprintf(out, cch, L"%ls\\passwords_%04u%02u%02u_%02u%02u.txt",
             dir, (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
             (unsigned)st.wHour, (unsigned)st.wMinute);
}

static void batch_create_controls(HWND h)
{
    g_hBdFont = CreateFontW(-BS(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            FF_DONTCARE, L"Segoe UI");

    g_hBdLblCount = create_child(L"Static", L"数量：", SS_LEFT, 0,
                                 0, 0, 0, 0, h, -1, g_hBdFont);
    /* 紧随其后创建 spin：UDS_AUTOBUDDY 绑定 z 序相邻的前一控件（即数量框） */
    g_hBdCountEdit = create_child(L"Edit", L"",
                                  WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 0,
                                  0, 0, 0, 0, h, IDC_BD_COUNT_EDIT, g_hBdFont);
    g_hBdCountSpin = CreateWindowExW(0, UPDOWN_CLASSW, L"",
                        WS_CHILD | WS_VISIBLE | UDS_AUTOBUDDY | UDS_SETBUDDYINT |
                        UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                        0, 0, 0, 0, h, (HMENU)(INT_PTR)IDC_BD_COUNT_SPIN,
                        g_hInst, NULL);
    SendMessageW(g_hBdCountSpin, WM_SETFONT, (WPARAM)g_hBdFont, TRUE);
    SendMessageW(g_hBdCountSpin, UDM_SETRANGE32, BD_MIN_COUNT, BD_MAX_COUNT);
    SendMessageW(g_hBdCountSpin, UDM_SETPOS32, 0, 10);

    g_hBdLblPath = create_child(L"Static", L"保存到：", SS_LEFT, 0,
                                0, 0, 0, 0, h, -1, g_hBdFont);

    wchar_t defpath[2 * MAX_PATH];
    make_default_batch_path(defpath, 2 * MAX_PATH);
    g_hBdPathEdit = create_child(L"Edit", defpath,
                                 WS_BORDER | ES_AUTOHSCROLL, 0,
                                 0, 0, 0, 0, h, IDC_BD_PATH_EDIT, g_hBdFont);

    g_hBdBrowse = create_child(L"Button", L"浏览…", BS_PUSHBUTTON, 0,
                               0, 0, 0, 0, h, IDC_BD_BROWSE, g_hBdFont);
    g_hBdHint = create_child(L"Static", g_bdHint, SS_LEFT, 0,
                             0, 0, 0, 0, h, -1, g_hBdFont);
    g_hBdGo = create_child(L"Button", L"开始生成",
                           BS_PUSHBUTTON | BS_DEFPUSHBUTTON, 0,
                           0, 0, 0, 0, h, IDC_BD_GO, g_hBdFont);
    g_hBdCancel = create_child(L"Button", L"取消", BS_PUSHBUTTON, 0,
                               0, 0, 0, 0, h, IDC_BD_CANCEL, g_hBdFont);
}

static void batch_layout(HWND h)
{
    if (!g_hBdPathEdit) return;
    RECT rc; GetClientRect(h, &rc);
    int cx = rc.right, cy = rc.bottom;
    int m = BS(14);

    /* 行 1：数量（标签需容纳「数量：」三个汉字+冒号，宽度 ≥56） */
    int y = m + BS(4);
    MoveWindow(g_hBdLblCount, m, y + BS(3), BS(58), BS(20), TRUE);
    MoveWindow(g_hBdCountEdit, m + BS(64), y, BS(64), BS(26), TRUE);
    /* UDS_ALIGNRIGHT：spin 自动贴数量框右端，无需定位 */

    /* 行 2：保存到 标签 + 路径框 + 浏览按钮 */
    int y2 = y + BS(34);
    MoveWindow(g_hBdLblPath, m, y2, BS(70), BS(20), TRUE);
    int bw = BS(72);
    MoveWindow(g_hBdPathEdit, m, y2 + BS(24), cx - 2 * m - bw - BS(8), BS(26), TRUE);
    MoveWindow(g_hBdBrowse, cx - m - bw, y2 + BS(24), bw, BS(26), TRUE);

    /* 行 3：当前设置提示（可两行） */
    int y3 = y2 + BS(58);
    MoveWindow(g_hBdHint, m, y3, cx - 2 * m, BS(36), TRUE);

    /* 底部：开始生成 / 取消（右对齐） */
    int bh = BS(30);
    int by = cy - m - bh;
    MoveWindow(g_hBdCancel, cx - m - BS(72), by, BS(72), bh, TRUE);
    MoveWindow(g_hBdGo, cx - m - BS(72) - BS(8) - BS(96), by, BS(96), bh, TRUE);
}

/* 浏览：系统保存文件对话框（自带覆盖确认） */
static void batch_on_browse(HWND h)
{
    wchar_t file[2 * MAX_PATH];
    GetWindowTextW(g_hBdPathEdit, file, 2 * MAX_PATH);
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = h;
    ofn.lpstrFilter = L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 2 * MAX_PATH;
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn))
        SetWindowTextW(g_hBdPathEdit, file);
}

/* 开始生成：读取弹窗输入 + 主界面当前设置 → 写文件 */
static void batch_on_go(HWND h)
{
    wchar_t nbuf[16];
    GetWindowTextW(g_hBdCountEdit, nbuf, 16);
    int count = _wtoi(nbuf);
    if (count < BD_MIN_COUNT || count > BD_MAX_COUNT) {
        MessageBoxW(h, L"数量需在 1 ~ 10000 之间。", L"提示",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    /* 读取路径并去首尾空白 */
    static wchar_t path[2 * MAX_PATH];
    GetWindowTextW(g_hBdPathEdit, path, 2 * MAX_PATH);
    wchar_t *p = path;
    while (*p == L' ' || *p == L'\t') p++;
    size_t n = wcslen(p);
    while (n > 0 && (p[n - 1] == L' ' || p[n - 1] == L'\t')) p[--n] = L'\0';
    if (n == 0) {
        MessageBoxW(h, L"请填写保存路径。", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }

    /* 手输路径覆盖已有文件时确认（浏览路径已由系统对话框确认过） */
    if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) {
        if (MessageBoxW(h, L"文件已存在，是否覆盖？", L"确认",
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    }

    /* 沿用主界面当前设置（主窗口已禁用但仍可读取控件状态） */
    int length = (int)SendMessageW(g_hLenSpin, UDM_GETPOS32, 0, 0);
    if (length < MIN_LEN) length = MIN_LEN;
    if (length > MAX_LEN) length = MAX_LEN;
    BOOL digits  = SendMessageW(g_hChkDigits,  BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL lower   = SendMessageW(g_hChkLower,   BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL upper   = SendMessageW(g_hChkUpper,   BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL special = SendMessageW(g_hChkSpecial, BM_GETCHECK, 0, 0) == BST_CHECKED;
    BOOL noAmbig = SendMessageW(g_hChkNoAmbig, BM_GETCHECK, 0, 0) == BST_CHECKED;

    CharsetInfo ci;
    build_charset(digits, lower, upper, special, noAmbig, &ci);
    if (ci.poolLen <= 0) {
        MessageBoxW(h, L"请先在主界面至少选择一种字符组成。", L"提示",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    if (!write_passwords_file(p, count, &ci, length)) {
        MessageBoxW(h, L"写入失败：请检查路径是否有效、磁盘是否可写。",
                    L"错误", MB_OK | MB_ICONERROR);
        return;
    }

    wchar_t msg[2 * MAX_PATH + 64];
    swprintf(msg, 2 * MAX_PATH + 64, L"已生成 %d 个密码：\n%ls", count, p);
    MessageBoxW(h, msg, L"批量生成", MB_OK | MB_ICONINFORMATION);
    DestroyWindow(h);
}

static LRESULT CALLBACK batch_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HDC dc = GetDC(h);
        g_bdScale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0;
        ReleaseDC(h, dc);
        batch_create_controls(h);
        batch_layout(h);
        return 0;
    }
    case WM_SIZE:
        batch_layout(h);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (code == BN_CLICKED || id == IDOK || id == IDCANCEL) {
            if (id == IDC_BD_BROWSE) { batch_on_browse(h); return 0; }
            if (id == IDC_BD_GO || id == IDOK) { batch_on_go(h); return 0; }
            if (id == IDC_BD_CANCEL || id == IDCANCEL) { DestroyWindow(h); return 0; }
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        if (g_hBdFont) { DeleteObject(g_hBdFont); g_hBdFont = NULL; }
        PostQuitMessage(0);            /* 结束弹窗模态消息循环 */
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void register_batch_class(void)
{
    WNDCLASSEXW bc;
    memset(&bc, 0, sizeof bc);
    bc.cbSize       = sizeof bc;
    bc.lpfnWndProc  = batch_wndproc;
    bc.hInstance    = g_hInst;
    bc.hCursor      = LoadCursorW(NULL, IDC_ARROW);
    bc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    bc.hIcon        = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    bc.hIconSm      = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    bc.lpszClassName = L"CrtBatchDlg";
    RegisterClassExW(&bc);
}

/* 打开批量弹窗（模态）：尺寸 = 主窗口客户区 50%（不低于控件所需最小值），居中于主窗 */
static void open_batch_dialog(HWND hMain)
{
    /* 提示行按主界面当前设置生成 */
    int length = (int)SendMessageW(g_hLenSpin, UDM_GETPOS32, 0, 0);
    if (length < MIN_LEN) length = MIN_LEN;
    if (length > MAX_LEN) length = MAX_LEN;
    wchar_t sets[64] = L"";
    if (SendMessageW(g_hChkDigits,  BM_GETCHECK, 0, 0) == BST_CHECKED) wcscat(sets, L"数字+");
    if (SendMessageW(g_hChkLower,   BM_GETCHECK, 0, 0) == BST_CHECKED) wcscat(sets, L"小写+");
    if (SendMessageW(g_hChkUpper,   BM_GETCHECK, 0, 0) == BST_CHECKED) wcscat(sets, L"大写+");
    if (SendMessageW(g_hChkSpecial, BM_GETCHECK, 0, 0) == BST_CHECKED) wcscat(sets, L"特殊+");
    size_t sn = wcslen(sets);
    if (sn > 0 && sets[sn - 1] == L'+') sets[sn - 1] = L'\0';
    BOOL noAmbig = SendMessageW(g_hChkNoAmbig, BM_GETCHECK, 0, 0) == BST_CHECKED;
    swprintf(g_bdHint, 128, L"按当前设置生成：长度 %d · %ls%ls",
             length, sets, noAmbig ? L"（排除易混淆）" : L"");

    g_hBdOwner = hMain;

    RECT rm; GetClientRect(hMain, &rm);
    int dw = (rm.right - rm.left) / 2, dh = (rm.bottom - rm.top) / 2;
    if (dw < S(340)) dw = S(340);           /* 控件可容纳的最小尺寸下限 */
    if (dh < S(240)) dh = S(240);

    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT r = { 0, 0, dw, dh };
    AdjustWindowRect(&r, st, FALSE);

    RECT rw; GetWindowRect(hMain, &rw);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int x = rw.left + ((rw.right - rw.left) - ww) / 2;
    int y = rw.top + ((rw.bottom - rw.top) - wh) / 2;

    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"CrtBatchDlg",
                    L"批量生成密码", st, x, y, ww, wh,
                    hMain, NULL, g_hInst, NULL);
    if (!hDlg) return;
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    /* 模态循环：禁用主窗口直到弹窗销毁 */
    EnableWindow(hMain, FALSE);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(hMain, TRUE);
    SetActiveWindow(hMain);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HDC dc = GetDC(hwnd);
        g_scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0;
        ReleaseDC(hwnd, dc);
        create_controls(hwnd);
        return 0;
    }

    case WM_COMMAND: {
        int id   = LOWORD(wp);
        int code = HIWORD(wp);
        if (code == BN_CLICKED) {
            if (id == IDC_BTN_GENERATE) { on_generate(hwnd); return 0; }
            if (id == IDC_BTN_COPY)     { on_copy(hwnd);     return 0; }
            if (id == IDC_BTN_BATCH)   { open_batch_dialog(hwnd); return 0; }
            if (id >= IDC_BTN_P8 && id <= IDC_BTN_P256) {
                /* 档位：设置长度 → 触发 EN_CHANGE 自动重生成；
                 * 位置未变时不会触发 EN_CHANGE，需手动生成 */
                int want = g_presetLens[id - IDC_BTN_P8];
                int cur = (int)SendMessageW(g_hLenSpin, UDM_GETPOS32, 0, 0);
                if (cur == want)
                    on_generate(hwnd);
                else
                    SendMessageW(g_hLenSpin, UDM_SETPOS32, 0, want);
                return 0;
            }
            if (id >= IDC_CHK_DIGITS && id <= IDC_CHK_NOAMBIG) {
                if (g_ready) on_generate(hwnd);   /* 选项变更自动重生成 */
                return 0;
            }
        } else if (code == EN_CHANGE && id == IDC_LEN_EDIT) {
            if (g_ready) on_generate(hwnd);       /* 含 spin 增减 */
            return 0;
        } else if (code == EN_KILLFOCUS && id == IDC_LEN_EDIT) {
            /* 失焦时把越界输入修正回范围内 */
            wchar_t wbuf[16];
            GetWindowTextW(g_hLenEdit, wbuf, 16);
            int v = _wtoi(wbuf);
            if (v < MIN_LEN || v > MAX_LEN)
                SendMessageW(g_hLenSpin, UDM_SETPOS32, 0,
                             v < MIN_LEN ? MIN_LEN : MAX_LEN);
            return 0;
        }
        return 0;
    }

    case WM_SIZE:
        layout_controls(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        RECT r = { 0, 0, S(MIN_CLIENT_W), S(MIN_CLIENT_H) };
        AdjustWindowRect(&r, (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE), FALSE);
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }

    case WM_DPICHANGED: {
        g_scale = HIWORD(wp) / 96.0;
        create_fonts();
        apply_fonts();
        RECT *pr = (RECT *)lp;      /* 系统建议的新窗口矩形 */
        SetWindowPos(hwnd, NULL, pr->left, pr->top,
                     pr->right - pr->left, pr->bottom - pr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;                   /* 随后的 WM_SIZE 触发重排 */
    }

    case WM_DESTROY:
        if (g_hFontUI)   DeleteObject(g_hFontUI);
        if (g_hFontMono) DeleteObject(g_hFontMono);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd;
    g_hInst = hInst;

    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_UPDOWN_CLASS };
    InitCommonControlsEx(&icc);

    const wchar_t *cls = L"CrtCipherPwdGen";
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = cls;
    wc.hIcon         = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm       = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    register_bar_class();                /* 强度条子窗口类（创建主窗口前注册） */
    register_batch_class();              /* 批量生成弹窗类 */

    DWORD style = WS_OVERLAPPEDWINDOW;   /* 含 THICKFRAME/MAXIMIZEBOX：可缩放 */
    HWND hwnd = CreateWindowExW(0, cls, L"随机密码生成器",
                    style,
                    CW_USEDEFAULT, CW_USEDEFAULT,
                    CW_USEDEFAULT, CW_USEDEFAULT,
                    NULL, NULL, hInst, NULL);
    if (!hwnd) return 0;

    /* WM_CREATE 已按实际 DPI 设置 g_scale；把客户区调到设计尺寸 640×440 */
    RECT rc = { 0, 0, S(CLIENT_W), S(CLIENT_H) };
    AdjustWindowRect(&rc, style, FALSE);
    SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER);

    /* 启动居中：在窗口所在显示器的工作区中央显示（多屏下也正确） */
    {
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        if (GetMonitorInfoW(mon, &mi)) {
            RECT wr;
            GetWindowRect(hwnd, &wr);
            int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
            int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww) / 2;
            int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - wh) / 2;
            SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    }

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    on_generate(hwnd);  /* 启动即生成一个默认密码 */

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
#endif /* CIPHER_TESTING */
