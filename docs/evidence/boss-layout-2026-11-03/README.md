# 本轮证据范围

代码提交为 `6a19db4d86163ae2bd81c71197f55c64a1340bdc`，起点为 `f721010`，C++ 对照固定为 `afab7e1`。`checks.json` 记录实际源码哈希、普通检查和正式 DLL；`verification.json` 汇总布局、真实组合测试及各自边界。构建后只有文档和这些证据新增，发行包的 `source.json` 指向最终文档提交。

- `final-region-audit/`：生产读取、事件与成员审计，`inputs.json` 指纹覆盖 612 项实际输入。只打包审计结果，不含 Param、FMG、EMEVD、MSB 或 EXE 文件。
- `old-to-new-regions.csv`／`.json`：207 场完整地区对照；`groups.json` 为 32 个大区数量，近邻及三个地点关系见 `region-evidence.json` 和 `neighbourhood-fallbacks.json`。
- `normal-final-06/`：最终正式 Boss DLL、归档资源、受控宿主与 WARP，未加载核心；包括三态字形观测、输入、复活拒绝和保护页验证。
- `fixture-final-05/`：有只读布局观测的 Boss feature DLL，不进入安装包；此处 Minimap 使用上一版 `f721010` 字节，专测原 176 字节前缀兼容。最终正式 Minimap 另由 normal 探针及核心组合测试覆盖。
- `negative-f721-columns/`：旧正式 Boss 同一字形路径预期退出 1，名称 x 为 877／868／872。新正式 DLL 三态均为 877。
- BMP 均为受控 WARP 读回，包括正式 Boss 两种状态及共享 renderer 重叠检查，属于离线证据，不是游戏截图。其余图片的哈希和本地来源保留在探针 receipt 中。

重现命令和限制见各 receipt 及 [修复记录](../../boss-layout-regions-2026-11-03.md)。此前闪烁与菜单预加载没有获得用户现场确认，本轮不改写其验收状态。
