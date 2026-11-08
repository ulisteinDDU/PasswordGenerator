# 随机密码生成器 (CrtCipher)

纯 C + Win32 原生 API 编写的 Windows（Win10+）可视化随机密码生成器。编译后得到一个双击即用的 `.exe`，无任何外部依赖。

## 功能

- 密码长度 **4–256**，默认 **16**；数值框 / 箭头调节 + **8/16/20/24/32/64/128/256 快捷档位**
- 默认字符集：**数字 + 小写字母 + 大写字母**；可选**特殊字符** `!@#$%^&*()-_=+[]{};:,.<>?/`
- 可选**排除易混淆字符**（剔除 `0 O o 1 l I`）
- **每种勾选字符至少包含一个**：先每类保底抽 1 个，其余全池均匀抽取，再 CSPRNG Fisher–Yates 洗牌
- **强度显示**：熵（bit）实时估算 + 分档强度条（<40 弱 / <60 中 / <128 强 / ≥128 极强）
- 长度或任一选项变化**自动重新生成**；**一键复制**到剪贴板
- **批量生成**：弹窗输入数量（1–10000）与保存路径（默认 `文档\passwords_日期_时间.txt`，可浏览选择），按主界面当前设置批量生成并以**每行一个密码**写入 txt
- 长密码**多行自动换行**显示 + 垂直滚动，256 字符完整可读
- 窗口**可拖边缩放 / 最大化**（最小客户区 560×360），PerMonitorV2 DPI 感知，高分屏不模糊；**启动时居中**于所在显示器工作区
- 自绘应用图标（标题栏 / 任务栏 / 资源管理器一致）
- 使用系统级密码学随机数 **BCryptGenRandom**（CSPRNG），拒绝采样消除模偏差，非 `rand()`

## 文件说明

| 文件 | 说明 |
|------|------|
| `src/password_generator.c` | 全部源码（单文件） |
| `app.manifest` / `app.rc` / `app.ico` | 清单（视觉样式 + DPI 感知）、资源、应用图标 |
| `tools/make_icon.py` | 重新生成 `app.ico` 的脚本（Pillow，仅开发用） |
| `tools/screenshot.ps1` | 开发辅助：给运行中的窗口截图（布局验证） |
| `tools/acceptance.ps1` | 开发辅助：UI 自动验收驱动（档位 256/剪贴板/自动关闭） |
| `tools/click_preset.ps1` 等 | 开发辅助：档位点击/真实屏幕截取/强度条像素分析/批量弹窗验收驱动 |
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

双击 `PasswordGenerator.exe`。设置长度（数值框 / 箭头 / 档位按钮）与字符组成，任意改动即时重新生成；点「复制」复制到剪贴板；点「批量生成」输入数量与保存路径，按当前设置批量导出 txt（每行一个密码）；窗口可自由拖边缩放。

## 等价编译命令

`build.bat` 本质执行：

```bat
windres -I . app.rc -O coff -o rsrc.o
gcc -O2 -municode -mwindows -c src\password_generator.c -o password_generator.o
gcc -O2 -municode -mwindows -o PasswordGenerator.exe password_generator.o rsrc.o -lbcrypt -lcomctl32 -lcomdlg32 -lshell32
```

- `-mwindows`：GUI 子系统，双击不弹控制台黑框
- `-municode`：使用 `wWinMain` 入口，正确处理中文界面
- `-lbcrypt`：链接 `BCryptGenRandom`
- `-lcomctl32`：链接 updown 等通用控件
- `-lcomdlg32`：链接「浏览」保存文件对话框
- `-lshell32`：链接「文档」目录获取
