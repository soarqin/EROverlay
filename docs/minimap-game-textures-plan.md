# Minimap 原生文件资源开发计划

目标：使用游戏文件加载层读取底图 DDS、图集区域与 GFX 帧定义，由 EROverlay 创建和管理纹理、SRV、上传与绘制，**替换全部外部底图和 sprite，并显示死亡位置及玩家编号标记**。依据[可行性报告](minimap-game-textures.md)与[完整 sprite 映射](minimap-game-sprites.evidence.json)，于 2026-10-02 补充并开始实施。

## 实施顺序与交付结果

2026-10-02 已开始实施：独立文件桥、自有 DDS 上传、统一原生 L0 瓦片、GFX 图标、圆桌厅和死亡标记已接入代码，取消/停止与 GPU 延迟回收已有运行证据。本次根据未展开底图反馈，修正事件标记 type=2 读取、改用参数和事件重建 activeMask，并补齐地下的地表底层。Debug / Release 构建、正式解析器对照和进度选图回归已通过。真实死亡、回收卢恩、读档、传送及各模式的视觉验收继续按 P6 执行；L1/L2 尚未默认开放。

| 阶段 | 实际工作 | 可检查的结果 | 前置阶段 |
|---|---|---|---|
| P1 | 接入已验证的独立文件请求，补齐取消和停止流程 | 标题菜单即可取得指定瓦片字节，卸载不留下回调 | 无 |
| P2 | 将 TPF 中的 DDS 变成 Overlay 自有纹理 | BC7 图像通过现有 ImGui API 绘制，上传不阻塞 Present | P1 |
| P3 | 用原生瓦片替换两条小地图底图绘制路径 | 矩形、圆形、旋转模式显示同一批正确位置的瓦片 | P2 |
| P4 | 跟随地下层、DLC、探索进度和读档更新缓存 | 传送、地图解锁、返回标题后不显示上一批地图 | P3 |
| P5 | 从 GFX 恢复全部图标、旋转中心、圆桌厅组合、死亡和编号标记 | 玩家、赐福、地标、罗盘、圆桌厅、死亡位置与编号箭头使用内部素材 | P1/P2；圆桌厅与标记坐标接入需 P3 |
| P6 | 验收完整内部资源模式并清理外部分发文件 | 删除 data/map 运行依赖；缺失资源按状态重试 | P4/P5 |

P1/P2 可以基于本次正向证据直接开始。首期地图使用 L0；L1/L2 在 P3 的位置和接缝验收后开放。GPU 资源共享路线不作为阶段前置条件。

素材替换的依据已齐全：93 个外部 sprite 中 92 个逐像素相同，圆桌厅为内部 Hub + 赐福 ID=48。别名保存在 GFX 帧定义中；后续工作的重点是实现自有资源生命周期和图形绘制，不再保留「找不到同名图片则继续读外部 atlas」的分支。

## P1：取得拥有独立生命周期的文件字节

建议新增游戏文件适配器，与 [hooking](../src/hooking.cpp) 的版本定位和状态读取整合。对外提供 request / cancel / poll / stop，不公开裸 manager、请求对象或游戏 allocator。

1. 注册当前 EXE 哈希和入口信息，校验 0x26EEB60、0x26EED40、0x26F42E0、0x26EE2C0 及 manager / allocator 槽。未知版本禁用适配器并返回明确状态；不能仅替换哈希继续使用旧 RVA。
2. 等待游戏文件系统、manager 和 menutpfbnd 挂载。先请求 menu:/71_MapTile.tpfbhd 验证就绪，未就绪保留重试状态；首次大地图打开不作为加载前提。
3. 在 Overlay 的单一文件请求线程调用独立 submit，并在每批后 flush。区分 submit 错误、读取失败、取消、超时和成功；记录路径、generation、requestId、原始 status、耗时。
4. 回调只检查结果、保留需要的字节并发布完成项。不得在回调里调用 D3D12、访问 ImGui、长时间写大文件、同步等待其他请求或递归取消；它在游戏请求锁内运行。
5. 用同一游戏 allocator 的 +0x68 槽释放成功转交的原 buffer。复制后返回 Overlay 自有字节块；不得让裸 buffer 或 allocator 生命周期穿过游戏文件系统停止。
6. cancel 调度到 manager +0x20 的游戏任务队列。generation 失效后丢弃旧结果，仍执行正确的 buffer 释放。请求状态从 Pending 进入 Succeeded / Failed / Cancelled，终态只发布一次。
7. stop 先禁止新请求，再发取消，处理终态并排空回调和排队任务，最后销毁线程及 callback 所属模块。超时不能强行卸载仍被游戏引用的代码；报告停止状态并等待终态，不靠固定 Sleep 保证安全。

