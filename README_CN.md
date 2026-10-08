# ELDEN RING Overlay Hook

一个艾尔登法环的 Windows DLL 覆盖层 Mod，通过钩入 DirectX 12 渲染管线来渲染基于 ImGui 的覆盖层界面。采用模块化插件架构——核心加载器（`EROverlay.dll`）在运行时自动发现并加载 `overlays/` 文件夹中的各个覆盖层 DLL。

**内置覆盖层：**
- **Boss** —— 追踪 Boss 击杀数，支持挑战模式和自定义显示格式
- **Achievements** —— 实时追踪 Steam 成就进度
- **Minimap** —— 渲染游戏内小地图，支持自定义大小、缩放和形状

> 📋 更新日志：[加载器](src/CHANGELOG_CN.md) · [Boss](src/boss/CHANGELOG_CN.md) · [Achievements](src/achievements/CHANGELOG_CN.md) · [Minimap](src/minimap/CHANGELOG_CN.md) · 🌐 [English](README.md)

---

## ⚠️ 兼容性说明

**请勿**将本 Mod 与 Nvidia GeForce Experience 中的 `FPS Counter`、`MSI Afterburner` 或 `RivaTuner Statistics Server` 等覆盖层同时使用。它们会钩入相同的 DirectX API，导致游戏崩溃。

---

## 安装方法

