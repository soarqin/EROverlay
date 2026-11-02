# Boss 移植验证记录

日期：2026-11-02。结果区分归档资源、受控进程和游戏现场；本轮没有安装、注入或启动游戏。

本页保留首交测量。`bab88d3` 之后的四项验收修复及复测见 [Boss 验收修复](boss-review-2026-11-02.md)，旗标读取的新旧数字见 [性能记录](boss-performance.md)。

## 来源与交付范围

注册工作树为 `E:/Projects/EROverlay/.cindy-worktrees/gentle-darwin`，开发分支为 `cindy/gentle-darwin`。宿主最初创建的干净工作树位于 C++ 提交 `afab7e14db2b62ae0bede980d5d0c96634321350`；确认没有用户修改或独有提交后，在此隔离目录切换到已验收 Rust 基线 `765fdcd4d1f12a68563faec262be2ef19848102c`，然后开始实现。没有修改父工作树、`master`、`rust-rewrite` 或既有验收标签。

本次安装仍为三个 DLL：核心、Minimap 和 Boss。公共字体、输入转换和网格工具静态链接进插件；没有第三个 UI 服务 DLL。C++ 对照显式导出自 `afab7e1` 到忽略目录，仅可选测试使用；普通构建不依赖旧工程或游戏资产。

## 已执行的检查

| 检查 | 结果与证据范围 |
|---|---|
| 根目录常规检查 | fmt、workspace check、all-targets clippy `-D warnings` 通过；135 项测试通过，8 项外部资源／可选测量检查默认跳过；显式提供 C++ 基线后的 all-features clippy 也通过 |
| 真实 Param／EMEVD／FMG 组合 | 6 张 Param、555 份可读事件脚本，生成 211 场战斗；0 未知完成条件、0 缺失名称，覆盖旧版全部 207 场；没有把数量或名单固化为运行时默认值 |
| 原事件解析差分 | 归档中 349 条启用生命条记录逐项相同；双 Boss、阶段、链接构造参数与完成条件关联见 [事件证据](boss-events.md) |
| 外部复活配方 | 207 项配方、955 次辅助写入，其中 38 项明确为空；显式导出器与签入配置逐字节核对，保留辅助写入顺序 |
| 生产存档／旗标服务 | 实际受控内存验证死亡数、时间、共享字节批读、未知状态、上下文变化、复活全量前置验证和邻位保留；不是只测试 mock 业务分支 |
| 包内三 DLL | 连续 3 轮真实 LoadLibrary、update、RGB10A2 WARP 上下层回调、viewport 变化与离线完整停止；要求两个插件都成功加载且无拒绝 |
| 多插件与 GPU 退休 | 包外加入合法上层 fixture，再跑 3 轮；真实纹理、网格及暂停 GPU 时的 stop 退休通过，fixture 不进入安装包 |
| 已验收 Minimap 二进制 | 来自 `765fdcd` 干净安装包的旧 Minimap.dll 与新核心、Boss 组合连续 3 轮通过；MFG 生产接口没有修改 |
| 原 Host 前缀 | 新 Minimap.dll 在恰好 176 个可读字节、紧接 `PAGE_NOACCESS` 的原 Host 表上 init／destroy 通过，Boss 在相同边界安全拒绝；不依赖零填充扩展内存 |
| ABI 拒绝与回滚 | 11 个实际 DLL 场景全部通过，包含短表、未知版本、能力缺失、缺失导出和失败 init 销毁 |
| 自动启动引用 | 提前释放与显式停止两种路径各 30 轮通过；不等于游戏原生退出已验收 |
| 原生生命周期与输入 | 专用 Release DLL 使用真实 MinHook、隐藏 HWND 和 DirectInput 入口；覆盖消息、UTF-16、捕获、client 外释放拖动、失焦、关闭、恢复 WndProc、禁用失败、PIN 和停止常驻；不获取前台或移动用户指针 |

可见 Boss DLL 探针使用真实资源与生产绘制，存档、旗标、文件传输和输入由受控宿主提供。它实际验证搜索、滚轮、设置仅在 update 保存、取消复活不写、确认后仅一次 update 写请求、重复序号、隐藏及 renderer reset。1280 × 900 RGB10A2 回读中，完整列表有 389962 个非黑 RGB 像素，隐藏后为 0；稳定采样没有文件请求或字体重新上传。复现与边界见 [输入和 DLL 探针](boss-input-validation.md)。WARP 图像不能代表游戏背景下的最终视觉或游戏 DirectInput 行为。

Release DLL 大小如下。默认字体功能保持关闭，两个插件各保留 Ubuntu Light 及系统／自选字体支持；没有加入等宽、emoji 或图片依赖。

