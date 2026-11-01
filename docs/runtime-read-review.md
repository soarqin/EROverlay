# 读档后等待与卡顿：实现对照和复测

记录日期：2026-11-02。用户环境为游戏 1.17.2，通过 Mod loader 在启动时加载 Rust DLL。旧包 `20fce70` 的现场日志只有字体和插件初始化两行，不能据此判断具体失败阶段。本轮以 C++ 生产实现为对照修正契约差异，诊断用于确认现场是否还存在其他失败。

本页测量发生在提升 workspace 前。C++ 行为来源为独立 `master` 基线 `afab7e1`；`20fce70` 等 Rust commit 是旧包标识，不要求保留在当前根提交历史。当前命令从仓库根目录执行，历史基线由 [显式导出工具](source-layout.md) 准备。

## 已确认并修正的实现差异

| 问题 | 可重复反例 | 修复与证据 |
|---|---|---|
| 首次素材读取依赖标记快照发布成功 | 透明预设跳过 `Data::update`，或开始读到地图 A、标记末尾复核读到 B；实时地图始终可读，但 invalid 标记快照令 metadata 请求为零 | 同 C++ 的独立资源阶段，以当前 display/map 就绪作为首次门控；生产 `Data::update → Resources::update` 覆盖 A/B/B、透明预设、Logo 和晚挂载。瓦片选择仍要求有效标记快照 |
| 无消费者的上层回调也改变 renderer 身份并重建 | 只有下层 Minimap，交替传入不同尺寸的上层 viewport；旧生产 DLL 改变 render 计数和 GPU 分配，重置下层 renderer | 在身份检查之前跳过没有插件的层。真实 DLL/WARP 回调交替上层尺寸，下层正常绘制、调整尺寸和退休；无消费者层不触发重建 |
| 旗标额外整类 stride 下界拒绝 | 同一可读内存中 `divisor=16, stride=1, id=17`：原 `gameflags.cpp` 返回 true，旧 Rust 返回 unavailable，从而阻止地图快照发布 | 与当前小地图实际调用的 `readMapPieceMasks → EventFlagCache` 一致，仅要求非零 stride、乘积和地址不溢出，以及实际字节能由 OS 复制；原 C++ 与 Rust 生产读数差分通过 |
| 等待状态下字体排队失败可每帧重交 | 核心纹理队列连续拒绝状态字体请求 | 字体失败按 250 ms 重试，pending 不重交，ready 不重复轮询；生产绘制回调计数验证连续等待的提交次数 |
| 文件请求上限只统计可用 token，未统计仍保留的请求 | 连续创建并 release 128 个请求后，Rust 仍接受第 129 个；C++ 对 `items.size()` 限制为 128 | 同时限制实际保留的 Item；只有 pump 证明退休后才恢复额度。已排队的同一 TPF 仍可合并命名成员。两个生产队列回归先在旧实现失败，修复后通过；此差异未被认定为用户现场症状的唯一根因 |

没有把所有更严格的检查都视为正确，也没有猜测新的固定偏移。内部旗标读取的 `UINT32_MAX` 拒绝与 C++ 内部 reader 不同，现已对齐；公共 `GameFiles::readEventFlag` 本来就把它视为 sentinel，Rust 公共 API 继续返回 false。

## 系统语义逐项对照

