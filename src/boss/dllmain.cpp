#include "render.hpp"
#include "data.hpp"

#include "api.h"

#include <memory>

EROverlayAPI *api;

int init(EROverlayAPI *erOverlayAPI) {
    api = erOverlayAPI;

    er::bosses::gBossDataSet.load(api->isDLC01Installed());
    er::bosses::gBossDataSet.initMemoryAddresses();
    return 0;
}

void update() {
    static int counter = 0;
    if ((counter++ & er::bosses::kUpdateTickMask) == 0) {
        er::bosses::gBossDataSet.update();
    }
}

static std::unique_ptr<er::bosses::Renderer> renderer;

int createRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    renderer = std::make_unique<er::bosses::Renderer>();
    renderer->init(context, allocFunc, freeFunc, userData);
    return 100;
}

void destroyRenderer() {
    renderer.reset();
}

bool render() {
    if (!renderer) return false;
    return renderer->render();
}

void uninit() {
    destroyRenderer();
}

static PluginExports exports = {
    init,
    uninit,
    update,
    createRenderer,
    destroyRenderer,
    render
};

PLUGIN_DEFINE(exports)
