# Minimap 使用游戏文件资源的可行性报告

**可行。全部外部底图和 sprite 都有游戏内部资源替代方案。** 已完成「游戏文件加载层读取归档并解压 → 取得 TPF/DDS 字节 → 创建自有 D3D12 纹理和 SRV → 自有 DIRECT 队列采样」验证；补充逆向确认了图标别名、旋转中心、组合图形和圆桌厅处理。游戏纹理句柄、描述符、驻留和游戏资源状态均不需要复用。

本报告对应 2026-10-01 的 Steam 样本，sprite 补充分析于 2026-10-02 完成。提供的外部图集有 93 个 sprite，其中 92 个与内部区域逐像素一致；圆桌厅由内部底图和赐福图标组合。全部素材替换的可行性已确认，完整 Minimap 的持续运行、视觉位置校准、设备重建和卸载仍属于开发验收。具体工作见[开发计划](minimap-game-textures-plan.md)。

## 1. 样本、方法与证据边界

| 项目 | 实际值 |
|---|---|
| EXE 来源 | Steam 安装的原始文件，未修改、未替换 |
| 路径 | D:\Steam\steamapps\common\ELDEN RING\Game\eldenring.exe |
| Steam App ID / Build ID | 1245620 / 25080141 |
| PE FileVersion / ProductVersion | 2.7.1.0 / 2.7.1.0 |
| 标题画面 | 应用版本 1.17.1，规则 1.17.1，脱机模式 |
| 文件大小 | 87,042,128 字节 |
| SHA-256 | 1a3547101327f65d0c76da2f9190ac0aa66871ea42bae2aecc61e11a8b597891 |
| IDA ImageBase | 0x140000000 |
| 静态逆向 | Python 3.14.8、idapro 0.0.10、IDA / idalib 9.4 |
| 另一已安装接口 | ida-domain 0.5.1；本次选择 idapro |
| 运行验证 | 普通 C++ 验证 DLL、独立 D3D12 程序，无调试器、无断点 |

游戏有 Arxan 防调试。后续验证不得附加调试器，不得使用软件、硬件或异常断点。早期调试采样已停止并作废，不作为本报告证据。静态逆向使用 Python 的 idapro；C++ 程序用于正常 mod 调用和 D3D12 验证，没有引入其他反汇编器或归档解包器。

证据区分为「实测确认」「静态确认」「开发验收」。前两者分别表示普通 DLL / D3D12 程序的实际结果，以及当前 EXE 的汇编、RTTI、调用点或项目源码。开发验收表示需要正式 Minimap 接入后验证的场景，不冒充已经测试。

可复查材料：

- [idapro 分析脚本](../tools/ida_minimap_research.py)与[当前样本查询配置](../tools/ida_minimap_queries.json)。
- [精选静态和运行证据](minimap-game-textures.evidence.json)：RVA、关键指令、文件哈希、纹理元数据和验证结果。
- [已读取资源的格式检查脚本](../tools/minimap_asset_inspect.py)：仅解析加载层返回的 CPU 数据。
- [无断点文件读取验证 DLL](../tools/minimap_file_probe.cpp)和[验证说明](../tools/minimap-probes.md)。
- [GFX 帧与图形检查脚本](../tools/minimap_gfx_inspect.py)、[sprite 对照脚本](../tools/compare_minimap_sprites.py)和[完整 sprite 映射证据](minimap-game-sprites.evidence.json)。

大体积 IDA 数据库、游戏资源和 GPU 回读图像保存在忽略目录 build/ida/，不纳入分发包。

## 2. 推荐的数据和渲染边界

~~~mermaid
flowchart LR
    A[瓦片键和游戏进度] --> B[游戏文件加载层]
    B --> C[归档读取和 DCX 解压]
    C --> D[Overlay 持有的 DDS 字节]
    D --> E[Overlay 自建纹理和 SRV]
    E --> F[Overlay 上传队列和 fence]
    F --> G[现有 ImGui 小地图绘制]
~~~

游戏负责虚拟路径、已挂载归档、读取和 DCX 解压。Overlay 负责保留需要的字节、解析容器、选择 DDS、上传、创建 SRV、缓存、绘制和 GPU 完成后的回收。

这条边界消除了游戏 GPU 纹理的引擎引用、placed heap 驻留、描述符、StateBefore 和游戏上传 fence 依赖。Overlay 自有资源的跨队列同步和回收仍需实现，但状态和 fence 均由项目控制。

队列需要准确区分：[D3DRenderer](../src/d3drenderer.cpp) 当前每张图片创建临时 DIRECT 上传队列；Present 绘制使用 commandQueue_，通常从游戏的 ExecuteCommandLists 捕获。Overlay 的命令列表、描述符堆和离屏目标由项目创建。字节路线兼容自建绘制队列，也兼容当前绘制方式，无需为更换素材迁移整个 Present 流程。

