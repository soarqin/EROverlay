# EROverlay Rust + egui

本仓库是 EROverlay 核心基础库、Minimap 和 Boss 的 Rust/egui 改写。Cargo workspace 位于仓库根目录。原 C++ 工程仅在独立 C++ 分支中保留，用于可选的行为和性能对照，不参与本分支的构建。安装包含 `EROverlay.dll`、`overlays/Minimap.dll` 和 `overlays/Boss.dll`，不加载旧 ImGui 插件；Achievements 尚未移植。

Boss 从游戏 Param、具名大生命条事件和当前语言文本生成目录，保留地区进度、筛选、死亡数、游戏时间、挑战记录及可选复活。默认按 `=` 打开或关闭完整列表。额外复活旗标仍在外部配置，发行包不再包含旧多语言名单。操作与限制见 [Boss 说明](crates/boss/README.md)、[数据来源](docs/boss-data-sources.md) 和 [事件证据](docs/boss-events.md)。

核心与 Minimap 的实现和历史验证见 [功能对照](docs/parity.md) 和 [验证记录](docs/validation.md)；Boss 的首交结果见 [Boss 验证记录](docs/boss-validation.md)，后续修复见 [Boss 验收修复](docs/boss-review-2026-11-02.md)。按用户选择恢复旧 207 项范围后，真实归档生成 **207 条记录，与 C++ 207 项逐项一一对应**，DLC 后门双守卫仍各占一条。完整映射、三条外部关系与场景残留检查见 [207 项范围与验证](docs/boss-207-scope-2026-11-02.md)。离线检查不能证明游戏内画面、运行开销或卸载已经通过验收。

DLL 体积调查与字体收敛见 [二进制尺寸分析](docs/binary-size.md)：Minimap 移除未使用的等宽和 emoji 字库，保留比例字体后备、自定义字体及系统语言字体。

Boss 首次现场测试的 `0/0` 持续读取问题已修正原生 `.dcx` 文件名契约、逐资源跨帧等待和无截止时间的请求，见 [Boss 首次读取修复](docs/boss-loading-fix-2026-11-02.md)。随后针对偶发层级异常和加载缓慢，恢复上层 GPU 提交槽保留，并将静态目录准备提前到 Param、语言和 VFS 可用的菜单阶段。受控真实资源的加载与双插件像素验证见 [菜单预加载与层级修复](docs/boss-menu-layer-fix-2026-11-02.md)。存档前的实际完成时点及游戏内层级稳定性仍待本轮复测，不能沿用核心／Minimap 的既有现场验收结论。

针对 1.17.2 实测卡顿及读档后持续等待，已修正首次资源与标记快照的错误依赖、无插件层的 renderer 重建和旗标读取契约，并恢复 PARAM 批读。用户已确认后续功能与加载提示修复正常。累计审查进一步修正资源背压、请求成员退休、尺寸变化与配置／ABI 边界，见 [审查和修复记录](docs/review-2026-11-02.md)。此前确定反例及 C++ 源码差分见 [读档与性能修复](docs/runtime-read-review.md)。

## 构建独立安装目录

要求 Windows x64、Rust MSVC 1.99 或更新工具链、Visual Studio C++ Build Tools 和 Windows SDK。本次使用 Rust 1.99.1。项目使用已签入的 MinHook 源码和 C++ 编译器构建 UI_SCENE 回调桥接。游戏接口、PE／RTTI／签名扫描、数据、资源和绘制主体均为 Rust；不链接旧 ImGui、Pattern16 或旧核心。原生源码和许可证范围见 [第三方声明](THIRD_PARTY.md) 与 [源码结构](docs/source-layout.md)。

在仓库根目录执行：

```powershell
./build.ps1
```

脚本使用 `Cargo.lock` 构建 Release，然后生成 `package/`：

```text
package/
  EROverlay.dll
  overlays/Minimap.dll
  overlays/Boss.dll
  configs/common.ini
  configs/input.ini
  configs/style.ini
  configs/minimap.ini
  configs/boss.ini
  configs/boss-revival.json
  configs/boss-encounters.json
  README.md
  LICENSE
  THIRD_PARTY.md
  third_party/          # MinHook 和内置字体许可
  manifest.json
  source.json
  docs/                 # 功能、设计、验证与性能记录
  crates/               # Minimap、Boss、公共 UI 和后端说明
```

