# 更新日志

本项目所有重要变更均记录于此文件。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)。

---

### [Unreleased]

#### 变更

- 优化成就数量上限和面板布局的配置说明，新增 `configs_CN/` 中文注释模板。

#### 修复

- 修复 `achievements.panel_pos` 含非法数值时解析抛出异常的问题；无法解析出数值的字段按 `0` 处理。

#### 升级说明

- 面板位置解析修复编译在 `Achievements.dll` 内，需重新编译或更新此 DLL；仅更新 `EROverlay.dll` 不会应用此修复。
- 切换到 `UI_SCENE` 渲染器本身无需迁移 Achievements API 或重新编译。此覆盖层未使用已变更的快捷键 API 或新增的原生 API 扩展，仍在游戏 UI 上层绘制。

### [0.1.1] - 2026-06-30

#### 变更
- 重构内部数据结构：以命名结构体（`Achievement`、`Locked`、`CollectionFlag`、`UnlockNotification`）替换匿名元组，将计时/动画常量提取至 `timing` 命名空间，并将渲染器切换为 RAII 生命周期管理。无行为变化。

### [0.1.0] - 2025-10-20

#### 新增
- 首次发布。
