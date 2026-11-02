# Boss 菜单预加载与原生层级修复

本页记录 `ac42ef4`／`411bc63` 阶段的实现和离线证据。用户随后确认 Boss 仍闪烁，槽保留与恢复后检查不足以完成现场验收；后续连续成图、逐帧反例和当前契约见 [Boss 连续显示修复](boss-continuity-fix-2026-11-02.md)。菜单预加载尚未获得用户现场验收。本页的旧数字和结果保留为历史记录。

本轮针对 Boss 进入存档后仍长时间初始化，以及上层显示顺序的现场反馈。目录准备改在 Param、语言和文件服务可用时于菜单推进；完成后的进档更新只消费已缓存目录。原有 207 项范围、三组外部遭遇关系、复活写入集合、原生请求文件名及 generation 退休契约保留。

本文的计时来自显式归档、受控存档／文件回调和实际 DLL 探针，未启动或注入用户游戏。此前 4 token 的实现、包和测量保留在 [首次读取修复](boss-loading-fix-2026-11-02.md) 及原证据目录；本轮不改写那些历史结果。

## 菜单期准备与进档更新

`Data` 每 500 ms 检查六张 Param 的可用地址。完整非零地址组和当前语言就绪后，更新线程开始读取文本、事件及必要的场景。语言优先采用非空 `common.language`，否则查询已加载的 Steam API；语言尚未返回时继续有间隔地等待，不先生成一套缺文本的目录。

生产服务的 `GameServices::update_snapshot_inner` 先推进 `files.pump()`，再检查地图／玩家／显示上下文；`find_param_table` 只通过已定位的 `SoloParamRepository` 取表，不要求有效玩家或地图。语言查询只访问已加载 Steam API，不主动初始化 Steam。这些源码契约允许标题期推进；它们不能证明某一游戏或 Mod 的资源实际已在标题期挂载。

目录准备不查询 `BossState`，也不读取存档旗标。插件更新仍可查询状态以准备显示快照，但状态回调返回不可用或 `OK` 且 `valid=0` 时，不批读进度旗标、不触发复活、不推进或保存挑战结果。「开始本轮」同样要求有效存档。复活操作继续要求确认时的上下文、当前为真的完成见证和完整可写目标，未放宽任何写入前置条件。

全部发现、场景筛选和目录准备完成后才发布完整目录，不在资源仍待发现时持续增加计数。进入有效存档后，按既有更新间隔读取去重完成旗标、死亡数和游戏时间；没有第二轮 Param 全表解析、事件展开、FMG 解析或场景扫描。

## 条件地名不再阻塞目录

地点来源仍是与完成旗标精确关联的 `WorldMapPointParam` 文本，或同地图近邻赐福的 `PlaceName`。Boss 名称仍来自生命条事件的名称 ID，地区分组仍来自赐福类别的 `GR_MenuText`，不依赖存档进度旗标。

候选地点的文本、显示旗标和隐藏旗标在准备时缓存为独立条件地名计划，实现为 `data.rs` 的 `Labels`。菜单只选择显示、隐藏旗标均为零的无条件候选，没有可用候选时显示地图标识。进入有效存档后，只批读候选实际需要的去重条件旗标：显示条件必须已知为真，隐藏条件必须已知为假，未知不能当作假值。只有旗标值导致标签变化时，才发布新的地点／搜索快照；不会因此重读游戏资源。

这组原版 Param、显式事件及文本输入的 207 行候选地名，条件旗标数为 0，菜单标签变化数也为 0，首次有效存档无额外地名旗标查询。此结论只覆盖该输入组。常规回归另覆盖 Mod 条件的真／假／未知、无条件回退、搜索文本同步和未变化时不发布快照，不能将原版的零条件数量推广到 Mod。

## 有界调度与依赖顺序

