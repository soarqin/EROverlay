# ELDEN RING 覆盖层加载器

ELDEN RING Overlay Hook 的核心加载器（`EROverlay.dll`）。通过钩入游戏的 DirectX 12 渲染管线来渲染基于 ImGui 的覆盖层界面，采用模块化插件架构——在运行时自动发现并加载 `overlays/` 文件夹中的各个覆盖层 DLL。

> 🌐 [English](README.md)

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

所有配置文件位于 `configs/` 文件夹中，可在启动游戏前用任意文本编辑器修改。

### `common.ini` —— 全局设置

| 配置项 | 默认值 | 说明 |
|---|---|---|
| `console` | `false` | 启用调试控制台输出。 |
| `font` | （空） | `data/` 文件夹中的字体文件路径（也可以是绝对路径）。留空则使用内置拉丁字体，其他语言回退到系统字体。 |
| `font_size` | `20` | 字体大小（磅）。 |
| `charset` | （空） | 字体加载使用的字符集（`enUS`、`jaJP`、`koKR`、`zhCN`、`ruRU` 等）。留空则根据游戏语言自动检测。 |
| `language` | （空） | 加载数据文件使用的语言。留空则使用游戏语言。 |

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

- [从源码编译](../docs/building.md)（英文）
- [编写覆盖层插件](../docs/writing-plugins.md)（英文）

---

## 许可证

[MIT 许可证](../LICENSE)

---

## 鸣谢

- 基于 [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL) 修改
- [minhook](https://github.com/TsudaKageyu/minhook)
- [imgui](https://github.com/ocornut/imgui)
- [stb](https://github.com/nothings/stb)
- [JSON for Modern C++](https://github.com/nlohmann/json)
- [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
- [Open Sans](https://fonts.google.com/specimen/Open+Sans)