1. 按需修改 `configs/` 文件夹中的 `.ini` 配置文件（详见下方[配置说明](#配置说明)）。
2. 将 `EROverlay.dll` 注入艾尔登法环，可选以下任一方式：

   | 方式 | 操作说明 |
   |---|---|
   | **代理 DLL**（推荐） | 将 `EROverlay.dll` 改名为 `winhttp.dll`，放到 `eldenring.exe` 所在目录，同时将 `configs/` 和 `data/` 文件夹也复制到该目录。 |
   | **Mod 加载器** | 通过 [EldenModLoader](https://www.nexusmods.com/eldenring/mods/117)、[ModEngine2](https://github.com/soulsmods/ModEngine2) 或 [me3](https://github.com/garyttierney/me3) 加载 `EROverlay.dll`。 |
   | **注入器** | 运行附带的 `injector.exe`。（不推荐——不稳定，且可能被安全软件拦截。） |

3. 等待数秒让 Mod 完成加载，按 `=` 键切换迷你/完整模式。

加载器保持向后兼容性——只需确保覆盖层加载器版本满足对应覆盖层模块的最低要求即可。

---

## 配置说明

加载器读取 `configs/` 中的 INI 文件。修改后使用 UTF-8 保存，重启游戏生效；注释单独成行。

`configs/` 使用英文注释，`configs_CN/` 提供同名的中文注释模板，键名和默认值一致。使用中文说明时，先备份已有的自定义配置，再将对应模板复制到 `configs/`，重新填写需要的设置。只修改 `configs_CN/` 不会影响 Mod；注释语言不决定游戏或数据语言。

### `common.ini` —— 全局设置

| 配置项 | 默认值 | 说明 |
|---|---|---|
| `console` | `false` | 启用调试控制台输出。 |
| `font` | （空） | `data/` 文件夹中的字体文件路径（也可以是绝对路径）。留空则使用内置拉丁字体，其他语言回退到系统字体。 |
| `font_size` | `20` | 字体大小，单位为像素。 |
| `charset` | （空） | 字体加载使用的字符集（`enUS`、`jaJP`、`koKR`、`zhCN`、`ruRU` 等）。留空则根据游戏语言自动检测。 |
| `language` | （空） | 加载数据文件使用的语言。留空则使用游戏语言。 |

### `boss.ini` —— Boss 覆盖层

| 配置项 | 默认值 | 说明 |
|---|---|---|
| `toggle_full_mode` | `=` | 切换完整/迷你模式的快捷键。 |
| `data_file` | `bosses.json` | Boss 数据文件名，位于 `data/<language>/` 目录下。 |
| `allow_revive` | `false` | 允许复活已击败的 Boss。 |
| `panel_pos` | `-10,10,15%,90%` | 面板位置和大小：`x, y, 宽度, 高度`。值可以是像素或百分比；负数 x/y 表示相对于屏幕右侧/底部的偏移。 |
| `boss_kill_text` | `{kills}/{total}` | Boss 击杀数的显示格式。支持 `{kills}`、`{total}`、`{deaths}`、`{igt}`、`$n`（换行）。 |
| `challenge_mode` | `false` | 记录未超过死亡上限时的个人最佳成绩；超限后不再提高最佳成绩，当前击杀数仍更新。 |
| `challenge_death_count` | `0` | 挑战模式允许的最大死亡次数。 |
| `challenge_status_text` | `PB: {pb}/{total}  Tries: {tries}$nCurrent: {kills}/{total}` | 挑战模式状态的显示格式。支持 `{kills}`、`{total}`、`{deaths}`、`{igt}`、`{pb}`、`{tries}`、`$n`。 |

复活会同时恢复数据文件中的场地与阶段 flag；复活后需传送或重新加载场地。拉塔恩/红狮子城和帕奇涉及共用场地或支线状态，详见[复活说明与逆向证据](docs/boss-revival.md)。

### `achievements.ini` —— 成就覆盖层

| 配置项 | 默认值 | 说明 |
|---|---|---|
| `max_achievements` | `20` | 未完成成就条目的数量上限；新解锁提示仍可能额外显示。 |
| `panel_pos` | `0,12%,0,60%` | `x, y, 最大宽度, 最大高度`；0 表示不限制该方向尺寸，位置锚点规则同 `boss.ini`。 |

### `minimap.ini` —— 小地图覆盖层

新配置按快捷键、底图、标记和边框分区，每个 `[preset.名称]` 集中填写一套显示预设。用新版附带的 [minimap.ini](configs/minimap.ini) 替换旧文件；旧平铺键名和多行逗号列表不再支持。使用 UTF-8 保存，修改后重启游戏。

| 分区 | 调整内容 |
|---|---|
| `[controls]` | 显示/切换默认 M，赐福/地标默认 N；留空禁用快捷键。 |
| `[map]` | `full_map = true` 显示全开底图，不修改存档。 |
| `[markers]` | `graces`、`landmarks`、`death`、`beacons` 默认 `true`。 |
| `[border]` | RGBA 颜色 `color` 和直接像素宽度 `width`，默认 1.5 像素。 |
| `[presets]` | `order = compact, large` 决定启动预设与切换顺序。 |
| `[preset.名称]` | 尺寸、居中或四向边距定位、缩放、透明度、旋转、形状、圆角与四类元素倍率，每项一个值。 |
| `[diagnostics]` | 可选 `log_file`，排查资源和地图状态。 |

默认按 M 循环「小地图 → 居中大地图 → 隐藏」。`zoom` 越大，地图细节越大，同一窗口看到的范围越小。[完整小地图配置说明](src/minimap/README_CN.md)包含全部字段、默认值和新增预设示例。

### `input.ini` —— 全局快捷键

| 配置项 | 默认值 | 说明 |
|---|---|---|
| `unload` | （空） | 卸载 Mod 的快捷键。 |

按键名称支持用 `+` 组合修饰键（例如 `CTRL+SHIFT+[`）。完整按键名称列表请参见 `input.ini` 文件中的注释。

### `style.ini` —— 视觉样式

自定义所有覆盖层面板使用的 ImGui 颜色主题。所有颜色值格式为 `R,G,B,A`（各值 0–255）。

| 配置项 | 说明 |
|---|---|
| `text_color` | 文字颜色 |
| `check_mark_color` | 复选框勾选标记颜色 |
| `bg_color` | 面板背景颜色 |
| `border_color` | 面板边框颜色 |
| `button_color` / `button_hover_color` / `button_press_color` | 按钮各状态颜色 |
| `node_color` / `node_hover_color` / `node_press_color` | 树节点各状态颜色 |
| `scroll_bg_color` / `scroll_color` / `scroll_hover_color` / `scroll_press_color` | 滚动条各状态颜色 |
| `border_width` | 边框宽度（像素） |
| `rounding` | 窗口圆角半径 |

---

## 开发文档

- [从源码编译](docs/building.md)（英文）
- [编写覆盖层插件](docs/writing-plugins.md)（英文）

---

## 许可证

[MIT 许可证](LICENSE)

---

## 鸣谢

- 基于 [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL) 修改
- [minhook](https://github.com/TsudaKageyu/minhook)
- [imgui](https://github.com/ocornut/imgui)
- [stb](https://github.com/nothings/stb)
- [JSON for Modern C++](https://github.com/nlohmann/json)
- [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
- [Open Sans](https://fonts.google.com/specimen/Open+Sans)
