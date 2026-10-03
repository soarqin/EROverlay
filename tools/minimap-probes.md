# Minimap 资源验证说明

这些程序用于复现[可行性报告](../docs/minimap-game-textures.md)的资源和 GPU 验证，不是完整 Minimap 实现。静态逆向使用 Python idapro；运行验证不附加调试器、不设置断点、不挂钩游戏函数，也不读取游戏 GPU 纹理。

## 静态分析

2026-10-03 起，小地图采用新的分区配置。资源/地图日志使用 `[diagnostics] log_file`，历史记录中的旧 `native_log` 仅保留为当时的配置名。配置解析可用 `tools/build_minimap_settings_check.bat` 构建并验证，或直接运行已构建的 `build/native-checks/minimap_settings_verify.exe`；这项检查读取真实 INI，不连接游戏。

通用扫描回归使用 [verify_minimap_scanner.py](verify_minimap_scanner.py)，先按共享规则解析，随后才与逐 EXE 证据比较。独立 `--resolve-only` 模式不读取历史期望值；调用命令与运行时接入范围见[通用扫描方案](../docs/minimap-scanner-design.md)。该工具继续使用 Python idapro 静态分析，没有调试器或游戏调用。

在项目根目录运行：

~~~powershell
python3 tools/ida_minimap_research.py --targeted --queries tools/ida_minimap_queries.json
~~~

脚本从 Steam 库和 appmanifest_1245620.acf 定位 EXE，直接读取原文件。数据库、输出缓存和查询配置都按 SHA-256 校验。游戏更新后使用新的 --database 和 --output，重新定位；不能修改哈希后继续调用旧 RVA。

IDA 数据库不应由两个会话同时打开。--request-dir 可以保持一个 idapro 进程处理多个编号查询；向该目录写入包含 stop=true 的请求后保存退出。

历史版本使用 [ida_minimap_versions.py](ida_minimap_versions.py) 按独立数据库分析，输出存入 `build/ida/versions/<version>/`。支持 RTTI、chained unwind、指令窗口、完整指令和分支核对，以及按已导出的旧函数补充 reference 变体。范围与具体结果见[历史版本报告](../docs/minimap-version-compatibility.md)。

~~~powershell
python3 tools/ida_minimap_versions.py analyze --exe 'D:/Games/EldenRingExe/1.17/Game/eldenring.exe' --database build/ida/versions/1.17/fresh.i64 --output build/ida/versions/1.17 --label 1.17 --profile build/ida/versions/reference.json
python3 tools/collect_minimap_version_evidence.py
~~~

证据收集器只读取 IDA 导出的 JSON，并逐样本核对哈希、函数、内部分支和调用关系；它不启动旧游戏，不代替历史资源和实际游戏验收。

## 构建运行验证程序

在 Visual Studio 2022 的 x64 Native Tools Command Prompt 中运行：

~~~bat
tools\build_minimap_probes.bat
~~~

产物在 build/ida/probes/。MSVC 使用 /std:c++latest 与项目的 C++23 工具链设置一致；不安装或替换游戏文件。

## 读取游戏归档中的图片字节

1. 正常启动对应 Steam 样本，进入标题菜单并等待资源初始化。使用脱机 mod 验证会话，保留原始存档。
2. 取得要验证的 eldenring.exe 的 PID。加载器要求显式 PID，并校验目标名称；不会枚举并自动选择任意游戏进程。
3. 用常用 mod 加载器加载 minimap_file_probe.dll，或者运行下列命令，将 PID 替换为本次验证进程的实际值：

~~~powershell
build/ida/probes/minimap_probe_loader.exe <PID> build/ida/probes/minimap_file_probe.dll
~~~

加载器返回仅说明 LoadLibraryW 线程已返回，实际读取结果以 events.log 为准。DLL 校验 EXE SHA-256 和已加载函数入口，样本不匹配时拒绝调用游戏接口。

DLL 在同目录创建独立的 run-<PID>-<tick> 输出目录。固定请求覆盖 raw BHF4 索引、mtmskbnd、地表/地下/DLC、L0/L1/L2、非零变体、取消请求和公共图集；调用独立 submit 后显式 flush。

