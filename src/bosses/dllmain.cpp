#include "render.hpp"
#include "data.hpp"

#include "api.hpp"

extern "C" {

#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

er::EROverlayAPI *api;

EXPORT const wchar_t *init() {
    api = er::getEROverlayAPI();
    return L"Bosses";
}

EXPORT void load() {
    er::bosses::gBossDataSet.load(api->isDLC01Installed());
    er::bosses::gBossDataSet.loadConfig();
    er::bosses::gBossDataSet.initMemoryAddresses();
}

EXPORT void update() {
    static int counter = 0;
    if ((counter++ & 0x1F) == 0) {
        er::bosses::gBossDataSet.update();
    }
}

static er::bosses::Renderer *renderer = nullptr;

EXPORT void loadRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    renderer = new er::bosses::Renderer();
    renderer->init(context, allocFunc, freeFunc, userData);
}

EXPORT void destroyRenderer() {
    delete renderer;
    renderer = nullptr;
}

EXPORT bool render() {
    return renderer->render();
}

}
