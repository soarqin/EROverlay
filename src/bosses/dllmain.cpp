#include "render.hpp"
#include "data.hpp"

#include "api.hpp"

er::EROverlayAPI *api;

const wchar_t *init() {
    api = er::getEROverlayAPI();

    er::bosses::gBossDataSet.load(api->isDLC01Installed());
    er::bosses::gBossDataSet.loadConfig();
    er::bosses::gBossDataSet.initMemoryAddresses();
    return L"Bosses";
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

static er::PluginExports exports = {
    init,
    uninit,
    update,
    createRenderer,
    destroyRenderer,
    render
};

PLUGIN_DEFINE() {
    return &exports;
}
