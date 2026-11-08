# 密码生成器增强（256 长度 / 图标 / 可缩放 / 交互优化）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把现有 4–32 位密码生成器扩展为 4–256 位，加应用图标，窗口可缩放（DPI 感知），并增强交互（熵/强度显示、档位按钮、自动重生成、每类字符保底）。

**Architecture:** 维持单文件纯 C + Win32 原生 API（`src/password_generator.c`），零新依赖。逻辑层（字符集/生成/熵）用控制台测试程序 TDD；UI 层用"设计像素 × DPI 缩放 + 自底向上绝对布局函数"实现缩放；图标用 Pillow 脚本生成多尺寸 `app.ico` 嵌入资源。

**Tech Stack:** C (MinGW-w64 gcc), Win32 API, BCryptGenRandom, windres, Python 3 + Pillow（仅生成图标）, PowerShell（仅验证截图）

## Global Constraints

- 平台 Windows 10+；纯 C + Win32 原生 API；除系统库（bcrypt/comctl32/gdi32）外**零运行时依赖**
- 编译器：便携 MinGW-w64，命令一律用 `gcc` / `windres`（已在 PATH；否则 `%MINGW64_HOME%\bin`）
- 构建入口 `build.bat` 的编译命令**不得改变**（仍单文件 + rsrc.o -lbcrypt -lcomctl32）
- 本机 Python 命令是 `python`（`python3` 是 Store stub，勿用）
- 界面文案全部中文；源码注释风格沿用现状（中文、分区横线注释）
- **本目录不是 git 仓库**：任务以"构建 + 验证"收尾，不执行 git 提交
- 长度常量：`MIN_LEN 4`、`MAX_LEN 256`、`DEFAULT_LEN 16`；强度阈值 40/60/128 bit；窗口默认客户区 640×440、最小 560×360
- 设计规格：`docs/superpowers/specs/2026-10-05-keygen-256-icon-resize-ui-design.md`

---

### Task 1: 生成逻辑扩展（TDD）— 256 长度 / 每类保底 / 洗牌 / 熵

**Files:**
- Modify: `src/password_generator.c`（常量、逻辑区、`on_generate` 调用处、UI 区加测试开关）
- Create: `tests/test_logic.c`

**Interfaces:**
- Consumes: 现有 `unbiased_index` / `gen_random_bytes`（签名不变）
- Produces（Task 4 的 UI 代码依赖）:
  - `typedef struct { char pool[128]; int poolLen; char sets[4][32]; int setLens[4]; int setCount; } CharsetInfo;`
  - `static void build_charset(BOOL digits, BOOL lower, BOOL upper, BOOL special, BOOL noAmbig, CharsetInfo *ci);`
  - `static BOOL generate_password(const CharsetInfo *ci, int length, char *out, size_t outSize);`
  - `static double estimate_entropy_bits(int poolLen, int length);`
  - 宏 `CIPHER_TESTING`：定义它时 UI 区（`/* ============ UI ============ */` 之后全部代码）不参与编译，供测试程序 `#include` 源文件

- [ ] **Step 1: 写失败测试**

创建 `tests/test_logic.c`，内容如下（完整文件）：