## 3. 独立文件字节入口

### 3.1 提交、批量唤醒和回调 ABI

当前样本的入口为 **RVA 0x26EEB60**，manager 指针槽为 **RVA 0x48611A0**。Windows x64 调用方式由汇编及普通 DLL 验证确认：

~~~cpp
int submit(void *manager, uint64_t *outRequestId, const ReadRequest *request);
void complete(uint32_t status, void *context, void *buffer, uint64_t size);
~~~

ReadRequest 为 64 字节：

| 偏移 | 字段 | 验证使用值 |
|---|---|---|
| +0x00 | const wchar_t *path | 生命周期覆盖请求完成的虚拟路径 |
| +0x08 | 游戏 allocator | base + 0x3D8B360 指针槽 |
| +0x10 | uint64_t alignment | 128 |
| +0x18 | complete callback | 四参数完成回调 |
| +0x20 | optional stage callback | nullptr |
| +0x28 | context | Overlay 自有上下文 |
| +0x30 | uint32_t flags | DCX 用 0x40，raw 用 0 |
| +0x34 | uint16_t 参数及对齐填充 | 0；未使用位的语义不作推断 |
| +0x38 | uint32_t 调度参数 | 0；完整优先级策略未验证 |
| +0x3C | 尾部填充 | 0 |

**submit() 只入队，必须调用 RVA 0x26EED40 的批量提交函数。** 它在锁内取走 manager +0xD70 的列表，再交给游戏任务队列。第一次验证遗漏此步，120 秒没有回调；补上后同一批请求完成。因此不能依赖游戏在标题或空闲状态自动提交 Overlay 的请求。

~~~cpp
submit(manager, &requestId, &request);
flush(manager); // RVA 0x26EED40，每批请求之后调用
~~~

实测从普通 DLL 工作线程提交与 flush 成功；完成回调在另一游戏线程执行。请求池、入队和 flush 有锁。正式实现串行化自己的管理逻辑，在 manager 初始化、停止和退出期间拒绝请求；不把高层游戏对象接口一并视为可从任意线程调用。

| 值 | 阶段 | 已确认的含义 |
|---|---|---|
| 0 | submit 返回值 | 已接受，尚未读取完成 |
| -251 | submit 返回值 | 必要参数缺失 |
| -252 | submit 返回值 | manager 正在停止 |
| -253 | submit 返回值 | 请求对象创建失败 |
| -255 | submit 返回值 | 请求池无空槽 |
| 1 | callback status | 成功，buffer 可用 |
| 2 | callback status | 取消；实测 buffer=nullptr、size=0 |
| 4096 | callback status | 本次不存在路径返回此失败状态 |

提交错误由静态代码确认，完成 1/2/4096 均已实际观察。其他错误先保留原始状态，不猜测为权限或解压错误。

### 3.2 字节所有权和取消

RVA 0x26F94C0 在成功时把 request +0x68 的 buffer 放入 R8，并清零原字段；R9 为大小，RCX 为 status，RDX 为 context。**成功回调取得 buffer 所有权。** Hex-Rays 曾误推断七参数回调，汇编和实际调用确认只有四参数。

回调快速复制需要的 DDS / 文件数据到 Overlay 内存，然后用请求中同一游戏 allocator 释放 buffer。当前释放槽为 allocator vtable **+0x68**。不得用 std::free、delete 或 Overlay allocator 释放游戏数据。RVA 0x26F9340 仅回收未转交的 buffer 和游戏请求对象。

取消函数为 RVA 0x26EE2C0。原生调用点通过 RVA 0x26F42E0 将它放入 manager +0x20 的任务队列；函数按请求 ID 的 generation 校验并置取消状态。正式实现使用相同调度边界，不从 Overlay 线程写内部取消标志。

实测取消一条请求得到 status=2，同路径另一独立请求仍正常返回 TPF。取消异步生效，不保证撤回已完成的请求；context、callback 代码和 generation 必须保留到终态和回调排空。验证 DLL 因此保留到研究进程退出。

### 3.3 为何不截留通用 FileCap

高层 RVA 0x1F5560 创建 TpfFileCap，会生成游戏 GPU 纹理。CSFile 又按路径复用 cap，同路径创建另一 cap 可能与游戏已有对象冲突。

通用 FileCap ctor 为 RVA 0x265B8A0，对象 0x90 字节，处理虚函数 +0x50 为空；加载结束会销毁 +0x78 的 processor，不能等状态 4 再取 CPU buffer。RVA 0x1F4F00 是文本列表请求，也不是 raw 入口。

独立异步入口已经验证，避开去重和 buffer 窗口限制，不需要替换游戏 TpfFileCap。

## 4. 实际路径和 CPU 数据格式

### 4.1 路径和压缩不能统一猜测

