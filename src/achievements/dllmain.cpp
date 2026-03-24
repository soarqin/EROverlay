#include "data.hpp"
#include "render.hpp"

#include "api.h"

#include <memory>

EROverlayAPI *api;

int init(EROverlayAPI *erOverlayAPI) {
    api = erOverlayAPI;

    er::achievements::gData.load();

    return 0;
}

void update() {
    static int counter = 0;
    if ((counter++ & er::achievements::kUpdateTickMask) == 0) {
        er::achievements::gData.update();
    }
}

static std::unique_ptr<er::achievements::Renderer> renderer;

int createRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    renderer = std::make_unique<er::achievements::Renderer>();
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
