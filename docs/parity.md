# 核心与 Minimap 功能对照

本表是迁移验收清单。实现状态和实际验证必须分开记录。各项最终结果在实现和检查完成后更新；未验证不等于已完成。

「C++ 行为来源」列均指独立 C++ 基线 `master` 的 `afab7e1`，不是当前分支中的文件。Rust workspace 已提升至根目录；基线导出和原生代码边界见 [源码布局](source-layout.md)。迁移前证据与新 Rust 扫描器的复核结果分别记录。

| 功能 | C++ 行为来源 | Rust 实现/证据 |
|---|---|---|
| 核心 + 插件动态 DLL | `src/plugin.cpp` | `crates/core/src/plugin.rs`，新 C ABI 校验；生命周期 harness |
| WinHTTP 代理加载 | `src/proxy/` | 本期未移植代理导出；当前使用普通 DLL loader/injector，不能把核心改名为 winhttp.dll |
| 初始化、失败回滚、退出 | `src/dllmain.cpp`、`src/uiscene.cpp` | 初始化线程、排空上下文访问、GPU 与文件退休；原生停止返回 1 并固定核心到进程退出，完整卸载仅离线模式验收；游戏内待测 |
| 原生 UI_SCENE 与 RGB10A2 | `src/uiscene.cpp`、`src/d3drenderer.cpp` | 专用 Rust DX12 后端；归档签名与 WARP 像素测试 |
| 游戏 UI 下层及未来上层 | `src/drawlayers.hpp` | ABI layer + RGBA8 下层合成；WARP 像素回读 |
| 渲染与更新并发 | `src/plugin.cpp`、Minimap snapshot | 有界 staging、快照、非阻塞 render；装卸载检查 |
| 游戏归档与异步资源 | `src/gamefiles.cpp`、`src/util/assets.cpp` | Rust 原生请求/回调、DDS/TPF/BND 解析和纹理准备 |
| 地表、地下、DLC 地形 | `src/minimap/resources.cpp` | 实现与素材检查结果见 `crates/minimap/FEATURES.md` |
| 地图碎片、全开底图 | `src/minimap/resources.cpp`、`src/util/gameflags.cpp` | 同上；不得把 synthetic fixture 等同游戏验证 |
| 命名预设、尺寸、边距、位置 | `configs/minimap.ini`、`settings.cpp` | Rust INI/预设解析；配置回归检查 |
| 旋转、形状、透明度、边框 | `src/minimap/render.cpp` | egui 网格 + 局部 RGBA8 合成；像素测试 |
| 玩家、方向、罗盘 | `src/minimap/render.cpp` | Minimap 功能记录及素材证据 |
| 赐福、地标、地图层级规则 | `src/minimap/data.cpp`、`gfx.cpp` | Minimap 功能记录及 PARAM/图标验证 |
| 死亡、编号 1–5、盖章 | `src/minimap/data.cpp`、`render.cpp` | Minimap 功能记录及回归检查 |
| 其他标记和原生颜色 | `src/minimap/data.cpp`、`gfx.cpp` | Minimap 功能记录；不额外置顶任何图标 |
| Map for Goblins 官方 API | `src/minimap/goblins.cpp` | Minimap 功能记录及官方 ABI 验证 |
| 加载/Logo/全屏地图隐藏 | `src/util/mapstate.cpp`、`data.cpp` | 独立原生显示状态；未验证状态默认隐藏 |
| 键盘切换与手动隐藏 | `configs/minimap.ini`、`render.cpp` | update 采样输入边沿，空键/none 禁用 |
| 缓存、上传预算、GPU 延迟回收 | `resources.cpp`、`textureupload.cpp` | 有界缓存与上传队列、fence 回收；性能证据单列 |
| 旧 ImGui 插件二进制兼容 | 用户已明确不需要 | 不实现；按新 API 直接重写 |
| Boss/Achievements | 本期范围外 | 后续直接重写 |