| DLL | 已验收基线字节 | 本次字节 |
|---|---:|---:|
| EROverlay.dll | 654336 | 688640 |
| Minimap.dll | 1755136 | 1766400 |
| Boss.dll | 未移植 | 2587648 |
| 两个 overlay 合计 | 1755136 | 4354048 |
| 全部 DLL 合计 | 2409472 | 5042688 |

性能与大小单独列证据，不能把文件解析、微基准或 WARP 时间当作游戏 FPS。更新组件与原 C++ 的直接对照见 [Boss 性能记录](boss-performance.md)。

## 复现入口

普通检查与打包在根目录运行，不需要游戏资产或旧工程：

```powershell
cargo fmt --all -- --check
cargo check --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --locked
./build.ps1
./target/release/eroverlay-harness.exe ./package 3
```

可选 C++ 读取差分需先准备明确基线，详见 [源码布局](source-layout.md)。复活配置的可复现导出不进入正常构建：

```powershell
python tools/export_boss_revival.py --repository E:/path/to/cpp-repository --check
cargo build -p eroverlay-core --release --locked --features native-lifecycle-fixture --target-dir target/native-retention
python crates/abi-fixture/check_native_retention.py target/native-retention/release/EROverlay.dll
```

专用 fixture DLL 不可用于安装。原生生产模式仍在首次启用 hook 前 PIN 核心，停止返回 `STOPPED_RESIDENT = 1`，再次启动需重启游戏；离线模式才验证完整解除映射。

## 干净源码复核

实现提交 `1e28973bf33c8f872fe78c702600ff409922af10` 完成后，使用以下命令创建无旧 C++ 工作目录、无 `target/`、无安装包的独立克隆，并清除可选 C++／Boss 资产环境变量。在克隆根目录重新执行上述 fmt、check、clippy、测试及 `build.ps1`，全部通过；135 项常规测试通过、8 项默认跳过，Release 从零构建约 44 秒。`source.json` 记录该提交、当前隔离分支和 `dirty=false`。表中的大小也已与这个克隆产生的 DLL 核对。

```powershell
git clone --no-local --no-tags --single-branch --branch cindy/gentle-darwin . target/clean-boss-checkout
```

独立包随后又通过三轮组合 DLL／WARP 检查。再显式提供归档资产，运行包内真实 Boss／Minimap DLL 的完整探针，保护页、搜索、设置、复活请求、隐藏、reset 和像素检查通过。该轮渲染 CPU 中位数／p95 为紧凑 `32/61 µs`、完整列表 `162/203 µs`；更新 `28 µs` 中位数使用受控旗标服务，不能与生产 OS 读取对照混用。稳定字体上传和文件请求均为 0。普通源码构建没有访问这些可选游戏资产。

`master` 和 `rust-rewrite` 仍分别指向原 C++ 基线及 `765fdcd`，既有 `rust-core-minimap-accepted-765fdcd` 标签保留。独立复核之后只追加本节验证文档，不改变已经检查的实现。

## 升级与游戏内复测

退出游戏后同时更新 `EROverlay.dll`、`overlays/Minimap.dll`，新增 `overlays/Boss.dll`、`configs/boss.ini` 和 `configs/boss-revival.json`。保留其他既有配置和 `Challenge.txt`。旧 Boss 的 JSON 名单和旧 INI 不能覆盖新模板。默认 `=` 打开完整列表，`=`、`Esc` 或关闭按钮返回紧凑状态。

1. 用同一存档和场景对比 C++ 与 Rust 的目录、进度、死亡数和游戏时间，覆盖普通、双 Boss、多阶段及 DLC。四个新增遭遇的完成不一定表示敌人死亡。
2. 核对游戏当前语言和 Mod 覆盖文本。目录不完整时查看日志，必要时点击「重新读取游戏数据」。没有离线多语言名单兜底。
3. 拖动、调整窗口、搜索、滚动和修改设置，确认游戏键鼠被捕获；关闭、隐藏、失焦、加载、全屏地图和停止后恢复。确认 Minimap 仍在游戏 UI 下层、Boss 在上层，MFG 标记遵循原层级。
4. 复活默认关闭。启用后核对外部配方及确认窗，分别检查取消、上下文变化拒绝、复活后传送和读档。缺少配方的新遭遇不允许复活；不能通过清主旗标猜测完整复位。
5. 挑战模式切换存档后点击「开始本轮」建立死亡基线。当前没有可靠的持久存档身份，不能自动识别跨存档死亡增量。
6. 按既有性能原则采集同场景完整 C++／Rust CPU、GPU、上传、显存和帧时间；特别覆盖首次目录生成、切图、文字覆盖和完整列表。当前离线结果没有完成这项验收。

地区来自游戏传送菜单和同地图近邻位置，可能跨越高低差或地理分区；它不是旧版手工地点的精确替代。文本框支持直接输入，当前没有剪贴板快捷键和控制器导航。具体证据及降级规则见 [数据来源](boss-data-sources.md) 和 [Boss 操作说明](../crates/boss/README.md)。