| 资源 | 成功路径 | flags | 返回内容 |
|---|---|---|---|
| 瓦片索引 | menu:/71_MapTile.tpfbhd | 0 | BHF4，4,359,952 字节 |
| 瓦片 | menutpfbnd:/71_MapTile/MENU_MapTile_M00_L0_20_20_00000000.tpf.dcx | 0x40 | 解压后 TPF，65,790 字节 |
| 变体和可用性 | menu:/71_MapTile.mtmskbnd.dcx | 0x40 | BND4，300,712 字节，四份 XML |
| 公共图集区域 | menu:/Hi/01_Common.sblytbnd.dcx | 0x40 | BND4，402,623 字节，47 份区域元数据 |
| 公共图集像素 | menu:/Hi/01_Common.tpf.dcx | 0x40 | TPF，203,701,084 字节，56 张 DDS |
| 图标帧、变换和矢量图形 | menu:/02_120_WorldMap.gfx | 0 | GFX v11，68,768 字节 |

71_MapTile.tpfbhd.dcx 猜测路径得到 status=4096；索引实际为 raw BHF4。00_Solo 和 71_MapTile 的 sblytbnd 猜测也未找到，公共图集实际在 01_Common 中。已用正向证据替代这些路径猜测。

GFX 路径由 RVA 0xD7D5B0 确认：先尝试 `menu:/Win/<name>.gfx`，再使用 `menu:/<name>.gfx`。当前样本实际成功的是 `menu:/02_120_WorldMap.gfx`；`Win/02_120_WorldMap.gfx`、`01_Common.gfx` 两个候选均失败。GFX 头声明长度 68,763 字节，回调末尾额外 5 字节均为 0；只在声明长度内解析，不将对齐尾部视为标签。

RVA 0x26F9820 检查 flags 的 0x40 位，RVA 0x26F8870 识别 DCX/DCS 并安排解压。回调实测是 TPF/BND4，Overlay 无需再次解压 DCX。TPF 也不能当 PNG 传给 stb。

游戏用 RVA 0xD79300 初始化 71_MapTile，经 TpfbhdMultiMountFileCap 挂到 menutpfbnd。**本次在标题菜单资源初始化后成功读取地表、地下和 DLC，研究工具未打开游戏大地图。** 后续复跑使用同一进程，后来观察到游玩画面，因此不把全部复跑结果描述为「始终未加载存档」。DataN.bdt 的具体归属由游戏文件系统解决，无需自行解密归档。

初始化更早时先等待挂载并重试。RVA 0xE7C180 是可用的原生路径展开候选，但本次成功请求直接使用虚拟路径，不依赖手工构造该对象。

### 4.2 TPF/DDS 与自有上传

| PC TPF 位置 | 用途 |
|---|---|
| +0x00 | TPF\0 |
| +0x04 / +0x08 | 数据大小 / textureCount |
| +0x0C / +0x0D | platform=0，当前 PC 头值为 3 |
| +0x0F bit 0 | entry 起点：未设置为 16，设置为 48 |
| entry +0x00 / +0x04 | DDS 偏移 / 大小 |
| entry +0x08 / +0x09 / +0x0B | TPF format / textureType / mip 字节 |
| entry +0x0C | 名称偏移，本次 UTF-16LE |
| entry +0x10 | 扩展记录数量；entry 固定部分 20 字节 |

本次瓦片 DDS 偏移为 106，大小 65,684。TPF mip 字节=0，DDS mip 数=1。**上传以 DDS 为准，不能把 TPF format=102 当 DXGI_FORMAT，也不能把 mip=0 当无图像。**

已读地表、地下、DLC、L0/L1/L2 和非零变体均为 256×256、2D、arraySize=1、mipCount=1，DDS DX10、DXGI_FORMAT_BC7_UNORM=98。块数据 65,536 字节，64×64 个 BC7 块，每块 16 字节。

保留 BC7 数据，按 GetCopyableFootprints 的行布局上传，再创建同格式资源和 SRV，无需先转换 RGBA8。公共 TPF 还含 DXT1，通用解析器应明确支持 FOURCC/DX10 映射，未知类型拒绝上传。

独立 D3D12 程序在 Intel(R) Arc(TM) B390 GPU 上创建自有 BC7 资源、SRV、DIRECT 队列和 fence，compute shader 采样后回读图像。零变体显示底纸；地表 00008000 和 DLC 00000003 出现地图细节。证明字节与自建纹理路线；ImGui 的滤波、透明度和完整 Minimap 尚待验收。

### 4.3 资源索引、进度变体和遮罩

BHF4 逐 entry 的有界解析确认 **28,469 个文件条目**，没有依赖字符串扫描猜测名单。包含 M00/M01/M10/M11 和额外 L3/L4；首期按游戏正常地图的 L0–L2 实现，不自动支持额外类别。

~~~text
MENU_MapTile_M%02d_L%u_%02d_%02d_%08x
tileKey = L * 10000 + X * 100 + Y
variant = activeMask & allowedMask(tileKey)
~~~

