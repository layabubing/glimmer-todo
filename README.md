# Glimmer Todo · 浮光待办

Windows 桌面悬浮 TODO 工具，使用 C++20 和 Windows 原生界面。支持 Windows 10 1703 及以上版本、Windows 11，要求 x64 系统。

## 下载

从本仓库的 Releases 页面下载 `GlimmerTodo-v1.0.0-windows-x64.zip`，解压后双击 `GlimmerTodo.exe` 运行。

默认窗口为 260 × 300，最小可调整至 220 × 160；尺寸使用 Windows 逻辑像素。界面包含紧凑标题、待办列表和新增输入框。标题旁的绿色圆点表示自动隐藏已开启。

## 使用

- 在底部输入待办，按 Enter 或点击加号保存。每条待办最多 500 个 UTF-16 字符。
- 点击左侧方框完成或恢复待办。双击待办文字或选中后按 F2 编辑；编辑时按 Esc 取消。
- 点击右侧删除按钮或选中后按 Delete 删除待办。右键菜单可以清理全部已完成待办。
- 拖动窗口顶部移动位置，拖动边缘调整大小。位置、尺寸和置顶选项自动保存。
- 点击“设置”，管理自动隐藏和置顶选项，勾选需要检测的软件。
- 在设置中输入进程文件名，例如 `notepad.exe`，点击“添加软件”；也可以通过“选择 EXE…”选择软件实际运行的主程序。
- 点击“收起”或关闭窗口可收起到系统托盘。双击托盘图标，或者再次运行 EXE，可以显示已有窗口。右键托盘图标可以退出。
- Ctrl+N 聚焦新增待办输入框。列表支持方向键、Space 完成切换、Enter 编辑和 Tab 切换控件。

自动隐藏按当前前台窗口所属的 EXE 文件名匹配，忽略英文大小写。已添加的软件进入前台时，待办、设置和程序选择窗口隐藏；软件进入后台、最小化或退出后恢复显示。恢复显示使用不激活窗口的方式，保留当前程序的键盘焦点。手动收起后保持收起状态，直到主动显示。

选择软件时，请选择实际运行程序；使用独立启动器的软件，需要添加运行后的程序文件名。相同文件名的软件共用检测规则。

## 内置游戏

| 游戏 | 进程文件名 |
| --- | --- |
| Counter-Strike 2 | `cs2.exe` |
| Counter-Strike: Global Offensive | `csgo.exe` |
| Counter-Strike 1.6 | `hl.exe` |
| Counter-Strike: Source | `hl2.exe` |
| 原神国服 | `YuanShen.exe` |
| 原神国际服 | `GenshinImpact.exe` |
| 崩坏：星穹铁道 | `StarRail.exe` |

CS 1.6 与 Half-Life 共用 `hl.exe`；CS: Source 与 Half-Life 2 共用 `hl2.exe`。每条规则可以单独停用或移除。

## 本地数据

待办和设置保存到 `%LOCALAPPDATA%\DesktopList\state.json`。每次修改立即写入文件，刷新到磁盘后原子替换正式文件。可以在退出应用后复制该文件备份。程序运行期间无需联网。

前台检测使用 Windows 前台切换事件通知，每秒检查一次当前状态。程序保持单个实例运行。

## 编译与测试

本项目附带 nlohmann/json 3.12.0 的单文件依赖与 MIT 许可。编译环境使用 w64devkit 2.11.0；安装脚本从官方发布地址下载，并检查 SHA256。环境保存在项目的 `.tools` 目录。

在项目目录的 PowerShell 中执行：

```powershell
.\setup.ps1
.\build.ps1
```

生成文件为 `dist\GlimmerTodo.exe`。可以通过 `./build.ps1 -Test` 运行数据逻辑检查。运行程序后执行下列命令，可以采样实际进程的内存、CPU 时间和句柄数量：

```powershell
.\tests\measure.ps1 -DurationSeconds 60
```

采样报告保存在 `.work\memory.json`。指标使用 MB，即 1,000,000 字节。采样期间工作集超过 50 MB 时，脚本报告测试失败。程序通过原生界面和较少的常驻对象控制内存占用。

## 许可

Copyright (c) 2026 layabubing。本项目采用 GNU GPLv3（`GPL-3.0-only`）许可，详见 `LICENSE`。第三方依赖及运行库许可见 `THIRD_PARTY_NOTICES.md` 和 `vendor` 目录。