完成回调复制字节到自己的进程堆，并通过相同游戏 allocator 释放原 buffer。文件写入在工作线程进行。公共图集单包约 194 MiB，完整验证会短暂保留该复制；正式方案应仅保留目标 DDS。

预期成功为 status=1，取消为 status=2。输出没有游戏 GPU 句柄。程序不改游戏代码和存档；读取出的游戏素材只用于本地研究，不加入分发包。

**研究 DLL 保留到验证进程退出。** 不使用固定 Sleep 后自卸载，以免仍有游戏回调引用。等待超过 120 秒会记录日志并继续等终态；停止验证时正常退出本次研究游戏，不对仍在回调中的 DLL 强制 FreeLibrary。

## 检查 CPU 资源格式

对实际输出路径执行：

~~~powershell
python3 tools/minimap_asset_inspect.py <run-dir>/map-index.bin <run-dir>/map-masks.bin <run-dir>/common-layouts.bin <run-dir>/surface-v8000.tpf <run-dir>/common-atlas.tpf --output build/ida/assets.json
~~~

脚本逐 entry 校验偏移和长度，读取 PC TPF/DDS 与本次观察到的 Binder4 格式。BHF4 中的数据偏移属于配套数据文件，不用它截取 BHF4 自身。脚本不解密或解包 Steam 的 Data*.bdt。

## 读取和核对全部 sprite

构建脚本另生成 `minimap_sprite_probe.dll`。它复用同一个文件接口和样本校验，读取 GFX 平台/通用候选，并只读记录 WorldMapPointParam、BonfireWarpParam 与 GameSystemCommonParam；不打开游戏地图，不设置断点。

~~~powershell
build/ida/probes/minimap_probe_loader.exe <PID> build/ida/probes/minimap_sprite_probe.dll
~~~

以本次已经完成的输出为例，恢复 GFX 的 348 帧定义：

~~~powershell
python3 tools/minimap_gfx_inspect.py build/ida/sprite-probe/run-24636-34293203/worldmap.gfx --output build/ida/sprite-compare/checked-gfx-recipes.json
~~~

GFX 检查脚本只解析文件层返回的 CPU 字节，格式与 idapro 导出的原生 tag loader 核对。它包含有界 tag/bit 读取、显示列表继承、depth 排序、矢量路径和相关 AS3 frame 方法检查；不执行游戏脚本。

先用下面的独立采样程序输出 SB_MapCursor、SB_MapCursor_02、SB_MapCursor_03_dlc、SB_Chara 四张 BGRA BMP，再对照本地外部图集。`compare_minimap_sprites.py` 需要 Pillow，只用于 PNG/BMP 图像对照，不用于 EXE 逆向。

~~~powershell
python3 tools/compare_minimap_sprites.py --external-atlas src/minimap/data/map/atlas.json --layouts build/ida/probes/run-26836-32694765/common-layouts.bin --gfx build/ida/sprite-probe/run-24636-34293203/worldmap.gfx --samples SB_MapCursor=build/ida/byte-probe4/map-atlas.bmp --samples SB_MapCursor_02=build/ida/byte-probe4/map-atlas02.bmp --samples SB_MapCursor_03_dlc=build/ida/sprite-compare/map-atlas-dlc.bmp --samples SB_Chara=build/ida/sprite-compare/chara.bmp --parameters build/ida/sprite-probe/run-24636-34293203 --output build/ida/sprite-compare/sprite-evidence.json --image-output build/ida/sprite-compare/verified-roundtable.png
~~~

预期为 93 个 sprite 中 92 个 RGBA 完全一致，圆桌厅由 Hub + 赐福 48 组合；参数字段的 105 个非零编号候选均有内部 recipe。覆盖候选与运行事件条件分开；不能认为所有备用字段均在当前存档显示。

完整证据可合并到报告：

