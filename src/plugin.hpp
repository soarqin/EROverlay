#pragma once

#include <functional>

namespace er {

void pluginsInit();
void pluginsOnLoad();
void pluginsUpdate();
void pluginsLoadRenderers(void *context, void *allocFunc, void *freeFunc, void *userData);
void pluginsDestroyRenderers();
bool pluginsRender();

}