已实现的结束确认来自 RVA 0x26F9340 → 0x26F4FD0：回调返回后，游戏回收请求并推进请求池 generation。文件桥在回调终态和该 generation 变化都出现后释放上下文；不以回调函数内部的完成标记单独证明函数已经返回。当前请求池由 base +0x4860D70 间接取得，实测为 manager +0xF0。

为避免晚回调进入已卸载的 Minimap.dll，建议文件桥接和 callback 放在核心 EROverlay.dll 中，插件仅拥有订阅 ID。插件销毁先取消订阅，核心在自身卸载时排空游戏任务。不要照搬研究 DLL 的「进程退出前常驻」策略作为正式卸载设计。

请求上下文至少包含 ownedPath、gameGeneration、requestId、终态标记、取消标记和完成数据。所有权流程必须在接口中写清，不使用 CSFile 同路径 cap 去重或 TpfFileCap 的处理窗口。

通过条件：不开大地图读取一张地表、一张地下、一张 DLC；同路径两请求互不影响；取消一条仍得到终态；连续初始化/停止后无残留回调、无游戏崩溃。

## P2：用 DDS 创建和回收自有纹理

修改 [D3DRenderer](../src/d3drenderer.hpp) 的纹理管理和[插件 API](../src/api.h)，实现可异步轮询的纹理扩展接口。

建议接口作为独立的、带 size/version 的纹理扩展表，通过新导出获取。现有 EROverlayAPI 和 TextureContext 的字段、偏移及旧导出保持兼容。新插件用 GetProcAddress 检查扩展是否存在；简单追加函数指针但让新插件读取旧核心结构尾部，不能称为双向 ABI 兼容。

接口语义：

~~~text
createTexture(DdsImage) -> TextureToken
pollTexture(TextureToken) -> UploadPending / Ready / Failed
getTextureView(TextureToken) -> 自有 SRV 和像素尺寸
retireTexture(TextureToken) -> 排队回收，仍等待实际 GPU 完成
~~~

DdsImage 用经校验的格式、尺寸、mip 和子资源字节描述；它是建议接口，不是游戏 ABI。调用方的数据保留到核心复制完毕，不能传短生命周期的 TPF 视图。

1. 对 TPF entry 偏移、长度、名称、扩展记录和 DDS 头做有界校验；格式参数来自 DDS。先支持 BC7_UNORM 和本次出现的 DXT1，扩展 BC1/BC3/SRGB 时补相应映射。数组、cube、volume 和未知格式暂时明确拒绝。
2. 新建持久 DIRECT 上传队列、fence 和批量 command list。用 GetCopyableFootprints 上传 BC 块和各 mip；初始状态为 COPY_DEST，上传结束后转入 PIXEL_SHADER_RESOURCE。纹理全程由 Overlay 拥有。
3. 上传缓冲区和 allocator 在 uploadFence 完成后复用。Ready 只能在上传 fence 完成后发布，或者由绘制队列 GPU Wait 显式等待；不允许 render 先拿 SRV 再猜测上传已结束。
4. 为当前 Present 命令提交增加持续 frameFence。每个 backbuffer allocator 复用前检查对应 fence；不足时跳过本次 overlay 绘制或复用另一个已完成槽，避免动态瓦片上传造成 CPU 无限等待。
5. TextureToken 使用 generation；记录它最后一次实际绘制对应的 frameFence。retire 需同时等待上传和最后绘制完成，再释放 ID3D12Resource、上传对象和 SRV 槽。
6. 本堆容量为 1024，SRV 分配失败返回可恢复错误，为字体、其他插件、图集和离屏目标预留空间。动态瓦片只按可见范围缓存，不能常驻全图。
7. 设备丢失、ResizeBuffers、离屏目标重建和卸载统一处理队列与 token 失效，不能让另一个插件持有已复用的 SRV。

