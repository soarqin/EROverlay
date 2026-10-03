# 更新日志

本项目所有重要变更均记录于此文件。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)。

---

### [Unreleased]

#### 新增

- 通过 `getEROverlayNativeAPI(1)` 提供原生资源扩展，支持异步读取游戏文件、地图快照、参数表和事件标记。通过 EXE 哈希和入口字节校验，在不支持的游戏版本上禁用原生接入。
- 新增 Overlay 自有 DDS 纹理，支持异步上传、上传与绘制 fence，以及纹理和 SRV 的延迟回收。
- 为已核对的 1.02～1.17 历史 EXE 和 Steam 1.17.1 增加原生资源适配表，保留哈希与入口校验，并通过 size-gated 扩展提供版本布局。

#### 变更

- 整理英文 INI 注释，澄清单位、示例和实际行为；新增 configs_CN/ 中文注释模板，相关模板纳入 Boss 和 Minimap 分发包，键名和默认值保持一致。
- Minimap 诊断日志改用分区配置中的 `[diagnostics] log_file`，不再读取旧 `native_log` 键。

- 统一使用 Win32 Virtual-Key 代码和修饰键组合处理快捷键。仅在游戏位于前台时响应，通过 `inputIsKeyDown` / `inputIsKeyPressed` 允许多个插件响应同一次按键。
- TPF 一次解析后发布各个 DDS 的读取结果，取消原先八个名称的限制；缺失项不再令其他有效图集读取失败。PARAM 读取支持 12/24 字节目录并检查行边界。

#### 修复

- 为交换链的每个后备缓冲区使用独立离屏渲染目标，修复离屏合成残影。
- 修复查找游戏窗口时将 ELDEN RING 窗口标题误当作窗口类名的问题。
- 修正直接存储的事件标记读取（`type=2`），使小地图正确读取地图碎片进度。

### [1.3.0] - 2026-06-30

#### 修复
- 改进 D3D12 设备丢失恢复：渲染器现可从 `Present`、`Present1`、`GetDeviceRemovedReason` 及命令分配器/列表失败中检测设备移除/重置事件，释放所有设备资源（含通过 `pluginsDestroyRenderers()` 释放插件渲染器），并等待新设备而非永久禁用覆盖层。
- 稳定 DX12 命令队列捕获：仅捕获直接命令列表类型队列，`ExecuteCommandLists` 钩子现独立安装/移除，以便在队列捕获重置后重新挂载。交换链创建钩子在设备存在时从设备捕获队列。
- 修复 SRV 描述符堆分配使用了错误的描述符大小（误用 `rtvDescriptorSize_` 而非 `srvDescriptorSize_`），并将 SRV 堆扩容至 1024 个描述符。
- 强化钩子安装，增加逐钩子错误日志并在禁用时通过 `MH_RemoveHook` 清理；`hook()` 现返回 `bool`，加载器会重试直至成功。
- `screenState()` 现读取稳定的 16 位屏幕状态值。
- 为插件的加载/更新/渲染调用增加互斥锁，并新增 `renderersLoaded` 标志，使 `pluginsRender()` 在渲染器（重新）加载期间直接返回。
- 修正 `SetSourceSize` 钩子签名，使用 `IDXGISwapChain2`。

### [1.2.0] - 2026-03-23

#### 新增
- 插件接口新增离屏渲染 API（`createOffscreen`、`destroyOffscreen`、`beginOffscreen`、`endOffscreen`），允许插件将内容合成为单次 alpha 通道渲染。

#### 变更
- 更新依赖：fmt v12.1.0、ImGui v1.92.6、inih 最新 HEAD、MinHook 最新 HEAD。

### [1.1.0] - 2026-03-21

#### 修复
- 修复 `backBuffer_` 未零初始化的问题，该问题可能导致交换链缓冲区获取失败时发生崩溃。
- 修复 `LoadTextureFromFile` 中文件大小查询失败时的文件句柄泄漏。
- 修复 `createDevice` 中的 COM 对象泄漏（适配器及中间交换链在部分错误路径上未被释放）。
- 在 `HeapDescriptorAlloc` 中添加描述符池耗尽时的越界访问保护。
- 修复插件配置 API 中的线程安全问题：值缓冲区改用 `thread_local`，避免数据竞争。
- 修复游戏地址为空时 `screenState` 返回值不一致的问题。
- 修复插件加载器日志消息中的拼写错误。
- 针对 NVIDIA Optimus（混合 GPU）添加 TIER 1 设备丢失防御性检查，实现优雅降级：检测到 D3D12 设备移除事件时，覆盖层将静默禁用自身，而不再导致游戏崩溃。

### [1.0.0] - 2025-10-20

#### 新增
- 覆盖层功能拆分为独立 DLL 模块，放置于 `overlays/` 文件夹中。
- 新模块：**Achievements**（`v0.1.0`）—— 追踪 Steam 成就进度。
- 新模块：**Minimap**（`v0.1.0`）—— 显示游戏内小地图。
- 每个覆盖层模块现在拥有独立的版本号。
- `EROverlay.ini` 拆分为 `configs/` 文件夹中的各模块独立 `.ini` 文件，以灵活支持各覆盖层模块。

#### 修复
- 尝试修复启动崩溃问题（感谢 [samjviana](https://github.com/samjviana) 在 [#8](https://github.com/soarqin/EROverlay/issues/8) 中的建议）。
