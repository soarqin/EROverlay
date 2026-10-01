# Minimap 原生素材与标记执行记录

2026-10-02 开始执行[开发计划](minimap-game-textures-plan.md)。当前代码从游戏文件系统读取素材，并使用 Overlay 自有纹理；死亡位置使用内部 `MENU_MAP_DropSoul`。用户提供的外部对照文件保留在本地，运行时和分发脚本已移除 `data/map` 依赖。

## 已实现的行为

- 核心独立文件桥：EXE SHA-256 和入口字节检查、请求提交和 flush、同路径独立请求、异步取消、成功 buffer 的原 allocator 释放、回调返回后排空。
- 自有 GPU 资源：有界 TPF/DDS 解析、BC1/BC2/BC3/BC7 上传、持久 DIRECT 上传队列、上传 fence、绘制 fence、SRV 延迟回收与设备资源重建。
- 图标：运行时解析 GFX 的帧继承、depth、矩阵和固色描边，读取内部图集区域与四张 DDS。全部 93 个外部图形有内部来源；118 个非空图标帧均恢复。
- 地图：L0 瓦片按原生坐标、参数与事件重建的进度 mask 和目录 exists 选择，地下模式先画地表底层，再叠地下层；矩形与旋转模式使用相同数据。L1/L2 格式和跨度已支持，默认等待位置与接缝验收后开放。
- 圆桌厅：玩家 raw map ID 与 Home 参数地图比较；内部 Hub 与当前赐福图标组合，使用小地图自己的中心布局。
- 死亡标记：游戏有效性和转换后坐标随快照更新，对应地图类别才绘制，位于普通地标之上、玩家之下。`death_marker=1` 默认开启，设为 0 关闭。
- 编号标记：同步游戏放置的编号 1～5，内部下箭头与动态数字一起绘制。每次 update 读取当前位置与槽位，删除后清空，保留非连续编号；`player_markers=1` 默认开启。

新接口通过 `getEROverlayNativeAPI(1)` 独立发现，带 size/version；旧 `EROverlayAPI` 和 `TextureContext` 布局保持兼容。CPU 文件、定义解析和标记刷新在 update 线程进行；GPU 创建、查询和绘制在 render 线程进行。

## 验证结果与范围

| 验证 | 结果 | 范围 |
|---|---|---|
| Debug / Release 构建 | 通过 | 核心、Minimap、Boss、Achievements 与 injector |
| 正式 GFX 解析器对照研究脚本 | 通过 | 348 帧、136 个图层、4 个矢量；矩阵最大误差约 0.00000305 |
| 正式 Binder、XML、TPF、DDS 解析 | 通过 | 实际游戏资源、截断 GFX、错误 DDS 与缺失 TPF 名称 |
| 正式 BC7 上传与 GPU 回读 | 通过 | 回读块数据与 DDS 完全一致；上传对象、绘制 fence 与 SRV 回收 |
| GPU 未完成时的延迟回收 | 通过 | fence 未完成时保留纹理和 SRV，完成后回收；取消未提交纹理 |
| 正式文件桥普通 DLL 验证 | 通过 | 同路径两条已提交请求，取消一条得到 native status=2，另一条成功；stop 等待回收后返回 |
| 公共 TPF 目标提取 | 通过 | 回调仅保留四张 DDS，共 7,340,624 字节，不复制整包 |
| 标记快照验证 | 通过 | 原生坐标换算、U8 地下状态与非零填充字节、死亡标记清除、Home 地图识别、无效快照清空 |
| 死亡标记正式绘制列表 | 通过 | 使用实测 view 字节进入正式 ImGui 绘制；矩形、旋转圆形、地表/地下/DLC、失效及其他地图隐藏 |
| 编号标记静态布局与只读采样 | 通过 | idapro 确认保存容量 10、正常上限 5、slot+1、地图类别 U8；用户当前两个标记为 1/2、id=4/5 |
| 编号标记正式绘制列表 | 通过 | 两个实际记录的箭头 UV、数字字形及位置；编号 1～5、非连续编号、删除/移动、旋转圆形、透明圆角、地图类别与上下文失效 |
| 进度读取与同坐标瓦片选择 | 通过 | type=1/type=2、位 31、PARAM 地表/地下/DLC ID、零与非零后缀、地下两层独立掩码、不可读进度拒绝 |
| 核心在游戏标题流程中加载 | 通过 | 实际文件请求和连续 Present 提交；不证明游戏内视觉已完成 |
| 真实死亡与回收卢恩、传送、地下/DLC、圆桌厅视觉 | 待游戏验收 | 需要自然发生的游戏状态；没有模拟存档或修改游戏状态 |
| 地图碎片获得状态同步 | 用户实测通过 | 2026-10-02 用户确认同步正常；不据此推断死亡图标或其他生命周期场景也通过 |
| 死亡标记修复后的游戏画面 | 待游戏验收 | 已取得未回收卢恩的有效记录与坐标，并完成正式绘制列表回归；新 DLL 的实际画面仍待确认 |
| 编号标记实际游戏画面 | 待正常加载新 DLL 后验收 | 当前游戏 PID 26560 加载旧 Minimap.dll；本次未覆盖已加载文件或重新注入 |