M00、M01、M10 为地表、地下、DLC。名称来自 RVA 0x885450，变体选择来自 0x884030。固定 00000000 已确认不能跟随游戏探索进度。

mtmskbnd 的 M00/M01/M10/M11 XML 记录 key、mask、exists。**它是允许变体和可用性元数据，不是可上传的像素遮罩。** 地表 key=2020 的 mask=33792，即 0x8400；DLC 同 key 为 3。索引可核验组合后的文件名存在，禁止把 allowedMask 当成「全部已解锁」。

WorldMapViewModel +0x39C/+0x3A0/+0x3A4 保存地表、地下、DLC 的 activeMask 缓存。**2026-10-02 修正实现：不再把这三个字段直接当作存档进度。** RVA 0x888860 仅在 view +0x08 未绑定时调用 0x88A0A0 更新掩码；标题流程的历史只读样本中三项均为 0，不能据此判断游戏内所有地图碎片均未获得。

正式实现按 RVA 0x8892C0 的规则读取 WorldMapPieceParam（repository group 88）和事件标记：分别查询 ID 0–31、100–131、1000–1031；每行 +0x04 为事件 ID，已成立时置位 1u << (rowId % 100)。事件 ID -1 转为 0，0 不成立。DLC 的 1070 行属于另一个展开区域流程，不把它折叠为瓦片变体位。游戏自身 reveal 状态非零时才沿用原生 UINT32_MAX 分支；Overlay 不写入该状态。更新线程每 200 ms 重读进度，view/player、参数表、标记管理器或 reveal 状态改变时立即重读。

RVA 0x5FA250 明确事件存储类型：type=1 使用 manager +0x28 的字节池和 stride × index；type=2 直接使用节点 +0x30 的指针；其他类型不提供标记。旧文件桥把 type=2 当作不可读，已修正。读取失败与「标记为 false」分别返回，参数或标记尚不可读时不发布有效地图快照，避免把失败自动解释成未获得地图碎片。

原生瓦片对象有独立 Image_0/Image_1。0x9E1F10 保存两个地图类别及各自掩码，0x9E07B0 分别创建资源，0x9E1C50 单独控制 Image_1 可见性。**地下模式需要先画 M00 地表底层，再画 M01 地下层，不能只画 M01。** 本次 M01 零变体样本为黑色、alpha=184；它是半透明覆盖层，缺少底层时合成结果不正确。地表和 DLC 继续使用各自进度变体。

「未搜索」遮罩是另一套对象：0x9CCC10 将 _/Base/UnsearchedMask/Layer_0、Layer_1 与 _/Base/Layer_0 瓦片层分别绑定。当前修复已补齐进度选图和地下底层；雾精灵、遮罩渐变与解锁淡入的逐帧动画仍需单独验收。不能把必需的地图底层合成列为可选动画。

同坐标回归已连接正式进度读取和瓦片请求代码：地表 key=2020、activeMask=0x8000 时请求 00008000；清除标记后请求 00000000；地下同一帧分别使用地表和地下掩码请求两层。2026-10-02 用户已实测确认地图碎片获得状态同步正常；实际只读采样中 view 缓存与按事件计算的地表掩码均为 0x000100EF。该确认不覆盖死亡标记及其他生命周期验收。

## 5. 世界坐标、缩放层和游戏对象

### 5.1 每层的世界跨度

RVA 0x8859D0 汇编明确世界跨度：

| 层 | 单瓦片世界跨度 | 每轴格子数 | 本次 DDS 尺寸 |
|---|---|---|---|
| L0 | 256 | 41 | 256×256 |
| L1 | 342，即 0x156 | 31 | 256×256 |
| L2 | 1288，即 0x508 | 9 | 256×256 |

~~~text
X = floor(mapX / span)
Y = axisCount - 1 - floor(mapY / span)
left = X * span
top = (axisCount - 1 - Y) * span
worldRect = {left, top, left + span, top + span}
~~~

按轴范围限制索引，再以 exists/归档条目决定绘制。screen zoom 常量 0.19875777、0.748538…、2.25 不等于世界跨度比。Scaleform 每张图片的 256 显示格子不等于 L1/L2 世界尺度。

### 5.2 锚点和 DLC 平移

RVA 0x8770F0 设置转换器，0x877130 转换坐标。ctor 0x8865A0 为 m60、m61 设置 originGrid=(28,64)、anchor=(0,0,0)、displayOffset=(128,128)、scale=1。运行快照与静态调用一致：

~~~text
mapX = (gridX - 28) * 256 + 128 + localX
mapY = (64 - gridZ) * 256 + 128 - localZ
~~~

**128 是加上的显示 offset，不是需要再减的 anchor。** 当前 [Data](../src/minimap/data.cpp) 的基本公式与游戏一致。