脚本不会复制文件到游戏目录，不会启动或注入游戏。源码结构和本次根目录构建验证见 [源码结构](docs/source-layout.md)。

升级前须完全退出游戏。从 `411bc63` 或更早 Rust Boss 包升级本轮连续显示修复，替换 `EROverlay.dll`、`overlays/Boss.dll` 和 `overlays/Minimap.dll`；两个插件都包含共享字体图集暂忙处理的修复。从 Boss 移植前版本升级还需加入 `configs/boss-encounters.json`。首次安装 Boss 另需加入 `configs/boss.ini` 和 `configs/boss-revival.json`；已有 Rust Boss 配置不必覆盖，复活配方内容未变。保留现有配置、`configs/boss-ui.json` 和 `Challenge.txt`。旧 C++ Boss 的 `data_file`、`panel_pos` 和语言 JSON 不再使用；不要以旧 Boss INI 覆盖本包模板。修复、逐帧验证及现场复测边界见 [Boss 连续显示记录](docs/boss-continuity-fix-2026-11-02.md)。

## 加载与退出

Rust DLL 加载后只在 `DllMain` 中调度初始化线程。该线程在 loader lock 释放后读取 DLL 同目录下的配置和 `overlays/`，扫描游戏接口并安装原生 UI_SCENE hook。渲染不安装 Present 或 DXGI hook，不创建独立窗口或窗口事件循环。

手动安装或测试时，使用独立的 Rust 目录和受控的 DLL 加载器。不能将 Rust 核心与 C++ Minimap 混装。`EROverlay.dll` 是普通 DLL，当前不提供 `winhttp.dll` 代理导出；不能通过改名为 `winhttp.dll` 加载。游戏加载方式需要使用现有能够加载普通 DLL 的 Mod loader 或注入器。此任务不会自动操作正在运行的游戏。

初始化失败会停止接入并返回错误。启动线程不会通过固定 EXE 哈希判断兼容，也不会在每帧重新扫描。通过 `configs/input.ini` 的 `unload` 快捷键请求停止；空值默认禁用，可配置为 `END` 等现有键名。停止先关闭 hook 和新回调，排空对回调上下文的访问及 update，再等待自有 GPU 工作和游戏文件请求退役，最后销毁插件和运行资源。文件回调或 GPU 未退役时返回 `ERR_BUSY`，保留 DLL 并重试。

独立宿主可在加载 DLL 前设置 `EROVERLAY_RUST_OFFLINE_HOST=1`，然后使用 `eroverlay_start`、`eroverlay_render`、`eroverlay_stats` 和 `eroverlay_stop` 导出。此模式不安装游戏 hook。它用于离线检查，不能作为游戏中已接入的证明。

原生模式在首次启用 hook 前通过 Windows `GetModuleHandleExW(FROM_ADDRESS | PIN)` 固定核心模块，直到进程退出。原有读锁只能排空访问桥接对象的区间，不能证明 detour 前导和尾声已返回；停用 hook 后仍执行这些代码的线程必须有有效代码页。即使安装过程随后失败，已固定的模块也不再解除固定。停止完成返回 `STOPPED_RESIDENT = 1`，表示功能和资源已停止，核心代码仍常驻。原生模式本期不支持同进程完整卸载或重新启动；再次使用需要重启游戏。这是安全保留策略，不是热重载实现。

自动启动线程持有独立的 DLL 引用。外部加载器必须保留自己的 `LoadLibrary` 引用直到 `eroverlay_stop` 返回 `OK = 0` 或 `STOPPED_RESIDENT = 1`；由其负责停止时禁用 `input.unload`，避免并发调用。停止完成后只释放各自持有的引用；`1` 不代表模块已解除映射。没有安装原生 hook 的离线模式仍返回 `0`，支持完整装卸载。运行中替换 DLL 不属于本期能力。

## 配置与素材

Minimap 延续现有 UTF-8 INI 的分区和键名，包括命名预设、按键、尺寸、边距、位置、缩放、透明度、旋转、形状、边框及标记开关。先读根目录 `EROverlay.ini`，再由 `configs/*.ini` 覆盖；配置在初始化时读取。空快捷键和 `none` 用于禁用快捷键。现有 `configs_CN/minimap.ini` 可以替换包内对应模板。