~~~powershell
python3 tools/collect_minimap_evidence.py --runtime build/ida/probes/run-26836-32694765 --diagnostics build/ida/byte-probe2 --initial-runtime build/ida/byte-probe --sprite-evidence docs/minimap-game-sprites.evidence.json --sprite-runtime build/ida/sprite-probe/run-24636-34293203
~~~

`docs/minimap-game-sprites.evidence.json` 仅保存名称、哈希、矩阵、区域和来源指针。实际游戏像素、GFX、参数字节与回读图片仍保存在 build/ida，不加入分发包。运行中的 DLL 继续保留到该进程正常退出；不要覆盖已经加载的 DLL 文件。

## 用自有 D3D12 资源采样

运行独立程序，它不连接游戏进程：

~~~powershell
build/ida/probes/minimap_dds_probe.exe <run-dir>/surface-v8000.tpf build/ida/surface.bmp
build/ida/probes/minimap_dds_probe.exe <run-dir>/common-atlas.tpf build/ida/map-atlas.bmp SB_MapCursor
~~~

程序创建自己的设备、BC7 纹理、SRV、DIRECT 队列、fence 和回读资源。compute shader 采样后写 BMP，控制台记录 adapter、尺寸和 DXGI format。支持本次 2D、单 mip、BC7_UNORM 样本；不是通用生产 DDS loader。

本次成功验证在 Intel(R) Arc(TM) B390 GPU。它确认自建纹理路线；同游戏设备下的 ImGui、透明度、纹理动态回收和持续运行仍按开发计划验收。

sprite 补充对照另成功采样 SB_MapCursor_03_dlc 和 SB_Chara，均为 BC7_UNORM。BMP 包含 alpha，必须按 BGRA 读出四通道；部分图像查看器把普通 BMP 的 alpha 忽略，不能以查看器背景代替字节比较。

## 正式实现验证

在 Visual Studio x64 Native Tools Command Prompt 中，先构建正式 Release 核心，再运行：

~~~bat
tools\build_minimap_native_checks.bat
build\native-checks\minimap_native_verify.exe
build\native-checks\minimap_map_verify.exe
build\native-checks\minimap_progress_verify.exe
build\native-checks\minimap_death_render_verify.exe
build\native-checks\minimap_marker_render_verify.exe
python3 tools/check_minimap_native_recipes.py
~~~

产物位于 build/native-checks。纹理验证程序使用 build/native 中 Release 的正式对象文件；运行前须让系统可找到 Steam 安装目录中的 steam_api64.dll。该程序只创建自有 D3D12 设备，检查 BC7 上传、回读与 fence 延迟回收，不连接游戏。

编号标记验证需要先保存游戏中已有编号点的只读快照。在附近存在编号 1/2 的游戏状态下执行：

~~~powershell
python3 tools/minimap_marker_capture.py --pid <PID> --output build/ida/player-marker-live
build/native-checks/minimap_marker_render_verify.exe build/ida/player-marker-live
~~~

采样工具只取得 QUERY_INFORMATION | VM_READ 权限，从 Steam 定位并校验 EXE、枚举实际模块基址，再复制 view、保存对象与 16 字节槽位。状态改变时拒绝发布混合记录；结果包含 JSON 和本地回放字节，保存在忽略目录，不加入分发。

绘制验证程序回放这些记录，通过正式 update 快照、GFX/XML/DDS 资源和 ImGui Renderer 核对内部箭头 UV、数字字形及位置。回归再覆盖五个编号、空槽、删除/移动、图层、旋转/透明度和失效清除；这些变化仅发生在验证程序自己的内存，不写回游戏。字体字形使用已有 ImGui 字体，原生软阴影近似为描边，仍需要正常加载新 DLL 后验收画面。

该程序另通过正式 Renderer 的配置读取与归档选图验证 `full_map`：省略或设为 `0` 时使用实际进度，设为 `1` 时使用全开地表、地下两层和 DLC 后缀，再切回真实进度。旋转透明模式使用相同路径；输入快照、view 和保存标记字节保持原样。