验证工具读取本地研究资源，像素和 GFX 不加入分发。游戏内测试不附加调试器、不设置断点；静态补充仍使用 Python idapro 分析 Steam 原始 EXE。

## 2026-10-02 未展开底图修正

用户截图显示地图碎片未获得时的底图。修正后，用户于 2026-10-02 确认地图碎片获得状态同步正常。随后只读游戏采样中，地表 activeMask 与事件标记计算均为 0x000100EF；这不代表所有区域已展开。

本次发现并修正了两类实现问题。文件桥的事件读取跳过 type=2 直接存储，并把其他类型当作指针；现在与 RVA 0x5FA250 保持一致。地图进度不再直接使用 view +0x39C 缓存，而是按 RVA 0x8892C0 从 group 88 的参数行和当前事件标记重建。不可读状态不会自动变成 activeMask=0；读取保持在 update 线程，200 ms 刷新并在对象变化时失效。

地下图像为半透明层，原实现只绘制 M01。本次按原生 Image_0/Image_1 结构补齐 M00 底层；两层共享坐标变换、各用自己的 activeMask。地表与 DLC 的非零变体已经包含细节，不额外画一张零变体盖在其上。

新增正式代码回归将合成 PARAM/事件存储连接到实际 BHF4/XML 和瓦片请求：地表 key=2020 在 bit 15 成立时请求 00008000，清除后请求 00000000；地下同坐标分别请求地表和地下后缀。Debug、Release、进度回归、图标帧对照、死亡快照和 GPU 上传回读均通过。此次未修改 Steam EXE、存档，也未附加调试器或设置断点。

## 2026-10-02 死亡标记隐藏修正

用户确认进度同步后，反馈死亡标记没有显示。本次在其保留未回收卢恩的状态中，只读采到 view +0xA9=1、deathMapId=0；玩家地图坐标为 (2807.2265625, 6776.87939453125)，死亡坐标为 (2802.62890625, 6778.857421875)，对应原始地图 m10_01_00，已正确转换至地表。

原因是 view +0x30 的地下状态被错误读取为 int32。RVA 0x887F37 只写 U8；此次实际字节为 00 60 A6 32，后三字节是填充。旧读取得到 0x32A66000，底图使用最低位仍判断为地表，但死亡条件使用「非零」而误判为地下，导致 deathMapId=0 与 1 比较失败。

正式读取改为 U8 后归一化到公开快照的 int32 0/1，保留已有 ABI；绘制使用当前展示地图类别与 deathMapId 比较。新增 [view 读取](../src/util/mapstate.cpp) 与 [死亡图形绘制回归](../tools/minimap_death_render_verify.cpp)，回放以上实测坐标和填充字节，验证 DropSoul 的实际 UV 四边形出现在 ImGui 绘制列表，而不只检查 deathValid。回归覆盖矩形、旋转圆形、地下和 DLC，失效记录及其他地图不会绘制。

Debug / Release 构建和相关回归均通过。新 DLL 的实际游戏画面、真实回收及再次死亡事件仍需验收。本轮观测到两次游戏进程退出，Windows Application Error 均记录 eldenring.exe +0x1EBB809 / 0xc0000005；与死亡隐藏条件的因果关系尚未确认，不将此修复声明为崩溃修复。

2026-10-02 02:18（Asia/Shanghai）确认游戏进程已退出后，将新 Release 核心和 Minimap 同步到 D:/Games/ERLoader/dll/EROverlay.dll 与 D:/Games/ERLoader/dll/overlays/Minimap.dll，并分别核对 SHA-256。旧版备份在 build/native/death-fix-backup-20261002-021810；未改配置、Steam 文件或存档。下次启动加载新版本，当前没有新画面的验收结论。

## 2026-10-02 编号标记接入

原生素材迁移及死亡修复已提交为 `bd644a6`（`feat(minimap): use native game assets and render dropped runes`）。编号标记增加独立快照、图形解析、绘制、默认开启的配置项和验证工具。

