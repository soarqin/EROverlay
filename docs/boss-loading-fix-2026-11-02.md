# Boss 首次读取与等待修复

用户在 1.17.2 首次测试 `ca8dd4e` 时，死亡数和游戏时间正常，但 Boss 为 `0/0`，持续显示「正在读取游戏文本与 Boss 事件」，并伴有较长卡顿。本轮修复了原生文件名契约和初始化调度中的确定缺陷；没有启动、注入或操作用户游戏，**修复包仍待游戏现场复测**。

## 原因与修改

### 原生请求必须携带 `.dcx`

Boss 原先将 `event:/…emevd`、`msg:/…msgbnd`、`mapstudio:/…msb` 这些逻辑名称直接交给原生 raw submit，同时设置 `0x40`。1.17.2 原生资源适配层会先追加 `.dcx` 再设置该位；raw submit 本身不追加，`0x40` 只启用 DCX 读取。Minimap 已使用带 `.dcx` 的实际文件名，所以小地图正常不能证明 Boss 的新路径也正确。

现在只在 Boss Host 的请求边界追加一次 `.dcx`，保留解压标志、64 MiB 单文件上限和原有别名。事件解析、链接与证据仍使用原逻辑名称。没有增加未经证实的 `event_dlc2:/` 等别名，没有修改核心 ABI、原生分配器或退休规则。

对照输入为磁盘上的 1.17.2 EXE，SHA-256 为 `f9118fea2cfd8fa3901e8060bfadb1552cb2471771ded8a5b662c4e298ffb328`。原生适配层 `0x14265ca30` 显式追加后缀，raw submit `0x1426f0290` 经初始化与路径存储按原长度复制名称。语言目录、event 统一挂载、文本 binder、场景别名和严格 DCX header 也已复核，见 [原生契约证据](evidence/boss-loading-2026-11-02/native-vfs.json) 和 [审计说明](evidence/boss-loading-2026-11-02/native-vfs.md)。这是确定的调用差异；没有据此断言已解释现场全部卡顿。

### 移除逐资源等待一帧和重复缺失请求

旧状态机每次更新最多解析一个完成文件，之后每次更新只展开一张地图。本次归档中有 555 份可读脚本、520 份缺失事件，其中 519 份为可选探测；全部缺失都再试一次。连同文本和场景共 1601 次请求，按 60Hz 逐项推进，文件处理和地图展开会人为增加约 36 秒的等待，尚未计入实际 I/O。请求返回 0 时，旧循环还可能在同一次更新尝试整份队列；已接受的请求则没有截止时间。

新版保留完整资源范围，将 GameAreaParam 已有关联的地图先排队。Boss 最多持有 4 个活动 token；文件完成与地图展开共用每次更新约 2ms 的软 CPU 预算，另各限制最多 32 次提交／完成或地图展开。每个 token 每次更新最多轮询一次，`ERR_BUSY` 保留句柄。

可选事件普通缺失只探测一次；必需资源传输失败最多重试一次，已取得但格式无效的数据不重读。显式事件链接将早先的可选缺失提升为必需资源时，允许有界的必需尝试；最终失败后不因后续链接反复重开。文本仍仅针对缺失的命名空间回退，合法空文本保持有效。

每项从首次尝试起有 10 秒期限，包括请求持续返回 0、pending 和 busy；本次 generation 的资源等待期限为 60 秒。到期后分批释放 token 并完成失败处理，不能作为场景实体不存在的负证据。释放 token 后尚未完成的原生回调仍按原有 generation 证据退休，核心保留项另受 128 项和共享拷贝预算约束。没有有效目录时显示明确失败并允许「重新读取游戏数据」，部分目录可用时保留资源不可用提示。不会将 `0/0` 当成成功，也不会自动循环重扫整个目录。

2ms 是可让出的工作预算，单次解析、目录收尾和宿主调用不能被中断；它不是单次更新的硬上限，更不约束原生 I/O 或解压线程耗时。达到整体期限后仍需少量有界更新排空剩余任务、展开已取得脚本。临时地图状态、普通切图、面板开关不会从头加载；明确重读、真正的非零 Param 表地址变化和销毁会取消旧 token，由核心原有 generation 机制退休。