DLC m61 使用同一原生尺度，M10 瓦片无需外部拼图的 3035/1864 平移。玩家和标记同时切换到原生坐标时，同时移除两处外部偏移。标记的 1024 单位分桶可以保留为独立空间索引。

WorldMapLegacyConvParam 的 196 行已只读取得，复杂场景通过 isBasePoint 和连接关系转到 m60/m61。不能硬编码地下城偏移，也不能把所有转换行视为相同优先级。

### 5.3 上下文和生命周期

指针关系由 ctor、初始化、更新和运行快照确认：

~~~text
menu = *(base + 0x3D6F820)
owner = *(menu + 0x80)
backreader = *(owner + 0x248)
viewModel = *(owner + 0x250)
Location = viewModel + 0x24
~~~

游戏更新 0x7EF6D0 先 poll backreader 再更新 viewModel；本次标题已有对象。更早初始化、返回标题和重建必须逐级空检查，并用 generation 让旧快照失效，不能跨读档持有裸指针。

M00/M01/M10 与 Location::mapId、underground 不是同一个枚举。开发中构造明确的地图上下文，把位置、地下层、activeMask 作为一个快照；矩形、圆形和旋转模式共用 worldRect/UV。

## 6. 全部 sprite 的内部资源替换

### 6.1 外部像素与内部区域逐项对照

对照源为提供的 `src/minimap/data/map/atlas.json` 与 `atlas.png`。JSON 有 93 个 sprite：89 个编号精灵，以及 Player、Arrow、Bearing、Roundtable。公共区域文件有 47 个 TextureAtlas、4,097 个 SubTexture；内部像素来自已读取的公共 TPF。

独立 D3D12 程序保留 BC7 解码后的 RGBA，包括 alpha，再逐区域比较。**92 个 sprite 的尺寸和全部 RGBA 字节完全一致；唯一非单张等价素材是圆桌厅组合图。** 本结论有逐 sprite 的像素 SHA-256，不依赖肉眼相似度。

| 外部名称 / 用途 | 内部资源 | 区域 x、y、w、h | 对照结论 |
|---|---|---|---|
| Player / 现有玩家中心 | SB_MapCursor / MENU_MAP_Host.png | 从 XML 读取，74×76 | 完全一致；之前候选 Player_02 不是外部 Player 的原图 |
| Arrow / 朝向外圈 | SB_MapCursor / MENU_MAP_Player_01.png | 1931、432、72、150 | 完全一致 |
| Bearing / 罗盘 | SB_MapCursor / MENU_MAP_Bearing.png | 0、388、284、328 | 完全一致 |
| 01 / 赐福 | SB_MapCursor_02 / MENU_MAP_01_Bonfire.png | 718、524、156、156 | 完全一致 |
| 03 / 教堂 | SB_MapCursor / MENU_MAP_Church.png | 从 XML 读取，140×188 | 完全一致；GFX 第 3 帧确认别名 |
| 15 / 地下墓地入口 | SB_MapCursor / MENU_MAP_Ungro.png | 从 XML 读取，206×130 | 完全一致；GFX 第 15 帧确认别名 |
| 其余编号 sprite | 按 GFX 帧解析到内部图片 | 从 XML 读取 | 全部完全一致 |
| Roundtable / 圆桌厅 | SB_Chara / MENU_FL_Hub.png，加 SB_MapCursor_02 / MENU_MAP_48.png | Hub 为 0、558、400、400 | 内部素材组合，见 6.4 |

需要保留四张 DDS：SB_MapCursor 为 2048×1024，SB_MapCursor_02、SB_MapCursor_03_dlc 为 1024×2048，SB_Chara 为 1024×1024。均为 BC7_UNORM、单 mip，块数据总计 **7 MiB**。公共 TPF 约 194 MiB，一次提取这四张即可，不上传全部 56 张。

这四张图集的 SubTexture 都是 half=0。GFX 位图宽高与 XML 裁剪尺寸一致，正式接入仍逐项校验；不把其他图集的 half 规则套到当前地图精灵。

### 6.2 游戏如何处理别名

**游戏并不把每个 iconId 都格式化为 MENU_MAP_%02d.png。数字选择的是 GFX 电影片段的帧。** 当前样本的实际调用关系：

~~~text
WorldMapPointParam iconId / altIconId / distViewIconId
  -> WorldMapPointPinData::GetIconId，RVA 0x87CF10
  -> WorldMapPinData::SetTo，RVA 0x87BE10
  -> 查找 WorldMapItem 的 Icon_0
  -> 0x74CB10 -> 0x74AB30 -> 0x74A7D0
  -> Scaleform 按一基 frame=iconId 选择帧
  -> DefineExternalImage2 的 export_name
  -> TextureAtlas SubTexture 的名称去扩展名 -> atlas 区域
~~~

