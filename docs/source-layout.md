# 源码布局与历史对照

`rust-rewrite` 分支的 Rust workspace 位于仓库根目录。旧 C++ 工程、ImGui、旧插件及其构建入口已移除；行为对照使用独立仓库中的 `master` 提交 `afab7e1`，不依赖本分支保留旧源码或历史对象。

| 位置 | 用途 |
|---|---|
| `Cargo.toml`、`Cargo.lock`、`crates/` | 核心、Minimap、Boss、静态 UI 工具、游戏服务、DX12 后端、稳定 ABI 和离线宿主 |
| `build.ps1` | Release 构建和独立安装包生成 |
| `configs/`、`configs_CN/` | common/input/style/minimap/boss 默认配置及中文注释模板；Boss 额外复活配方独立为 JSON，程序只自动读取 `configs/` |
| `third_party/minhook/` | 原生 hook 所需的 MinHook C 源码和许可证 |
| `tools/` | 显式导出历史对照、绘制及读取性能比较 |
| `docs/`、各 crate 的 Markdown | 设计、功能、验收边界和历史证据 |
| `target/`、`package/` | 忽略的构建／对照临时目录和安装产物 |

PE、签名、RTTI、unwind 和调用关系扫描由 Rust 实现。正常安装构建保留的原生代码只有 `crates/renderer/native/` 的游戏 UI_SCENE hook 桥及 MinHook，不链接旧 C++ 核心或扫描器。`crates/game/native/read_checks.cpp` 仅由显式选择的 `runtime-read-fixture` 测试功能编译，用于和导出的 C++ reader 对照，不进入普通安装构建。

配置缺失时程序使用内置默认值；发行包保留五份 INI 模板和必要的外部复活配方。`configs_CN/` 不会自动覆盖配置，需要复制所选文件到 `configs/`。游戏素材通过原生 VFS 读取，不签入或打包；`data/` 仅可用于用户自备字体，不需要保留旧 Boss/Achievements 数据。

[MapForGoblins 官方 C ABI 头文件](api/mfg_map_api.h) 作为接口参考保留，来源为 [官方 SDK 的固定提交](https://github.com/VirusAlex/ERR-MapForGoblins-DLL/blob/7506219bd4923662fdc7fdb3c6753495f6abc29e/sdk/mfg_map_api.h)。Rust 使用自己的 `repr(C)` 声明，不通过 C 头文件生成或编译该接口；头文件保留 MIT 许可。

## 显式准备 C++ 对照

普通构建和常规测试不需要旧 C++ 工程。以下命令从仓库根目录执行；`--repository` 指定确实包含 C++ 基线提交的独立仓库，导出目录放在忽略的 `target/` 内：

```powershell
python tools/export_cpp_reference.py --repository E:/path/to/cpp-repository --output target/cpp-reference
$env:EROVERLAY_CPP_REFERENCE=(Resolve-Path target/cpp-reference).Path
cargo test -p eroverlay-game --release --locked --features runtime-read-fixture --lib -- --nocapture --test-threads=1
```

导出默认选择 `afab7e1`，可用 `--commit` 显式指定其他基线。只有启用差分 feature 或 `--all-features` 时才需要该导出；缺少基线时应报告准备要求，不能静默链接其他来源。

C++ 绘制对照使用 `tools/build_cpp_perf.bat <referenceRoot>` 和 `tools/compare_perf.ps1 -CppReference <referenceRoot>`，完整步骤见 [性能记录](performance.md)。旧 Rust 包 `20fce70` 的比较另用 `tools/compare_game_reads.py --repository <含该历史提交的仓库> --baseline-commit <提交>`，脚本将基线导出到忽略的 `target/`，不读取本分支已删除的 `src/`。

## 迁移前证据

既有 CSV、JSONL、日志和旧 commit 编号保持原样。文档中的旧 `rust/`、`build/` 和 `src/` 路径若标注为历史布局，仅用于定位当时的测量，不是当前操作入口。迁移前 C++ 扫描适配层的归档结果不能直接作为新 Rust 扫描器的验证结果；迁移后的实际执行结果在 [验证记录](validation.md) 单独补录。


## 本次独立构建与边界核验

2026-11-02 在同一注册工作树整理并验证。最终源码树不包含 `rust/`、`src/`、`deps/`、`cmake/`、`injector/`、CMake 工程或旧发布入口；四份配置模板及中文模板、项目 MIT 许可证、MinHook 和内置字体许可均保留。MinHook 的 13 个文件与 C++ 基线逐字节一致。公共 ABI 头及可选比较 fixture 不承载旧业务实现。

游戏扫描的 30 个归档版本（1.02–1.17.2）全部通过，完整 23 字段／92 字节 `Bindings` 与显式导出的 `afab7e1` 原 C++ oracle 相同。逐版还核对了请求 ABI 篡改拒绝、无关哈希变化和 +8 GiB ASLR。结果见 [新扫描证据](../crates/game/scan-evidence-rust.csv)。UI_SCENE 的全部 15 个地址／偏移及拒绝原因也与 C++ 相同：21 个支持、9 个拒绝，见 [UI 扫描证据](../crates/renderer/scan-evidence.csv)。独立只读复核确认 85 个游戏扫描模式与原规则一致。

使用以下命令创建只含新分支历史的克隆。克隆起初没有 `target/`、旧 C++ 源码或 C++ 参考环境变量，普通验证完全从零编译：

```powershell
git clone --no-local --no-tags --single-branch --branch rust-rewrite . target/clean-root-checkout
cd target/clean-root-checkout
cargo fmt --all -- --check
cargo check --workspace --locked
cargo clippy --workspace --all-targets --locked -- -D warnings
cargo test --workspace --locked
./build.ps1
```

本轮实际结果：fmt、check、clippy 全部通过；110 项常规测试通过，4 项需外部素材或测量输入的检查默认跳过；根目录 `build.ps1` 完成 x64 Release 并生成独立安装目录。随后显式提供导出的 C++ 参考目录，all-targets／all-features clippy 及 game Release 差分测试通过（35 项通过，3 项选择性检查跳过）。C++ 参考目录仅在选择性检查中使用，不参与普通 Release 构建。

真实 DLL 检查中，Minimap-only 和合法上层 fixture 两种 WARP 模式各通过 3 轮，覆盖 RGB10A2、交替层尺寸、GPU 在途退休、初始化及完整离线装卸载；ABI 拒绝／失败回滚 11 场景全部通过。自动启动引用检查两种模式各 20 轮通过。单独构建的真实 native-lifecycle DLL 也通过 12 个前导／尾声暂停窗口和 3 个禁用失败分支，停止返回 1，调用者释放引用后代码仍映射。导出脚本的固定提交内容、拒绝覆盖和越界目录检查通过；C++ 绘制对照程序已从显式导出的 `afab7e1` 在忽略目录完整构建，旧 Rust 读取对照脚本以 100 样本／1 轮运行成功。后两项用于验证新入口可运行，没有据此更新游戏性能结论。

本轮没有注入、启动、安装或复测实际游戏。归档契约、独立内存和 WARP 结果不能替代游戏内画面、完整 CPU／GPU 开销或 native hook 的安全解除映射证明；原生模式仍采用 PIN／`STOPPED_RESIDENT = 1`，不承诺同进程完整卸载或重启。此前 C++／Rust 性能和现场确认保留在原证据中，没有改写为本次新测量。
