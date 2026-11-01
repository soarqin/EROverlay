# DLL 尺寸分析

2026-11-02，在同一台 Windows x64 机器上检查正式 Release DLL。确定的多余内容是 Minimap 内嵌的三份未使用字体；本次移除后，Minimap 减少 **1,050,624 字节（37.45%）**，两个 DLL 合计减少 **30.36%**。其余差异来自 UI、素材处理的职责分布及各自静态链接的代码，没有发现需要把共享模块移到新 DLL 的依据。

## 实际文件与构建口径

| 实现 | EROverlay.dll 字节 | Minimap.dll 字节 | 合计字节 |
|---|---:|---:|---:|
| C++ `afab7e1` Release | 1,048,064 | 518,656 | 1,566,720 |
| Rust `bc96b83`，修改前 | 654,336 | 2,805,760 | 3,460,096 |
| Rust，本次字体收敛后 | 654,336 | 1,755,136 | 2,409,472 |

这三行是磁盘上的 **DLL 原始长度**，不含 PDB，不是 ZIP 大小或内存驻留量。修复后的 Rust 合计仍比这组 C++ 大 842,752 字节（53.79%）。C++ 与 Rust 使用不同工具链，不能将差额全部归因于语言运行时。

- C++：从 `afab7e14db2b62ae0bede980d5d0c96634321350` 导出到忽略的 `target/cpp-reference-lf/`；正式产物为 `build/bin/EROverlay.dll` 和 `build/bin/overlays/Minimap.dll`。MSVC v142 14.29.30133，Release、`/MD /GL /LTCG`，未生成调试信息。它们是生产构建，不是检查 fixture。
- Rust 修改前：根提交 `bc96b833ba4d9c12cc4ac27c6e96d3a89cfeadaf` 的独立干净 clone `target/clean-root-checkout/` 构建产物，与此前安装包 DLL 哈希一致。根目录调整前后的产物没有混作两个版本。
- Rust 修改后：先在新的 `target/binary-size/after-build/` 完整构建，再在正常根目录 `target/release/` 构建、加载、打包。两次 DLL 长度相同。Rust 1.99.1 MSVC x64、默认 Release `opt-level=3`、ThinLTO、一个 codegen unit、`debug=1`、`panic=unwind`；依赖版本未变。没有使用 `s/z`、UPX、裁掉诊断或移除地图格式。
- 最终安装包的 `source.json` 给出本次提交；[修复前 PE/PDB 证据](evidence/binary-size-before.json) 与 [修复后证据](evidence/binary-size-after.json) 保存来源绝对路径、SHA256、各节、导入、PDB 身份和字体匹配。路径标识测量现场，不是构建依赖。

修改后核心的 `.text/.data/.pdata/.reloc` 与修改前逐字节相同，导入相同；`.rdata` 差异只在三处调试目录时间戳和 CodeView PDB GUID。核心源码、ABI 和运行行为没有变更。已有 `bc96b83` Rust 安装只需替换 `overlays/Minimap.dll`；完整新包仍带同次构建的核心。不能与旧 C++ DLL 混装。

## 字体是主要可修复来源

修改前 `egui/default_fonts` 启用了完整默认字体集。扫描 DLL 中完整字体字节序列，而不是按 crate 包大小估算，发现：

| 字体 | DLL 内实际字节 | 本次处理 |
|---|---:|---|
| Ubuntu-Light.ttf | 361,676 | 保留，比例文字后备 |
| Hack-Regular.ttf | 309,408 | 移除，生产界面不用等宽文字 |
| NotoEmoji-Regular.ttf | 418,804 | 移除，生产界面不用文字 emoji |
| emoji-icon-font.ttf | 317,864 | 移除，地图图标来自游戏图集／矢量素材 |
| 合计 | 1,407,752 | 保留 361,676 |

这四份位于 Minimap 的 `.rdata`，占修改前 DLL 的 50.17%、该节的 87.37%；核心没有这些字体。移除字体原始载荷 1,046,076 字节；DLL 额外减少的 4,548 字节是字体初始化代码、链接及节对齐的净变化，不能也算成字体文件。

旧 C++ 的字体载荷也实际存在：[C++ 源码与 MAP 证据](evidence/binary-size-cpp.json) 验证核心包含未压缩的 Open Sans 130,832 字节；两个 DLL 又各带 ImGui 的 ProggyClean 压缩数据 9,583 字节和 ProggyForever-minimal 14,562 字节，合计 **179,122 字节**。不能把 C++ 原 TTF 长度当作其压缩载荷，也不能漏掉 Minimap 中的 ImGui 字库。