```c
/*
 * test_logic.c — 生成逻辑单元测试（控制台）
 * 编译：gcc -O2 -Wall -Wextra tests/test_logic.c -o tests/test_logic.exe -lbcrypt
 */
#include <stdio.h>
#include <string.h>

#define CIPHER_TESTING            /* 排除 password_generator.c 的 UI 区 */
#include "../src/password_generator.c"

static int g_fails = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %d: %s\n", __LINE__, #cond); g_fails++; } \
} while (0)

/* pwd 前 len 个字符中属于 set 的个数 */
static int count_from_set(const char *pwd, int len, const char *set, int setLen)
{
    int cnt = 0;
    for (int i = 0; i < len; i++)
        for (int j = 0; j < setLen; j++)
            if (pwd[i] == set[j]) { cnt++; break; }
    return cnt;
}

static int chr_in(const char *s, char c) { return strchr(s, c) != NULL; }

int main(void)
{
    CharsetInfo ci;
    char pwd[MAX_LEN + 1];

    /* --- build_charset：默认三集合 --- */
    build_charset(TRUE, TRUE, TRUE, FALSE, FALSE, &ci);
    CHECK(ci.setCount == 3);
    CHECK(ci.poolLen == 10 + 26 + 26);
    CHECK(strlen(ci.pool) == (size_t)ci.poolLen);
    CHECK(ci.setLens[0] == 10 && ci.setLens[1] == 26 && ci.setLens[2] == 26);

    /* --- build_charset：全勾选 + 排除易混淆 --- */
    build_charset(TRUE, TRUE, TRUE, TRUE, TRUE, &ci);
    CHECK(ci.setCount == 4);
    CHECK(ci.setLens[0] == 8);              /* 数字剔 0/1 剩 8 */
    CHECK(ci.setLens[1] == 24 && ci.setLens[2] == 24);
    CHECK(!chr_in(ci.pool, '0') && !chr_in(ci.pool, 'O')
          && !chr_in(ci.pool, 'o') && !chr_in(ci.pool, '1')
          && !chr_in(ci.pool, 'l') && !chr_in(ci.pool, 'I'));
    CHECK(ci.poolLen == 8 + 24 + 24 + 26);

    /* --- build_charset：全不勾 --- */
    build_charset(FALSE, FALSE, FALSE, FALSE, FALSE, &ci);
    CHECK(ci.setCount == 0 && ci.poolLen == 0);

    /* --- generate_password：长度、合法性、每类保底（200 次随机抽样） --- */
    build_charset(TRUE, TRUE, TRUE, TRUE, FALSE, &ci);
    for (int trial = 0; trial < 200; trial++) {
        int len = 4 + (trial % 253);        /* 4..256 全范围 */
        CHECK(generate_password(&ci, len, pwd, sizeof pwd));
        CHECK((int)strlen(pwd) == len);
        for (int i = 0; i < len; i++)
            if (!chr_in(ci.pool, pwd[i])) { CHECK(0 && "非法字符"); break; }
        for (int s = 0; s < ci.setCount; s++)
            if (count_from_set(pwd, len, ci.sets[s], ci.setLens[s]) < 1) {
                CHECK(0 && "某勾选集合缺字符"); break;
            }
    }

    /* --- generate_password：长度 < 集合数 → 拒绝 --- */
    build_charset(TRUE, TRUE, TRUE, TRUE, TRUE, &ci);
    CHECK(!generate_password(&ci, 3, pwd, sizeof pwd));

    /* --- generate_password：长度 == 集合数 → 每类恰好 1 个 --- */
    CHECK(generate_password(&ci, 4, pwd, sizeof pwd));
    for (int s = 0; s < 4; s++)
        CHECK(count_from_set(pwd, 4, ci.sets[s], ci.setLens[s]) == 1);

    /* --- generate_password：缓冲区不足 → 拒绝 --- */
    char small[8];
    CHECK(!generate_password(&ci, 10, small, sizeof small));

    /* --- estimate_entropy_bits --- */
    double e = estimate_entropy_bits(10, 8);          /* 8*log2(10)≈26.5754 */
    CHECK(e > 26.57 && e < 26.58);
    CHECK(estimate_entropy_bits(1, 100) == 0.0);
    CHECK(estimate_entropy_bits(0, 8) == 0.0);

    if (g_fails == 0) printf("ALL TESTS PASSED\n");
    return g_fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行测试确认失败**

```bash
gcc -O2 -Wall -Wextra tests/test_logic.c -o tests/test_logic.exe -lbcrypt && ./tests/test_logic.exe
```
预期：编译错误（`CharsetInfo` 未定义 / `generate_password` 参数不匹配），证明测试先行。

- [ ] **Step 3: 修改 `src/password_generator.c` 逻辑区**

3a. 文件头注释第 7 行改为 `- 长度 4~256，默认 16`，并在功能列表加一行 `- 每种勾选字符至少含一个（保底抽取 + 洗牌）`。

3b. `#include <string.h>` 之后加：

```c
#include <math.h>
```

3c. 参数区改为：

```c
/* ---- 参数 ---- */
#define MIN_LEN     4
#define MAX_LEN     256
#define DEFAULT_LEN 16
```

3d. 用以下代码**整体替换** `/* ============ 字符集与生成 ============ */` 区（从该注释行到 `generate_password` 函数结束）：

```c
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
```

3e. UI 区加测试开关：在 `/* ============ UI ============ */` 注释行**之前**插入：

```c
#ifndef CIPHER_TESTING
```

文件末尾（`wWinMain` 结束后）追加一行 `#endif`。同时在该注释行上方加说明：

```c
/* 定义 CIPHER_TESTING 时以下 UI 区不编译（供 tests/test_logic.c 单测逻辑层） */
```

3f. `on_generate` 中读取设置与生成段改为（保留钳制回写，Task 4 再改交互）：

```c
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
```

（`MessageBoxW` 之后的转宽字符、`SetWindowTextW` 段不变。）

3g. `create_controls` 中范围提示 `L"(4 ~ 32)"` 改为 `L"(4 ~ 256)"`。

- [ ] **Step 4: 运行测试确认通过**

```bash
gcc -O2 -Wall -Wextra tests/test_logic.c -o tests/test_logic.exe -lbcrypt && ./tests/test_logic.exe
```
预期输出：`ALL TESTS PASSED`（返回码 0）。

- [ ] **Step 5: 构建 GUI 并冒烟验证**

