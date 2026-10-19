# MapForGoblins DLL 双边适配

此目录为 [VirusAlex/ERR-MapForGoblins-DLL](https://github.com/VirusAlex/ERR-MapForGoblins-DLL/tree/72239d1148470ab03b1e3ff869b878625807a089) 提供只读地图接口补丁。基线为提交 `72239d1148470ab03b1e3ff869b878625807a089`，版本 `2.1.6`。Minimap 自动连接同进程中已加载的 `MapForGoblins.dll`；需要同时使用带补丁的 DLL，并在 `minimap.ini` 中启用 `[markers] others = true`。

原仓库的默认实现自行创建地图显示对象，并把新增 PARAM 行的显示掩码清零；图标则在游戏加载 GFX 时从内嵌资源追加。因此，只读磁盘 GFX 和 PARAM 无法完整复现它的标记。这个机制与修改地图文件的旧版 Map for Goblins 不同。

## 构建配套 DLL

在上游仓库根目录应用补丁：

```powershell
git checkout 72239d1148470ab03b1e3ff869b878625807a089
git apply --check E:/Projects/EROverlay/tools/compat/mapforgoblins/map-api.patch
git apply E:/Projects/EROverlay/tools/compat/mapforgoblins/map-api.patch
```

随后按上游说明配置 `tools/config.ini`、生成对应游戏或整合包的数据，再编译。例如原版游戏使用：

```bat
py tools/generate_menu_icon_tags.py
build.bat release --vanilla
```

该基线的 `build.bat` 未调用菜单图标生成器，因此首次构建需要先执行上面的生成命令。Python 工具还需要 Pillow；在 pythonnet 中调用 SoulsFormats 时，需要让 `libzstd.dll` 和游戏自带的 Oodle DLL 可被加载。

补丁适用于默认的原生标记实现，各整合包仍需使用各自生成的数据。`MFG_LEGACY_PIN_MARKERS=1` 的备用构建不导出可用接口。不要把 Vanilla 版 DLL 用于 ERR、Convergence 等整合包。

部署时保留原有加载方式，替换配套的 `MapForGoblins.dll` 和 `overlays/Minimap.dll`，再重启游戏。无需调整两者的加载顺序；Minimap 会重试发现稍后加载的模块。没有兼容接口时，Minimap 继续显示原有地图内容，并在可选诊断日志中记录原因。

## 接口约定

公共 C ABI 头文件为 [`src/minimap/compat/mfg_map_api.h`](../../../src/minimap/compat/mfg_map_api.h)，上游补丁包含相同副本。Minimap 请求 `MfgGetMapAPI(MFG_MAP_ABI)`，通过 `view_points(layer, &points, &count)` 直接遍历提供方的只读标记数组。两侧使用相同的接口定义并同时重新编译；接口检查版本、结构大小和元素大小。

`view_points` 返回借用的只读标记视图，在 Minimap 的更新线程同步消费；绘制线程只读取 Minimap 发布的快照。视图底层缓冲区由提供方按调用线程复用。返回的指针有效至同一线程下一次调用 `view_points`、该线程退出或 DLL 卸载；其他线程的接口调用、原生布局刷新和图标创建不会改写这份视图。调用方不能重入 `view_points`，也不能修改、释放或跨有效期保存视图。遍历视图时不持有提供方的布局锁或深度锁。

提供方复用原生标记的显隐与错位计算，直接填入 ABI 缓冲区，省去中间的 `NativeMarkerPoint` 数组。缓冲区保留容量，按原生深度排序后返回；Minimap 无需申请接收数组、扩容重试或再次排序。这仍需计算当前显隐与坐标，并构建 Minimap 自有的绘制快照，不能将整个刷新过程称为零复制。调用方在使用接口和返回指针期间持有模块引用；图标字节仅在首次导入时复制和解码。

- 标记沿用上游的显隐和位置计算，包含完成标记、焦点圈、分类开关、手动隐藏和位置错开。参数注入完成前返回空视图。
- 图标使用稳定的源编号，独立于运行时追加的 GFX 帧号。位图和局部变换来自上游内嵌的 `DefineBitsLossless2` 与 `MATRIX`，不会覆盖同编号的原版图标。
- 图标仍位于游戏的 `ItemList` 中，容器顺序来自加载的 GFX。已创建的 Mod 子对象使用其实际深度，地图关闭后保留最近顺序；尚未创建的对象按上游快照顺序及普通/焦点圈深度段排序。没有 Boss 类别置顶。
- 图标目录仅导入一次，CPU 数据发布后保持不变；GPU 上传、重建和闲置回收复用现有纹理流程。解码支持调色板 RGBA 和 ARGB，并还原为直通透明度。目录数量、单图尺寸和总内存均有限制。
- 空数据或初始化未完成时返回 `MFG_MAP_OK` 和空指针／零数量；无效参数、超限及内部失败同样清空输出，并返回对应错误码，不返回部分数据。跨 DLL 边界没有 STL 对象、堆所有权或异常；上游的参数、事件和存档不通过此接口写入。

小地图继续使用自己的缩放和显示范围。此接口不复制上游地图菜单，也不读取游戏显示对象的私有内存布局。

## 离线验证

在 Visual Studio x64 Native Tools 命令行中运行：

```bat
tools\compat\mapforgoblins\verify.bat patched-upstream-dir actual-minimap-fixture-dir
```

第一个目录必须已应用补丁，并用上游生成器生成 `src/generated_shared/goblin_map_icons.cpp`。第二个目录提供现有地图回归素材：`vanilla-worldmap.gfx`、`swapped-layer-worldmap.gfx`、`common-layouts.bnd`、`common-atlases.tpf`，以及 `vanilla-params/` 下的两个 PARAM 文件。

验证使用实际补丁代码和上游生成的图标，替换游戏状态来源，在自身进程内连接正式 Data、Resources 和 ImGui Renderer。覆盖两种位图编码、透明度、错误输入、跨 CRT 调用、首次初始化、三层地图、显隐、源编号冲突、实际深度及模块／纹理释放。视图检查覆盖返回指针与提供方相同、每次读取只生成一次数据、缓冲区复用、其他线程刷新不改写已有视图，以及超限／异常时不返回部分数据。交换容器顺序的 GFX 用于验证赐福在上或在下都按素材顺序绘制。

`build/native-checks/goblins-api/MapForGoblins.dll` **仅为测试夹具，不能安装进游戏**。离线回放模拟 GPU 提交，不能替代游戏内验收。

上游为 MIT 许可；补丁随项目保留供审阅和重新构建，未提交到上游仓库。
