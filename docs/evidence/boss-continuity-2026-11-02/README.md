# Boss 连续显示证据

代码提交：`0819f40da74f8fcaf916a1ba62d9b70f60e4e913`。本目录记录离线验证；`411bc63` 的用户反馈仍是最新现场结果，新包闪烁尚未现场验收，菜单预加载也尚未得到现场确认。

## 先看这些文件

| 文件 | 用途 |
|---|---|
| [verification.json](verification.json) | 普通检查、最终安装 DLL、逐帧分类、边界和原始图片指纹 |
| [layer-receipt.json](layer-receipt.json) | 最终两轮实际 DLL／探针 SHA-256、命令及关键帧 |
| [dll-hashes.json](dll-hashes.json)、[pe-exports.json](pe-exports.json) | 最终三个正常安装 DLL，与测试核心和故障钩子 DLL 分开 |
| [中间版本 DLL 指纹](negative-dll-identities.json) | 两项中间版本反例实际使用的核心、Boss、Minimap 及保存位置 |
| [生产插件日志](final-production-12-duplicates.txt)、[逐帧记录](final-production-12-duplicates-frames.txt) | 120 个稳态帧及 326 个矩阵呈现，实际 Boss 与 Minimap |
| [故障插件日志](final-fixture-13.txt)、[逐帧记录](final-fixture-13-frames.txt) | 120 个稳态帧及 336 个矩阵呈现，另加真实快照／UI 锁持有 |
| [Renderer 证据](renderer/renderer-evidence.json) | 四槽全忙、16 次独立合成、双 fence 退休及新增复制成本 |
| [正常三 DLL](multidll.txt)、[额外上层插件](multidll-upper.txt) | 使用最终正常核心，各三轮实际装卸载、viewport 和 GPU 退休 |
| [实际输入／PIN](native-input.txt) | 独立 fixture 核心的真实隐藏 HWND、Win32／DirectInput、MinHook 和停止常驻 |
| [最终归档探针](boss-probe-final.txt) | 最终正常 Boss／Minimap，真实归档生成 207 项、设置／搜索／复活保护及隐藏；存档／写入受控 |
| [独立源码重现](clean/clean-source-reproduction.json) | 全新干净浅克隆、默认 feature、无游戏／C++ 资产；183 项通过、133 项 manifest 完整匹配 |

日志仅替换本工作树绝对路径并去除混合原生宽字符输出中的 NUL，未修改结果。原始文件仍在忽略的 `target/boss-continuity-review/`、`target/boss-presentation-final/` 和 `target/boss-continuity-clean-evidence/`；嵌套收据的原始 artifact 指纹对应这些原文件，最终安装包内文件另由 manifest 校验。保留日志末尾空行和 unified diff 的空白上下文行。BMP 只保留指纹，不分发游戏资源；逐帧清目标／独立回读使用生产路径，结果不依赖旧目标像素。

## 五项反例

1. [旧重复序号](negative-duplicate.txt)：旧 `411bc63` 实际 DLL 在刚清目标的重复帧丢失 26,460／26,940 个覆盖像素。
2. [旧图集更新](negative-atlas.txt)：旧实际 DLL 首次输入新字符时丢失全部覆盖。
3. [旧快照锁](negative-snapshot.txt)：只对固定旧源码添加故障钩子，同帧丢失全部覆盖。[源码收据](old411/source-receipt.json)与[唯一 hook 差分](old411/fixture-only.patch)区分旧产品代码和控制入口。
4. [旧图集准入](negative-atlas-admission-old-ui.txt)：中间 DLL 仍链接旧共享 UI，真实创建返回 0 时网格仍增加，新旧图集坐标混配。新实现等待约 0.53 秒完成两次拒绝／退避后进入准备状态，期间逐帧保持面板。
5. [缺上传维护](negative-duplicate-upload-maintenance.txt)：中间核心在重复序号期间停在 `Uploading`，两秒后仍不释放源字节；最终核心不推进 UI 也能达到 `Ready`、源字节归零并保持覆盖。

前两项原旧 DLL、第三项 backport DLL 和后两项中间版本均单独保留，失败是预期的反例，不以这些 DLL 交付。`old411/export-recipe.py` 须放回 `target/continuity-old411/` 后在有该固定提交的源码仓库执行；它不更改分支或引用。

## 当前通过范围与硬界

每轮矩阵区分 `Boss`、`Hidden`、`Lower`、`Closing` 和 `Overload`。稳定应显示 Boss 的 263／273 个矩阵帧全部满足覆盖，另各有 120 稳态帧；28 个真实隐藏和 5 个显式关闭／下层可见帧分别检查，不能用长期隐藏 Minimap 通过。新 atlas pending／创建拒绝、重复／交错序号、六帧在途、GPU／gate／绘制包／插件锁竞争、未知显示／存档、真实隐藏后未知、关闭、逐步 resize 和停止均有独立呈现检查。

显式重新打开窗口保留 egui 淡入，观测第 5 个呈现达到严格覆盖阈值，测试设定固定 11 次上界；一旦可见，不再放宽后续覆盖。人工完全暂停 GPU 后连续排入 24 帧，固定池耗尽产生 6 帧缺图，排空后下一帧恢复。Renderer 单独测试则在四个几何槽全忙时连续合成 16 次，第 17 次触及明确硬界。以上过载和开关动画不计入普通连续性通过；初次尚未有成图的丢弃计数也不冒充稳态失败或零丢弃保证。

独立干净 clone 的构建路径会改变 DLL 字节，它的哈希不能替代本目录的最终安装 DLL。正常安装核心没有任何测试控制导出；像素矩阵使用单独 `layer-order-fixture` 核心，游戏输入、存档和 U 阶段内容受控，UI、网格／纹理所有权、原生 L／U／H 适配器及 GPU 提交是真实生产代码。

## 复现

从源码根目录使用新的输出目录；fixture DLL 不得安装：

```powershell
cargo build -p eroverlay-core --release --locked --features layer-order-fixture --target-dir target/layer-order-fixture
cargo build -p eroverlay-harness --release --locked --bin layer_order_probe
./target/release/layer_order_probe.exe ./target/layer-order-fixture/release/EROverlay.dll ./package ./target/layer-continuity-new all
```

第二轮在独立输入目录只把 Boss 替换成 `cargo build -p eroverlay-boss --release --locked --features continuity-fixture --target-dir target/continuity-fixture` 生成的测试 DLL；正常 Minimap 和配置保持一致。最终实际执行路径和参数见 `layer-receipt.json`。复现 GPU 复制微基准时，把 `renderer/copy_perf.py` 与 `renderer/run-copy-perf.ps1` 放回 `target/boss-presentation-final/`，将脚本 `$root` 设为本地源码根目录后执行；该 renderer-only 测量不包含游戏或 egui。

真实归档与原生输入探针的命令和输入要求沿用 [Boss 验证](../../boss-input-validation.md)。普通构建、默认测试和独立源码重现不需要这些外部资源。