| 项目 | 当前契约 |
|---|---|
| Boss 活动文件 token | 最多 16；核心共享请求池仍为 128 |
| 每次更新预算 | 文件完成和地图展开共享约 2 ms 软 CPU 预算 |
| 批次上限 | 最多 32 次提交、32 项完成／终止、32 张地图展开 |
| 活动 token 轮询 | 每次更新最多一次；pending／`ERR_BUSY` 保留 token |
| 请求未接收 | 返回 0 后退避 250 ms；计入原任务期限 |
| 普通可选文件缺失 | 只探测一次，后续明确升格仍按原有契约处理 |
| 必需资源传输失败 | 最多延后重试一次，保留退避；已取得的无效内容不重读 |
| 单任务期限 | 从首次尝试起 10 秒，涵盖拒绝、pending、busy 及重试等待 |
| 资源等待期限 | 本次 Loading 创建后 60 秒；到期分批结束等待，不再发起新读取或文本回退 |

原计划将新发现的必需链接放在宽泛地图探测的末尾，第一次尝试后再等一秒。现在只有新插入的必需事件任务排到队首，尽早与其他地图读取重叠。已经排队的可选任务升格时原位保留，重试仍受退避和期限约束，重复链接不增加重复任务。活动 token 数在批内增量维护，避免每完成一项再扫描整份队列计数。

本轮没有删除 `MapDefaultInfoParam` 地图、缩减 Mod 范围、取消必需重试或把缺失链接改为可选。归档缺少 `common_macro.emevd`，仍保留必需资源不可用提示。9 个未解析事件调用也保留为证据缺口，不能根据缺少文件就推断这些调用均无关。

2 ms 是调度让出预算，不能中断单次 Host 调用、拷贝、解析或目录收尾。60 秒是资源等待期限，之后排空、展开和准备仍需要少量有界更新，不能将它写成游戏初始化或帧耗时保证。释放 token 后的原生回调仍由核心验证 generation 退休；16 个活动 token 不代表只保留 16 个原生回调上下文。

## 目录失效与恢复

普通进入存档、存档 context 变化、传送、全屏地图和临时零 Param 地址不清空目录或重复请求文件。完整非零表地址组真正变化时，取消旧任务、清空旧目录及条件地名计划，再按新表重新准备；不能为避免重建而复用旧 generation 的地址。

同地址的 Param 内容、文本或 Mod 覆盖变化通过「重新读取游戏数据」显式刷新。外部遭遇关系和复活配方仍只在插件初始化时读取。失败的空目录不显示 Ready；部分目录可用但资源失败时保留资源不可用提示。无法读取的场景继续视为未知，不能作为排除候选的负证据。

## 调度与组件测量

`archived_loading_scheduler_profile` 使用生产 Loading、六张真实 Param、555 份实际事件脚本、两个解压文本 binder 和三张 MSB。资源预先读入内存；受控文件传输最早在后续更新返回，按虚拟 60 Hz 推进时间，不使用长时间休眠。三种方案均为 1,082 次请求、207 项目录，必需资源重试和资源范围一致。

| 调度方案 | 三轮更新数 | 折合虚拟 60 Hz 时间 | 峰值活动 token |
|---|---:|---:|---:|
| 原 4 token | 356–357 | 5.933–5.950 秒 | 4 |
| 16 token，尚未提前新必需依赖 | 154 | 2.567 秒 | 16 |
| 16 token，新必需依赖先尝试 | 93–94 | 1.550–1.567 秒 | 16 |

最终三轮全部 Loading 更新 CPU 合计为 36.951–38.995 ms；单次更新 p95 为 2.073–2.108 ms，观测最大值为 2.276–2.448 ms。初始 Param 解析及资源计划为 0.702–0.851 ms，场景筛选为 0.424–0.480 ms，目录构建为 0.475–0.513 ms。主要等待来自跨更新的资源调度，因此未引入额外通用 Param 索引或后台 worker。基线的最终诊断打印曾进入其单次样本，最终方案已将诊断打印排除；整体 CPU 数字只作这组受控测量的观察，不用来推算游戏 FPS 或严格 CPU 降幅。

事件解析器另以相同 555 份脚本、552 张地图独立测量。无参数实例借用原指令、合并重复证据遍历后，展开 CPU 三轮中位数从 26.354 ms 降至 22.702 ms，约减少 13.9%。16,213 个实例、347 条静态生命条、464 条完成事件和 9 个缺失调用均不变。前后三轮完整 Discovery 快照共六份逐字相同，摘要见 [事件组件证据](evidence/boss-menu-layers-2026-11-02/event-profile.json)。这不包含原生文件服务、DCX 解压、整个插件更新或 GPU。