| 链路 | 对照结果 |
|---|---|
| RVA 与指针层级 | repository `+0x88 + group*72 → +0x80 → +0x80`、菜单 owner/view、角色和相机链没有找到确定偏移错误；不因未知 EXE 哈希拒绝现有扫描 |
| 地图与显示 | 原 C++ `readDisplayState/readMapContext/readWorldMapView` 与 Rust 在同一真实分配内存上运行。正常、无/坏 PARAM、无旗标管理器、零 divisor、加载、全屏地图、无 view、坏 RTTI、NaN 和 reveal 共 11 个场景一致；比较坐标、地图、死亡、view 和 masks |
| PARAM | 对照 2、3、4、5 格式及 12/24 字节目录、signed event、下个 data/name/string/file-size 界限，六种目录变体的字段读数与原 C++ 相同。恢复原有 ≤4 MiB 整表快照，快照内 field 不再发系统调用 |
| 旗标 | MSB 位序、树下界、缺失 category、未知 storage type、空直接指针、零 stride、索引溢出、不可读内存和环共 13 个边界场景与 C++ 一致；另有 small-stride 和 MAX 差分反例 |
| 原生文件请求 | 对照 64 字节请求表的指针、alignment=128、回调参数、flags/parameter/priority、submit 的 signed i32 返回值、flush、cancel 和 pool generation 退休。未发现确定字段或调用约定偏差；实际 native fixture 覆盖排队、提交、回调、释放和 generation 退休。缓存/复制预算差异保留为明确上限，不把受控回调等同游戏 VFS 验收 |

C++ 对照 reader 只在 `runtime-read-fixture` feature 下链接导出基线的 `src/util/mapstate.cpp`、`gameflags.cpp` 和 `ParamRows`，通过 `EROVERLAY_CPP_REFERENCE` 指定根目录。当前分支不保留旧业务实现；安装构建不链接这些 C++ 运行服务。

## 修正性能放大

- 正常内存读取保留 Windows `ReadProcessMemory` 的整段可读校验和复制，移除每字段重复 `VirtualQuery`；没有改成直接解引用游戏指针。跨页不可读、guard page、保护变化、decommit 和溢出回归均安全失败。平台契约见 [Windows 官方文档](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-readprocessmemory)。`VirtualQuery` 仍用于启动时 EXE 区域遍历。
- 地图片段 PARAM 使用与 C++ 相同的有界快照。Minimap 对无文件长度/较大表合并读取所需行前缀；只为 common 表查找 ID 0，单次标记构建对重复事件 ID 去重。
- mask 失败与首次标记构建末尾失败使用独立 200 ms 尝试期限；mask 源指针或游戏上下文变化可立即重试，标记构建最迟在期限后重试。失败期间不把默认 masks 或未验证标记发布为有效数据。失败重试算法在 C++ 也存在，本轮修正的是 Rust 系统调用和分配造成的放大，并增加恢复约束。
- 无消费者层早退发生在 renderer 重建之前。有消费者但当前隐藏时仍执行必要的上传完成检查和资源退休，普通绘制不等待 GPU。

## 可复现测量

在仓库根目录执行，均不启动游戏。先从包含 `afab7e1` 的独立仓库导出 C++ reader 基线：

```powershell
python tools/export_cpp_reference.py --repository E:/path/to/cpp-repository --output target/cpp-reference
$env:EROVERLAY_CPP_REFERENCE=(Resolve-Path target/cpp-reference).Path

# 原 C++ 与 Rust 读取同一分配内存，覆盖接受/拒绝契约。
cargo test -p eroverlay-game --release --locked --features runtime-read-fixture --lib -- --nocapture --test-threads=1

# 同一 fixture 调用原 C++ / Rust 地图片段计算，组件 CPU 对照。
cargo test -p eroverlay-game --release --locked --features runtime-read-fixture cpp_and_rust_mask_read_profile -- --ignored --nocapture --test-threads=1

# 20fce70 与修复版的真实 update_snapshot + fresh read_map_state，串行交替三轮。
python tools/compare_game_reads.py --repository E:/path/to/history-repository --samples 1000 --runs 3
```

比较脚本将 `--repository` 指定仓库中的旧 Rust 基线导出到当前忽略的 `target/`，加入同一 fixture 并构建；默认比较 `20fce70`，可用 `--baseline-commit` 显式指定。迁移前的原记录位于旧布局 `rust/target/game-read-comparison/`。脚本不依赖当前分支已删除的 `src/` 或不可达历史。旧实现仍保留原 VirtualQuery 和逐字段读路径，未将旧算法替换成模拟。数据是短批次 CPU 调用耗时，不包含游戏调度、native hook、Minimap 全部工作、GPU 或游戏帧时间。旧 C++ 的整条游戏帧对照仍缺证据。

