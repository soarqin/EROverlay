# 菜单预加载与事件组件证据

本目录仅保存测量和回执摘要，不包含原始 Param、EMEVD、FMG、MSB、旧多语言目录或游戏 EXE。完整说明见 [菜单预加载与原生层级修复](../../boss-menu-layer-fix-2026-11-02.md)。

- `loading-summary.json`：三阶段生产 Loading 样本、资源上限、菜单无效存档契约、条件地名及 207 项审计等价性。保留 profile 时的源码指纹，另列后续仅扩展测试后的源码指纹，避免混淆执行版本。
- `event-profile.json`：相同 555 脚本的前后三轮解析／展开组件样本，以及六份完整 Discovery 快照的指纹。快照指纹一致；原始快照保留在本机 `target/boss-events-profile/`。
- `boss-probe-loading.txt`：实际 Boss DLL 最终探针的三次目录读取及首次有效存档更新摘录。Host 同步读取归档，存档／旗标／写入受控，不代表游戏原生 VFS 或 FPS。
- `boss-dll-probe.txt`、`combined-harness.txt`：最终 Boss DLL 的完整交互回执和正常核心／两插件三轮加载回执；分别标明受控存档和离线隐藏条件。
- `renderer-slot-regression.json`：固定 C++ 源码中的上层槽保护、旧策略丢上层／新策略保留上层的 WARP 像素反例，以及更多上层组不使下层永久饥饿的回归。
- `layer-probe.txt`、`layer-probe-old-minimap.txt`、`layer-fixture-contract.txt`：当前和旧 Minimap 的真实双插件重叠回执、命令与 fixture 边界。fixture core 独立编译，不能安装；生产核心不替换游戏回调。
- `dll-hashes.json`、`pe-exports.json`、`verification.json`：最终安装 DLL、测试 core 的区别、正式导出、普通测试汇总，以及本机六张 BMP 的指纹。BMP 不复制进源码或安装包。
- `clean-source-reproduction.json` 和 `clean-*.log`：独立浅克隆的六项默认检查和独立安装包 manifest 验证；不含本机旧 C++ 导出、游戏数据或共享 target。

此前 `boss-loading-2026-11-02`、`boss-207-scope-2026-11-02` 等历史目录保持不变。当前运行时代码提交为 `ac42ef4e9e445e75c730a0db98b326f74b2a04d3`；安装包最终来源以 `source.json` 为准，包含后续文档与证据提交。