## 验证及其边界

- 严格探针现在检查插件发出的实际 `.dcx` 文件名和 `0x40`。旧 `ca8dd4e` DLL 在首个更新就因 `msg:/engUS/item_dlc02.msgbnd` 缺后缀退出，新 DLL 通过。探针仅在检查通过后将实际路径转换为显式归档文件；不再由宽松 fixture 悄悄容忍错误的原生名称。
- 生产 Loading 回归覆盖请求拒绝、长时间 pending／busy、延后完成、普通缺失、必需重试、可选升级、4 MiB 无效内容、总期限、重新加载和 Param 地址变化。使用可控时间推进，不靠长时间休眠。地图展开也检查批次和过期 CPU 预算。
- 真实 `GameFiles` 回归使用实际请求结构、原生入口和分配器 ABI 的受控实现，执行生产 request／pump／callback／poll／release：覆盖延迟 100 次 pump、missing、同步拒绝、超限回调、取消后迟到完成、generation 前不释放和退休后归还预算。它验证原生调用契约，不声称游戏 VFS 已实际返回。
- 真实归档再次生成 207 项、208 个去重完成旗标、345 条保留显示指令，旧 207 项逐项一对一且成员完整，0 缺失、0 额外。新 [输入指纹](evidence/boss-loading-2026-11-02/catalog-inputs.json) 和 [审计摘要](evidence/boss-loading-2026-11-02/catalog-audit.md) 可复核本次生产代码；完整 [旧项映射](evidence/boss-207-scope-2026-11-02/old-to-rust.csv) 字节未变，SHA-256 为 `30095b135ac115bfe29d174d607017144a8dac9e5bf614f9daa5cb920ece296a`。

| 检查 | 本轮结果 |
|---|---|
| 根目录 fmt／check／all-targets clippy `-D warnings` | 通过；workspace 测试 166 通过、0 失败、10 项外部资源／测量检查默认跳过 |
| Release 和实际三 DLL | 根目录构建通过；core＋Minimap＋Boss 连续 3 轮加载、上下层 WARP、viewport 变化及停止通过 |
| 仅更新 Boss 的兼容性 | 将旧 `ca8dd4e` Verified 包完整解压，只替换新版 Boss，旧 core／Minimap 组合再跑 3 轮通过；92 个原始文件中仅 Boss.dll 改变 |
| 严格实际 Boss DLL 探针 | 三次重新初始化均为 207 项／208 个旗标，`.dcx` 和 `0x40` 检查通过，无错误的 base 场景 DLC 别名 |
| 原有交互 | 搜索、设置仅 update 保存、取消复活零写、确认恰好一次请求、早场见证、未知／上下文拒绝、滚轮、隐藏／加载／全图释放捕获、reset 和重复序号通过 |
| WARP 像素 | 完整列表 389,962 个非黑 RGB 像素，隐藏为 0；176 字节 Host 保护页检查通过 |

每次探针更新按 60Hz 调度；每个原生文件请求最早下一次更新才返回，另注入 `ERR_BUSY`，不会同步返回所有 fixture 文件而掩盖状态机等待。三次加载为 `6.691 / 6.683 / 6.722 s`，`393 / 392 / 394` 次更新，每次 1082 个请求（1075 个事件路径、2 个文本、4 个场景路径，另有一次必需资源重试），较旧计划少 519 次。峰值在途 4，pending 轮询 `1080 / 1076 / 1080` 次，busy `64 / 63 / 63` 次。均为 555 脚本、519 个可选缺失、1 个不可用必需资源、9 个未解析调用、2 条场景残留排除、0 超时。归档缺少 `common_macro.emevd`，因此正确保留资源不可用提示。

单次实际插件更新的观测最大值为 `7.691 / 7.848 / 9.361 ms`，包含受控 Host 回调、拷贝、解析、展开和目录准备；首轮还有 fixture 懒读 MSB 的本地磁盘开销，后两轮复用缓存。没有细阶段计时证据，不能把最大值精确归于某个解析器，更不能称作原生游戏 I/O 耗时。这也明确说明 2ms 软预算不是单次更新硬上限。稳定期 500ms 更新的受控旗标批读中位数为 73µs、最大 133µs，不含生产 OS 旗标读取；紧凑／完整 render CPU 中位数为 31／160µs，p95 为 49／209µs，稳定字体上传和文件请求均为 0。