0x74A7D0 通过对象接口 +0x148 传入原数字；0xD84320 把引擎的零基当前帧加 1，确认编号没有隐藏的 +1 偏移。`WorldMapItem` 为 character 174，`Icon_0` 为 character 171，其 `DefineSprite` 从 GFX 偏移 `0x25C0` 开始，声明和实际都有 **348 帧**。

| iconId / GFX 帧 | 实际绑定 | 关键记录 |
|---|---|---|
| 1 | MENU_MAP_01_Bonfire | character 169，放置标签 0x25CA |
| 3 | MENU_MAP_Church | character 168，放置标签 0x260C |
| 15 | MENU_MAP_Ungro | character 157，放置标签 0x2737 |
| 48 | MENU_MAP_48 | character 129，放置标签 0x2A35 |
| 103 | Church 加 MENU_MAP_80_Add | 第 103 帧保留/更新两个 depth，不能直接格式化名称 |
| 107 | MENU_MAP_35 加 MENU_MAP_80_Add | 不是「107 减 100 等于 07」；映射必须来自帧 |
| 348 | MENU_MAP_248 加 MENU_MAP_80_Add | DLC 状态组合，第 348 帧 |

GFX tag 1009 的原生加载器是 RVA 0x11E5890；标签格式由原生读取顺序和日志字面量确认。RVA 0x11BC9F0 确认标签长度，0x11BF220 确认 PlaceObject3，0x11BDBC0 确认 16.16 矩阵。TextureAtlas parser 0xD665A0 调 0x120DA0 去除目录和扩展名；0xD67F20 再查 imagePath 和区域。因此 `MENU_MAP_Church.tga` 也不是需要独立加载的 TGA 文件，它指向图集内 `MENU_MAP_Church.png` 的逻辑图片。

已检查相关 ActionScript 帧方法：Timeline_23、PC__48、PC__49 的 frame1 只调用 `stop()`，初始化器注册该帧方法。别名保存在帧的显示列表中，恢复素材不需要执行这些脚本。完整 JSON 保留帧号、character、标签偏移、depth、矩阵和图片名称，可在当前样本复查。

### 6.3 旋转中心、大小与状态图形

GFX 的矩阵定义了原生图片位置；XML 仅有裁剪矩形。对单张图片，定义 `p = A × pixel + t`，则像素坐标中的原点为 `pivot = -inverse(A) × t`。不能全部使用图片中心，也不能忽略负缩放。

| 图形 | 原生矩阵 / 推导 | 接入含义 |
|---|---|---|
| Player_01 / Rotate | scale=0.5，t=(-15.95,-43.7) | pivot=(31.9,87.4)；外部 (32,87) 是近似值 |
| Player_02 / Fix | scale=0.5，t=(-10.5,-15.5) | 自身 pivot=(21,31)，父 Fix 另有 (2.25,2.75) 平移 |
| 外部 Player / Host | 像素为 Host，外部 anchor=(37,38) | 默认兼容外部中心图；可保留其构图而使用内部像素 |
| 地标 3 | scale≈0.54，t=(-39.55,-60.7) | 各地标的 pivot、比例均从帧得到 |
| 地标 83、84 | 两轴 scale=-0.5 | 自带 180° 翻转；再合成参数 angle 和地图旋转，不能重复 +180° |
| 禁止传送 | 在普通赐福上增加 Shape4 | 自有 ImGui 线条即可绘制 |

原生玩家是 Rotate 与 Fix 两层，Fix 位于较高 depth；当前 Overlay 则先画 Host，再画 Arrow。**替换现有像素默认使用 Host + Player_01 的既有构图**，将 Arrow 旋转中心改为原生矩阵值。若改成原生 Player_02 双层构图，应作为另一次视觉变更验收；不把它混同为必要的素材替换。

当前 Location::oriDeg 已由游戏执行「yaw 转角度，再 +180°」；地标 angle 由 0x87BE10 原样传给旋转函数。旋转函数按 360° 取模。实现应将 `frameMatrix`、参数角度与 `mapRad` 按顺序组合，并移除现有「非零地标角度额外 +180°」的统一规则。

WorldMapPointParam 472 行中，当前筛选得到 393 个基础标记、87 种基础 iconId；全部 87 种都有内部帧定义，3/15 的缺口为 0。继续检查 altIconId 和 distViewIconId，以及 BonfireWarpParam 422 行中的 normal、forbidden、alternate、alternate-forbidden 字段，共覆盖 **105 个非零编号候选，全部可以恢复为内部位图或矢量组合**。这 105 个是资源覆盖候选；字段存在不等于当前事件条件满足。

证据文件保留 106 个编号 recipe：上述 105 个候选，以及仅为对照外部 atlas 而保留的 ID=80。ID=80 被当前 Overlay 的基础标记筛选排除，两个计数的范围不同。