地形、图集、Scaleform 图标和布局从游戏原生虚拟文件服务按需读取，不分发游戏素材。首次请求元数据须等待有效地图状态，避免标题阶段提前固定尚未被 Mod 覆盖的原版素材。正常等待和纹理排队不显示文字，单块瓦片失败仅记录日志并重试；元数据和图集的实际读取／处理失败保留错误提示。纹理入队暂未接收时保留源数据并退避，文件轮询忙时保留原请求。关键失败由 update 线程按实际失败尝试记录路径和处理阶段。DDS 在工作线程解析，保留 BC1/2/3/7 压缩格式及 mip 层上传；RGBA 素材在工作线程准备预乘透明度。地表、地下和 DLC 使用现有进度及坐标规则。地图和标记的实际对齐情况见功能对照。

## 新插件 ABI

插件导出 `getEROverlayPluginV1(version)`，返回带 `magic/size/version/capabilities` 的 C 函数表。完整定义在 `crates/abi/src/lib.rs`。核心验证固定表头后才读取其余成员；缺失导出、未知版本、短表、能力缺失和无效层级会被拒绝。

各 DLL 私有管理自己的 Rust 分配器、egui context、容器和状态。跨 DLL 仅传 `repr(C)` POD、句柄、函数指针和明确长度的借用缓冲。插件提交网格后核心立即复制到有界 staging；插件内存不被核心保留。文件缓冲借用到 `release_file`，纹理由核心拥有并经 GPU fence 延迟回收。`Vec`、`String`、trait object、egui context 和 Rust ABI 均不跨 DLL。

旧 Boss、Achievements 和第三方 ImGui 插件需要按新 API 直接重写，不提供旧二进制 ABI 或 ImGui 桥接。

## 验证命令

在仓库根目录执行：

```powershell
cargo fmt --all -- --check
cargo check --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --locked
cargo build --workspace --release --locked
./target/release/eroverlay-harness.exe ./package 10
```

`eroverlay-harness` 实际加载包内核心、Minimap 和 Boss DLL，检查初始化、重复启动拒绝、未知版本、空指针、update、RGB10A2 WARP 渲染、交替层尺寸、分辨率变化和重复装卸载。可选的合法上层测试插件提交真实纹理和三角形，检查尺寸变化不重建 renderer，GPU 在途时停止等待退休；命令见 [审查记录](docs/review-2026-11-02.md)。无游戏状态时两个实际插件保持隐藏；Boss 的可见绘制和交互另由 [归档数据 DLL 探针](docs/boss-validation.md) 验证。这些检查不启动游戏。

可选 C++ 差分测试先使用 `tools/export_cpp_reference.py --repository <C++仓库路径>` 导出明确提交，再设置 `EROVERLAY_CPP_REFERENCE` 并启用 `runtime-read-fixture`；普通构建和测试不需要 C++ 分支。独立 clone 不含旧提交时须显式指定含该提交的 C++ 仓库，不会自动获取旧历史。具体命令见 [源码结构](docs/source-layout.md)。

归档 EXE、素材和性能检查的实际参数及结果见验证记录；受控 C++/Rust 对照见 [性能记录](docs/performance.md)。完整验收还需同场景游戏内画面和整体性能对照。

## 性能测量原则

主要验收依据是相同硬件、分辨率、配置、地图、标记数量和样本时长下的 C++/Rust 对照。统计 Rust、egui、依赖、分配、FFI、CPU、GPU 和显存的全部开销，不扣除所谓 runtime 开销，不设置用户未认可的百分比门槛。固定毫秒预算仅作观察参考。

渲染线程不读取磁盘、解压、解析配置、执行全局签名扫描或等待 update 锁。资源由更新线程和纹理准备线程处理，绘制使用快照与 `try_lock`，GPU 使用有界提交环和 fence 回收。上层绘制暂忙或新图集尚未就绪时，独立合成环重放最后完整成图；明确关闭、真实隐藏或不兼容的设备／viewport 变化使其失效。固定环全部在途时不逐帧等待或无限扩容。测量 harness 内为读取结果而执行的同步与产品绘制路径分开统计。

运行场景至少覆盖隐藏、稳定显示、大尺寸、旋转、透明、切图、碎片变化、标记变化和大量标记。记录 CPU p50/p95/p99、分配次数和字节、draw call、上传量、驻留显存和缓存上限。离线 fixture、WARP 和真实游戏数据必须分别标注。

依赖选择、线程边界、失败回滚和原生代码复用理由见 [设计记录](docs/adr-0001.md)。
