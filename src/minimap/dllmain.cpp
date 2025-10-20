#include "data.hpp"
#include "render.hpp"

#include "api.h"

EROverlayAPI *api;

int init(EROverlayAPI *erOverlayAPI) {
    api = erOverlayAPI;

    er::minimap::gData.load();

    return 0;
}

void update() {
    er::minimap::gData.update();
}

static er::minimap::Renderer *renderer = nullptr;

int createRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    renderer = new er::minimap::Renderer();
    renderer->init(context, allocFunc, freeFunc, userData);
    return 0;
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
