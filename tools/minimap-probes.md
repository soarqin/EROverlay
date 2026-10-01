# Minimap 资源验证说明

这些程序用于复现[可行性报告](../docs/minimap-game-textures.md)的资源和 GPU 验证，不是完整 Minimap 实现。静态逆向使用 Python idapro；运行验证不附加调试器、不设置断点、不挂钩游戏函数，也不读取游戏 GPU 纹理。

## 静态分析

在项目根目录运行：

~~~powershell
python3 tools/ida_minimap_research.py --targeted --queries tools/ida_minimap_queries.json
~~~

脚本从 Steam 库和 appmanifest_1245620.acf 定位 EXE，直接读取原文件。数据库、输出缓存和查询配置都按 SHA-256 校验。游戏更新后使用新的 --database 和 --output，重新定位；不能修改哈希后继续调用旧 RVA。

IDA 数据库不应由两个会话同时打开。--request-dir 可以保持一个 idapro 进程处理多个编号查询；向该目录写入包含 stop=true 的请求后保存退出。

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
python3 tools/check_minimap_native_recipes.py
~~~

产物位于 build/native-checks。纹理验证程序使用 build/native 中 Release 的正式对象文件；运行前须让系统可找到 Steam 安装目录中的 steam_api64.dll。该程序只创建自有 D3D12 设备，检查 BC7 上传、回读与 fence 延迟回收，不连接游戏。

bridge_verify.dll 可通过上文加载器在指定脱机 mod 会话运行，它直接使用正式 GameFiles 源码。预期取消请求返回 native status=2，独立请求仍成功，四张目标 DDS 共 7,340,624 字节，stop 在请求回收后返回。DLL 仍保留到进程退出。

正式帧对照和实测范围见[执行记录](../docs/minimap-native-implementation.md)。

`minimap_death_render_verify.exe` 使用实际 GFX、图集与已捕获的未回收卢恩状态，连接正式 view 读取、Data 快照和 Renderer，检查 DropSoul 的实际 ImGui 四边形。它不创建游戏窗口、不连接游戏 GPU，不代表最终屏幕画面已验收。

`minimap_progress_verify.exe` 只读取本进程构造的 PARAM/事件存储和已有本地地图资源，不连接游戏。它验证直接与索引两类标记存储、地表/地下/DLC 的参数 ID、位 31、已获得和未获得时的请求后缀、地下两层独立掩码，以及不可读进度的拒绝行为。实际游戏地点另用 `native_log` 的 `map-progress` 和文件请求行对照，不能把该回归结果替代游戏画面验收。

## 结果边界

一次请求成功不证明早于挂载的启动时机、返回标题竞态、完整游戏进度变化和跨版本适配。最终 DLL 已在本次研究进程复跑，12 条请求均取得终态。历史试验和复跑证据见[精选证据](../docs/minimap-game-textures.evidence.json)；重新运行可获得新的日志。
