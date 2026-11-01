# 性能实测与对照范围

读档等待与卡顿修复的生产内存路径测量另见 [实现对照](runtime-read-review.md)。该记录比较原 C++ 组件、旧 Rust 包 `20fce70` 与修复后的真实读取路径，不与下述合成绘制数字混算；完整游戏场景对照仍待复测。

主要验收依据是旧 C++ 与 Rust + egui 在同一游戏场景中的整体开销。本记录没有完成这项游戏内验收，也没有扣除 Rust、egui、依赖或 FFI 的开销来宣称达标。以下是已执行的离线测量，固定毫秒目标不作为本版硬性门槛。

## 同硬件的受控绘制对照

2026-11-02 资源预算修复后复测：i9-13900KF、RTX 4080、1920×1080、RGB10A2 下层目标。C++ 使用独立 `master` 基线 `afab7e1` 的 Release 生产 ImGui/DX12 renderer 对象；Rust 使用锁定的 egui 0.33.3 和 Release 专用 DX12 renderer。两者绘制同样的 64 或 1,024 个绿色半透明四边形、320 或 1,000 像素区域、相同旋转和全组透明度；圆形合成算法各自沿用其实现。测量发生在提升 workspace 前，数字和原始回执保留为历史证据。

三组串行交替运行，每个场景预热 32 帧、采样 600 帧。未按 60 FPS 限速；每个测量调用后在计时区外排空 GPU。CPU 包含 UI 帧构造、网格转换/ABI staging 和绘制提交。Rust 使用按纹理分组的 egui 网格；C++ 使用原生产 ImGui 绘制列表。均关闭 GPU 时间戳，排除真实游戏、动态插件分发、Minimap 数据采集和资源 update。原始回执记录各场景样本时长，约 0.001–0.40 秒；这些短批次适合排查绘制成本，不能代表长时游戏帧时间分布。

下表是三次运行各自分位数的中位数，单位 µs；不是把三次原始逐帧样本混合后重新计算的分位数。

| 场景 | C++ p50 / p95 / p99 | Rust + egui p50 / p95 / p99 |
|---|---:|---:|
| 隐藏 | 1.6 / 1.7 / 1.7 | 1.7 / 1.7 / 1.8 |
| 稳定 64 个四边形 | 30.6 / 77.2 / 101.4 | 26.2 / 66.8 / 88.3 |
| 大尺寸、旋转、透明，1,024 个四边形 | 50.1 / 100.6 / 137.9 | 34.0 / 83.5 / 121.1 |
| 每帧变更区域尺寸 | 337.9 / 486.8 / 654.1 | 321.4 / 495.5 / 591.0 |

本轮 fixture 中，稳定和大量四边形场景的 Rust p95 低于 C++，尺寸变化场景的 p95 略高；三次运行仍有调度波动。不能从这张表推出游戏内不回归。尺寸变化的高耗时在两个实现中都存在，与离屏资源重建有关，保留为后续优化点。Rust 保留独立提交槽的离屏目标，降低普通绘制同步需求，同时增加大区域时的并行在途资源占用。

C++ 对照的测量线程在预热后 `new` 与 ImGui allocator 调用为 0。Rust fixture 的可见场景为每帧 19 次 Rust 分配：稳定场景约 21.1 KiB、大量四边形场景约 216 KiB；这包含 egui 帧和网格输出，属于实际框架成本。两个计数器均不覆盖驱动、COM 或 C 库内部分配，也不是进程总内存采样。这个通用 fixture 隐藏时仍运行 egui，产生 12 次分配；生产 Minimap 在进入 egui 前即返回，其隐藏探针为 0 分配、0 网格。

Rust 可见场景每帧 2 次 draw，隐藏为 0；正常稳定场景上传为 0。三组无忙槽丢帧。Rust 记录的全部自有 committed GPU 资源峰值约为稳定 3.06 MiB、大区域 17.06 MiB、尺寸变化 4.50 MiB。C++ fixture 只记录了全屏 underlay 的 8.44 MiB 和 4 个 allocator，未统计其全部资源，因此不能用这两个数直接比较总显存。真实 DXGI 驻留、驱动内存和游戏总显存仍缺实测。

原始记录：`docs/evidence/{cpp,rust}-matched-{1,2,3}.jsonl`。2026-11-01 首轮单独启用 GPU 时间戳的 Rust RGBA8 fixture 见 `crates/renderer/evidence/rtx4080-egui-batched-renderer.jsonl`：稳定场景 GPU p95 5.12 µs、大区域 16.384 µs；它是预算修复前的历史记录，与本轮关闭时间戳的 CPU 对照不是同一轮测量，C++ GPU 时间也尚未对照。新的在途上限由阻塞 WARP 测试验证，不由这些短周期、逐次排空的性能循环证明。

## 生产 Minimap 的局部测量

[Minimap CPU 探针](../crates/minimap/PERFORMANCE.md) 使用归档游戏的真实 GFX、布局和 BC7 图集，通过生产绘制函数、egui 文本/曲线处理及 POD 网格转换计时，覆盖隐藏、117 个标记、大尺寸旋转透明和 1,000 个标记。它排除游戏读内存、资源 update、核心 staging 和 GPU，不与上面的合成四边形测试混用。原始游戏素材不随源码或安装包分发。

资源和热路径上限见 [功能对照](../crates/minimap/FEATURES.md) 与 [后端说明](../crates/renderer/README.md)。纹理保留压缩和 mip，解析及 RGBA 转换在工作线程完成；普通绘制不等待 GPU，缓冲、descriptor 和退役资源按 fence 管理。遇到容量不足明确跳过或失败，不能扩大为无界队列。

## 复现

从仓库根目录显式导出包含 `afab7e1` 的 C++ 仓库，再构建对照 fixture。当前分支不保留旧工程，导出目录由工具在忽略的 `target/` 下准备：

```powershell
python tools/export_cpp_reference.py --repository E:/path/to/cpp-repository --output target/cpp-reference
cmd /d /c tools\build_cpp_perf.bat target\cpp-reference
cargo build --release --locked -p eroverlay-renderer --example gpu_perf
./tools/compare_perf.ps1 -CppReference ./target/cpp-reference -Samples 600 -Runs 3
```

暂停其它编译和测量任务，保持相同电源、驱动和显示配置。单独的 GPU 时间戳及 WARP 命令见后端说明。游戏内还需采集相同地图、配置、标记量和样本时长，覆盖隐藏、稳定显示、大尺寸/旋转/透明、地表/地下/DLC 切换、地图碎片变化、标记变化和大量标记，汇总 update/render CPU、分配、draw、GPU 时间、上传和驻留峰值。本任务没有启动或注入游戏，实际游戏内证据保留为验收缺口。
