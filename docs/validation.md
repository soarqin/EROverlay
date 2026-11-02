# Rust 改写验证记录

记录日期：2026-11-01；资源预算复查与复测：2026-11-02。迁移前注册工作树：`E:\Projects\EROverlay\.cindy-worktrees\steady-jepsen`，分支为 `cindy/steady-jepsen`，当时使用 `rust/` 和 `rust/package/` 布局。下列旧 commit 编号、工具输出和测量值保留为历史证据。

当前 Rust workspace、`build.ps1` 和 `package/` 均位于仓库根目录，不再保留旧 C++ 工程。C++ 行为起点是独立 `master` 基线的 `afab7e1`；显式导出及新旧扫描证据的区分见 [源码布局](source-layout.md)。安装包的 `source.json` 记录打包时的 commit 和是否有未提交修改，`manifest.json` 记录文件 SHA-256。

## Boss 菜单预加载与层级修复（2026-11-02）

当前 Boss 目录在六张 Param 表地址及语言就绪后即可在菜单准备，不等待有效存档。存档状态不可用或 `valid=0` 时不批读进度旗标、不复活、不保存挑战结果。条件地名独立缓存，进入有效存档后只更新进度和必要的标签快照；临时无效状态不触发目录重扫，真正的非零 Param 地址换代或显式重读仍取消旧任务并重新准备。

Boss 活动文件 token 上限为 16，核心共享请求池仍为 128。文件处理和地图展开共享约 2 ms 软预算及各 32 项批次上限；新发现的必需依赖先尝试，使保留的一次重试与其他读取重叠。10 秒任务期限、60 秒资源等待期限、原生 generation 退休和缺失证据保守处理保持不变。

纯生产 Loading 的真实归档、虚拟 60 Hz 调度从 356–357 次更新降至 93–94 次；折合约 5.94 秒降至 1.55–1.57 秒。真实 Boss DLL 的受控 60 Hz 最终探针中，两次无额外门槛初始化为 2.058／1.893 秒；故意延迟 Param／VFS 的启动为 2.670 秒。首个有效存档更新观测为 63 µs，新增文件请求为 0，仅有 6 次 Param 指针查询。DLL 探针的单次更新最坏为 25.092 ms，包含受控 Host 的同步 OS 读取和系统调度；不能用纯 Loading 样本的 2.448 ms 最坏值代替。这些数字不证明原生游戏 VFS、进档完成时间或 FPS。

Boss 常规检查为 27 通过、0 失败、3 项显式归档检查默认忽略；Boss all-targets Clippy `-D warnings` 通过。显式归档 profile 和 selected audit 另行通过，207 项逐项映射、完整成员、场景及关系证据与先前相同，CSV 字节未变。当前契约、组件测量、真实 DLL 回执及复现命令见 [菜单预加载与层级修复](boss-menu-layer-fix-2026-11-02.md)。本页其余通过次数、旧包和早期 Core／Minimap 范围说明保留为各阶段历史记录；上一轮 4 token 的 Boss 回执见 [首次读取修复](boss-loading-fix-2026-11-02.md)，不作为当前参数。

## 根目录迁移后的验证

