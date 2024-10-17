#include "render.hpp"

#include "api/api.h"

#include <imgui.h>

extern EROverlayAPI *api;

namespace er::achievements {

void Renderer::init(void *context, void *allocFunc, void *freeFunc, void *userData) {
    ImGui::SetCurrentContext((ImGuiContext *)context);
    ImGui::SetAllocatorFunctions((ImGuiMemAllocFunc)allocFunc, (ImGuiMemFreeFunc)freeFunc, userData);
}

bool Renderer::render() {
    return false;
}

}