```bash
cmd //c build.bat
```
预期：`Build OK: PasswordGenerator.exe`。双击运行：默认长度 16、spin 可到 256、生成/复制正常。

---

### Task 2: 应用图标 — 生成 app.ico 并嵌入

**Files:**
- Create: `tools/make_icon.py`
- Create: `app.ico`（构建产物，入库）
- Modify: `app.rc`（加 ICON 资源）
- Modify: `src/password_generator.c`（窗口类加载图标）

**Interfaces:**
- Consumes: 无
- Produces: 资源 ID `1` 的 ICON（`LoadIconW(g_hInst, MAKEINTRESOURCEW(1))`，Task 3/4 沿用）

- [ ] **Step 1: 确认 Pillow 可用（缺则装到用户目录）**

```bash
python -c "import PIL; print(PIL.__version__)" || python -m pip install --user pillow
```
预期：打印版本号（如 `10.4.0`）。

- [ ] **Step 2: 写图标生成脚本**

创建 `tools/make_icon.py`（完整文件）：

```python
# -*- coding: utf-8 -*-
"""
生成 app.ico：深蓝->青紫渐变圆角方块 + 白色粗钥匙。
用法：python tools/make_icon.py   （在项目根目录运行，输出 app.ico）
依赖：Pillow
"""
from PIL import Image, ImageDraw

C1 = (28, 40, 160)     # 深蓝 #1C28A0
C2 = (110, 60, 220)    # 青紫 #6E3CDC
MASTER = 256           # 母版尺寸，小尺寸由它缩放
SIZES = [16, 24, 32, 48, 64, 128, 256]


def make_master() -> Image.Image:
    s = MASTER
    # 垂直渐变
    grad = Image.new("RGBA", (s, s))
    d = ImageDraw.Draw(grad)
    for y in range(s):
        t = y / (s - 1)
        col = tuple(int(C1[i] + (C2[i] - C1[i]) * t) for i in range(3)) + (255,)
        d.line([(0, y), (s, y)], fill=col)
    # 圆角遮罩
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1],
                                           radius=int(s * 0.22), fill=255)
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    img.paste(grad, (0, 0), mask)

    # 白色钥匙：圆环头 + 竖杆 + 两齿
    d = ImageDraw.Draw(img)
    w = max(3, int(s * 0.075))            # 笔画粗细
    cx, r = int(s * 0.46), int(s * 0.17)  # 圆心与半径（偏上偏左）
    cy = int(s * 0.34)
    d.ellipse([cx - r, cy - r, cx + r, cy + r],
              outline=(255, 255, 255, 255), width=w)
    top = cy + r - w // 2
    bottom = int(s * 0.80)
    d.rounded_rectangle([cx - w // 2, top, cx + w // 2, bottom],
                        radius=w // 2, fill=(255, 255, 255, 255))
    d.rectangle([cx + w // 2, top + int(s * 0.12),
                 cx + int(s * 0.30), top + int(s * 0.12) + w],
                fill=(255, 255, 255, 255))
    d.rectangle([cx + w // 2, top + int(s * 0.26),
                 cx + int(s * 0.26), top + int(s * 0.26) + w],
                fill=(255, 255, 255, 255))
    return img


def main():
    master = make_master()
    frames = [master.resize((sz, sz), Image.LANCZOS) for sz in SIZES[:-1]]
    frames.append(master)  # 256 用原生
    master.save("app.ico", format="ICO",
                append_images=frames[:-1],
                sizes=[(sz, sz) for sz in SIZES])
    print("app.ico written:", SIZES)


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: 生成并检查 app.ico**

```bash
python tools/make_icon.py && ls -la app.ico
```
预期：打印 `app.ico written: [16, 24, 32, 48, 64, 128, 256]`；`app.ico` 存在且 > 10 KB。

- [ ] **Step 4: 嵌入资源并加载**

`app.rc` 全文改为：

```rc
// 资源文件：嵌入应用清单（视觉样式 + 权限声明）与应用图标
// 1 = CREATEPROCESS_MANIFEST_RESOURCE_ID, 24 = RT_MANIFEST
1 24 "app.manifest"
// 应用图标（资源 ID 1；exe 文件图标与窗口类图标共用）
1 ICON "app.ico"
```

`src/password_generator.c` 中 `RegisterClassExW` 之前的两行：

```c
    wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
```
改为：

```c
    wc.hIcon         = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm       = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
