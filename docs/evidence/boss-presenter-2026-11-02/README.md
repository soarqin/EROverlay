# 成图发布锁竞争证据

代码 `9d623d0f4cc9063524b2538938f7505afdd1569f`，对照 `5ee822c78921cd29e7c7252ed3810e563663bd54`。本轮只补 Presenter 锁竞争连续性；新包没有游戏现场验收，菜单预加载也尚未确认。

| 文件 | 用途 |
|---|---|
| [锁审查](lock-audit.md) | 所有生产锁持有者、允许的并行 native 调用及固定 C++ 对照 |
| [汇总](verification.json)、[检查收据](checks.json) | 186 项通过、所有被测源文件字节、正常 DLL 及验证范围 |
| [最终矩阵收据](layer-receipt.json) | 实际 core／插件／probe 指纹、源码、命令、故障帧和限制 |
| [生产插件日志](final-production-03.txt)、[逐帧记录](final-production-03-frames.txt) | 120 稳态＋362 呈现；新增普通竞争当帧通过 |
| [故障插件日志](final-fixture-04.txt)、[逐帧记录](final-fixture-04-frames.txt) | 120＋376 呈现；另加真实 Boss 快照／UI 锁 |
| [旧图集负例](negative-atlas-with-state.txt)、[旧重复序号负例](negative-presenter-duplicate.txt)、[旧 render 回退负例](negative-presenter-render.txt) | 另一 CPU 确实持锁时，当前清空目标漏绘；各自附 `-frames.txt` |
| [旧直接绘制对照](negative-visible.txt) | 目录沿用 negative 命名，但此运行 exit 0：可直接提交新几何时，单独持成图锁不会漏画 |
| [旧源码收据](old5ee/source-receipt.json)、[最小差分](old5ee/fixture-only.patch)、[规范化证明](old5ee/normalized-patch-receipt.json) | 307 个固定输入，仅 fixture 控制区域变化；真实被测 DLL 不重建或替换 |
| [Renderer 收据](renderer-receipt.json) | 新 WARP 回归、strict feature Clippy 及 artifact 指纹 |
| [两图复用](renderer-immutable-reuse.txt)、[未发布 copy](renderer-unpublished-copy.txt)、[提交前取消](renderer-hide-close-before-submit.txt) | 资源／Arc 两幅轮换，跨线程发布／失效及停止安全 |
| [性能收据](copy-comparison.json)、[原始轮次](perf/) | 同基准旧／新 Release，三轮交替 CPU／GPU／分配／资源字节 |
| [正常核心](multidll.txt)、[附加上层 fixture](multidll-upper.txt) | 最终正常核心各三轮；后者包含 GPU 在途退休 |
| [独立干净重现](clean/clean-source-reproduction.json) | 全新 target、默认 feature、无游戏／C++ 资产；186 通过、190 项独立代码包清单匹配 |
| [安装 DLL](dll-hashes.json) | 与测试核心区分；Boss／Minimap 与 5ee 字节一致 |

矩阵每帧先清目标，再执行生产 L／U／H 并为该次呈现建独立 ticket。真实维护锁由确认握锁的另一 CPU 执行上下文持有，完成本次 H 才放锁并 join；未声明线程固定到不同物理核。新图集确实停在 prepared，重复序号不推进 UI／input poll，render 回退持真实核心锁。旧版只回移植 fixture 控制，不替换其生产实现；旧原始 probe 未另存，最终 probe 指纹只对应两轮最终正例。

283／297 个应显示 Boss 的矩阵帧全部满足覆盖，各另有 120 稳态帧。判定为坏像素不超过 ROI 的 1%（26,940 个像素时最多 269 个）；两轮各有 80 个 Boss 帧为 120 个，其余为 0，新增三类发布锁故障帧全部为 0。每轮 44 Hidden／5 Lower 分别证明真实隐藏／关闭，6 Closing 保留 egui 淡入，24 Overload 则故意完全暂停 GPU。后者 6 帧缺图属于固定池硬界，不算普通维护锁竞争通过；释放后下一帧恢复。首次无成图、viewport／device／queue 不兼容及固定 16 槽全忙不承诺无限连续。

日志只替换工作树绝对路径和宽字符 NUL，保留原始结果与空白。嵌套原始 artifact 指纹对应忽略目录中的原文件；安装包内实际文本另由 manifest 校验。正常 `multidll.txt` 保留初次成功调用的工具 stdout；额外上层探针曾因位置参数错误被拒绝，改正后的三轮结果另存。性能探针首次链接误选缓存依赖的 feature 变体，最终从 Cargo artifact 选匹配版本后才取样，失败编译不产生测量样本。BMP、游戏素材、测试 DLL／EXE 不随包分发，收据保留原图指纹。

独立 clean clone 的 190 项 manifest 属于代码包，最终文档包项目数另计。其路径不同造成的 DLL 指纹不替代本目录最终正常安装 DLL。正常核心无 fixture 导出；像素矩阵使用 `layer-order-fixture` 核心，受控部分是显示、存档、资源输入和 native U 内容，不是游戏原生 VFS 或现场 FPS。

## 复现入口

从对应源码根目录构建，fixture DLL 不得安装：

```powershell
cargo test --workspace --locked
cargo build -p eroverlay-core --release --locked --features layer-order-fixture --target-dir target/layer-order-fixture
cargo build -p eroverlay-harness --bin layer_order_probe --release --locked --target-dir target/input-agent
./target/input-agent/release/layer_order_probe.exe ./target/layer-order-fixture/release/EROverlay.dll ./package ./target/presenter-new all
```

第二轮只把独立输入目录内的 Boss 换为 `--features continuity-fixture --target-dir target/continuity-fixture` 生成的测试 DLL；正常 Minimap、配置保持不变。`layer-receipt.json` 保留实际参数。三项新 Renderer 回归的精确名称和命令在 `renderer-receipt.json`。

旧负例把 `old5ee/prepare.py`、`refresh_patch.py` 放回忽略的 `target/presenter-cpu-old5ee/`；先从明确固定提交导出 `source.zip` 并解压至其中 `source/`，再执行脚本，检查仅 fixture 控制区域变化，然后按收据构建。需要含 5ee 历史对象的明确仓库，普通构建不需要该输入；不要覆盖已保存负例目录。

性能复现把 `copy_perf.rs` 放回 `target/boss-presentation-final/`、`run_copy_comparison.py` 放回 `target/boss-presenter-review/`，并显式准备其记录的旧干净源码目录；脚本用同一基准分别链接旧／新 renderer，交替运行三轮。它只测 renderer，不能推导游戏 FPS。
