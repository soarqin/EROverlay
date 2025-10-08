#include "data.hpp"
#include "render.hpp"

#include "api.h"

EROverlayAPI *api;

int init() {
    api = getEROverlayAPI();

    er::minimap::gData.load();

    return 0;
}

void update() {
    er::minimap::gData.update();
}

static er::minimap::Renderer *renderer = nullptr;

void createRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    renderer = new er::minimap::Renderer();
    renderer->init(context, allocFunc, freeFunc, userData);
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
