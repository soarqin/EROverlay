# Minimap 小地图

在游戏画面上显示当前区域的小地图、已发现的赐福和地标、卢恩死亡位置，以及游戏大地图中放置的编号标记 1～5。

## 安装与更新

将 `EROverlay.dll`、`overlays/Minimap.dll` 和 `configs/` 放入使用的 Mod 加载目录。可用 [EldenModLoader](https://www.nexusmods.com/eldenring/mods/117)、[ModEngine2](https://github.com/soulsmods/ModEngine2) 或 [me3](https://github.com/garyttierney/me3) 加载核心 DLL。使用代理方式时，将核心改名为 `winhttp.dll`，并与 `eldenring.exe` 放在同一目录。

更新到新的分区配置格式时，先备份旧 `configs/minimap.ini`，再用随新版附带的文件替换它。旧的平铺键名和多行逗号列表不再读取，也不会自动转换。

小地图从游戏归档读取素材，使用自己的纹理绘制，无需外部 `data/map` 图片。不要与会钩入相同 DirectX 接口的 MSI Afterburner、RivaTuner Statistics Server 或 Nvidia FPS Counter 同时使用。

## 先这样使用

默认配置可直接使用：

- 按 **M** 循环切换「右上角小地图 → 居中大地图 → 隐藏」。
- 按 **N** 同时显示或隐藏赐福和地标。
- 死亡标记和编号标记默认显示；回收卢恩、删除标记时随游戏清除。
- 地图底图默认跟随已获得的地图碎片。

用文本编辑器修改 `configs/minimap.ini`，使用 UTF-8 保存，然后重启游戏。核心在加载时读取文件；修改后仅重建渲染器不会重新读取文件。注释放在单独一行，以 `#` 或 `;` 开头。布尔值推荐使用 `true` 和 `false`。

`configs/` 使用英文注释；中文注释模板位于 `configs_CN/`，键名和默认值一致。使用中文模板时，先备份已有的自定义文件，将对应模板复制到 `configs/`，再填写需要的设置。只修改 `configs_CN/` 不会生效，加载器读取的是 `configs/`。

## 找到要改的分区

| 分区 | 用途 |
|---|---|
| `[controls]` | 显示、预设切换和标记快捷键 |
| `[map]` | 是否显示全开底图 |
| `[markers]` | 四类标记的显示开关 |
| `[border]` | 所有预设共用的边框颜色和像素宽度 |
| `[presets]` | 启动预设和切换顺序 |
| `[preset.名称]` | 一套完整的尺寸、位置、缩放、透明度和形状 |
| `[diagnostics]` | 可选的排查日志 |

### 快捷键

| `[controls]` 配置项 | 默认值 | 作用 |
|---|---|---|
| `toggle` | `M` | 显示或隐藏小地图；与 `cycle` 同键时改为预设循环 |
| `cycle` | `M` | 按 `[presets]` 的顺序切换 |
| `toggle_graces` | `N` | 切换赐福标记显示 |
| `toggle_landmarks` | `N` | 切换地标标记显示 |

按键可用 `A`～`Z`、数字、`F1`～`F24` 等名称，也支持 `CTRL+M`、`ALT+N`、`CTRL+SHIFT+F8`。修饰键和按键之间不要加空格。完整名称见 `configs/input.ini`。留空或填写 `none` 可禁用一个快捷键。

同一按键可触发多个标记开关。显示键和切换键相同时，若列表中没有 `zoom = 0` 的预设，会自动在末尾添加隐藏步骤。两项都禁用时不会添加隐藏步骤。小地图快捷键只在游戏前台且没有打开游戏菜单时响应。

希望 M 只负责显示/隐藏，F8 只负责切换大小，可填写：

~~~ini
[controls]
toggle = M
cycle = F8
~~~

### 地图与标记

| 分区与配置项 | 默认值 | 作用 |
|---|---|---|
| `[map] full_map` | `false` | `true` 时显示地表、地下和 DLC 全开底图；`false` 时跟随碎片进度 |
| `[markers] graces` | `true` | 显示已发现的赐福，运行中可用快捷键切换 |
| `[markers] landmarks` | `true` | 显示游戏允许显示的地标，包括已发现的远眺标记 |
| `[markers] death` | `true` | 在对应地图层显示尚未回收的卢恩位置 |
| `[markers] beacons` | `true` | 显示在游戏大地图放置的编号标记 1～5，保留各自编号 |

全开选项只改变小地图底图，不授予碎片、不修改存档，也不自动发现赐福或地标。编号标记仍在游戏大地图中放置和删除；小地图负责同步显示。

### 边框

`[border] color` 填写红、绿、蓝、透明度四个 0～255 的整数；默认 `255, 255, 255, 100` 为半透明白色。最后一项设为 `0` 时不可见。边框透明度单独控制，不随预设 `opacity` 改变。

`[border] width` 直接使用像素，默认 `1.5`；设为 `0` 关闭。边框设置对所有预设生效。

## 修改或新增显示预设

`[presets] order = compact, large` 表示启动使用 `compact`，随后切到 `large`。分区在文件中的位置不影响切换顺序；没有列在 `order` 中的预设不会使用。

每个预设独立填写，没有跨预设继承，也不需要让多行列表一一对应。下表默认值适用于任意预设省略的字段；附带的 `large` 已显式设置为 90% 宽高、居中、1.5 倍缩放和 60% 不透明度。

| `[preset.名称]` 配置项 | 缺省值 | 如何填写 |
|---|---|---|
| `width` / `height` | `30%` / `30%` | 宽高相对于游戏的 16:9 基准高度；`30%` 与 `0.3` 等价 |
| `position` | `margins` | 按边距定位；`center` 为屏幕居中，忽略所有边距 |
| `margin_left` / `margin_right` | 两侧均不填时：右边距 `0` | 左右选择一项填写；像素如 `24`、`24px`，或屏幕宽度百分比 `2%` |
| `margin_top` / `margin_bottom` | 两侧均不填时：上边距 `0` | 上下选择一项填写；像素如 `24`、`24px`，或屏幕高度百分比 `2%` |
| `zoom` | `0.75` | 数值越大，地图细节越大、同一窗口显示范围越小；`0` 隐藏该预设 |
| `opacity` | `80%` | `0%` 完全透明，`100%` 完全不透明；也可用 0～1 小数 |
| `rotate` | `false` | `true` 时镜头前方朝上，自动使用圆形并显示方位指示 |
| `shape` | `rect` | `rect` 矩形、`rounded` 圆角矩形、`circle` 圆形；圆形边长取宽高中较小值 |
| `rounding` | `20%` | 仅用于圆角矩形；百分比相对于较短边的一半，也可填 `30px` 或 `30` |
| `map_scale` | `1` | 与 `zoom` 相乘，调整底图及跟随地图缩放的图标；有效缩放限制在 0.01～16 |
| `decoration_scale` | `1` | 调整赐福、普通地标和圆桌厅图标；范围型地标仍随底图缩放 |
| `player_scale` | `1` | 调整玩家点/箭头、死亡标记和编号标记 |
| `compass_scale` | `1` | 调整方位指示，仅在旋转模式中显示 |

尺寸基准为 `min(屏幕高度, 屏幕宽度 × 9 / 16)`。1920×1080 下，30% 宽高得到 324×324 像素；90% 得到 972×972 像素。

缩放倍数 `1` 表示原大小，`1.5` 表示 150%；`decoration_scale`、`player_scale` 或 `compass_scale` 设为 0 可隐藏对应图标组。改变 `zoom` 也会改变随地图缩放的图标；只想把玩家标记放大，修改 `player_scale` 即可。

### 用边距定位

在要修改的 `[preset.名称]` 中设为 `position = margins`，从左右选择一侧、上下选择一侧。小地图以对应角落为锚点，边距表示这个角落到屏幕相应边缘的距离；修改尺寸或切换为圆形后，仍保持这两个边距。

| 填写的边距 | 小地图锚点 |
|---|---|
| `margin_right` + `margin_top` | 右上角 |
| `margin_left` + `margin_top` | 左上角 |
| `margin_right` + `margin_bottom` | 右下角 |
| `margin_left` + `margin_bottom` | 左下角 |

`24` 或 `24px` 表示 24 像素，`2%` 表示完整屏幕宽度或高度的 2%，与上面的地图尺寸基准无关。左右和上下可以使用不同单位。正值向画面内移动，负值向外移动；移出屏幕的部分会被裁剪。

留空或省略表示不选这一侧，`0` 则明确选择该侧并贴边。一个轴的两侧都未填写时，水平方向默认右边距 `0`，垂直方向默认上边距 `0`。两侧同时有有效值时，左侧或上侧优先，并在控制台提示；小地图保持配置的尺寸，不会被两侧边距拉伸。无效边距按未填写处理，另一侧的有效值仍可使用。

例如把已有小地图移到左下角，距左边 24 像素、底部 36 像素。在 `[preset.compact]` 中替换原位置设置，并将右、上边距留空：

~~~ini
position = margins
margin_left = 24px
margin_right =
margin_top =
margin_bottom = 36px
~~~

改回右上角时，填写 `margin_right` 和 `margin_top`，将另外两侧留空。设为 `position = center` 时直接居中，全部边距均被忽略。

### 增加预设

例如增加一个右上角、距右边和上边各 24 像素、随镜头旋转的圆形预设：

~~~ini
[presets]
order = compact, rotating, large

[preset.rotating]
width = 35%
height = 35%
position = margins
margin_right = 24px
margin_top = 24px
zoom = 0.75
opacity = 90%
rotate = true
shape = circle
player_scale = 1.25
~~~

把示例的 `order` 修改合并到文件中已有的 `[presets]`；保留已有 `[preset.compact]` 和 `[preset.large]`，再加入 `[preset.rotating]`。不要创建第二份同名分区。

名称使用最多 64 个英文字母、数字、下划线或连字符，区分大小写。每个预设至少保留一项设置。删除一个预设时，同时从 `order` 移除其名称；只保留 `order = compact` 可只使用小地图。重复名称仅使用第一次，未定义名称会跳过。没有可用预设时恢复附带的两套默认显示设置。

想把隐藏步骤放到指定位置，可在 `order` 中加入 `off`，再增加 `[preset.off]` 和 `zoom = 0`。

## 故障排查

普通数值、形状和开关为空或无效时，恢复该项默认值；边距为空或无效时按未填写处理。非法值会在控制台记录具体键名。数值使用小数点，不能继续填写旧的逗号列表。若需要查看配置错误，在 `common.ini` 中设置 `console = true`，再重启游戏。

在 `[diagnostics]` 设置 `log_file = D:/Logs/minimap.log` 可记录资源请求和地图状态。先创建上级文件夹；绝对路径便于定位，相对路径按游戏进程工作目录解析。平时留空以关闭文件日志。该日志用于资源与游戏状态排查，配置错误看控制台。

| 现象 | 检查方法 |
|---|---|
| 修改后没有变化 | 确认编辑的是加载器旁的 `configs/minimap.ini`，保存后重启游戏 |
| 按键没有响应 | 检查是否留空、名称是否正确、游戏是否在前台、是否打开菜单 |
| 新预设没有出现 | 检查 `order` 与分区名称的大小写是否一致 |
| 边距没有按预期生效 | 检查 `position` 是否为 `margins`，并将同一轴的另一侧边距留空；`0` 也算已填写 |
| 小地图突然隐藏 | 检查隐藏步骤、`zoom = 0`、尺寸为 0 或 `opacity = 0%` |
| 地图缩放方向与预期相反 | 增大 `zoom` 是放大细节；减小它能看到更大范围 |
| 圆角没有效果 | 使用 `shape = rounded`；旋转模式会自动改为圆形 |

## 游戏与资源支持

原生适配覆盖已核对的 1.02～1.17 历史 EXE 和 Steam 1.17.1，按完整哈希选择地址与布局。全部提供的旧 EXE 已通过离线核对与布局回归，逐旧游戏画面仍待实测；未知 EXE 会显示状态并关闭原生调用。详见[兼容性报告](../../docs/minimap-version-compatibility.md)。

图集名称和数量由内部 GFX/TextureAtlas 定义，可使用增加图集的 mod；12 图集的加载、绘制和回收验证已通过。资源须能通过游戏文件层读取，并采用支持的 PC GFX/TPF/DDS 格式。旧版只要求地表和地下资源，不要求 M10 或 DLC 图集；资源大小和纹理预算仍适用。

## 许可证与鸣谢

[MIT 许可证](https://github.com/soarqin/EROverlay/blob/master/LICENSE)。项目使用 [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)、[minhook](https://github.com/TsudaKageyu/minhook)、[ImGui](https://github.com/ocornut/imgui)、[stb](https://github.com/nothings/stb)、[JSON for Modern C++](https://github.com/nlohmann/json) 和 [Pattern16](https://github.com/Dasaav-dsv/Pattern16)。