bridge_verify.dll 可通过上文加载器在指定脱机 mod 会话运行，它直接使用正式 GameFiles 源码。预期取消请求返回 native status=2，独立请求仍成功，四张目标 DDS 共 7,340,624 字节，stop 在请求回收后返回。DLL 仍保留到进程退出。

正式帧对照和实测范围见[执行记录](../docs/minimap-native-implementation.md)。

`minimap_death_render_verify.exe` 使用实际 GFX、图集与已捕获的未回收卢恩状态，连接正式 view 读取、Data 快照和 Renderer，检查 DropSoul 的实际 ImGui 四边形。它不创建游戏窗口、不连接游戏 GPU，不代表最终屏幕画面已验收。

该绘制验证还检查预设切换、四角边距与居中位置，以及圆形、圆角、透明绘制的合成 UV；边距覆盖像素、百分比、负值、零值和留空，另核对视口原点及运行中的分辨率变化。配置字段的缺省、冲突和无效值由 `minimap_settings_verify.exe` 通过真实 INI 读取验证。

宽、高和百分比边距的短边基准通过正式窗口尺寸与位置验证，覆盖四角等百分比、混合单位、负百分比、圆形/旋转，以及横屏、方形、竖屏和超宽屏之间的运行时切换；像素边距沿用原单位。

圆形边框验证直接核对正式绘制命令的 scissor 与边框顶点，包含抗锯齿区域，检查四侧都未被裁切。场景覆盖零宽、细线、小数宽度、粗线和超过半径的配置，以及旋转、贴屏幕边缘、全局窗口边框样式和无离屏目标的路径。

`minimap_progress_verify.exe` 只读取本进程构造的 PARAM/事件存储和已有本地地图资源，不连接游戏。它验证直接与索引两类标记存储、地表/地下/DLC 的参数 ID、位 31、已获得和未获得时的请求后缀、地下两层独立掩码，以及不可读进度的拒绝行为。实际游戏地点另用 `native_log` 的 `map-progress` 和文件请求行对照，不能把该回归结果替代游戏画面验收。

## 历史布局与多图集验证

2026-10-02 的旧 EXE 适配与 mod 图集改动可在本地复核，不需要连接游戏：

~~~powershell
python3 tools/generate_minimap_profiles.py --check --verify-exes
python3 tools/make_minimap_mod_fixture.py
build/native-checks/minimap_compatibility_verify.exe
build/native-checks/minimap_atlas_verify.exe
build/native-checks/minimap_file_verify.exe
build/native-checks/minimap_texture_verify.exe
~~~

先用 VS x64 Native Tools Command Prompt 运行 `tools/build_minimap_native_checks.bat` 编译验证程序，纹理验证需要当前 Release 正式对象文件。生成资源仅写到忽略目录 `build/native-checks/mod-fixture`，不加入分发。

`minimap_compatibility_verify.exe` 将全部 28 条哈希布局送入正式读取代码，覆盖 menu/view、早期赐福、六种 PARAM 目录、保护页短行、死亡与编号，以及 size-gated 旧核心接口。`minimap_atlas_verify.exe` 使用 12 图集和 117 个可见图标检查正式 ImGui 纹理/UV，包含缺失图集、错误 DDS、重复别名、上传失败和重建；无 M10 的删减资源另检查旧版地表/地下选图。

`minimap_file_verify.exe` 直接执行正式 GameFiles 回调，验证 12 个 DDS 提取、逐项结果、原 allocator 单次释放与未知 EXE 拒绝。`minimap_texture_verify.exe` 将 12 张 BC1 图集通过正式 D3D12 多帧上传，逐张 GPU 回读并比较 DDS 块，随后核对全部 SRV 回收；原 BC7 与 fence 回归保留。以上通过只证明代码与本地资源，不代表全部旧游戏已经实测。

## 结果边界

一次请求成功不证明早于挂载的启动时机、返回标题竞态、完整游戏进度变化和跨版本适配。原生素材研究 DLL 的复跑已取得 12 条请求终态，历史试验和复跑见[精选证据](../docs/minimap-game-textures.evidence.json)。本次历史适配只运行本地验证程序，没有重新注入研究 DLL 或部署当前构建。