0x87CF10 先按发现/开启事件选择 distant 图标，否则由 0xD59D80 判断可显示文本是否含 type=1，再选择 altIconId。0xD5A220 先确认文本 ID 非负，再检查 enable/disable 事件；两个 enable 条件都须通过，disable 判定有自己的两字段逻辑，不能凭字段名猜测。实际显示应读取游戏已选择的 iconId 快照，或逐项复现已经逆向的谓词，并沿用 Overlay 的标记筛选。

赐福 normal=1/48、forbidden=2/49、alternate 包含 101、alternate-forbidden 包含 102。第 2/49/102 帧的禁止标记都包含内部 Shape4 character 170：RGBA=(204,0,0,255)，宽 2 个原生 UI 单位，端点 (-17.85,-14.3) 到 (17.6,13.75)。第 49 帧额外平移 (-1,3.25)。组合恢复必须重放此前帧的移除/保留/移动，并按 depth 排序；第 102 帧只增加斜线，已有图片与附加图仍保留。

### 6.4 圆桌厅的完整内部替代

外部 `Roundtable` 是 **内部 Hub 底图加内部赐福 ID=48 的成品组合**。用 Hub 在 400×400 画布平移 (-1,-2)，叠加 ID=48 于 (118,109)，与外部图在黑、灰、白背景合成后，各 RGB 通道平均误差为 0.51–0.63 / 255。它不是 RGBA 完全相同的单张图；微小差异和裁剪属于外部成品，正式实现使用游戏原生布局。

原生 `Body/_/Base/Home` 为 character 198：

1. depth=1 放置 character 197，其填色 alpha=0、没有描边，完全透明，只扩展布局边界。
2. depth=2 放置 `MENU_FL_Hub`，scale=0.5，t=(-98.15,-100.15)，显示 200×200。
3. 赐福作为独立 WorldMapWarpPinData 绘制。0x9BF830 从 GameSystemCommonParam 第 0 行 +0x278 读取 Home 对应的 BonfireWarpParam ID；实测为 **111000**，基础 iconId=48，地图为 m11_10_00。
4. 0x9C96D0 把该赐福位置改到 Home 图形所在的地图视图；0x9C4B20 随后将 Home 图形移到同一位置。Player 位于 Home 之后的更高 depth。

0x9C96D0 的位置计算已经还原为：

~~~text
B = Home 的布局 bounds，P = Home 的初始 x/y
V = 当前 map viewport 对应的 view rectangle
homeView = (V.left + P.x - B.left, V.bottom + P.y - B.bottom)
homeMap = (viewOffset + homeView) / viewScale
homeWarpPin.position = homeMap
~~~

透明 bounds 与 Hub 图片 bounds 取并集，故不能只用 400×400 图片的中心代替 Home 原点；RVA 0x9CD710 和 0x9CE2F0 分别确认正反转换。原生 Home 按视口边缘布局，**不存在一个可复用的固定瓦片坐标**。

RVA 0x888860 控制死亡位置标记：死亡记录的 map ID 与 Home 参数对应 map ID 相同时，将可见标记字段 +0xA9 清零。0x9BF830 将 `Body/_/Base/Dead` 绑定到 +0xCA8，0x9C4B20 根据 +0xA9 控制该对象，确认这里处理的是死亡标记。玩家原始 map ID 由 0x887C30 写入 WorldMapViewModel +0x14，迁移后的快照读取此字段；不能把死亡记录当作当前玩家位置。

Overlay 比较玩家原始 map ID 与 Home 参数地图来判断进入圆桌厅，显示自有圆桌厅视图，以内部 Home/赐福图形提供内容，并保留玩家位置图标。当前旧范围 X=2740..2940、Y=7510..7710 以及替代位置 (800,8319) 来自外部拼图，应删除。

另有实际源码问题：提供的 atlas 名称为 `Roundtable`，两条 render 路径查找 `RoundTable`，`unordered_map` 区分大小写，因此旧圆桌厅图片根本查找失败。迁移采用语义 ID，彻底移除这一大小写依赖，不保留外部精灵。

### 6.5 可行性结论与开发边界

全部 sprite 的素材来源、别名、原生变换及组合方式均已确认；**无需任何外部像素回退**。推荐运行时依次读取 GFX、公共区域 BND 和四张目标 DDS，生成 Overlay 自有 `IconRecipe`；图片用自有 SRV 绘制，线条用 ImGui 绘制。

研究完成后，已于 2026-10-02 按计划开始运行时代码迁移，并加入内部死亡标记。已完成的代码、验证结果和游戏事件验收范围见[执行记录](minimap-native-implementation.md)。素材和别名结论不依赖这些后续验收。

死亡图形从 `Body/_/Base/Dead` 取得：character 205 使用 `MENU_MAP_DropSoul`，图片为 86×92，scale=0.5，translation=(-23.15,-23.15)，pivot=(46.3,46.3)。状态读取 WorldMapViewModel +0xA9、+0xAC/+0xB0、+0xB4，分别对应有效性、转换后坐标和地图类别。默认在普通地标之上、玩家之下绘制。

