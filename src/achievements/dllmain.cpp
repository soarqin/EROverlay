#include "data.hpp"
#include "render.hpp"

#include "api/api.h"

EROverlayAPI *api;

const wchar_t *init() {
    api = getEROverlayAPI();

    er::achievements::gData.load();

    return L"Achievements";
}

void update() {
    static int counter = 0;
    if ((counter++ & 0x1F) == 0) {
        er::achievements::gData.update();
    }
}

static er::achievements::Renderer *renderer = nullptr;

void createRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    renderer = new er::achievements::Renderer();
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

PLUGIN_DEFINE() {
    return &exports;
}