上传和绘制可以使用不同 DIRECT 队列，同步仅涉及自有资源和自有 fence。当前字节方案无需探索游戏贴图的 StateBefore；swapchain 的既有同步仍按现有绘制流程处理。

通过条件：实际 DDS 经现有 ImGui 显示；批量上传期间 Present 无 Wait(INFINITE)；纹理释放后 SRV 不会提前复用；旧 Boss/Achievements 插件仍可加载和绘制。

## P3：统一原生瓦片的坐标和绘制

重构 [Minimap Renderer](../src/minimap/render.cpp) 中 prepareTile() 和 renderRotatedTile() 的独立 PNG/JPG 加载逻辑，使用同一个瓦片提供者。

~~~text
NativeTileKey = {M, L, X, Y, variant}
TileView = {NativeTileKey, worldRect, uvRect, TextureToken}
MapSnapshot = {gameGeneration, position, nativeMap, activeMask}
~~~

世界矩形、DDS 像素尺寸、UV 和 SRV 分开存储。原生 L0/L1/L2 世界跨度分别为 256/342/1288，轴为 41/31/9；不是用图片 Width 替代跨度。Y 编号按 axisCount - 1 - floor(mapY/span) 计算。

1. 先以 L0 替换地表底图，按可见世界矩形请求一圈相邻瓦片。矩形和旋转模式只改变几何变换，不重复请求或创建纹理。
2. 圆形、圆角和透明度继续使用现有离屏合成；UV 与几何裁剪共用 TileView，不引入原生 Scaleform 图像对象。
3. 以地表相邻边、赐福坐标和移动越界核对 origin=(28,64)、+128 offset、Y 翻转和边缘 clamp。对负坐标使用 floor，不能用整数截断代替。
4. 原生 DLC 使用 m61 游戏坐标，移除 render 中的 3035/1864 外部平移；同步去掉 [Data](../src/minimap/data.cpp) 标记位置上的外部平移。
5. 标记继续按独立的 1024 单位索引查询可见范围，并按标记 ID 去重。换成 256 瓦片后不得按瓦片逐次查询同一个桶，导致漏标记或重复绘制。
6. L1/L2 根据屏幕像素密度和预算选择，验收切层尺度和接缝后再开放。归档中的 L3/L4 不自动作为通用层使用。
7. 添加明确的 MapPresentation::Roundtable。读取 GameSystemCommonParam 第 0 行 +0x278 对应的 Home BonfireWarpParam，并比较玩家原始 map ID；当前为 111000 / m11_10_00。快照增加原始 map ID 与展示类型，移除旧坐标范围及 (800,8319) 外部替代位置；圆桌厅内容由 P5 的内部图形绘制，不再请求虚构的瓦片位置。
8. 地下模式按原生 Image_0/Image_1 关系先画 M00，再画 M01，两层使用独立进度掩码和共同世界矩形。此次已接入；实际地下画面与透明度仍须 P6 验收。

通过条件：同位置的三种形状一致；移动跨过四个方向的瓦片边缘无半格跳变；两个层切换后赐福位置不漂移；标记不重不漏。

## P4：跟随地图进度和生命周期更新缓存

用 mtmskbnd 的 key/mask/exists 和 BHF4 实际名称构建只读瓦片目录，用游戏 MapSnapshot 的 activeMask 选择 variant。