```

- [ ] **Step 5: 构建、验证图标已嵌入**

```bash
cmd //c build.bat
```
预期：`Build OK`。提取 exe 图标存为 PNG 以便查看：

```bash
powershell -NoProfile -Command "Add-Type -AssemblyName System.Drawing; [System.Drawing.Icon]::ExtractAssociatedIcon('D:\data\qx\claude\crt-cipher\PasswordGenerator.exe').ToBitmap().Save('D:\data\qx\claude\crt-cipher\tools\icon_check.png')"
```
预期：生成 `tools/icon_check.png`（32×32），用 Read 工具查看应显示渐变底+白钥匙。再启动 `PasswordGenerator.exe`，标题栏左上角与任务栏应显示同款图标（若资源管理器仍显示旧图标为图标缓存延迟，刷新/重启 explorer 即可，不算失败）。

---

### Task 3: 可缩放窗口 + 绝对布局 + DPI 感知

**Files:**
- Modify: `src/password_generator.c`（窗口样式、WM_CREATE/WM_SIZE/WM_GETMINMAXINFO/WM_DPICHANGED、布局函数、字体缩放）
- Modify: `app.manifest`（PerMonitorV2）
- Create: `tools/screenshot.ps1`（验证用截图脚本）

**Interfaces:**
- Consumes: Task 1 的常量；Task 2 的图标加载（本任务不改动那两行）
- Produces（Task 4 依赖）:
  - `static int S(int v);` — 设计像素 → 实际像素（× g_scale）
  - `static void layout_controls(HWND hwnd);` — 全量重排所有控件（Task 4 扩充其内容）
  - `static void create_fonts(void);` / `static void apply_fonts(void);` — 重建/重发字体
  - 全局 `static double g_scale;`、控件全局句柄（含 `g_hLblLen`、`g_hLblRange`）

- [ ] **Step 1: manifest 声明 DPI 感知**

`app.manifest` 在 `</trustInfo>` 之后、`</assembly>` 之前插入：

```xml
  <!-- PerMonitorV2 DPI 感知：高分屏与跨屏拖动不模糊 -->
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true</dpiAware>
    </windowsSettings>
  </application>
```

- [ ] **Step 2: 改窗口样式与默认尺寸**

`wWinMain` 中窗口创建改为（样式常量、CW_USEDEFAULT 尺寸 + 事后按 DPI 调整）：

```c
    const wchar_t *cls = L"CrtCipherPwdGen";
    WNDCLASSEXW wc = { sizeof wc, 0 };
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = cls;
    wc.hIcon         = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm       = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);

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
```

参数区（Task 1 的常量后）追加：

```c
/* ---- 窗口（设计像素，96dpi 基准） ---- */
#define CLIENT_W     640
#define CLIENT_H     440
#define MIN_CLIENT_W 560
#define MIN_CLIENT_H 360
```

- [ ] **Step 3: 缩放助手 + 字体重建**

UI 区顶部（`create_child` 之前）加：

```c
static double g_scale = 1.0;   /* 96dpi 基准的 DPI 缩放系数 */

/* 设计像素 → 实际像素 */
static int S(int v) { return (int)(v * g_scale + 0.5); }
```

`create_controls` 里现有一段 `g_hFontUI = CreateFontW(15,...) ... g_hFontMono = CreateFontW(17,...)` 删除，替换为独立函数（放在 UI 区顶部）：

```c
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
```

（负高度 = 按字符高缩放，DPI 更准。）

- [ ] **Step 4: 全局句柄扩充 + 布局函数**

控件全局区改为（新增标签句柄）：

```c
/* ---- 全局控件 ---- */
static HINSTANCE g_hInst;
static HWND g_hLblLen, g_hLenEdit, g_hLenSpin, g_hLblRange;
static HWND g_hChkDigits, g_hChkLower, g_hChkUpper, g_hChkSpecial, g_hChkNoAmbig;
static HWND g_hPwdEdit, g_hBtnGen, g_hBtnCopy;
static HFONT g_hFontUI, g_hFontMono;
```

UI 区加布局函数（Task 4 将扩充中间段）：

```c
/* 依据当前客户区尺寸全量重排控件（自底向上：先定底行，再给密码框剩余空间） */
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
    MoveWindow(g_hLblRange, m + S(164), y + S(5), S(90), S(20), TRUE);

    /* 行 2~3：字符集复选框 */
    y = S(64);
    MoveWindow(g_hChkDigits, m, y, S(180), S(22), TRUE);
    MoveWindow(g_hChkLower,  m + S(200), y, S(200), S(22), TRUE);
    y = S(92);
    MoveWindow(g_hChkUpper,   m, y, S(180), S(22), TRUE);
    MoveWindow(g_hChkSpecial, m + S(200), y, S(200), S(22), TRUE);
    y = S(120);
    MoveWindow(g_hChkNoAmbig, m, y, S(420), S(22), TRUE);

    /* 底部：按钮行（生成 + 复制，右下角） */
    int bh = S(34);
    int by = cy - m - bh;
    MoveWindow(g_hBtnCopy, cx - m - S(96), by, S(96), bh, TRUE);
    MoveWindow(g_hBtnGen,  cx - m - S(96) - S(10) - S(140), by, S(140), bh, TRUE);

    /* 中部：密码框 = 剩余全部空间（弹性区） */
    y = S(156);
    int pwdBottom = by - S(12);
    int pwdH = pwdBottom - y;
    if (pwdH < S(32)) pwdH = S(32);
    MoveWindow(g_hPwdEdit, m, y, cx - 2 * m, pwdH, TRUE);
}
```

- [ ] **Step 5: 重写 create_controls 与 wnd_proc 消息**

`create_controls` 全文替换为（创建全部现有控件，位置 0,0 占位，末尾统一布局）：

```c
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

    /* 密码显示 + 按钮 */
    g_hPwdEdit = create_child(L"Edit", L"",
                              WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 0,
                              0, 0, 0, 0, hwnd, IDC_PWD_EDIT, g_hFontMono);
    g_hBtnGen  = create_child(L"Button", L"生成密码", BS_PUSHBUTTON, 0,
                              0, 0, 0, 0, hwnd, IDC_BTN_GENERATE, g_hFontUI);
    g_hBtnCopy = create_child(L"Button", L"复制", BS_PUSHBUTTON, 0,
                              0, 0, 0, 0, hwnd, IDC_BTN_COPY, g_hFontUI);

    /* 默认勾选：数字 + 小写 + 大写 */
    SendMessageW(g_hChkDigits, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_hChkLower,  BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_hChkUpper,  BM_SETCHECK, BST_CHECKED, 0);

    layout_controls(hwnd);
}
```

`wnd_proc` 的 `WM_CREATE` 改为（先取 DPI 再建控件）：

```c
    case WM_CREATE: {
        HDC dc = GetDC(hwnd);
        g_scale = GetDeviceCaps(dc, LOGPIXELSX) / 96.0;
        ReleaseDC(hwnd, dc);
        create_controls(hwnd);
        return 0;
    }
