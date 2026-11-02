# Boss 状态样式证据

代码提交 `3ca251b942b4c588e28ed3fefe796aa72f7c21b6`，基线 `daf3539`。

- `checks.json` 和同名日志：根目录检查、205／0／12 默认测试、正式三 DLL 哈希。
- `normal-final-09/`：最终正式 Boss DLL 完整输入、受控复活、隐藏和 WARP 验证。
- `feature-final-08/`：只读观测 DLL 的 207 项／32 大区对照、自动折叠和重读轨迹。
- `chinese-final-07/`：实际中文 UI 的五个尺寸／权限 case 和输入回执。
- `images/`：五张代表画面的完整像素 PNG 及无损裁剪 PNG；完整转换坐标见 `png-conversion-receipt.json`，选择及输出哈希见 `verification.json`。原始 WARP BMP 保留在本地探针目录，未重复签入体积较大的容器。
- `chinese-assets.json`：从本机游戏 Data0 只读提取的中文文本来源、字节数和哈希，不包含 BND／FMG 资产。
- `multi-dll.log`、`two-plugin-basic.log`：正式核心／两插件三轮及受控原生入口的共享 renderer 基础兼容检查。原生入口、核心和 Minimap 源码未改。

图片为真实 DLL／egui／WARP 输出，中文名称来自真实 FMG。游戏状态和资源延迟由受控宿主提供；观测版含测试故障钩子和只读 POD 控件记录，界面使用同一生产绘制路径。正式包不含观测导出、测试 DLL、EXE 或游戏资源。40 字号窄面板通过真实滚动逐行验证；只有常用字号的选图保证三态完整同屏。

此前帧连续性和菜单预加载的现场验收仍待用户确认。basic 的有意 skip／恢复测试不能用来宣称闪烁已通过，CPU 样本也不代表游戏 FPS。