纯 Loading 的原始机器记录为 `target/boss-menu-loading-evidence.json`，日志为 `target/boss-menu-profile-{before,final,priority}.log`；事件组件原始记录位于 `target/boss-events-profile/final-{before,after}.log`。仓库保留 [加载摘要](evidence/boss-menu-layers-2026-11-02/loading-summary.json)，不分发原始游戏资源。

## 实际 Boss DLL 探针

实际 DLL 探针同样按 60 Hz 推进，检查真实提交的 `.dcx` 名称和 `0x40` 标志，文件最早下一次更新完成，并注入 pending／`ERR_BUSY`。存档、旗标、输入和写入是受控实现，Host 会同步读取本机归档；它不是游戏原生 VFS。三次初始化记录如下：

| 条件 | 实际经过时间 | 更新数 | 请求次数 | 峰值 token | 单次更新最大值 |
|---|---:|---:|---:|---:|---:|
| 故意延迟 Param／VFS，存档始终不可用 | 2.670 秒 | 157 | 1,658，其中 576 次为早期拒绝尝试 | 16 | 4.553 ms |
| 无额外就绪门槛，再次初始化 | 2.058 秒 | 120 | 1,082 | 16 | 25.092 ms |
| 无额外就绪门槛，再次初始化 | 1.893 秒 | 111 | 1,082 | 16 | 17.580 ms |

第一组在存档始终不可用时完成 207 项目录，批量旗标读取和写入均为零；`OK` 但 `valid=0` 的反例同样通过。首个有效存档更新观测为 63 µs，额外文件请求为 0，只新增 6 次 Param 指针查询，没有重新解析 Param 表或扫描目录。

DLL 的单次更新最坏值 25.092 ms 包含受控 Host 的同步 OS 读取和系统调度，不能用纯 Loading 的 2.448 ms 替代，也不能在没有细阶段计时的情况下归因于某个解析器。它再次表明 2 ms 不是硬时限。这些实际经过时间不能用作「进入游戏存档前必定完成」或原生游戏 I/O 耗时的保证。

最终原始回执为 `target/boss-menu-layer-final/boss-probe-final.log`，仓库保留 [加载与进档摘录](evidence/boss-menu-layers-2026-11-02/boss-probe-loading.txt)。

## 回归与范围复核

Boss 普通测试为 27 通过、0 失败、3 项显式归档检查默认忽略，all-targets Clippy `-D warnings` 通过。常规测试覆盖晚 Param、晚语言、VFS 暂时拒绝、pending／busy／超时、零地址保留、完整 Param 地址搬迁、同地址显式重读及新语言路径、必需依赖队列顺序、菜单无旗标查询，以及条件地名的三态与快照更新。使用可控时间推进，不依赖真实等待。

显式 profile 另覆盖存档回调始终不可用和 `valid=0` 两条路径，两者均完成 207 项目录，目录准备的存档查询与旗标查询均为零，进档后资源重扫为零。完整 selected audit 继续得到 207 项、345 条保留生命条、555 份生产脚本；与旧 207 项一对一、具名成员完整、无缺失或额外条目。`catalogue`、`old_to_rust`、`summary`、`scene_evidence`、`relationship_configuration` 与前一审计相同，CSV 逐字相同。完整本轮产物在 `target/boss-menu-audit`，没有再次分发一份游戏资源或旧多语言名单。

## 复现入口

普通构建和测试不需要游戏数据或 C++ 导出。显式 profile 需要自行提供上述原始归档：

```powershell
$env:EROVERLAY_BOSS_PARAM_DIR='E:/path/to/PARAM-and-BND'
$env:EROVERLAY_BOSS_EVENTS_DIR='E:/path/to/decompressed-EMEVD'
$env:EROVERLAY_BOSS_SCENE_DIR='E:/path/to/decompressed-MSB'
cargo test -p eroverlay-boss --release --locked archived_loading_scheduler_profile -- --ignored --nocapture
python tools/audit_boss_catalog.py --reference target/cpp-reference --params $env:EROVERLAY_BOSS_PARAM_DIR --events $env:EROVERLAY_BOSS_EVENTS_DIR --scope selected --scenes $env:EROVERLAY_BOSS_SCENE_DIR --output target/boss-menu-audit
cargo run -p eroverlay-harness --release --locked --bin boss_probe -- package/overlays/Boss.dll $env:EROVERLAY_BOSS_PARAM_DIR $env:EROVERLAY_BOSS_EVENTS_DIR target/boss-menu-probe $env:EROVERLAY_BOSS_SCENE_DIR
```