1. 路径按已验证格式形成，variant = activeMask & allowedMask。核验目录存在再请求；没有当前变体时明确返回缺失，不改成「全部展开」。
2. 地表/地下/DLC 的资源类别独立于 Location::mapId，使用 nativeMap。位置、类别和掩码一起发布快照，避免旧地表掩码被用于新 DLC。
   - activeMask 按 WorldMapPieceParam（group 88）和事件标记重建，读取规则与 RVA 0x8892C0 一致。view +0x39C 的缓存只作诊断，不直接决定进度。
   - 修正 type=2 直接存储的事件读取；区分不可读与未成立。参数或进度不可读时等待有效快照，不请求伪造的零变体。
3. 按 viewport、缩放和旋转包围矩形计算需要的瓦片，给定 CPU 字节、显存、SRV 和并发请求预算；使用 LRU 淘汰离开范围的 Ready 项。
4. 传送和变体改变时使旧请求 generation 失效。新纹理 Ready 后替换旧图，再经 frameFence 回收旧资源；短暂未就绪显示上一张可用地图或空白区域的策略需按相同地图上下文实施，不能显示其他存档的地图。
5. 返回标题、重新读档、DLC 切换、核心重建时清空地图快照并取消请求；重新就绪后重建目录和缓存。反复失败带重试间隔，不用「loaded=true」永久锁死。
6. 文件 adapter 停止和 D3D adapter 停止分开处理：CPU 请求排空之后仍需等实际 GPU 提交完成，最后释放插件对象。
7. 添加可选 `full_map=1`，只在 Renderer 的选图掩码中将地表、地下和 DLC 视为碎片全部获得。默认值为 `0`，继续跟随真实进度；游戏快照、存档、事件标记和地标发现状态保持原样。全开仍使用 `UINT32_MAX & allowedMask` 并核对目录，切回真实进度时沿用掩码变化的缓存清除。

实现将 GFX、XML 和目录解析放在 update 线程，render 只读取已发布定义、查询纹理就绪状态并提交绘图。公共 TPF 回调用不抛异常的进程堆复制目标 DDS；运行时区域和 TPF/DDS 校验失败时显示状态并拒绝上传。

正确底图变体和地下底层合成作为首期效果。雾精灵和淡入另行验收透明边、未探索效果和图层顺序；地图碎片已获得时不得用未展开底纸作为读取成功的最终地图。mtmskbnd 本身只是元数据。

通过条件：首次不开大地图显示当前位置；关闭大地图后持续显示；获得地图碎片刷新；冷传送、返回标题换档和停止/重启不显示旧 generation。

2026-10-02 已接入全开选项。当前资源目录中，L0/L1/L2 共 3,710 个存在的瓦片均有全开变体；默认仍使用 L0。正式 Renderer 回归覆盖省略/开启/关闭选项，地表、地下两层和 DLC 的请求后缀，返回真实进度及旋转透明模式。

## P5：全部图标与圆桌厅使用内部资源

### P5.1：生成可绘制的图标定义

1. 通过 P1 的文件接口读取 `menu:/02_120_WorldMap.gfx`、`menu:/Hi/01_Common.sblytbnd.dcx`、`menu:/Hi/01_Common.tpf.dcx`。依照 0xD7D5B0 的路径规则尝试 GFX 平台路径和通用路径，当前样本通用路径成功。
2. 对 GFX 签名、版本、声明长度、tag 长度、位字段、character ID 和帧数做边界检查。恢复 WorldMapItem/Icon_0，而不是固定使用本次 character 171；当前 348 帧为样本证据，更新版本时重新验证。
3. 重放 PlaceObject2/3、RemoveObject2 和 ShowFrame，继承仅更新的矩阵、颜色或 character，按 depth 输出当前显示列表。第 102 帧只添加斜线，第 103 帧的附加图来自此前帧；不能只读取目标帧新增标签。
4. 从 tag 1009 得到 export_name，以去扩展名的 XML SubTexture key 查 atlas；name 和矩形分别校验。第 3/15 帧由资源定义映射到 Church/Ungro，第 107 帧绑定 35 + 80_Add，不设置「全部编号减 100」规则。
5. 将最终显示列表转换为自有 IconRecipe 和 IconLayer，保留 2×3 矩阵、depth、图集纹理 token、UV、矢量路径。解析的是数据；不创建游戏 GFX 对象、不执行 ActionScript、不复用游戏 renderer。
6. 只抽取 SB_MapCursor、SB_MapCursor_02、SB_MapCursor_03_dlc 和 SB_Chara 四张 DDS，共 7 MiB BC7 块数据。公共 TPF 约 194 MiB，限制为一条大请求；回调只复制目标 DDS，释放原 buffer，细致解析在工作线程进行。

