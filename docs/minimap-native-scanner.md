# Minimap 原生接口扫描

2026-10-20。`GameFiles` 已使用当前进程的 PE 映像定位原生接口，不再计算 EXE 哈希或按游戏版本选择地址。历史 `gameprofiles.inc` 仅作为离线回归期望值。

此前 UI_SCENE 已使用扫描，小地图的文件和数据读取却仍经过哈希白名单。Steam 更新到 1.17.2 后，渲染回调正常执行，但 `GameFiles` 拒绝新哈希，显示状态无法验证，导致整张小地图被隐藏。

运行时扫描复用 `Signature` / Pattern16、`PeImage` 和已有的 [IDA 共享规则](minimap-scanner-signatures.json)。在文件工作线程初始化时扫描一次，解析结果通过原有的原子状态发布；渲染和数据更新只读取已发布结果。

- 两段特征必须归属同一份 PE 异常展开函数；共享代码的完整指令约定同时校验局部分支。
- 文件提交、分配、工厂初始化、完成和退休之间的调用目标必须一致；请求对象分配器的全局引用须位于数据段。事件读取叶函数通过地图碎片读取链确认。
- RTTI、RIP 引用、菜单和地图对象成员、赐福步长及参数仓库布局共同约束扫描结果。相对位移按有符号数解析。
- 缺失、歧义、越界或约定不匹配时返回具体原因，不发布部分结果或调用未验证地址。

扫描器接入时还修复了 Pattern16 在 SIMD 收尾处未应用特征锚点偏移的问题。该问题会漏掉局部函数末尾的匹配；回归测试覆盖 64 种起始对齐及收尾区域的每个合法起点。

审查补充修复了 x64 分支的候选位映射和多候选覆盖问题，并使 SSE2 收尾使用对应指令集实现。短特征和长特征都在受保护的内存页上检查 x64、SSE2 及 CPU 支持的 SSE4.1、AVX2 分支；指令操作数中的伪造 `E8` 不能满足标记添加的调用约定。生成器将掩码后完全相同的规则从 52 条去重为 35 条，保留原有特征、完整指令体和调用偏移。

## 验证

构建 Release 后运行：

```bat
cmd /d /c tools\build_ui_scene_checks.bat
python tools\generate_minimap_scan_rules.py --check
python tools\ui_scene_evidence.py D:\Games\EldenRingExe
```

`ui_scene_evidence.py` 已包含原生接口扫描检查。也可单独运行 `python tools\minimap_scan_evidence.py D:\Games\EldenRingExe`：扫描器不接收历史地址，脚本在得到解析结果后才按 EXE 哈希核对历史期望值。可用 `--extra-exe <path>` 检查归档外的 EXE，用 `--ida-evidence <json>` 对照 `verify_minimap_scanner.py --resolve-only` 生成的独立 IDA 结果。输出分别记录扫描通过和独立证据是否吻合，并包含源码及补充证据的哈希。

本次已核对 1.02～1.17.2 的原生接口地址和布局，1.17.2 同时与独立 IDA 结果一致。负向测试覆盖更改文件哈希、重复候选、改写调用目标、改写请求 ABI、缺失 RTTI、截断 PE 和 ASLR 重定位。UI_SCENE 的版本覆盖及 GPU 检查单独记录在 `build/ui-scene/evidence.json`；原生接口扫描通过不代表旧版渲染入口受支持，也不代替游戏内画面验收。

## 启动日志

更新实际加载的核心 DLL 后，扫描成功会在标准错误输出中记录：

```text
[Minimap] native bindings resolved by signature scan: submit=+... menu=+... owner=+... view=+...
```

扫描失败会记录具体缺失或不匹配的接口。该日志不依赖 `[diagnostics] log_file`；可选文件诊断仍保留资源和标记刷新记录。