事件组件可用 `tools/boss_load_profile.rs` 独立编译，输入同一有序 555 份脚本清单；读取和快照格式化不进入组件样本。原始资源的存在、范围和解析成功不证明在线覆盖顺序、任务可达性、复活行为或整帧性能。

## 原生层级原理

Minimap 在已验证的最后一条原生 UI 合成命令前提交下层，Boss 在原生 UI 工作之后提交上层。下层使用 RGBA8 透明度表达场景透过率，上层使用游戏最终 RGB10A2 目标；两层沿同一 device／queue 异步提交，并为上层保留在途提交容量。无法验证原生合成边界时只抑制下层，不能据此取消 Boss 上层。受控 GPU 合成不等于 live hook 的游戏内视觉验收。

确定修复的是 C++ 移植遗漏的 GPU 提交容量保护。固定 C++ 基线 `afab7e14db2b62ae0bede980d5d0c96634321350` 的 `src/d3drenderer.cpp:377–380` 在下层绘制前检查后续槽，给上层保留机会。原 Rust 实现共用四槽，但未保留上层容量：已有三个提交尚未完成时，Minimap 可以占用最后空槽，随后 Boss 只能跳过。这条可复现路径会呈现面板被覆盖的现象；没有发现可确认的原生命令反序，不能声称用户每一帧都由同一原因导致。

核心现在将已加载上层组数交给 renderer，下层只能使用超过保留数量的空闲槽。保留数最多为三，避免四个或更多上层插件使下层永久饥饿；更多上层工作仍尽力提交。不扩大四槽池、不等待 GPU，不增加逐帧分配。上层仍可在完全无空槽时跳过；核心锁竞争等非阻塞跳过策略也保留，不能承诺任何压力下都绘制每一层。重复序号不再次推进 egui，viewport 改变不重建同设备／队列的 renderer，原有透过率、纹理退休、PIN 和停止契约未变。

普通 renderer WARP 回归在同样 RGB10A2 重叠区域、三个 GPU 忙槽下对比：

| 策略 | Minimap 下层 | Boss 上层 | 重叠像素 RGB10 |
|---|---|---|---|
| 旧策略，无保留 | 提交 | 跳过 | `(1023,0,0)` |
| 当前策略，保留一个槽 | 跳过 | 提交 | `(0,1023,0)` |

同一回归另验证超出槽数的上层组配置不会永久禁止下层：四槽全空闲时，下层真实边框仍提交，并通过白色像素回读。D3D12 调试层无错误。

## 真实插件重叠验证

`layer_order_probe` 加载专用 fixture core、实际生产 Boss.dll 和 Minimap.dll。fixture core 仅控制游戏／存档／输入来源并提供三个显式测试入口；纹理与网格所有权、插件加载、`Runtime::render`、原生队列适配器、GPU 绘制和 fence 退休使用生产代码。原生 U 阶段为同队列实际 GPU 合成，游戏 UI 内容由测试宿主控制。测试时 Boss 使用真实等待面板、Minimap 使用真实边框，不以该探针证明游戏资源加载。

使用本包 Minimap 和原 `a456002` Minimap 各跑一轮，均有：

- 120 个连续帧，每帧回读 26,940 个实际重叠区域像素；普通序列没有丢帧。
- 三个槽在途时，下层跳过一次，Boss 仍遮挡下层红边框；八帧异步提交后排空并恢复完整重叠。
- 五种原生场景覆盖正常合成、缺失边界、边界优先级替换、双合成和同地址队列但本帧无合成。检查上层像素、下层边界外红区及 U 命令次数。
- 重复序号没有新增网格；viewport 尺寸变化、render 锁和 callback gate 竞争后恢复。测试不会以旧几何绕过失效边界。
- GPU 未完成时停止返回 `ERR_BUSY`，保留 14,614,528 B；解除测试 gate 并排空后返回 `OK`。D3D12 debug layer 和 InfoQueue 均要求可用，无验证错误。