```

`WM_COMMAND` 之后新增三个 case：

```c
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
```

`apply_fonts`（UI 区，`create_fonts` 之后）：

```c
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
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (*map[i].ph)
            SendMessageW(*map[i].ph, WM_SETFONT, (WPARAM)*map[i].pf, TRUE);
}
```

注意：`layout_controls` 引用 `g_hLblLen` 等句柄需在函数之前声明（全局区已含）；`apply_fonts` 需在 `wnd_proc` 之前定义。

- [ ] **Step 6: 构建并验证逻辑测试不回归**

```bash
cmd //c build.bat && gcc -O2 -Wall -Wextra tests/test_logic.c -o tests/test_logic.exe -lbcrypt && ./tests/test_logic.exe
```
预期：`Build OK` + `ALL TESTS PASSED`。

- [ ] **Step 7: 截图验证缩放布局**

创建 `tools/screenshot.ps1`（按窗口类查找，避免中文标题编码问题；完整文件）：

```powershell
param([int]$W = 0, [int]$H = 0, [string]$Out = "shot.png")
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  public static extern IntPtr FindWindowW(string cls, string title);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
}
"@
$h = [Win32]::FindWindowW("CrtCipherPwdGen", $null)
if ($h -eq [IntPtr]::Zero) { throw "window not found: run PasswordGenerator.exe first" }
if ($W -gt 0 -and $H -gt 0) {
  [Win32]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, $W, $H, 0x0004) | Out-Null
  Start-Sleep -Milliseconds 400
}
$r = New-Object Win32+RECT
[Win32]::GetClientRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[Win32]::PrintWindow($h, $dc, 2) | Out-Null   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($dc); $g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "saved $Out (${w}x${ht})"
```

运行验证（启动 → 默认尺寸截图 → 拉大截图 → 缩到最小截图）：

```bash
./PasswordGenerator.exe & sleep 1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/screenshot.ps1 -Out tools/shot_default.png
powershell -NoProfile -ExecutionPolicy Bypass -File tools/screenshot.ps1 -W 900 -H 700 -Out tools/shot_big.png
powershell -NoProfile -ExecutionPolicy Bypass -File tools/screenshot.ps1 -W 576 -H 399 -Out tools/shot_min.png
```
预期：三张 PNG 生成。用 Read 查看：默认布局整齐；大窗口下密码框随宽高变大、按钮仍在右下；接近最小尺寸（窗口 576×399 ≈ 客户区 560×360）时无控件重叠。任务完成后关闭 exe 进程。

---

### Task 4: 新控件与交互 — 档位 / 多行显示 / 强度条 / 自动重生成

**Files:**
- Modify: `src/password_generator.c`（控件 ID、强度条窗口类、create_controls、layout_controls、on_generate、WM_COMMAND）

**Interfaces:**
- Consumes: Task 1 的 `CharsetInfo` / `estimate_entropy_bits`；Task 3 的 `S()` / `layout_controls` / `apply_fonts`
- Produces: 最终版 UI（Task 5 只改文档）

- [ ] **Step 1: 新增控件 ID 与全局**

ID 区追加：

```c
#define IDC_BTN_P8      1011
#define IDC_BTN_P16     1012
#define IDC_BTN_P32     1013
#define IDC_BTN_P64     1014
#define IDC_BTN_P128    1015
#define IDC_BTN_P256    1016
#define IDC_BAR         1017
#define IDC_LBL_ENTROPY 1018
```

控件全局区追加（并入 Task 3 的全局区，`apply_fonts` 的 map 数组同步加后面 Step 6 列出的项）：

```c
static HWND g_hPresets[6];
static HWND g_hBar, g_hLblEntropy;
static BOOL g_ready = FALSE;                 /* 控件初始化完成前不自动生成 */
static const int g_presetLens[6] = { 8, 16, 32, 64, 128, 256 };
```

- [ ] **Step 2: 强度条窗口类**

UI 区加（放在 `create_child` 之前）：

```c
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
        HBRUSH bg = CreateSolidBrush(RGB(233, 233, 233));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        if (frac > 0) {
            RECT fill = rc;
            fill.right = rc.left + (int)((rc.right - rc.left) * frac);
            HBRUSH fg = CreateSolidBrush(tier_color(bits));
            FillRect(dc, &fill, fg);
            DeleteObject(fg);
        }
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
    WNDCLASSEXW bc = { sizeof bc, 0 };
    bc.lpfnWndProc   = bar_wndproc;
    bc.hInstance     = g_hInst;
    bc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    bc.lpszClassName = L"CrtStrengthBar";
    RegisterClassExW(&bc);
}
```

`wWinMain` 中主窗口 `RegisterClassExW(&wc);` 之后加一行：

```c
    register_bar_class();