建议替换 SpriteInfo 单一图片假设：

~~~text
IconRecipe = {semanticId, sourceFrame, layers, localBounds}
IconLayer = Bitmap{textureToken, uv, size, matrix, depth}
          | Vector{path, fill, stroke, matrix, depth}
MarkerSnapshot = {markerId, position, selectedIconId, angle, visible}
~~~

`selectedIconId` 先由游戏状态或相同谓词选择，再索引 recipe。图片 token 就绪后才发布可绘制定义；设备重建使 token 失效，CPU 元数据可按 generation 重新绑定。

通过条件：当前 93 个外部 sprite 全部映射到内部定义；WorldMapPoint/Bonfire 各状态字段的 105 个非零编号候选都能生成 recipe。空帧、未知格式、越界或缺失名称返回明确状态，运行代码没有外部图集回退。

### P5.2：恢复图标几何和游戏选择规则

1. 默认 Player 继续使用内部 Host（74×76），Arrow 使用 Player_01（72×150），保持既有中心构图。Arrow 使用原生 pivot=(31.9,87.4)，不使用 XML 中心；Host 兼容 anchor=(37,38)。原生 Player_02 两层构图可另做视觉模式，本次全量素材替换不要求改变既有玩家图标风格。
2. 地标使用完整 GFX matrix，原生 UI 尺度与世界缩放分离。frameMatrix 先定位原点，再施加 parameterAngle 和 mapRad；83/84 的负缩放提供 180° 翻转，删掉现有对所有非零 angle 额外 +180° 的规则。复核世界图像范围 isAreaIcon 与普通大小图标的缩放语义。
3. 罗盘使用内部 Bearing 像素；Overlay 的旋转罗盘 anchor 和布局按既有界面定义，与底图 mapRad 同步。不要将游戏静态罗盘角落坐标误当作小地图中心。
4. 在 update 线程发布 selectedIconId / visible / angle 快照。基础 iconId、altIconId、distViewIconId 和赐福 forbidden 的选择按报告 6.3；完整静态资源覆盖包含未触发字段，实际显示必须满足原生文本/事件条件。继续保留目前 Overlay 的 ID、来源和显示开关筛选。
5. 为组合帧创建多层绘制：位图通过自有 SRV；Shape4 character 170 按 GFX 解出的红色端点、2 单位线宽与矩阵绘制。实现通用直线/曲线路径解析，只对实际出现的填色/描边类型开放，不写外部图片补丁。

通过条件：玩家转向时中心不移动，箭头四个方向正确；3/15/83/84 和至少一组 NPC 组合图正确；赐福 1/48、2/49、101/102 的 depth、斜线、附加图符合帧定义。验证 sample 容器截断、仅更新深度和第 102/103 帧的继承；这些检查验证格式行为，不重复实现逐句写测试。

### P5.3：用内部图形绘制圆桌厅