因此修改前总差额 1,893,376 字节中，已精确定位的字体净增加为 1,228,630 字节（约 64.89%）；剩余 664,746 字节是代码、其他数据及链接结构的合计差额，不是“Rust runtime”的精确费用。修改后保留字体比 C++ 两 DLL 的全部已核实字体载荷多 182,554 字节。

本次直接关闭 egui 的 `default_fonts`，仅引用已经锁定的 `epaint_default_fonts::UBUNTU_LIGHT`。没有新增依赖版本，也没有新抽象或跨 DLL 字体服务。生产绘制仍只使用比例字体；为 egui 两个字体族注册同一个后备，防框架默认文字查询缺族。

保留 `common.font` 自选字体、按语言选择的 Meiryo／Malgun／LeelawUI／MSYH 及原路径优先级。初始化时校验当前四条中文资源错误所需字形；主字体不足且 MSYH 可用时才补入。所有外载字体都不可用时，保留数字、英文、波兰文和俄文字形，并将四条资源错误静态回退到英文，避免方框。已有韩文／泰文字体在本机不包含这些中文字形，新增后备也修正了该情形。字形检查、文件读取和后备选择都在初始化完成，帧内只读缓存的布尔值，无新增 IO／字体扫描／字符串分配。普通加载和单块瓦片错误继续静默。

这是内嵌冗余收敛，不是删除语言功能；但不承诺在操作系统和自定义字体全部缺失时提供完整 CJK／泰文／韩文字库。本机没有 Meiryo，日文路径实际采用 MSYH；该主字体缺失及其他降级均有注入文件读取器的回归覆盖。字体 cmap 与语言检查见 [字体证据](evidence/binary-size-fonts.json)。

## 为什么核心变小、Minimap 变大

| 文件 | `.text` 原始字节 | `.rdata` 原始字节 | `.data` 原始字节 | `.pdata` 原始字节 |
|---|---:|---:|---:|---:|
| C++ 核心 | 678,912 | 201,728 | 133,632 | 30,720 |
| C++ Minimap | 404,992 | 88,064 | 2,560 | 20,480 |
| Rust 核心，前后相同 | 466,944 | 162,304 | 1,536 | 20,480 |
| Rust Minimap，修改前 | 1,159,680 | 1,611,264 | 512 | 31,232 |
| Rust Minimap，修改后 | 1,155,584 | 564,736 | 512 | 31,232 |

表格未列 PE 头、资源节和重定位节，完整数据在 JSON 中。旧核心的大 `.data` 主要含 Open Sans 字节数组。Rust Minimap 的此次收益集中于 `.rdata`，并未靠重编译整个绘制器来压缩 `.text`。

C++ MAP 的去重命名地址跨度（包含对齐，**不是完整精确函数体大小**）显示：核心有 ImGui 主体约 269,968 字节、平台／GPU 后端 13,312 字节，以及旧通用纹理加载器中 `stb_image` 约 66,488 字节；Minimap 自己也含约 194,107 字节的 ImGui。这些不是两个 DLL 简单共享一份 ImGui。`stb_image` 数字是原对象内子集，不能再加到该对象大小上。

Rust 核心负责扫描、游戏／文件／纹理服务、插件生命周期、DX12 提交及合成，不包含 egui 或字体；所需图片链为 DDS／RGBA，未保留旧核心通用 PNG／JPEG／HDR 解码器。插件私有持有 egui context、文字布局和网格生成，核心只接收稳定 C ABI 网格。这样避免将 Rust／egui ABI 或分配器所有权跨 DLL，代价是 Minimap 自己携带完整的 UI 路径。

修复后匹配 GUID/age 的 PDB 显示下列去重编译单元归属（`.text` / `.rdata` 字节）。这是 **链接归属**，ThinLTO 内联／泛型可能把调用方代码计入单元，不能当作可删除组件的独立费用：

