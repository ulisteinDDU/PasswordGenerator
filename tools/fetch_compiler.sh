#!/usr/bin/env bash
# 下载便携版 MinGW-w64 (WinLibs GCC) 到 tools/mingw64
# 不写入系统环境变量，用完可整体删除 tools/mingw64。
# 用法（在 Git Bash 中，项目根目录）：bash tools/fetch_compiler.sh
set -u
cd "$(dirname "$0")/.." || exit 1
mkdir -p tools

# 若 MINGW64_HOME 已指向可用编译器（如工具链已迁到别处），直接跳过下载
if [ -n "${MINGW64_HOME:-}" ] && [ -x "$MINGW64_HOME/bin/gcc.exe" ]; then
  echo "已配置 MINGW64_HOME=$MINGW64_HOME 且编译器可用，无需下载。"
  exit 0
fi

echo "[1/4] 请求 WinLibs 最新 release..."
API="https://api.github.com/repos/brechtsanders/winlibs_mingw/releases/latest"
if ! curl -sL --fail -H "Accept: application/vnd.github+json" "$API" -o tools/_latest.json; then
  echo "API 请求失败，请检查网络后重试。"; exit 1
fi

echo "[2/4] 选择 .zip 资源 (x86_64, UCRT, posix)..."
# 探测真正可用的 Python（Windows 上 python3 可能是 Microsoft Store 的静默 stub）
PYBIN=""
for c in python3 python; do
  if [ "$("$c" -c 'print(1)' 2>/dev/null)" = "1" ]; then PYBIN="$c"; break; fi
done

URL=""
if [ -n "$PYBIN" ]; then
  URL="$("$PYBIN" - <<'PY'
import json
d = json.load(open('tools/_latest.json'))
assets = d.get('assets', [])
def want(n, *keys):
    n = n.lower()
    return n.endswith('.zip') and 'x86_64' in n and all(k in n for k in keys)
for keys in (['posix','ucrt'], ['ucrt'], []):
    for a in assets:
        if want(a['name'], *keys) and 'minimal' not in a['name'].lower():
            print(a['browser_download_url']); raise SystemExit
print('')
PY
)"
fi

if [ -z "$URL" ]; then
  # grep 兜底（含 Python 不可用 / 解析失败的情况）
  URL="$(grep -o 'browser_download_url": "[^"]*\.zip"' tools/_latest.json \
        | grep -i 'x86_64' | grep -i 'ucrt' | grep -i 'posix' | grep -vi 'minimal' \
        | head -1 | sed 's/.*": "//; s/"$//')"
fi
if [ -z "$URL" ]; then echo "未找到合适的 .zip 资源。"; exit 1; fi
echo "      -> $URL"

echo "[3/4] 下载（约 260MB，请稍候；支持断点续传，失败可重跑）..."
# 断点续传 + 失败自动重试；--speed-limit 防止挂死在 0 速连接上
if ! curl -L --fail -C - --retry 5 --retry-all-errors \
        --connect-timeout 20 --speed-time 30 --speed-limit 1024 \
        -o tools/winlibs.zip "$URL"; then
  echo "下载失败。重跑本脚本可从断点继续。"; exit 1
fi

echo "[4/4] 解压到 tools/mingw64 ..."
if tar -xf tools/winlibs.zip -C tools/ 2>/dev/null; then
  echo "      (tar)"
else
  echo "      (PowerShell Expand-Archive)"
  if ! powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
        "Expand-Archive -Path 'tools\winlibs.zip' -DestinationPath 'tools' -Force"; then
    echo "解压失败。"; exit 1
  fi
fi
rm -f tools/winlibs.zip tools/_latest.json

# app.rc 已内嵌应用清单；禁用 GCC 默认清单对象，
# 否则链接时报 ".rsrc merge failure: multiple non-default manifests"
DEFMAN=tools/mingw64/x86_64-w64-mingw32/lib/default-manifest.o
if [ -f "$DEFMAN" ]; then mv "$DEFMAN" "$DEFMAN.off"; fi

GCC=tools/mingw64/bin/gcc.exe
if [ ! -x "$GCC" ]; then
  echo "未找到编译器：$GCC"
  ls -la tools/
  exit 1
fi
echo "OK: $("$GCC" --version | head -1)"
echo "完成。"
