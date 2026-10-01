# ELDEN RING Overlay Hook

## 注意
* 请不要与一些覆盖层(如`Nvidia GeForce Experience`中的`FPS Counter`、`MSI Afterburner`、`RivaTuner Statistics Server`等)一起使用此Mod，它们可能导致游戏崩溃。
* 你可以使用`EROverlay.dll`来加载多个覆盖层，我会保持向后兼容性，所以你只需要确保`覆盖层加载器`的版本满足特定覆盖层的最低要求即可。

## 用法

小地图从游戏归档读取瓦片和图标，并由 EROverlay 创建自己的纹理。无需外部 `data/map` 文件。当前原生资源适配对应 Steam 应用版本 1.17.1 的研究样本；EXE 校验不匹配时禁用游戏调用并显示状态。

死亡位置默认显示内部卢恩图标，游戏清除死亡记录后隐藏。使用 `minimap.ini` 中的 `death_marker=0` 关闭。

* 按自己需求修改 `configs` 目录内的 `minimap.ini` 文件
* 将 `EROverlay.dll` 注入艾尔登法环游戏，你可以：
  + 将 `EROverlay.dll` 改名为 `winhttp.dll` 放到游戏 `eldenring.exe` 所在目录，同时复制 `configs` 和包含 `Minimap.dll` 的 `overlays` 目录。
  + 使用Mod加载器(你可以选择[EldenModLoader](https://www.nexusmods.com/eldenring/mods/117) 或 [ModEngine2](https://github.com/soulsmods/ModEngine2) 或 [me3](https://github.com/garyttierney/me3))
  + 运行Mod附带的 `injector.exe` 注入 (不推荐，因为这种方法不太稳定且被一些安全软件阻止)

## [代码许可证](https://github.com/soarqin/EROverlay/blob/master/LICENSE)

## 鸣谢
* 修改自 [ELDEN RING Internal Menu](https://github.com/NightFyre/ELDENRING-INTERNAL)
* [minhook](https://github.com/TsudaKageyu/minhook)
* [imgui](https://github.com/ocornut/imgui)
* [stb](https://github.com/nothings/stb)
* [JSON for Modern C++]( https://github.com/nlohmann/json)
* [Pattern16](https://github.com/Dasaav-dsv/Pattern16)