| Rust 编译单元 | 核心 `.text` / `.rdata` | Minimap `.text` / `.rdata` |
|---|---:|---:|
| 项目及其单态化 | 158,132 / 45,743 | 338,595 / 411,580 |
| egui | 0 / 0 | 321,977 / 50,206 |
| epaint | 0 / 0 | 174,340 / 25,877 |
| ttf-parser | 0 / 0 | 81,794 / 5,720 |
| ab_glyph | 0 / 0 | 16,416 / 1,408 |
| Rust std | 62,446 / 22,136 | 48,595 / 16,617 |
| Rust core | 41,455 / 12,482 | 47,480 / 24,148 |
| rustc_demangle | 23,187 / 3,024 | 23,187 / 3,064 |
| 游戏 crate | 88,057 / 40,854 | 10,668 / 1,402 |
| texture2ddecoder | 0 / 32 | 8,413 / 3,838 |
| quick-xml | 0 / 0 | 8,679 / 1,462 |
| miniz_oxide | 0 / 0 | 15,432 / 3,546 |

调整前字体归属在 epaint 单元；调整后保留的 Ubuntu 归属在 Minimap 单元。因此不能把两个单元变化分别称为代码增减。原生 `scene.cpp` 在核心约 8,192 字节（代码／只读／可写数据合计），Minimap 不含它，不是 Minimap 膨胀原因。

Rust 标准库、panic／诊断所需符号在各独立 DLL 中静态链接；两种实现仍动态导入 MSVC／Windows 运行库，Rust 也不是“整套 CRT 静态链接”。Minimap 的导入只有 Win32、同步、随机数、NT、VCRUNTIME 和 UCRT，未导入 D3D12／shader 编译器；这些属于核心。完整符号与 DLL 名称在 PE 证据中。PDB 的外部大小（核心 3,887,104、Minimap 6,033,408 字节）没有进入 DLL 或安装包；`debug=1` 不能解释几 MB 的 DLL 载荷。

## 排除的结构性猜测

- `cargo tree -d` 没有重复版本。Minimap 虽声明使用游戏 crate，生产 PDB 的实际函数只含 `wide_string`、binder／TPF／DDS 解析和 BC7 解码，不含 `GameServices`、签名扫描、文件服务、旗标或进程内存读取。链接器没有把整套核心带入插件。
- BC7 外部函数体为 8,413 字节；其他未用解码器没有一并链接。核心仅残留该编译单元 32 字节只读数据，无解码代码。不能为几十字节增加 feature／动态库边界，更不能删掉 BC7 支持。
- 两 DLL 明确重复的素材辅助函数体只有 `wide_string` 390、`parse_dds` 906、`parse_tpf` 775 字节，共 2,071 字节；引入共享 DLL 或扩展 ABI 搬移它们收益很小，增加部署／所有权复杂度，本次不做。
- `ttf-parser` 的直接引用与 `ab_glyph` 链共用 0.25.1；仅关闭直接引用的默认 feature 不会关闭间接依赖已启用的同一 feature。它用于字体校验／覆盖检查，不能按声明依赖推断重复载荷。`quick-xml` 无默认 feature、`miniz_oxide` 的分配支持分别服务真实布局解析／解压。
- 没有把模块压缩或移到磁盘旁路来掩盖 DLL 长度，也没有改变双 DLL、UI_SCENE 或帧性能编译策略。保留 unwind 和错误诊断，未为尺寸换成不允许 panic 隔离的 abort。

## 验证和性能边界

本轮 fmt、workspace check、all-targets clippy `-D warnings`、workspace 113 项测试通过（5 项依赖外部素材／性能环境的测试默认跳过）；Python 工具测试 6 项通过。新的生产字体回归覆盖：三份默认字体不再需要、全部文件缺失／损坏、主字体字形不足、自选路径、语言选择、真实错误文字绘制和非空网格。显式 Windows 七语言测试通过，真实读取本机 en/pl/ru/ja/ko/th/zh 对应字体并检查错误与编号字形。

最终 Release DLL 在 RGB10A2 WARP 下实际装卸载：普通模式三轮，每轮 19 次消费层回调；合法上层插件模式三轮，每轮 48 次，包含纹理绘制及 GPU 在途关停，均通过。[验证证据](evidence/binary-size-validation.json) 保留结果、日志哈希和核心节对比。离线无游戏状态时 Minimap 隐藏，不能将该检查当作地图可见的游戏内验收；后者由以下生产绘制探针补充。

在 i9-13900KF 上，前后各三轮交替运行同一归档 GFX／布局／BC7 图集的生产绘制、egui tessellation 和 ABI sink；1920×1080，每场景预热 100、采样 1,000 帧。位置为受控输入，排除 update、资源 IO 和 GPU。[全部观测](evidence/binary-size-probes.json) 包括：

