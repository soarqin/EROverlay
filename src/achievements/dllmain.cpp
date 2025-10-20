#include "data.hpp"
#include "render.hpp"

#include "api.h"

EROverlayAPI *api;

int init(EROverlayAPI *erOverlayAPI) {
    api = erOverlayAPI;

    er::achievements::gData.load();

    return 0;
}

void update() {
    static int counter = 0;
    if ((counter++ & 0x1F) == 0) {
        er::achievements::gData.update();
    }
}

static er::achievements::Renderer *renderer = nullptr;

int createRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    renderer = new er::achievements::Renderer();
    renderer->init(context, allocFunc, freeFunc, userData);
    return 100;
}

void destroyRenderer() {
    delete renderer;
    renderer = nullptr;
}

bool render() {
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