同一 i9-13900KF 上，96 行 PARAM 的原 C++ 地图片段组件三轮 p50 为 `25.3/24.1/23.9 µs`，Rust 为 `24.7/24.8/24.5 µs`；每轮 100 次热身、1,000 次测量，使用实际 OS 读取和相同旗标缓存规则。这里只证明这个组件的对照，不能推导游戏整体开销。

旧 Rust 包与修复版完整快照读取的三轮 CPU p95 如下（µs）。每场景每轮先执行一次不计时初始化更新，再测量 1,000 次，三轮串行交替，Release ThinLTO；[逐轮 p50/p95/p99 原始记录](evidence/runtime-reads.json) 同时保留有效快照计数和 masks。

| 生产读取场景 | `20fce70` p95，三轮 | 修复版 p95，三轮 |
|---|---|---|
| 稳定地图，复用片段缓存 | 138.0 / 129.8 / 139.1 | 16.6 / 16.4 / 16.9 |
| 强制刷新 96 行片段 PARAM | 271.0 / 271.0 / 272.2 | 42.8 / 43.6 / 42.0 |
| PARAM 尚未发布 | 58.2 / 54.2 / 56.9 | 7.2 / 7.1 / 7.2 |
| 旗标未就绪 | 68.6 / 67.9 / 68.9 | 7.8 / 7.7 / 7.8 |
| 加载后恢复，后半批有效 | 132.1 / 126.5 / 133.3 | 16.6 / 16.4 / 16.6 |

等待场景保持零有效快照，恢复场景两版均为 500/1,000 有效；不是把失败改成假成功换取耗时下降。退避覆盖连续失败而实际 PARAM/flags 变化后的恢复由生产路径回归单独验证。

另有实际 OS 读取的 Minimap 标记构建 fixture：1,000 个标记、2,000 行 common 表，无文件长度时为 15 次 RPM／292,808 字节，有文件长度时为 14 次 RPM／292,856 字节，均为零 VirtualQuery。重复事件 ID 只调用一次 flag 服务。Release 单次首次构建分别为 327 µs 和 278 µs；这是一次受控构建的诊断数值，不是 p95 或游戏帧耗时。20,000 行大表另有读取次数和边界回归。

## 新包短复测

1. 使用新包替换独立 Rust 安装目录的核心和 Minimap；原生模块已 PIN，必须退出游戏后再替换并重新启动，不能在同一进程内热替换。
2. 在实际加载目录的 `configs/minimap.ini` 中配置下列节。父目录已存在；新日志追加到原文件，不删除原记录。

   ```ini
   [diagnostics]
   log_file = E:/Projects/EROverlay/build/minimap-rust-runtime.log
   ```

3. 进入存档后保留约 15 秒，记录地图是否出现、卡顿是否改善及时间。若仍等待，提供此次启动新增的 `core-game`、`core-stages`、`core-queues`、`minimap-data` 和资源阶段行；不必继续重复运行缺少这些行的旧包。
4. 日志区分快照/flag/PARAM 失败、文件排队/提交/完成、纹理准备/上传、renderer 身份和重建。状态变化限频，稳定状态低频采样；渲染线程不做文件写入或逐帧字符串格式化。日志关闭时不启用线程内系统调用统计。

这些数字记录的是读档修复阶段的受控证据。后续用户已确认 `7fce39d` 功能正常，以及 `86cd403` 的加载提示修复“测试没问题”；本轮 [累计审查](review-2026-11-02.md) 继续保留该行为。仍缺完整的同场景 C++/Rust CPU/GPU 轨迹，不把这些局部数字当作游戏整体性能结论。
