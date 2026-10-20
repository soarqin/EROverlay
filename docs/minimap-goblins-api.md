# Minimap / MapForGoblins DLL 接入

Minimap 使用 [MapForGoblins DLL](https://github.com/VirusAlex/ERR-MapForGoblins-DLL) 2.1.7 起提供的官方地图 API。需同时加载该 Mod，并在 `minimap.ini` 中设置 `[markers] others = true`。各整合包使用与其数据匹配的 MapForGoblins 构建；无需额外源码补丁。两者的加载顺序不限，Minimap 会重试发现稍后加载的模块。

## 接口与刷新

本地 [`mfg_map_api.h`](../src/minimap/compat/mfg_map_api.h) 与官方 `master` 提交 [`7506219bd4923662fdc7fdb3c6753495f6abc29e`](https://github.com/VirusAlex/ERR-MapForGoblins-DLL/blob/7506219bd4923662fdc7fdb3c6753495f6abc29e/sdk/mfg_map_api.h) 一致。ABI 仍为 `1`，通过 `MfgGetMapAPI(MFG_MAP_ABI)` 获取函数表；连接时检查结构大小、元素大小和图标目录限制。

官方在后台维护每层地图的不可变快照，`view_points` 仅取得已发布快照，不触发显隐或位置重算。首次读取、恢复读取或切换地图层时，可能先返回空视图或旧快照，Minimap 按现有 200 ms 标记刷新周期继续读取；提供方的刷新策略以官方 SDK 说明为准。

视图在调用线程下一次读取同一地图层、调用线程退出或提供方 DLL 卸载时失效；其他线程、其他地图层的读取和后台发布不会使它失效。Minimap 在更新线程同步消费视图，并将自己的绘制快照发布给渲染线程。调用和消费期间持有提供方的模块引用，不修改、释放或跨有效期保存返回的数组。图标目录只在连接时导入。

点位包含完成标记和焦点圈，并已按原生子对象深度排序。Minimap 保留该顺序，结合游戏素材的容器顺序绘制。世界状态地图共用同一位置数据，`raw_map_id` 的变体字节为零。

## 离线验证

在 Visual Studio x64 Native Tools 命令行中运行：

```bat
tools\verify_minimap_goblins.bat official-upstream-dir actual-minimap-fixture-dir [spdlog-include-dir]
```

第一个目录为未修改的官方源码，需先按官方构建流程生成 `src/generated_shared/goblin_map_icons.cpp`。默认从该目录的 `builds/build-vanilla/_deps/spdlog-src/include` 读取 spdlog 头文件；构建目录不同时使用第三个参数指定。

第二个目录提供 `vanilla-worldmap.gfx`、`swapped-layer-worldmap.gfx`、`common-layouts.bnd`、`common-atlases.tpf`，以及 `vanilla-params/` 下的 `BonfireWarpParam.param` 和 `WorldMapPointParam.param`。

验证直接编译官方 `goblin_map_api.cpp` 和生成的图标，仅替换游戏状态来源并模拟后台轮询。覆盖首次空视图、重复读取不重算、后台发布、同层及跨层／跨线程视图有效期、失败后保留已发布快照、点数限制、真实图标解码和原生层级，并通过正式 Data、Resources 和 ImGui Renderer 回放。

`build/native-checks/goblins-api/MapForGoblins.dll` 是测试夹具，不能安装进游戏。离线回放不能替代游戏内验收。