每轮共 1,190 个非空网格，14 次受控压力跳过，普通基线为零。记录的 4.548／4.493 ms 测试步骤最大值包含 GPU 排空，不能当作游戏帧或 render 回调 CPU 时间。缺失／替换／本帧无合成的检查也不能证明能识别所有完全相同的陈旧内存复用。未运行实际游戏 hook，未确认现场偶发帧全部消失。

两轮原始日志及三张 BMP 分别保留在 `target/boss-menu-layer-final/layer-probe-release-final` 和 `layer-probe-old-minimap-release-final`。精简日志、槽保护反例、导出与哈希保存在[本轮证据目录](evidence/boss-menu-layers-2026-11-02/README.md)。复现须为输出指定新目录：

```powershell
cargo build -p eroverlay-core --release --features layer-order-fixture --locked --target-dir target/layer-fixture
cargo run -p eroverlay-harness --release --locked --bin layer_order_probe -- target/layer-fixture/release/EROverlay.dll package target/layer-probe-new
```

fixture core 不得安装。正常 `build.ps1` 不启用 fixture feature，安装核心的 PE 导出不含 `eroverlay_test_layer_host`、`eroverlay_test_layer_drive` 或 `eroverlay_test_layer_busy`。

## 构建与升级

本轮生产修复提交为 `ac42ef4e9e445e75c730a0db98b326f74b2a04d3`。根目录 fmt、check、严格 all-targets clippy、workspace 测试和 Release 构建通过；普通测试为 **172 通过、0 失败、11 忽略**。三个正常安装 DLL 实际加载、交替层尺寸／resize、装卸载三轮通过；离线游戏状态下插件保持隐藏，因此可见绘制另由上述两类探针验证。

另外对该提交作独立浅克隆，使用全新 target、默认 feature、无 C++ 导出和无游戏素材，重新执行以上检查与完整 `build.ps1`，全部通过。源码前后干净，独立包的 112 项 manifest 全部匹配。此独立包包含代码提交时的旧文档，不作为本轮交付包；其构建路径造成的 DLL 字节差异也不替代下表实际测试和交付的哈希。回执见[独立重现](evidence/boss-menu-layers-2026-11-02/clean-source-reproduction.json)。

| 安装文件 | 字节 | SHA-256 |
|---|---:|---|
| `EROverlay.dll` | 694,272 | `3e0fedf5db1dd38e2d811ce7ecd83aea9fecd8060a24cd1aebb297674d9793aa` |
| `overlays/Minimap.dll` | 1,766,400 | `7b300ebd8bd7ba1065e350c19ad0eca9e488d5749c14fd11e916699660118fe1` |
| `overlays/Boss.dll` | 2,690,048 | `3322defc3f2ba1e9bebf415ff2a263ab4062fb23119cce59075c191256c859dd` |

三 DLL 合计 5,150,720 B，比 `a456002` 增加 4,608 B；未加入新字库或第三个公共 UI DLL。Minimap 本轮没有源代码行为变化，重新构建的字节不同；旧 `a456002` Minimap 的双插件回归同样通过。对已有 `a456002`／`09cd6e9`／`ca8dd4e` 安装，本次必须更新 **EROverlay.dll 和 Boss.dll**，也可同时采用包内 Minimap.dll。配置没有新增或修改，保留现有配置、`boss-ui.json` 和 `Challenge.txt`，不要覆盖用户文件。

完全退出游戏后替换 DLL，按原 Mod loader 正常启动并以平常节奏继续存档，无需为测试人为多等一段时间。进档后按 `=` 检查是否已是 207 项并立即给出当前进度，再让完整面板与 Minimap 重叠，观察移动、切图及多帧在途时的稳定性。不要安装 fixture core，也不在运行中替换 DLL。

代码已经解除存档依赖，并缩短可控资源调度；真实标题期的 Param／语言／VFS 最早可用时点、Mod 挂载时序、原生解压耗时及「进档前完成」仍需本轮现场确认。没有可靠的同地址 Mod／资源版本信号，不启用持久化目录缓存；同地址覆盖和当前语言变化需显式重新读取。C++ 使用既有名单，Rust 从实际资源发现目录，不能把本轮 Rust 内部优化倍率当作与 C++ 整体加载或游戏 FPS 的等价证明。