2026-10-02 死亡隐藏修正：玩家地下状态 view +0x30 只有 U8，RVA 0x887F37 的 mov [rdi+30h], al 已确认。实际未回收卢恩时，该处字节为 00 60 A6 32，死亡有效性为 1、类别为 0。旧 int32 读取令布尔判断误判地下，隐藏本来可见的地表死亡标记。实现已归一化该 U8，并用当前展示类别比较死亡类别；正式绘制回归用该实测状态确认 DropSoul 四边形进入 ImGui 列表。游戏画面和实际回收事件的最终验收见执行记录。

## 7. 旧 GPU 复用路线

静态仍确认 repo 槽 0x3D77EC8 → lookup 0xB820C0 → entry +0x78 → 虚函数 +0x20 → CG +0x20 → ID3D12Resource。资源 IID 与 SDK 相同。

此路线依赖引擎和 COM 引用、延迟销毁、驻留及游戏队列状态；仅 AddRef 不够。未测其同步契约。按当前需求保留为研究备选，正式方案不依赖；这些 GPU 未知项由字节路线规避。

## 8. 不确定项的处理结果

| 问题 | 状态和结论 | 证据或后续通过条件 |
|---|---|---|
| 游戏文件能否交出独立图片字节 | 实测确认 | 地表、地下、DLC、多个层得到 TPF/DDS |
| 是否必须打开大地图 | 本次否 | 标题初始化后可读取，研究工具未打开大地图 |
| 入队后不完成 | 已解决 | 显式 batch flush |
| 是否解压 DCX | 实测确认 | 0x40 返回 TPF/BND4 |
| 是否所有文件追加 .dcx | 已解决 | 索引 raw BHF4；图片、区域、mask 有 .dcx |
| callback ABI 和 buffer 所有权 | 汇编与实测确认 | 四参数、成功转交、相同游戏 allocator 释放 |
| 同路径请求会否替换游戏 cap | 独立入口规避 | 同路径独立取消和正常请求均得到终态 |
| 取消是否同步 | 已确认是异步 | 游戏任务队列、status=2、context 保留 |
| 自有纹理和独立队列是否可用 | 实测确认 | BC7 资源/SRV/DIRECT/fence 采样回读 |
| 尺寸、格式、mip | 样本已确认 | 瓦片 256×256、BC7_UNORM、1 mip；仍逐资源校验 |
| 后缀能否固定 0 | 已排除 | 0 是底纸，非零变体出现细节；使用进度掩码 |
| 稀疏瓦片和允许变体来源 | 已确认 | mtmskbnd XML、BHF4 entry |
| L1/L2 跨度、Y 方向 | 静态确认 | 342/1288、31/9、反向 Y；实景接缝验收 |
| 128 与 DLC 偏移 | 静态与快照确认 | 128 为 offset；移除外部 3035/1864 |
| 挂载过早、返回标题、退出竞态 | 开发验收 | 等待、generation、取消排空，无悬空调用 |
| 游戏 GPU 驻留和 fence | 主路线规避 | 不复用游戏 GPU 句柄 |
| 自有动态纹理同步和回收 | 开发工作 | 上传 fence、frame fence、SRV 延迟回收 |
| 3/15 和特殊图标别名 | 已确认 | GFX 帧直接绑定 Church/Ungro；完整映射覆盖所有基础标记 |
| 原生 pivot、方向和比例 | 已确认 | GFX 矩阵；83/84 自带翻转，状态图按 depth 组合 |
| 圆桌厅内部素材与游戏布局 | 已确认 | Hub + ID=48、Home 参数 111000、视口相对布局 |
| 全部外部图集删除 | 素材可行性确认 | 92/93 完全一致；圆桌厅内部组合；接入后移除外部依赖 |
| 地图雾、层间合成和淡入 | 部分确认 | 底图变体已确认；动画另做绘制验收 |
| 持续耗时、显存收益、冷传送延迟 | 尚未测量 | 完整 Minimap 后测量，不给猜测收益 |
| 跨版本签名 | 当前样本限定 | 哈希与入口字节校验，新版用 idapro 重定位 |

## 9. 进入开发的条件

资源入口、CPU 格式、自有上传、图标映射和圆桌厅替代均已得到正向证据，**可以按全量内部资源目标开发**。当前代码迁移已开始；构建、解析和资源验证不等于完整游戏事件验收。

旧 [纹理 API](../src/api.h) 仍保留 stb → RGBA8 接口以兼容旧插件。原生 Minimap 使用独立版本化扩展，已接入 DDS 异步上传、预算缓存、上传后发布、绘制 fence 和延迟回收。

首期按已验证路径实现，保持旧插件 ABI，提供就绪等待、失败重试和明确错误状态。最终版本不分发、不读取外部 atlas 或地图 PNG。未知 EXE 版本禁用当前 RVA；本报告入口字节不是跨版本通用签名。
