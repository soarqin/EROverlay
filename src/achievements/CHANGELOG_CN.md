# 更新日志

本项目所有重要变更均记录于此文件。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)。

---

### [0.1.1] - 2026-06-30

#### 变更
- 重构内部数据结构：以命名结构体（`Achievement`、`Locked`、`CollectionFlag`、`UnlockNotification`）替换匿名元组，将计时/动画常量提取至 `timing` 命名空间，并将渲染器切换为 RAII 生命周期管理。无行为变化。

### [0.1.0] - 2025-10-20

#### 新增
- 首次发布。
