#pragma once

namespace er {

enum class RenderLayer { AboveUI = 0, BelowUI = 1 };

void pluginsInit();
void pluginsUninit();
void pluginsUpdate();
void pluginsLoadRenderers(void *context, void *allocFunc, void *freeFunc, void *userData);
void pluginsDestroyRenderers();
using PluginRenderObserver = void (*)(RenderLayer layer, bool before, void *context);
bool pluginsRender(PluginRenderObserver observer = nullptr, void *context = nullptr);
[[nodiscard]] bool pluginsUseLayer(RenderLayer layer);

} // namespace er
