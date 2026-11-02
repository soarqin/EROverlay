# Boss 旗标更新的 C++／Rust 对照

2026-11-02 在本机完成真实旗标输入的离线差分和更新组件测量。两个实现的全部布尔结果一致；Rust 的 OS 内存拷贝与上下文检查比旧版裸指针路径更慢，冷缓存重建成本尤其明显。这些结果不证明完整插件或游戏 FPS 已通过验收。

## 比较的实际调用路径

C++ 直接编译固定提交 `afab7e14db2b62ae0bede980d5d0c96634321350` 的原始 `src/boss/data.cpp`，调用未修改的 `BossDataSet::load()`、`initMemoryAddresses()` 和 `update()`。没有重写一份简化循环作为旧版。`native/read_checks.cpp` 提供最小 API 回调，其中 `resolveFlagAddress` 与旧 `src/api.cpp` 一样，读取 manager 槽位后调用原始 `util/eventflag.hpp::resolveEventFlag()`。该路径遍历原生树后用 `atomic_ref<uint8_t>::load(relaxed)` 读取完成位，不使用 `ReadProcessMemory` 或 `VirtualQuery`。

Rust 调用生产 `GameServices::boss_flags()`：前后检查有效存档／显示上下文，读取死亡数、IGT、墓地旗标，批量读取完成位，复核上下文后输出 `0/1/2`。旗标地址采用现有 1 秒缓存；共享字节只拷贝一次。内存读取使用生产 OS 拷贝契约，不在测试中替换为裸指针。

两个路径处理相同完成旗标和同一块内存，但校验工作并不完全相同。C++ 测量还包括原 dataset 的互斥锁、区域计数和死亡数更新；Rust 包含更完整的前后上下文校验。计时不包含初始化文件读取、目录构建、渲染快照复制、挑战持久化、动态 DLL 分发、egui 或 GPU。没有从结果中扣掉 Rust 的安全读取成本。

这与现有 Minimap 的 `readGameEventFlag` 公共读取器对照不同，不能混用两条路径的数字。C++ 对照只在显式启用 `runtime-read-fixture` 时编译；正常 DLL 构建与安装包不链接旧 Boss、JSON 或 nlohmann。

## 输入和检查

207 个旗标由原版 `data/engUS/bosses.json` 经原 `BossDataSet::load()` 直接读出，保留原来的区域结构。211 个旗标来自 [真实资产组合回归](boss-events.md) 输出的 `boss,<flag>,...` 行，确认包含前述全部 207 个旗标，再在忽略的 `target/boss-read-profile/` 生成单区域测试输入。211 情形没有模拟生产目录的 95 个分组，不能据此比较区域分组算法。

本轮目录输出 SHA-256 为 `81557662d44f628d179b88ddb70711142f7a20df6c664bb930c38831d83ad0dc`。测试强制核对输入数量、唯一性和包含关系；源码中没有再保存一套 207／211 旗标名单。

两者共享一个 1 MiB、地址稳定的原生内存 fixture。树由这些真实旗标的 category 组成，采用平衡树、divisor `1000` 和原生 MSB-first 位序，分别覆盖 packed storage（kind 1）和 direct storage（kind 2）。每组输入／布局均执行三种交错真值，逐项核对全部状态、总击败数、死亡数和 IGT，并确认整片旗标存储及相邻位未被读操作修改。所有检查通过。

加载隐藏情形先改变底层完成位，再设置加载状态。原 dataset 保留旧快照，Rust 返回不可用并将输出置为 `2`，两者均停止完成位更新。这里的「隐藏」指加载画面门控；仅关闭 Boss 面板时，新旧实现仍在更新线程采集进度。

## 测量方法与结果

环境为 Windows x64、i9-13900KF、Rust MSVC `1.99.1`、workspace Release 配置。测试线程固定到逻辑 CPU 0，并在结束后恢复原亲和性，减少混合核迁移的影响。每个实现／场景预热 100 次，采样 1000 次，串行交替顺序执行三轮。使用 `Instant` 测量调用侧经过时间，包含系统调用和调度影响，不是硬件周期计数器。

`full_update` 保留缓存；`full_update_cold` 在每次调用前清空 Rust 地址缓存，清空动作位于计时区外，用于观察重建成本。它不表示每次 500 ms 更新都会重建缓存。C++ 原路径每次更新都重新解析地址。读取计数另行单次采样，计时期间关闭诊断计数。

下表为三轮各分位数的中位数，单位为 µs；没有合并逐次样本重新计算分位数。原始分位数和读取计数见 [机器可读记录](evidence/boss-update-2026-11-02.jsonl)。