1. 圆桌厅检测来自 P3 的原始地图 ID / Home 参数，记录 MapPresentation::Roundtable；不继续使用外部拼图坐标区域。
2. 从 GFX Home 恢复 Hub 位图矩阵与透明布局矩形。Hub 显示 200×200，透明矩形只影响 localBounds，绘制时不产生像素；圆桌厅视图按该语义保持与普通底图不同的显示比例。
3. 从 Home BonfireWarpParam 的当前状态取赐福 recipe，组合 Hub、赐福和玩家，保持明确绘制顺序。恢复原生 Home/marker 位置计算，或将其转换为小地图自身视图中心；小地图居中的动作是 Overlay 展示策略，不能记录成原生固定 world coordinate。
4. 同步圆形/圆角/透明度与现有离屏合成。原生 Home 是视口边缘布局，不加载 Roundtable PNG，也不请求原有 (800,8319) 的瓦片。
5. 用语义 ID 替换 `findSprite("RoundTable")` 字符串查询，移除旧 `Roundtable` / `RoundTable` 大小写差异。现有外部成品与内部 Hub + 48 的对照只作研究材料，不带入分发。

通过条件：进入/离开圆桌厅时底图类别正确；圆桌厅内部底图和赐福组合可见，玩家构图清楚；无外部坐标跳变、无其他区域瓦片混入，旋转/形状/alpha 各模式一致。

### P5.4：显示死亡位置标记

1. 从 `Body/_/Base/Dead` 恢复内部 `MENU_MAP_DropSoul` recipe。当前图片为 86×92，矩阵 scale=0.5、translation=(-23.15,-23.15)，像素 pivot=(46.3,46.3)；运行时按 GFX 定义读取，不以图片中心代替。
2. 在 update 线程读取 WorldMapViewModel 的死亡快照：+0xA9 为有效标记，+0xAC/+0xB0 为转换后地图坐标，+0xB4 为地图类别。快照与玩家、地下层、activeMask 和 generation 一起发布；不把死亡记录的原始 map ID 当作玩家所在地图。
3. 玩家地下状态 +0x30 按 U8 读取并归一化，不包含其后三个填充字节。依据 RVA 0x888860 的有效性与 Home 地图排除，以及 0x9C4B20 的地图类别比较，只在对应地图层绘制。矩形、圆形和旋转模式共用标记坐标变换，死亡标记绘制在普通地标之后、玩家之前。
4. 添加 `minimap.death_marker` 开关，默认开启。读取失败、返回标题、换档或死亡标记失效时立即清空快照；不缓存已回收卢恩的位置。

本次已实现以上四项。2026-10-02 已修正地下状态字段宽度，利用未回收卢恩时的实测字节和坐标回放正式读取、快照、资源和 ImGui 绘制，确认内部 DropSoul 图形进入绘制列表；矩形、旋转圆形、地表、地下、DLC、有效/失效死亡和地图类别不匹配均有回归。实际游戏画面与回收事件仍列在 P6 中。

通过条件：死亡后位置正确；回收卢恩后消失；再次死亡时位置更新；地下、DLC、圆桌厅和其他地图类别之间不串图；旋转、形状与透明度模式一致。

### P5.5：同步玩家放置的编号标记

1. 用 idapro 确认 `WorldMapMarkerDataList` 与保存槽位：view +0x338、16 字节记录、slot+1 编号、地图类别 U8。区分保存容量 10 与正常放置上限 5，不用保存 id 给图形编号。
2. 每次 update 复制当前编号槽位，直接使用游戏已转换的地图 x/y。发布前校验保存对象、数组、容量、计数和地图上下文；删除、到达自动清除、返回标题或读档失效后不残留旧标记。沿用公开 transient viewModel，不扩展旧 ABI。
3. 从 `Body/_/Base/MarkerList/Item_0/Icon_0` 恢复内部箭头，并解析 `Text_0` 的字号、颜色、矩阵和对齐。用已有 ImGui 字体绘制数字，标记位置采用死亡标记的地图变换；旋转模式下箭头和数字保持正向。
4. 添加 `player_markers=1` 默认开关。位于死亡标记之后、玩家之前，地表/地下/DLC 按类别筛选；透明度和形状使用已有离屏合成。标记放置与删除仍由游戏大地图完成。
5. 回放用户附近两个实际标记，通过正式 ImGui 绘制列表核对箭头 UV、数字和位置，再覆盖编号 1～5、空槽、删除、移动、错层、旋转/透明度、无效记录和上下文失效。