通过 Python idapro 分析 Steam 原始 EXE，确认 `WorldMapMarkerDataList` 位于 view +0x300，+0x38 指向保存对象。保存对象的 slots、capacity、active count 分别位于 +0x08、+0x10、+0x40。每槽 16 字节，id<0 为空；x/y 已是地图坐标，map/icon 各为 U8。原生 0x879330 把 slot+1 写入 Text_0，编号不取 id 或列表排序位置。

0x81A550 和读档函数 0x81ACC0 分配 10 个保存槽；正常放置 0x887440 使用全局上限 5，超限前删除最早标记。MarkerList UI 与 view +0x300 列表绑定，正常上限以放置函数为准。正式读取有界复制 10 个保存记录，核对 active count，并发布编号 1～5 的正常槽；不因 capacity=10 绘制额外编号。

2026-10-02 02:38（Asia/Shanghai）从 PID 26560 取得稳定只读快照。玩家为 (2804.794678,6774.085938)，标记 1 为 (2795.357910,6774.881836)、id=4，标记 2 为 (2810.024414,6782.881836)、id=5，均为 map=0、icon=1，其余八槽为空。快照及原始字段字节存于 `build/ida/player-marker-live`；只使用 QUERY_INFORMATION | VM_READ，没有附加调试器、断点、调用游戏函数或修改存档。

内部下箭头使用 `MENU_MAP_Marker`，从 GFX `MarkerList/Item_0/Icon_0` 恢复矩阵与图集区域。`Text_0` 的 DefineEditText 恢复 18 单位字号、灰白颜色、居中布局和 (-6.15,-7.05) 放置矩阵。数字使用 Overlay 已有字体，深色描边近似游戏滤镜；不复用游戏字体或 GPU 句柄，不新增外部数字图片。旋转小地图只旋转世界位置，箭头和数字保持屏幕正向。

编号记录在每次 update 的发布前读取，不进入普通地标的 200 ms 缓存。两次记录复制与对象身份、容量、计数核对发现变化时丢弃当次采样；标记不可读时清空当次编号列表，下次继续读取。地图 generation、view、raw map 或地图层在更新中改变时整份显示快照失效。新增日志记录 `markers`、`markers-read`、`player-marker` 的编号、id、坐标与类别，沿用可选 `native_log`。

[编号标记绘制验证](../tools/minimap_marker_render_verify.cpp)回放实际采样，并执行正式 Data、Resources、Renderer，检查 ImGui 中内部箭头区域的四边形和数字 1/2 字形及位置。随后验证编号 1～5、空槽不重编号、位置变化、删除清空、地表/地下/DLC、错层、开关、菜单隐藏、旋转圆形与透明圆角，以及不可读/计数不一致/非有限坐标/上下文失效和下一次更新恢复。Debug / Release 构建及原有素材、地图进度、死亡标记回归通过。

当前运行的游戏仍加载 `D:/Games/ERLoader/dll/overlays/Minimap.dll` 的旧版本。本次新产物在 `build/native/bin/overlays/Minimap.dll`；未覆盖加载中的 DLL，未重新注入。真实画面、正常放置/到达清除和读档事件在下一次加载后继续验收；静态与绘制回放结果不替代这些游戏验收。详细来源见[编号标记证据](minimap-player-markers.evidence.json)。

## 产物与复查

构建产物在 `build/native/bin/EROverlay.dll` 和 `build/native/bin/overlays/Minimap.dll`。验证目录不提供外部 `data/map`，不覆盖 Steam 安装文件。实际游戏日志保存在 `build/native/acceptance-20261002-005211/native.log` 和 `build/native/bridge-verification-final/bridge.log`。运行中的验证 DLL 保留到该游戏进程正常退出。

可选诊断配置 `native_log=<absolute-path>` 记录请求、纹理就绪与少量地图快照；默认不启用。`map-progress` 行同时记录参数/事件得到的 masks、view cached masks 和 reveal 状态，可在同地点对照；文件路径的后缀记录实际选图。

复查程序：

- [解析器和帧对照](../tools/minimap_native_verify.cpp)。
- [地图快照与死亡状态](../tools/minimap_map_verify.cpp)。
- [地图进度与瓦片后缀回归](../tools/minimap_progress_verify.cpp)。
- [死亡标记实际 ImGui 绘制回归](../tools/minimap_death_render_verify.cpp)。
- [编号标记只读采样](../tools/minimap_marker_capture.py)与[实际 ImGui 绘制回归](../tools/minimap_marker_render_verify.cpp)。
- [正式纹理上传、回读和延迟回收](../tools/minimap_texture_verify.cpp)。
- [普通 DLL 文件桥验证](../tools/minimap_bridge_verify.cpp)。

完整游戏验收继续按开发计划 P6 执行；当前版本限定已校验 EXE，未知版本显示状态并拒绝调用旧 RVA。