2026-11-02 已从仅包含 `rust-rewrite` 的独立干净克隆运行根目录 fmt、check、clippy、110 项单测、Release 及真实 DLL/WARP 检查。扫描的完整字段与 C++ oracle 的归档差分、可选 feature 和脚本验证详见 [源码布局与独立构建证据](source-layout.md#本次独立构建与边界核验)。下表保留迁移前测量，不与本次结果混用。

## 环境

- CPU：13th Gen Intel Core i9-13900KF。
- GPU：NVIDIA GeForce RTX 4080；另有虚拟显示适配器。使用哪个适配器由各检查回执明确记录。
- Rust：`rustc 1.99.1 (58cf8f9c3 2026-10-13)`，`cargo 1.99.1 (5f94df478 2026-08-27)`。
- C++ 对照：VS 2022 / MSVC v142 19.29，Windows SDK 10.0.26100.0，Release、动态 CRT、LTO。
- 游戏 EXE 归档：`D:\Games\EldenRingExe`，现有 1.02–1.17.2 共 30 个版本目录。归档测试不启动游戏。

## 已执行

2026-11-02 按现有文档复查，修复了单次提交预算被上下层重复获取、跨帧槽在途暂存缺少总量门控，以及 CPU 源数据在准备完成后过早退还排队额度的问题。继承的资源规则为：瓦片缓存目标 64 MiB，CPU 源数据总队列 64 MiB、单图 32 MiB、768 个用户纹理，update／每个原生队列分别最多四图约 4 MiB，在途实际上传暂存目标 16 MiB／八图；超预算单图只在空闲时独占。没有修改 CPU/GPU 固定毫秒值为硬性门槛。

新增回归直接调用生产队列、分发和 renderer：重复序号不推进 egui，两层共用上传额度，暂停 GPU 时跨槽仍不越过图片数和字节目标，行距/heap 对齐计入实际暂存，大图保持独占；取消、失败、重建和释放后 CPU 额度正确归还，旧任务不能重新发布。下层透过率方程及 RGB10A2／RGBA8 像素检查继续通过。完整装卸载证据只覆盖无原生 hook 的受控离线宿主；原生停止采用下述常驻策略，不包含运行中替换 DLL。

| 检查 | 实际结果 | 证据范围 |
|---|---|---|
| C++ Release 全项目构建 | 通过，历史记录 | 迁移前 C++ 工作树的 `build/bin/`；保留原有 linker/codepage warnings |
| Rust `fmt --check`、workspace check、all-targets/all-features clippy `-D warnings` | 全部通过 | 锁定依赖；包含新增原 C++ 读取差分 feature |
| Rust workspace 单元测试 | 106 通过、0 失败 | 含累计审查新增的队列／配置／ABI／资源恢复回归；默认跳过 2 项归档素材测试及 1 项读取基准 |
| 原 C++/Rust 真实内存读取差分 | Release 32 项通过、2 项基准默认跳过 | 含 28 项重叠的常规游戏库测试；4 项差分覆盖 11 种地图状态、6 种 PARAM 目录及旗标树/stride/MAX 契约 |
| Rust workspace Release x64 构建 | 通过，迁移前记录 | 当前入口为 `build.ps1`；核心和 Minimap 均为真实 cdylib |
| Rust 游戏服务调用旧扫描适配层 | 30/30 通过，历史记录 | `crates/game/scan-evidence.csv`；不等于新 Rust 扫描器复核，也不覆盖 UI_SCENE renderer 版本限制 |
| 原有 UI_SCENE 离线工具 | 通过；21 版本解析、9 版本不支持，历史记录 | C++ 基线中的 `tools/build_ui_scene_checks.bat` 和 `tools/ui_scene_evidence.py`，当前分支不提供这些入口；1.05–1.17.2 解析，1.02–1.04.1 不绘制。原始 C++ 回执在 `docs/evidence/cpp-ui-scene-evidence.json` |
| 新原生桥接的真实 MinHook 生命周期 | 回调上下文门控通过；确认原卸载缺口 | 对受控本地函数安装两处 detour；12 次前导/尾声暂停确认 stop 返回后线程仍需模块代码。首次启用前 PIN 模块直到进程退出；不宣称原生 DLL 已完整卸载 |
| 原生代码常驻的实际 DLL fixture | Release 通过 | 同一桥接的 12 个窗口、3 次真实 MinHook 禁用失败分支；停止返回 1，调用者释放引用后 DLL 仍映射且导出可调用；专用 feature 不进入安装包 |
| Rust renderer 测试 | 9 项通过 | 包含 WARP 像素回读：分组透明、圆形、独立边框、原生 UI 透过率、RGB10A2；忙槽不等待、隐藏退休、缓冲增长、压缩 DDS mip/alpha/资源计数；启用 D3D12 调试层，无 ERROR/CORRUPTION |
| 累计审查实际 DLL 装卸载 | 两模式各连续 3 轮通过 | Minimap-only 模式检查无消费者层与尺寸变化；合法上层 fixture 模式实际绘制并检查 generation/reset/纹理创建各为 1，GPU gate 在途时 stop 等待退休。后者约 48 次消费层回调，受纹理准备线程调度影响；均非游戏进程 |
| 真实 DLL ABI 拒绝/回滚 | 11 场景全部通过 | 空/未对齐/短表、未知版本、magic、能力、必需回调、缺失导出、失败 init 清理及有效实例销毁计数 |
| 自动启动线程引用 | 最终 Release 200 次通过 | 启动前释放调用者引用 100 次；外部 stop 后立即释放并重载 100 次。均为非游戏原生发现失败路径，不等于游戏内退出验证 |
| 真实归档 GFX/布局/TPF | 2 项 Release 测试通过 | 118 种图标 recipe、148 个 region、4 张 BC7 图集、10 个盖章；素材不签入仓库 |
| C++/Rust 受控性能对照 | 3 组各 600 样本通过运行 | 相同 RTX 4080 / 1080p / RGB10A2 / 合成几何，详见 [性能记录](performance.md)；不是游戏内完整性能验收 |

Rust 游戏服务的补充 `game_probe verify` 检查了未知哈希容忍、破坏 request ABI、截断 PE 和 +8 GiB ASLR。游戏服务单元测试使用受控内存及异步回调 fixture，不调用实际游戏 VFS。

上述 30 份扫描回执来自迁移前的 C++ 适配层。新的纯 Rust 游戏扫描器使用独立 `crates/game/scan-evidence-rust.csv` 记录与导出 C++ 基线的完整 `Bindings` 字段差分；UI_SCENE 的 Rust 扫描记录另见 `crates/renderer/scan-evidence.csv`。这两份新记录和根布局构建结果由 [源码布局的执行记录](source-layout.md) 单独说明，不沿用旧扫描的通过次数。

本轮修复后的 Release 素材探针 CPU p50/p95/p99（µs）：117 标记 `43.2/47.5/63.0`，大尺寸旋转透明 `42.3/45.3/57.7`，1,000 标记 `344.8/391.0/479.5`；隐藏 `0/0.1/0.1` 且零分配。117 标记每帧 427 次／213,708 字节分配、81 个网格；1,000 标记每帧 3,505 次／1,743,060 字节、672 个网格；稳定上传为零。该探针只覆盖生产 paint/egui/POD sink，不含 update、核心复制和 GPU，不能用它断言游戏内性能或与 C++ 整体持平。条件见 [Minimap 性能记录](../crates/minimap/PERFORMANCE.md)。

用户实测卡顿和等待后的修复详见 [生产读取对照](runtime-read-review.md)：先修正独立素材就绪、旗标 stride 和无消费者层重建三项确定差异，再恢复 PARAM 快照、合并读取并约束失败重试。相同真实内存 fixture 的 `update_snapshot + read_map_state` 三轮稳定 p95 从旧 Rust 包 `20fce70` 的 129.8–139.1 µs 降至 16.4–16.9 µs；没有把该调用批次当作游戏帧。阶段日志追加、限频并区分地图/PARAM/flag、文件及 renderer 队列。用户已确认后续 `86cd403` 正常；本轮审查后的代码另有 [修复与清理记录](review-2026-11-02.md)，未重新宣称一组游戏性能数字。

## 历史 C++ 命令与当前复现入口

以下 C++ 命令记录迁移前的执行方式，只适用于独立 `master` 基线 `afab7e1` 的工作树；当前分支没有 CMake 工程及这些旧工具：

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -T v142 -DRELEASE_USE_STATIC_CRT=OFF -DRELEASE_USE_LTO=ON .
cmake --build build --config Release --parallel 4
cmd /d /c tools\build_ui_scene_checks.bat
python tools/ui_scene_evidence.py D:/Games/EldenRingExe --output build/ui-scene/evidence.json
```

以下命令从当前仓库根目录执行。普通检查不需要 C++ 基线：

```powershell
cargo fmt --all -- --check
cargo check --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --locked
```

运行差分 feature 或 all-features 检查前，显式导出含 C++ 基线的仓库：

```powershell
python tools/export_cpp_reference.py --repository E:/path/to/cpp-repository --output target/cpp-reference
$env:EROVERLAY_CPP_REFERENCE=(Resolve-Path target/cpp-reference).Path
cargo clippy --workspace --all-targets --all-features --locked -- -D warnings
cargo test -p eroverlay-game --release --locked --features runtime-read-fixture --lib -- --nocapture --test-threads=1
python tools/compare_game_reads.py --repository E:/path/to/history-repository --samples 1000 --runs 3
```

从当前仓库根目录执行打包及真实 DLL 检查；路径已按新布局更新，本页历史通过次数不代表这些命令已在迁移后重跑：

```powershell
./build.ps1
./target/release/eroverlay-harness.exe ./package 10
python crates/abi-fixture/check_rejections.py target/release/EROverlay.dll target/release/Minimap.dll target/release/BadPlugin.dll --missing-export target/fixture-noexport/release/BadPlugin.dll
python crates/abi-fixture/check_autostart.py target/release/EROverlay.dll --iterations 100
```

缺失导出 fixture 另用 `cargo build -p eroverlay-abi-fixture --release --features missing-export --target-dir target/fixture-noexport` 构建。归档素材环境变量和 opt-in 测试命令见 Minimap 性能记录；对照 fixture 命令见性能记录。

## 本轮验收修复

- 首次 GFX／布局／瓦片目录请求要求可见请求、当前显示就绪及独立 `read_map_state` 成功，不再依赖较重的标记快照发布。初始标题、Logo 和无地图状态不会锁定原版资源；首次有效地图时读取已晚挂载的覆盖素材。透明预设及标记末尾上下文变化不会卡住首次素材请求。已经发起的请求仍可在隐藏或地图暂失效时轮询、释放，解析重试计时不阻塞退休。renderer reset 保留已发布定义并重建纹理；新资源实例重新执行首次门控。
- 正常等待和单块瓦片失败不叠加提示；元数据／图集实际失败时，生产 `Renderer::render` 仍生成错误文字的真实 egui 网格。Logo／加载、全屏地图、无效显示状态和手动隐藏仍在绘制前退出。无可绘地图或 opacity 为零时错误使用不透明组，边框参数独立。部分地图与错误文字共享组透明度、提示居中这一差异列入 Minimap 功能表。
- 文件请求／完成、GFX／布局和目录解析、图集 DDS／裁色、瓦片 TPF 和已接收纹理上传失败记录路径及处理阶段。只有实际失败尝试或延迟重试失败才格式化和输出；等待重试的普通更新不刷日志。纹理入队返回 0 可代表暂忙，保留源数据并退避而不报错；文件轮询 `ERR_BUSY` 保留 token。单块瓦片错误只写日志，其他资源错误仍提供给绘制快照。
- 通过真实 MinHook 分别在 submit/composite 的读锁前及释放读锁后暂停，各重复三次，确认 12 个窗口均可在 detour 尚未退出时完成 stop。原锁不能证明安全解除代码映射。当前选择启用 hook 前固定核心模块；停止运行资源后返回 `STOPPED_RESIDENT = 1`，不再承诺原生模式完整卸载或同进程重启。离线模式 `OK = 0` 的完整装卸载能力保留。

本轮修改后重新执行 Release 构建、3 轮包内 DLL/WARP 装卸载、11 个 ABI 拒绝／回滚场景及 200 次自动启动线程引用检查，全部通过。先前的 10 轮包检查保留为历史记录；本轮未重跑未改动的游戏 EXE 扫描和 C++／Rust GPU 性能对照。

原生固定模块的独立 DLL fixture 使用同一桥接代码和核心 stop 导出，但不调用游戏、不卸载正在执行的代码；检查调用者释放 `LoadLibrary` 引用后 DLL 仍映射。fixture 导出只存在于专用 feature 构建，不进入安装包：

```powershell
# 在仓库根目录构建独立 fixture；不能用其 DLL 替换安装包。
cargo build -p eroverlay-core --release --locked --features native-lifecycle-fixture --target-dir target/native-retention
python crates/abi-fixture/check_native_retention.py target/native-retention/release/EROverlay.dll
```

## 交付前补充复核

独立复跑了 5 项 C++／Rust 生产读取差分及相关旗标恢复回归、3 项首次素材门控回归，并核对安装包文件和真实 DLL/WARP 交替层级回调。随后源码对照发现文件服务只以 token 数限制请求：release 后仍未退休的 Item 可绕过原 C++ 的 128 项上限。新增的两个生产队列回归在修复前均失败；补上实际保留 Item 的上限后，文件服务 6 项测试全部通过，同时确认请求退休后恢复额度、同一已排队 TPF 仍能合并成员。该补充通过 `fmt --check`、game 全 target clippy `-D warnings` 和 workspace Release 构建。它没有被认定为用户现场唯一根因，也不替代下述游戏内验收。

## 加载提示修复（2026-11-02）

对照 C++ `src/minimap/resources.cpp` 的瓦片任务处理，`queueTexture == 0` 仅延后重试，未将整张地图设为失败。Rust 核心 `texture.rs` 的 registry `try_lock`、CPU 队列限额或发送队列繁忙均可使有效 DDS 入队返回 0；旧 Rust 却立即记录 `tile-upload-request` 并生成全地图错误文字。这是确定的暂态误报路径，没有据此声称已抓到用户那一帧的唯一原因。

本轮使正常初始／元数据／图集等待静默，瓦片真实失败只写限频日志；atlas／tile 入队暂未接收时保留源数据并等待 250ms 后重试，已接收后的解析和上传失败仍走原错误处理。另修正 `Host::poll(ERR_BUSY)` 误释放健康文件 token 的接口问题；当前单 Minimap 常见路径未证实会发生此锁竞争，不将其归为现场根因。

3 项新增回归直接运行生产资源更新与 Host／FileSlot 路径，覆盖初始等待及元数据 pending 无文字、busy 文件轮询后成功不取消或重读，以及 atlas／tile 拒绝 → pending → ready 时不误报、源数据复用、等待期间不重复提交。原错误回归继续确认真实解析错误只按实际尝试记录，元数据／图集错误仍可显示。workspace 97 项测试、Minimap all-targets clippy `-D warnings`、fmt 检查通过。

## 尚未建立的结论

用户在 1.17.2 复测 `7fce39d` 后反馈“功能全部正常”，并确认 `86cd403` 的静默加载与瓦片闪错修复“测试没问题”。本轮累计审查包尚无新增的游戏内测量。用户的功能确认不等于 HDR 亮度、不同游戏版本、所有 Mod 素材组合和退出均已验收；尚无同一游戏场景下 C++ 与 Rust 的整条路径 CPU/GPU/显存性能对照。离线 fixture 的数字只描述对应工作负载，不能作为游戏 FPS 或整条路径不回归的证明。

本地未找到可配对验证的真实 terrain 索引/碎片 XML 归档素材；其格式、选择和坐标逻辑已有实现与单元检查，真实地形放置、切图与进度变化仍需游戏验收。实际 PARAM/存档读数、游戏原生异步文件请求、Steam 游戏语言和运行中的 Map for Goblins 官方 DLL 同样缺游戏内证据。完整实现/差异见 [Minimap 功能表](../crates/minimap/FEATURES.md)。未移植 `winhttp.dll` 代理入口，当前替代是已有普通 DLL loader 或 injector；旧 ImGui 插件、Boss/Achievements 不在本期兼容/移植范围。

## 游戏内复测步骤

1. 使用独立的 Rust 安装目录，按受控的普通 DLL 加载方式启动。记录游戏版本、图形设置、分辨率、存档地图、标记数量及日志。
2. 检查启动黑屏/Logo、加载和游戏全屏地图时整张小地图隐藏。普通菜单按游戏自身 UI 遮挡小地图。
3. 检查地表、地下和 DLC、地图碎片、全开底图、移动和传送；保留相同 C++ 配置与坐标截图作对照。
4. 检查各预设、旋转、圆形/圆角/矩形、低透明度、独立边框、不同位置和窗口尺寸。
5. 检查赐福、地标、死亡、编号 1–5、盖章、其他图标和 Map for Goblins 开关。按原生层级核对图标遮挡。
6. 对 C++ 和 Rust 分别采集同场景样本。覆盖隐藏、稳定、最大预设、旋转/透明、切图、碎片/标记变化和大量标记，报告 CPU p50/p95/p99、分配、draw call、GPU 时间/上传/驻留显存和样本时长。
7. 配置并按 `input.unload` 快捷键请求停止，确认 hook、更新和资源停止；原生模式核心保持常驻，再次使用需重启游戏。外部 loader 负责停止时禁用该键，持有自身引用直到 stop 返回 `0` 或 `1`；`1` 表示停止完成但模块常驻，不能当作完整卸载。GPU 或文件请求未退役时保留 DLL 并重试。