| 场景 | 修改前 p50 范围 μs | 修改后 p50 范围 μs | 前后每帧分配次数／字节 | 前后网格数 |
|---|---:|---:|---:|---:|
| 隐藏 | 0 | 0 | 0 / 0 | 0 |
| 117 标记稳定显示 | 42.7–43.1 | 43.1–43.7 | 427 / 213,708 | 81 |
| 大尺寸／旋转／透明 | 42.3–42.7 | 43.0–43.5 | 427 / 213,708 | 81 |
| 1,000 标记 | 341.7–344.4 | 343.1–345.4 | 3,505 / 1,743,060 | 672 |

前后顶点／索引数完全一致，稳定期上传均为零。大量标记的 p99 前三轮为 435.0/435.5/454.3 μs，后三轮为 433.6/530.9/431.2 μs；后第二轮有尾延迟波动，保留原值，不宣称 p99 已严格等价。这个有限 CPU 探针支持“没有新增逐帧工作或分配”，不能证明完整游戏帧耗时无回归，也不是 C++／Rust 同场景游戏性能结论。本轮没有测真实 GPU 帧时间、RSS 或游戏 FPS；硬件 RTX 4080 没用于 WARP 的 GPU 性能归因。

`SizeOfImage`：核心 663,552 字节不变，Minimap 从 2,822,144 降到 1,769,472 字节。这是 PE 映像地址范围，不是当前物理驻留或显存。系统／自选字体由初始化加载，其字节不会出现在 DLL 中；韩／泰字体需要中文后备时可保留额外 MSYH 数据，不能将 DLL 减少量直接宣称为运行 RSS 收益。

## 复现

普通构建不需要旧分支。先构建 Release，使用 Python 标准库和已有 LLVM 工具检查：

```powershell
cargo build --workspace --release --locked
cargo tree -d
cargo tree -p eroverlay-minimap -e features
cargo tree -p eroverlay-core -e features
python tools/analyze_binary_size.py core=target/release/EROverlay.dll minimap=target/release/Minimap.dll --pdb core=target/release/EROverlay.pdb --pdb minimap=target/release/Minimap.pdb --font-dir <Cargo缓存中的epaint_default_fonts-0.33.3/fonts> --llvm-bin <LLVM的bin目录> --output target/binary-size/recheck
python -m unittest discover -s tools -p 'test_*.py'
```

工具逐个记录 DLL 与匹配 PDB 的 GUID/age，PE 节和完整导入，按完整字体数据查找偏移；PDB 节贡献先做区间并集，避免重复计算 ICF 别名，保留共享区间并明确 LTO 限制。LLVM 原始 readobj／PDB 输出写到指定忽略目录；仓库证据只保留精简数值，不签入 DLL、PDB 或巨型 dump。

C++ 对照通过 [明确基线导出](source-layout.md) 获得，Release 选项按上文配置。需源码／函数归因时，可对该导出已编译的对象和原 link response file 在独立输出目录增加 `/MAP /DEBUG:NONE /OPT:REF /OPT:ICF`；本轮验证原 DLL 与 MAP 重链接 DLL 的长度和各节，除 `.rdata` 的 POGO 时间戳两字节外一致。另做 `/DEBUG:FULL` 的诊断链接会改变代码／重定位，未拿它作为尺寸基线。

字体／归档可选检查：

```powershell
cargo test --release --locked -p eroverlay-minimap installed_language_fonts_cover_status_and_produce_text_meshes -- --ignored --nocapture --test-threads=1
# EROVERLAY_GFX / EROVERLAY_LAYOUTS / EROVERLAY_TPF 指向自行保留的游戏素材；不随仓库分发。
cargo test --release --locked -p eroverlay-minimap archived -- --ignored --nocapture --test-threads=1
```

升级前须完全退出游戏。从 `bc96b83` 基线升级只需替换 `overlays/Minimap.dll`；从此前 `c519cdb`、`86cd403` 等旧包升级，必须同时替换 `EROverlay.dll` 和 `overlays/Minimap.dll`。保留现有 `configs/`，包括 `[diagnostics]` 中的 `log_file` 设置，不要用包内默认配置覆盖。

本次没有操作游戏安装目录、启动／注入游戏、改写 C++ 分支或推送远端。