2026-10-02 已实现并完成上述回归。真实采样确认为编号 1/2、id=4/5，均在当前位置附近。当前游戏加载着旧 DLL；下一次正常加载新 Release 版本后继续视觉验收。具体记录与证据见[执行记录](minimap-native-implementation.md)和[编号标记证据](minimap-player-markers.evidence.json)。

通过条件：游戏放置的五个编号点正确显示下箭头与数字；空槽不改变其他编号；删除/到达后立即消失；地表、地下、DLC 不串层；旋转、形状和透明度一致。

## P6：游戏验收、默认值与分发

游戏测试使用正常 mod 加载和日志，不附加调试器、不设置任何断点。静态适配继续只用 Python idapro / ida-domain，并分析 Steam 原始 EXE。

| 场景 | 通过条件 |
|---|---|
| 冷启动、尚未打开大地图 | 文件桥等待挂载后自行加载当前位置，render 不阻塞 |
| 大地图打开再关闭 | 小地图继续绘制，不依赖游戏 GPU 瓦片缓存 |
| 地表、地下、DLC 移动越界 | 正确 M/X/Y、世界矩形和 UV，无接缝跳变 |
| 传送到未缓存区域 | 请求预算有效，旧区域失效，新瓦片逐步 Ready |
| 地图碎片和世界变体变化 | activeMask 变化触发正确后缀，旧图在 fence 后回收；获得状态同步已由用户实测确认 |
| 矩形、圆角、圆形和旋转 | 来源一致、裁剪正确、标记数量不变 |
| 返回标题、换档、重新加载插件 | CPU 回调和 GPU 提交均完成后回收，无悬空指针 |
| ResizeBuffers、设备重建 | 纹理 token、描述符和离屏目标按新设备重建 |
| 多 overlay 同时加载 | ABI 向后兼容，描述符容量预留有效 |
| 全部基础/状态图标与圆桌厅 | 来源都是游戏资源；别名、继承矩阵、图层和圆桌厅视图正确 |
| 死亡、回收卢恩、再次死亡、换档 | 死亡标记使用内部 DropSoul，位置更新和隐藏正确，不显示其他地图或旧档的标记 |
| 放置、删除、到达编号点；地图换层与读档 | 内部下箭头与数字 1～5 位置正确；删除及时清除，编号不重排；当前两个标记的只读采样与正式绘制回归已通过，游戏画面待加载新 DLL |
| 未知游戏版本、缺失资源 | 禁用不匹配入口；显示就绪/失败状态，允许重试，未就绪图层跳过 |
| data/map 不存在 | 瓦片、玩家、地标、赐福、罗盘、圆桌厅都正常；不再读外部 atlas 或 PNG |

先按项目要求构建 Debug / Release，再正常游戏内验收。不为纯文档新增 C++ 测试；真正实现解析器时补必要的越界、错误 DDS、取消晚回调和 generation 验证，避免写仅复述实现的测试。

记录读取延迟、上传延迟、Present CPU 时间、可见纹理显存、缓存峰值和取消排空耗时，比较当前外部底图实现。指标在验收中测量，不预先承诺收益。

开发期可在分支保留旧版本作对照。正式交付只使用内部资源，失败时通过等待/重试/状态提示恢复，不读取外部像素。P4/P5 回归通过后更新 [Minimap README](../src/minimap/README_CN.md)、配置和 dist.bat，移除全部 data/map 的文件读取与打包依赖，再做一次完全不提供外部数据的正常游戏验收。用户提供的本地对照文件先保留，清理运行依赖不等于删除研究用原文件。

实现与验证记录见[执行记录](minimap-native-implementation.md)。