另在 Release、预读入真实归档的组件测量中，555 个脚本共 6,158,648 B，三轮总解析约 5.86–6.60ms；552 张地图共 16,213 个事件实例，总展开约 26.18–26.29ms，最慢单图 0.81ms。item／menu binder 分别为 4,740,132／3,903,844 B，100 样本解析最大 1.36／0.62ms，单独拷贝最大 0.65／0.50ms；三张 MSB 最慢单次解析 0.73ms。仅描述这些输入的组件 CPU，不含 VFS、DCX 解压和帧率。原 C++ Boss 在初始化时读取随包静态 JSON，没有同等动态目录读取；不能把它的初始化时间与本管线直接称为相同工作量。常态更新的公平 C++ 对照保留在 [旗标性能记录](boss-performance.md)。

| DLL | 上一包字节 | 本轮字节 | 本轮 SHA-256 |
|---|---:|---:|---|
| EROverlay.dll | 693760 | 693760 | `a1b9528a038c77996e57b16139e636235ab7995c988b92e10b85be5feaf6ad61` |
| Minimap.dll | 1766400 | 1766400 | `231c5596cb36cddaa923de687158527cb691b60b424f1f3bc066d948e1decab8` |
| Boss.dll | 2676736 | 2685952 | `c14b2fb42e7f996124ad6c705aa0b1a9179a4ca91a2756239b837b14b67059df` |

全部 DLL 合计 5,146,112 B，比上一包增加 9,216 B；没有新增依赖、字库或公共服务 DLL。原始回执保留在 [本轮证据目录](evidence/boss-loading-2026-11-02/)。此前旗标批读、紧凑模式延后构造、三条外部遭遇关系、复活写入集合、输入、PIN 和 MFG 契约均保留。

## 复现入口

普通检查不需要游戏或旧 C++：

```powershell
cargo fmt --all -- --check
cargo check --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --locked
./build.ps1
./target/release/eroverlay-harness.exe ./package 3
```

有显式归档输入时，运行生产目录审计和严格 DLL 探针；`target/research` 为六张 Param 与两个解压 binder，事件目录和场景目录不分发：

```powershell
python tools/audit_boss_catalog.py --reference target/cpp-reference --params target/research --events E:/path/to/events --scope selected --scenes target/boss-contract/scene-fixtures --output target/boss-loading-fix-audit
cargo run -p eroverlay-harness --release --locked --bin boss_probe -- package/overlays/Boss.dll target/research E:/path/to/events target/boss-probe-loading-final target/boss-contract/scene-fixtures
```

原生文件生命周期回归可独立运行：

```powershell
cargo test -p eroverlay-game --locked boss_native_requests_cover_busy_missing_large_and_late_retirement -- --nocapture
cargo test -p eroverlay-boss --locked data::tests -- --nocapture
```

## 更新与最小复测

从 `ca8dd4e` 包升级，只需完全退出游戏后替换 `overlays/Boss.dll`。包内核心和 Minimap 的产品代码本轮没有变化；首次从 Boss 移植前版本升级，仍需按根 README 更新三个 DLL 并补齐 Boss 配置。保留现有配置、`configs/boss-ui.json`、复活配方、三条遭遇关系及 `Challenge.txt`，无需恢复默认配置。

使用现有 Mod loader 启动并进入同一存档，确认目录结束读取、名称出现且普通归档为 207 项；按 `=` 打开列表检查进度，再关闭并观察首次加载和传送后的流畅度。Mod 场景证据不可用时仍可能安全保留额外候选，不能强行显示 207。若读取失败，可直接在列表点击「重新读取游戏数据」；错误不会静默变为成功。

离线探针、WARP 和组件耗时不能替代此次现场复测。特别是 Mod 挂载顺序、游戏原生 I/O、DLC 挂载优先级和同场景 C++／Rust 帧时间，仍没有本轮现场数据。
