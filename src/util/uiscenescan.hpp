#pragma once

#include "peimage.hpp"

namespace er::util::uiscene {

#include "uiscene_patterns.inc"

struct Addresses {
    uint32_t submit = 0, enqueue = 0, consume = 0, pixelFormat = 0;
    uint32_t renderViewVtable = 0, textureVtable = 0;
    uint32_t swapChainsOffset = 0, viewportOffset = 0, contextEnvironmentOffset = 0;
    uint32_t backendOffset = 0, queueOffset = 0;
    uint32_t composite = 0, renderPostOffset = 0;
    uint32_t queueIndexOffset = 0, queuesOffset = 0;
};

[[nodiscard]] inline bool resolve(const PeImage &image, Addresses &result, const char *&error) {
    result = {};
    auto fail = [&](const char *message) {
        error = message;
        return false;
    };
    uint32_t submit = image.unique(QUEUE_SUBMIT);
    if (!submit)
        return fail("draw queue submit signature is missing or ambiguous");
    uint32_t enqueue = image.relative(submit + 0x3a, 1, 5);
    if (enqueue != image.relative(submit + 0x57, 1, 5) || !image.matches(enqueue, QUEUE_CALLBACK))
        return fail("draw queue callback contract changed");
    uint32_t sort = image.relative(submit + 0x5f, 1, 5);
    uint32_t execute = image.relative(submit + 0x84, 1, 5);
    if (!image.matches(sort, QUEUE_SORT) || !image.matches(execute, QUEUE_EXECUTE))
        return fail("draw queue sort/execute contract changed");
    uint32_t consume = image.relative(execute + 0xd, 1, 5);
    if (!image.matches(consume, QUEUE_CONSUME))
        return fail("draw queue flush-before-callback contract changed");
    uint32_t begin = image.relative(submit + 0x30, 3, 7);
    if (!image.matches(begin, BEGIN_CALLBACK))
        return fail("draw scene begin callback changed");
    uint32_t setup = image.relative(begin + 0x4a, 1, 5);
    if (!image.matches(setup, SCENE_SETUP))
        return fail("draw scene layout changed");
    uint32_t factory = image.unique(RTV_CREATE);
    if (!factory)
        return fail("render target view format contract changed");
    Addresses found{};
    found.composite = image.unique(UI_COMPOSITE);
    uint32_t postAgain = 0, compositeSwap = 0;
    if (!found.composite || !image.read(found.composite + 0x114, found.renderPostOffset) || !image.read(found.composite + 0x248, postAgain) ||
        !image.read(found.composite + 0x46, compositeSwap) || found.renderPostOffset != postAgain || !found.renderPostOffset || found.renderPostOffset > 4096)
        return fail("native UI composition/scene target contract changed");
    uint32_t nativeEnqueue = image.unique(NATIVE_ENQUEUE), indexAgain = 0;
    if (!nativeEnqueue || image.relative(found.composite + 0x4b0, 1, 5) != nativeEnqueue || !image.read(nativeEnqueue + 0x23, found.queueIndexOffset) ||
        !image.read(nativeEnqueue + 0x4e, indexAgain) || !image.read(nativeEnqueue + 0x5e, found.queuesOffset) || found.queueIndexOffset != indexAgain || !found.queueIndexOffset ||
        found.queueIndexOffset > 4096 || !found.queuesOffset || found.queuesOffset > 4096)
        return fail("native UI composition queue layout changed");
    found.submit = submit;
    found.enqueue = enqueue;
    found.consume = consume;
    found.pixelFormat = image.relative(factory + 0x64, 1, 5);
    found.renderViewVtable = image.vtable(".?AVCGRenderTargetView@DLCG3@@");
    found.textureVtable = image.vtable(".?AVCGTexture2D@DLCG3@@");
    if (!found.pixelFormat || !found.renderViewVtable || !found.textureVtable)
        return fail("render target RTTI/format converter is missing");
    if (!image.read(setup + 0x4c, found.swapChainsOffset) || !image.read(setup + 0xfe, found.viewportOffset) || !image.read(consume + 0x25, found.contextEnvironmentOffset))
        return fail("draw scene operands are truncated");
    if (compositeSwap != found.swapChainsOffset)
        return fail("native UI compositor uses a different swap-chain layout");
    // The consumer's batched flush forwards through two small engine thunks.
    uint32_t flush = image.relative(consume + 0xb2, 1, 5);
    constexpr auto forward = "48 8B 49 ?? E9 ?? ?? ?? ??";
    if (!image.matches(flush, forward))
        return fail("draw queue backend forwarding changed");
    uint8_t backend = 0;
    if (!image.read(flush + 3, backend))
        return fail("draw queue backend operand is truncated");
    uint32_t batch = image.relative(flush + 4, 1, 5);
    for (int i = 0; i < 4; ++i) {
        uint8_t opcode = 0;
        if (!image.read(batch, opcode) || opcode != 0xe9)
            break;
        batch = image.relative(batch, 1, 5);
    }
    constexpr auto queueLoad = "48 8B 41 ?? 48 89 45 C0 41 8B D8";
    constexpr auto submitLists = "48 8B 07 4D 8B C7 8B D3 48 8B CF FF 50 50";
    uint8_t queue = 0;
    if (!image.matches(batch + 0x3d, queueLoad) || !image.matches(batch + 0x229, submitLists) || !image.read(batch + 0x40, queue))
        return fail("native ExecuteCommandLists contract changed");
    found.backendOffset = backend;
    found.queueOffset = queue;
    if (!found.swapChainsOffset || found.swapChainsOffset > 4096 || !found.viewportOffset || found.viewportOffset > 4096 || found.contextEnvironmentOffset > 16384 || !backend ||
        !queue)
        return fail("draw scene offsets are out of range");
    result = found;
    error = nullptr;
    return true;
}

} // namespace er::util::uiscene