```

- [ ] **Step 3: create_controls 增补控件**

在 Task 3 版 `create_controls` 中，`/* 密码显示 + 按钮 */` 段替换为：

```c
    /* 快捷档位按钮 */
    wchar_t txt[8];
    for (int i = 0; i < 6; i++) {
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
    g_hBtnGen  = create_child(L"Button", L"生成密码", BS_PUSHBUTTON, 0,
                              0, 0, 0, 0, hwnd, IDC_BTN_GENERATE, g_hFontUI);
    g_hBtnCopy = create_child(L"Button", L"复制", BS_PUSHBUTTON, 0,
                              0, 0, 0, 0, hwnd, IDC_BTN_COPY, g_hFontUI);
```

默认勾选三行之后、`layout_controls(hwnd);` 之前加：

```c
    g_ready = TRUE;
```

注意：多行 Edit 不用 `ES_AUTOHSCROLL`（启用软换行），保留 `WS_BORDER`。

- [ ] **Step 4: layout_controls 插入新行**

Task 3 版 `layout_controls` 中，`/* 行 1：长度 */` 段之后插入档位行，密码框起始 y 由 `S(156)` 改 `S(188)`，并在密码框与底行之间插入强度条/熵值两行。改后的完整函数：

```c
static void layout_controls(HWND hwnd)
{
    if (!g_hPwdEdit) return;
    RECT rc; GetClientRect(hwnd, &rc);
    int cx = rc.right, cy = rc.bottom;
    int m = S(20);

    /* 行 1：长度 */
    int y = S(18);
    MoveWindow(g_hLblLen,   m, y + S(3), S(84), S(20), TRUE);
    MoveWindow(g_hLenEdit,  m + S(90), y, S(64), S(26), TRUE);
    MoveWindow(g_hLblRange, m + S(164), y + S(5), S(90), S(20), TRUE);

    /* 行 2：快捷档位（窗口够宽时拉开间距，封顶 S(24)） */
    y = S(54);
    int pw = S(56), ph = S(26), gap = S(8);
    if (cx - 2 * m > 6 * pw + 5 * gap) {
        gap = (cx - 2 * m - 6 * pw) / 5;
        if (gap > S(24)) gap = S(24);
    }
    for (int i = 0; i < 6; i++)
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

    /* 底部：按钮行（生成 + 复制，右下角） */
    int bh = S(34);
    int by = cy - m - bh;
    MoveWindow(g_hBtnCopy, cx - m - S(96), by, S(96), bh, TRUE);
    MoveWindow(g_hBtnGen,  cx - m - S(96) - S(10) - S(140), by, S(140), bh, TRUE);

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
```

- [ ] **Step 5: on_generate 更新强度 + 不回写输入**

Task 1 版 `on_generate` 中，删除回写行 `SendMessageW(g_hLenSpin, UDM_SETPOS32, 0, length);`（打字过程中不覆盖输入，失焦时统一修正），函数末尾 `SetWindowTextW(g_hPwdEdit, wpwd);` 之后追加：

```c
    /* 更新强度条与熵值文本 */
    double bits = estimate_entropy_bits(ci.poolLen, length);
    SendMessageW(g_hBar, BAR_MSG_SETBITS, (WPARAM)(int)(bits * 10), 0);
    wchar_t ebuf[64];
    swprintf(ebuf, 64, L"熵 ≈ %.1f bit · %ls", bits, tier_name(bits));
    SetWindowTextW(g_hLblEntropy, ebuf);
```

（`%ls`：MinGW C99 与 MS CRT 均按宽字符串解释，跨运行时安全。）

- [ ] **Step 6: WM_COMMAND 自动重生成 + 档位 + 失焦修正**

`wnd_proc` 的 `WM_COMMAND` 整体替换为：

```c
    case WM_COMMAND: {
        int id   = LOWORD(wp);
        int code = HIWORD(wp);
        if (code == BN_CLICKED) {
            if (id == IDC_BTN_GENERATE) { on_generate(hwnd); return 0; }
            if (id == IDC_BTN_COPY)     { on_copy(hwnd);     return 0; }
            if (id >= IDC_BTN_P8 && id <= IDC_BTN_P256) {
                /* 档位：设置长度 → 触发 EN_CHANGE 自动重生成 */
                SendMessageW(g_hLenSpin, UDM_SETPOS32, 0,
                             g_presetLens[id - IDC_BTN_P8]);
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
```

`apply_fonts` 的 `map[]` 数组末尾追加三项（并加预设按钮循环）：

```c
        { &g_hLblEntropy, &g_hFontUI },
```

数组初始化器之后、for 循环之前加：

```c
    for (int i = 0; i < 6; i++)
        if (g_hPresets[i])
            SendMessageW(g_hPresets[i], WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
```

（`g_hBar` 无文本，不需字体。）

- [ ] **Step 7: 构建 + 回归 + 截图验证**

```bash
cmd //c build.bat && gcc -O2 -Wall -Wextra tests/test_logic.c -o tests/test_logic.exe -lbcrypt && ./tests/test_logic.exe
./PasswordGenerator.exe & sleep 1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/screenshot.ps1 -Out tools/shot_t4_default.png
powershell -NoProfile -ExecutionPolicy Bypass -File tools/screenshot.ps1 -W 900 -H 700 -Out tools/shot_t4_big.png
```
用 Read 查看截图，检查：档位 6 按钮整齐一行；强度条与熵值文本可见（启动即有密码 → 有颜色）；密码 16 位单行显示；大窗口下密码框明显变大。验证后关闭 exe。

---

### Task 5: 文档更新 + 完整手动验收

**Files:**
- Modify: `README.md`
- Modify: `src/password_generator.c`（仅文件头注释同步）

**Interfaces:**
- Consumes: 前四个任务的全部成果
- Produces: 最终交付

- [ ] **Step 1: 更新源码头注释**

`src/password_generator.c` 头部功能列表改为：

```c
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
```

- [ ] **Step 2: 重写 README**

`README.md` 全文替换为：

```markdown
# 随机密码生成器 (CrtCipher)

纯 C + Win32 原生 API 编写的 Windows（Win10+）可视化随机密码生成器。编译后得到一个双击即用的 `.exe`，无任何外部依赖。

## 功能

- 密码长度 **4–256**，默认 **16**；数值框 / 箭头调节 + **8/16/32/64/128/256 快捷档位**
- 默认字符集：**数字 + 小写字母 + 大写字母**；可选**特殊字符** `!@#$%^&*()-_=+[]{};:,.<>?/`
- 可选**排除易混淆字符**（剔除 `0 O o 1 l I`）
- **每种勾选字符至少包含一个**：先每类保底抽 1 个，其余全池均匀抽取，再 CSPRNG Fisher–Yates 洗牌
- **强度显示**：熵（bit）实时估算 + 分档强度条（<40 弱 / <60 中 / <128 强 / ≥128 极强）
- 长度或任一选项变化**自动重新生成**；**一键复制**到剪贴板
- 长密码**多行自动换行**显示 + 垂直滚动，256 字符完整可读
- 窗口**可拖边缩放 / 最大化**（最小 560×360），PerMonitorV2 DPI 感知，高分屏不模糊
- 自绘应用图标（标题栏 / 任务栏 / 资源管理器一致）
- 使用系统级密码学随机数 **BCryptGenRandom**（CSPRNG），拒绝采样消除模偏差，非 `rand()`

## 文件说明

| 文件 | 说明 |
|------|------|
| `src/password_generator.c` | 全部源码（单文件） |
| `app.manifest` / `app.rc` / `app.ico` | 清单（视觉样式 + DPI 感知）、资源、应用图标 |
| `tools/make_icon.py` | 重新生成 `app.ico` 的脚本（Pillow，仅开发用） |
| `tools/screenshot.ps1` | 开发辅助：给运行中的窗口截图（布局验证） |
| `tests/test_logic.c` | 生成逻辑单元测试（控制台程序） |
| `build.bat` | 一键构建脚本 |
| `tools/fetch_compiler.sh` | 获取便携版 MinGW 编译器 |
| `D:\soft\tools\mingw64` | 便携版编译器实际所在（已迁出项目，经 `MINGW64_HOME` 用户环境变量指向） |
| `PasswordGenerator.exe` | 构建产物，双击即用 |

## 构建

1. **获取编译器**（只需一次）：在 Git Bash 中，项目根目录运行
   ```bash
   bash tools/fetch_compiler.sh
   ```

2. **构建**：双击 `build.bat`，或在命令行运行 `build.bat`，生成 `PasswordGenerator.exe`。

### 工具链位置

便携工具链已迁至 `D:\soft\tools\mingw64`（用户级环境变量 `MINGW64_HOME` 已指向它，`bin` 目录也已加入用户 PATH）。`build.bat` 会优先读 `MINGW64_HOME`，未设置时回退到项目内 `tools\mingw64`。目录内的 `default-manifest.o.off` 改名随目录走、保持有效。

## 测试

逻辑层单元测试（字符集构建 / 保底与洗牌 / 熵计算）：

```bash
gcc -O2 -Wall -Wextra tests/test_logic.c -o tests/test_logic.exe -lbcrypt && ./tests/test_logic.exe
```

预期输出 `ALL TESTS PASSED`。

## 使用

双击 `PasswordGenerator.exe`。设置长度（数值框 / 箭头 / 档位按钮）与字符组成，任意改动即时重新生成；点「复制」复制到剪贴板；窗口可自由拖边缩放。

## 等价编译命令

`build.bat` 本质执行：

```bat
windres -I . app.rc -O coff -o rsrc.o
gcc -O2 -municode -mwindows -c src\password_generator.c -o password_generator.o
gcc -O2 -municode -mwindows -o PasswordGenerator.exe password_generator.o rsrc.o -lbcrypt -lcomctl32
```

- `-mwindows`：GUI 子系统，双击不弹控制台黑框
- `-municode`：使用 `wWinMain` 入口，正确处理中文界面
- `-lbcrypt`：链接 `BCryptGenRandom`
- `-lcomctl32`：链接 updown 等通用控件
```

- [ ] **Step 3: 最终构建 + 完整手动验收**

```bash
cmd //c build.bat && gcc -O2 -Wall -Wextra tests/test_logic.c -o tests/test_logic.exe -lbcrypt && ./tests/test_logic.exe
```

启动 `PasswordGenerator.exe`，按设计规格第 7 节清单逐项验收：

1. 长度设 256 生成：多行换行显示、滚动可看全；复制到记事本粘贴为 256 字符。
2. 勾选全部 4 类（含排除易混淆）多次生成：每类至少 1 个字符。
3. 拖边缩放 / 最大化 / 还原 / 缩到最小：布局不重叠不残留。
4. 档位按钮：点击后长度正确且立即重新生成；spin/直接输入/复选框变化即时重生成，强度条与熵值同步。
5. 构造四种强度：长度 4 仅数字（≈13.3 bit 弱）、长度 6 三类（≈34 中附近按实际验证分档边界：用长度 8 仅数字 ≈26.6 弱 / 长度 11 三类 ≈63.9 强 / 长度 16 三类 ≈93.2 强 / 长度 32 四类 ≈210 极强），颜色与文字对应。
6. 图标：资源管理器 .exe、标题栏、任务栏、Alt-Tab 四处一致。
7. 系统缩放 150%/200% 的机器上文字清晰（如有条件；跨不同 DPI 显示器拖动不乱版）。
8. 全部取消勾选 → 弹窗提示，不崩溃。

预期：全部通过。截图留证（`tools/shot_final_*.png`，可用 screenshot.ps1）。

---

## Self-Review 记录

- 规格覆盖：256 长度/默认 16（Task 1）、档位（Task 4）、保底+洗牌（Task 1）、熵+强度条（Task 1/4）、可缩放+最小尺寸+默认 640×440（Task 3）、DPI（Task 3）、图标三处（Task 2）、自动重生成（Task 4）、多行显示（Task 4）、README（Task 5）、测试清单（Task 5）✓
- 类型一致性：`CharsetInfo` / `build_charset` / `generate_password` / `estimate_entropy_bits` / `S()` / `layout_controls` / `apply_fonts` / `BAR_MSG_SETBITS` 各任务签名一致 ✓
- 无占位符：所有步骤含完整代码与确切命令 ✓