| 旗标数／存储 | 场景 | C++ p50 / p95 / p99 | Rust p50 / p95 / p99 |
|---|---|---:|---:|
| 207／packed | 全量，缓存热 | 1.8 / 1.8 / 1.9 | 66.9 / 76.7 / 92.2 |
| 207／direct | 全量，缓存热 | 1.7 / 1.8 / 1.9 | 66.8 / 75.6 / 88.9 |
| 211／packed | 全量，缓存热 | 1.8 / 1.9 / 1.9 | 68.1 / 78.0 / 94.2 |
| 211／direct | 全量，缓存热 | 1.8 / 1.8 / 1.8 | 68.1 / 77.3 / 91.6 |
| 207／packed | 全量，重建缓存 | 1.8 / 1.8 / 2.0 | 1184.0 / 1261.6 / 1390.8 |
| 207／direct | 全量，重建缓存 | 1.7 / 1.8 / 1.9 | 1112.2 / 1177.4 / 1388.3 |
| 211／packed | 全量，重建缓存 | 1.8 / 1.9 / 2.0 | 1207.2 / 1284.0 / 1551.8 |
| 211／direct | 全量，重建缓存 | 1.8 / 1.8 / 1.8 | 1132.8 / 1201.8 / 1417.2 |
| 207／packed | 加载隐藏 | < 0.1 / 0.1 / 0.1 | 3.6 / 3.7 / 8.8 |
| 207／direct | 加载隐藏 | < 0.1 / 0.1 / 0.1 | 3.6 / 3.6 / 8.7 |
| 211／packed | 加载隐藏 | < 0.1 / 0.1 / 0.1 | 3.6 / 3.6 / 3.7 |
| 211／direct | 加载隐藏 | < 0.1 / 0.1 / 0.1 | 3.6 / 3.6 / 5.9 |

C++ 加载隐藏的 p50 原始值为 0 ns，低于本次计时的约 100 ns 粒度，不能解释为零开销。短循环不能代表长时游戏的 p99。

| 情形 | Rust RPM 次数 | Rust 拷贝字节 |
|---|---:|---:|
| 207，缓存热，两种布局 | 292 | 919 |
| 211，缓存热，两种布局 | 296 | 923 |
| 207，冷 packed | 5866 | 26219 |
| 207，冷 direct | 5504 | 24771 |
| 211，冷 packed | 5975 | 26697 |
| 211，冷 direct | 5607 | 25225 |
| 加载隐藏，全部输入／布局 | 18 | 182 |

Rust 全部单次采样的 VQ 次数和失败读取数均为 0。C++ 的 RPM／VQ 次数为 0，是上述原始调用路径的源码性质，不是给旧版循环注入计数器后测得。它仍然进行了实际内存读取。

Rust 缓存热时约为旧路径的 37～40 倍，缓存重建约为 1.1～1.2 ms，而旧路径约为 1.7～1.8 µs。本轮实现保留这些更新侧成本，不能宣称旗标更新与 C++ 持平。生产 Boss 每 500 ms 采集一次，旧 wrapper 每 32 个约 60 Hz tick 采集一次；本测试去掉两者的调用节流，比较单次实际工作。缓存重建不会发生在渲染回调内，但仍需在同游戏场景中评估更新线程与游戏的整体竞争。

冷路径的主要可见来源是按 category 分别遍历旗标树，并对节点各字段发起 OS 拷贝。后续可以评估一次读取单个节点所需的有界快照，减少字段级 RPM；若跨页快照失败，必须保留必要字段可独立读取的回退语义。此项尚未实施，不能通过改成裸指针或放宽无效／换存档检查来抹去差异。

## 复现

先按 [真实目录回归](boss-events.md) 准备自有归档资产，并将完整输出保存到忽略目录。下面的 C++ 导出只在目标目录不存在时执行：

```powershell
python tools/export_cpp_reference.py --repository E:/path/to/cpp-repository --commit afab7e14db2b62ae0bede980d5d0c96634321350 --output target/cpp-reference
$env:EROVERLAY_BOSS_PARAM_DIR='E:/path/to/exported-params-and-bnds'
$env:EROVERLAY_BOSS_EVENTS_DIR='E:/path/to/exported-EMEVDs'
cargo test -p eroverlay-boss --locked archived_catalog_uses_param_map_plan_and_current_game_text -- --ignored --nocapture *> target/boss-catalog.txt
$env:EROVERLAY_CPP_REFERENCE=(Resolve-Path target/cpp-reference).Path
$env:EROVERLAY_BOSS_PROFILE_CATALOG=(Resolve-Path target/boss-catalog.txt).Path
$env:EROVERLAY_READ_PROFILE_SAMPLES='1000'
cargo test -p eroverlay-game --release --features runtime-read-fixture --locked cpp_and_rust_boss_update_profile -- --ignored --nocapture --test-threads=1
```

此检查要求显式导出的固定旧源码、原 207 项 JSON 和含 211 项的目录输出；普通测试、构建和安装不依赖这些资产。fixture 不启动或注入游戏，也不验证真实游戏页面映射、画面、双 DLL 调度或完整帧性能。
