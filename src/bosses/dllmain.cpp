#include "render.hpp"
#include "data.hpp"

#include "api.h"

EROverlayAPI *api;

int init() {
    api = getEROverlayAPI();

    er::bosses::gBossDataSet.load(api->isDLC01Installed());
    er::bosses::gBossDataSet.loadConfig();
    er::bosses::gBossDataSet.initMemoryAddresses();
    return 0;
}

void update() {
    static int counter = 0;
    if ((counter++ & 0x1F) == 0) {
        er::bosses::gBossDataSet.update();
    }
}

static er::bosses::Renderer *renderer = nullptr;

void createRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    renderer = new er::bosses::Renderer();
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

void toggleFullMode() {
    renderer->toggleFullMode();
}

static PluginExports exports = {
    init,
    uninit,
    update,
    createRenderer,
    destroyRenderer,
    render,
    toggleFullMode
};

PLUGIN_DEFINE() {
    return &exports;
}
