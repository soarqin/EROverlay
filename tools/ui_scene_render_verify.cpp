// Real D3D12 checks for the production UI_SCENE renderer.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <imgui.h>

#include "../src/plugin.cpp"
// Include the production bridge to exercise its detour and teardown with a
// controlled game-queue fixture. Do not also link uiscene.obj in this tool.
#include "../src/uiscene.cpp"

#include "config.hpp"
#include "d3drenderer.hpp"
#include "global.hpp"
#include "uiscene.hpp"

bool waitForRendererHook();

namespace {
ID3D12InfoQueue *gInfo = nullptr;
std::atomic_uint validationIssues{0};
bool textSeen = false;
int mode = 0;
int renderCalls = 0, aboveCalls = 0;
int lowerDrawCallbacks = 0, upperDrawCallbacks = 0;
bool checkLayers = false;
bool callbacksOnly = false;
bool checkOpacity = false;
int opacityAlpha = 229;
ImU32 upperColor = IM_COL32(255, 255, 0, 255);
const char *uiCompositorPath = nullptr;
const char *uiCompositorVertexPath = nullptr;
uint64_t layerTexture = 0;
int glyphSize = 18;
void *offscreen = nullptr;
void callbackWindow(const char *name, int &calls) {
    ImGui::SetNextWindowPos({500, 250});
    ImGui::SetNextWindowSize({32, 32});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
    ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList *, const ImDrawCmd *command) { ++*static_cast<int *>(command->UserCallbackData); }, &calls);
    ImGui::End();
    ImGui::PopStyleVar();
}
bool render() {
    ++renderCalls;
    if (mode == 0)
        return false;
    if (callbacksOnly) {
        callbackWindow("lower callback only", lowerDrawCallbacks);
        return false;
    }
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({256, 256});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("check", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
    auto *draw = ImGui::GetWindowDrawList();
    if (checkOpacity) {
        draw->AddRectFilled({10, 10}, {50, 50}, IM_COL32(255, 0, 0, opacityAlpha));
        // Later commands must still cover earlier commands within the lower
        // layer, even though the completed layer goes behind native game UI.
        draw->AddRectFilled({20, 20}, {40, 40}, IM_COL32(255, 255, 0, opacityAlpha));
        if (er::gD3DRenderer->BeginOffscreenRegion(static_cast<er::OffscreenContext *>(offscreen), 100, 100, 64, 64)) {
            draw->AddRectFilled({100, 100}, {140, 164}, IM_COL32(0, 255, 0, 255));
            draw->AddRectFilled({140, 100}, {164, 124}, IM_COL32(0, 255, 0, 51));
            auto handle = er::gD3DRenderer->EndOffscreen(static_cast<er::OffscreenContext *>(offscreen));
            draw->AddImage(reinterpret_cast<ImTextureID>(handle), {100, 100}, {164, 164}, {0, 0}, {1, 1}, IM_COL32(255, 255, 255, opacityAlpha));
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        return false;
    }
    draw->AddRectFilled({10, 10}, {50, 50}, IM_COL32(255, 0, 0, 255));
    if (mode >= 2) {
        // A new size bakes new glyphs, forcing an ImGui atlas update.
        ImGui::PushFont(nullptr, float(glyphSize));
        ImGui::SetCursorPos({60, 10});
        ImGui::TextUnformatted("Atlas 0123456789 ABCDEFGH");
        ImGui::PopFont();
    }
    if (mode >= 3 && offscreen) {
        // Same sequence as the minimap's translucent local target.
        if (er::gD3DRenderer->BeginOffscreenRegion(static_cast<er::OffscreenContext *>(offscreen), 100, 100, 64, 64)) {
            draw->AddRectFilled({100, 100}, {140, 164}, IM_COL32(0, 255, 0, 255));
            draw->AddRectFilled({140, 100}, {164, 124}, IM_COL32(0, 255, 0, 51));
            void *handle = er::gD3DRenderer->EndOffscreen(static_cast<er::OffscreenContext *>(offscreen));
            if (handle)
                draw->AddImage(reinterpret_cast<ImTextureID>(handle), {100, 100}, {164, 164}, {0, 0}, {1, 1}, IM_COL32(255, 255, 255, 128));
        }
    }
    if (mode >= 3)
        draw->AddRectFilled({200, 200}, {220, 220}, IM_COL32(255, 255, 0, 255));
    if (checkLayers) {
        ImGui::GetBackgroundDrawList()->AddRectFilled({310, 20}, {330, 40}, IM_COL32(255, 0, 0, 255));
        ImGui::GetForegroundDrawList()->AddRectFilled({270, 20}, {290, 40}, IM_COL32(255, 0, 0, 255));
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    if (checkLayers)
        callbackWindow("lower callback only", lowerDrawCallbacks);
    return false;
}
void destroy() {
    if (offscreen && er::gD3DRenderer)
        er::gD3DRenderer->DestroyOffscreen(static_cast<er::OffscreenContext *>(offscreen));
    offscreen = nullptr;
}
int create(void *, void *, void *, void *) {
    offscreen = er::gD3DRenderer->CreateOffscreen();
    return 0;
}
PluginExports exports{nullptr, nullptr, nullptr, create, destroy, render};
bool renderAbove() {
    ++aboveCalls;
    if (callbacksOnly) {
        callbackWindow("upper callback only", upperDrawCallbacks);
        return false;
    }
    // Reuse the lower plugin's window and shared viewport lists: filtering
    // must work per command, not just per ImGui window or draw list.
    ImGui::Begin("check", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
    ImGui::GetWindowDrawList()->AddRectFilled({42, 42}, {48, 48}, upperColor);
    ImGui::End();
    ImGui::GetBackgroundDrawList()->AddRectFilled({315, 20}, {325, 40}, IM_COL32(255, 255, 0, 255));
    ImGui::GetForegroundDrawList()->AddRectFilled({275, 20}, {285, 40}, IM_COL32(255, 255, 0, 255));
    if (layerTexture) {
        ERTextureView view{};
        if (er::gD3DRenderer->pollTexture(layerTexture, view) == ER_TEXTURE_READY)
            ImGui::GetForegroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(view.gpuHandle), {400, 100}, {420, 120});
        er::gD3DRenderer->retireTexture(layerTexture);
    }
    callbackWindow("upper callback only", upperDrawCallbacks);
    return false;
}
PluginExports aboveExports{nullptr, nullptr, nullptr, nullptr, nullptr, renderAbove};
} // namespace

namespace er {
struct NativeTextureVerifier {
    struct LayerQueue {
        uint8_t padding[0x28]{};
        QueueEntry *entries = records.data();
        uint32_t count = 0, unused = 0, lastPriority = 0, tail = 0;
        struct Command {
            uint8_t opcode = 0;
            char tag = 0;
            UiSceneBridge::Draw callback = nullptr;
            UiSceneBridge::Invocation *data = nullptr;
        };
        std::array<QueueEntry, 16> records{};
        std::array<Command, 16> commands{};
        std::string order;
        void append(uint8_t opcode, char tag, uint32_t priority, UiSceneBridge::Draw callback = nullptr, UiSceneBridge::Invocation *data = nullptr) {
            commands[count] = {opcode, tag, callback, data};
            records[count] = {reinterpret_cast<uintptr_t>(&commands[count]), priority, 0};
            ++count;
            lastPriority = std::max(lastPriority, priority);
        }
    };
    static void compositeFixture(uintptr_t drawSystem, uintptr_t) {
        auto &queue = *reinterpret_cast<LayerQueue *>(pointer(drawSystem + 0x20));
        queue.append(0, 'U', queue.lastPriority + 0x800000);
    }
    static bool enqueueLayerFixture(void *queue, UiSceneBridge::Draw callback, UiSceneBridge::Invocation *data, void *, uint32_t priority) {
        static_cast<LayerQueue *>(queue)->append(2, data->layer == RenderLayer::BelowUI ? 'L' : 'H', priority, callback, data);
        return true;
    }
    static void consumeLayerFixture(uintptr_t, void *opaque) {
        auto &queue = *static_cast<LayerQueue *>(opaque);
        // Teardown case: consume callbacks normally while drawing is stopped.
        gSceneBridge.load()->accepting_ = false;
        std::sort(queue.records.begin(), queue.records.begin() + queue.count, [](const auto &a, const auto &b) { return a.priority < b.priority; });
        for (uint32_t i = 0; i < queue.count; ++i) {
            auto &command = *reinterpret_cast<LayerQueue::Command *>(queue.records[i].command);
            queue.order += command.tag;
            if (command.callback)
                command.callback(0, command.data, nullptr);
        }
    }
    static bool boundaryCheck() {
        static_assert(offsetof(LayerQueue, entries) == 0x28 && offsetof(LayerQueue, count) == 0x30 && offsetof(LayerQueue, lastPriority) == 0x38);
        D3DRenderer renderer;
        UiSceneBridge bridge(renderer);
        bridge.addresses_.queueIndexOffset = 0x10;
        bridge.addresses_.queuesOffset = 0x20;
        bridge.originalComposite_ = &compositeFixture;
        bridge.original_ = &consumeLayerFixture;
        bridge.enqueue_ = &enqueueLayerFixture;
        plugins.push_back({0, &exports, true, RenderLayer::BelowUI});
        gSceneBridge = &bridge;
        bool ok = true;
        for (int stale = 0; stale < 3; ++stale) {
            LayerQueue queue;
            std::array<uintptr_t, 16> drawSystem{};
            drawSystem[0x20 / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&queue);
            bridge.accepting_ = true;
            queue.append(0, 'S', 0x800000);
            if (stale != 2)
                UiSceneBridge::composite(reinterpret_cast<uintptr_t>(drawSystem.data()), 0);
            if (stale == 1)
                queue.records[1].priority += 10; // Replaced entry must not reuse the boundary.
            queue.append(0, 'T', queue.lastPriority + 0x800000);
            UiSceneBridge::submit(reinterpret_cast<uintptr_t>(drawSystem.data()), &queue);
            ok &= queue.order == (stale == 0 ? "SLUTH" : stale == 1 ? "SUTH" : "STH");
        }
        gSceneBridge = nullptr;
        plugins.clear();
        std::puts(ok ? "PASS: exact native UI command boundary survives later native work; stale/missing boundaries suppress only the lower stage."
                     : "FAIL: native UI command boundary/order fixture");
        return ok;
    }
    struct QueueFixture {
        UiSceneBridge::Draw callback = nullptr;
        UiSceneBridge::Invocation *data = nullptr;
        HANDLE entered = nullptr, resume = nullptr;
        unsigned callbacks = 0;
        unsigned compositeCalls = 0;
    };
    static bool enqueueFixture(void *queue, UiSceneBridge::Draw callback, UiSceneBridge::Invocation *data, void *, uint32_t priority) {
        auto &fixture = *static_cast<QueueFixture *>(queue);
        if (priority != UINT32_MAX - 1)
            return false;
        fixture.callback = callback;
        fixture.data = data;
        return true;
    }
    __declspec(noinline) static void submitFixture(uintptr_t, void *queue) {
        auto &fixture = *static_cast<QueueFixture *>(queue);
        SetEvent(fixture.entered);
        WaitForSingleObject(fixture.resume, 5000);
        if (fixture.callback) {
            ++fixture.callbacks;
            fixture.callback(0, fixture.data, nullptr);
        }
    }
    __declspec(noinline) static void compositeLifecycleFixture(uintptr_t, uintptr_t opaque) {
        auto &fixture = *reinterpret_cast<QueueFixture *>(opaque);
        SetEvent(fixture.entered);
        WaitForSingleObject(fixture.resume, 5000);
        ++fixture.compositeCalls;
    }
    static bool bridgeCheck() {
        if (MH_Initialize() != MH_OK)
            return false;
        D3DRenderer renderer;
        bool ok = true;
        for (int pass = 0; pass < 2; ++pass) {
            UiSceneBridge bridge(renderer);
            QueueFixture fixture;
            fixture.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            fixture.resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!fixture.entered || !fixture.resume)
                return false;
            bridge.target_ = reinterpret_cast<void *>(&submitFixture);
            bridge.compositeTarget_ = reinterpret_cast<void *>(&compositeLifecycleFixture);
            bridge.enqueue_ = &enqueueFixture;
            bridge.accepting_.store(true);
            gSubmitEntry = &submitFixture;
            gCompositeEntry = &compositeLifecycleFixture;
            gSceneBridge.store(&bridge);
            if (MH_CreateHook(bridge.target_, reinterpret_cast<void *>(&UiSceneBridge::submit), reinterpret_cast<void **>(&bridge.original_)) != MH_OK ||
                MH_CreateHook(bridge.compositeTarget_, reinterpret_cast<void *>(&UiSceneBridge::composite), reinterpret_cast<void **>(&bridge.originalComposite_)) != MH_OK ||
                MH_EnableHook(bridge.target_) != MH_OK || MH_EnableHook(bridge.compositeTarget_) != MH_OK)
                return false;
            std::thread producer([&] {
                if (pass == 0)
                    submitFixture(0, &fixture);
                else
                    compositeLifecycleFixture(0, reinterpret_cast<uintptr_t>(&fixture));
            });
            ok &= WaitForSingleObject(fixture.entered, 5000) == WAIT_OBJECT_0;
            std::atomic_bool stopped{false};
            std::thread stopper([&] {
                bridge.stop();
                stopped.store(true);
            });
            auto deadline = GetTickCount64() + 5000;
            while (bridge.accepting_.load() && GetTickCount64() < deadline)
                Sleep(1);
            ok &= !bridge.accepting_.load() && !stopped.load();
            SetEvent(fixture.resume);
            producer.join();
            stopper.join();
            ok &= stopped.load() && fixture.callbacks == (pass == 0 ? 1u : 0u) && fixture.compositeCalls == (pass == 1 ? 1u : 0u) && gSceneBridge.load() == nullptr;
            // A subsequent call goes to the restored game entry, with no enqueue.
            fixture.callback = nullptr;
            submitFixture(0, &fixture);
            compositeLifecycleFixture(0, reinterpret_cast<uintptr_t>(&fixture));
            ok &= fixture.callbacks == (pass == 0 ? 1u : 0u) && fixture.compositeCalls == (pass == 1 ? 2u : 1u);
            CloseHandle(fixture.entered);
            CloseHandle(fixture.resume);
        }
        MH_Uninitialize();
        std::puts(ok ? "PASS: two real MinHook detours; teardown drains in-flight submit/compositor calls, restored entries remain callable, reinstall succeeds."
                     : "FAIL: bridge queue/teardown fixture");
        return ok;
    }
    static int messages(ID3D12InfoQueue *queue) {
        int errors = 0;
        for (UINT64 i = 0, count = queue->GetNumStoredMessages(); i < count; ++i) {
            SIZE_T length = 0;
            queue->GetMessage(i, nullptr, &length);
            std::string storage(length, 0);
            auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
            queue->GetMessage(i, message, &length);
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
                std::printf("D3D12 %s: %s\n", message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR ? "ERROR" : "WARNING", message->pDescription);
                ++errors;
            }
        }
        queue->ClearStoredMessages();
        validationIssues.fetch_add(errors);
        return errors;
    }
    static bool readPixel(D3DRenderer &r, ID3D12Device *device, IDXGISwapChain3 *swap, int x, int y, uint8_t (&rgba)[4]) {
        ID3D12Resource *buffer = nullptr;
        if (FAILED(swap->GetBuffer(swap->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer))))
            return false;
        auto desc = buffer->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 total = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = total;
        bufferDesc.Height = bufferDesc.DepthOrArraySize = bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource *readback = nullptr;
        ID3D12CommandAllocator *allocator = nullptr;
        ID3D12GraphicsCommandList *list = nullptr;
        ID3D12Fence *fence = nullptr;
        bool ok = SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))) &&
                  SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
                  SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list))) &&
                  SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        if (ok) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = buffer;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            list->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION destination{}, source{};
            destination.pResource = readback;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;
            source.pResource = buffer;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(1, &barrier);
            ok = SUCCEEDED(list->Close());
        }
        if (ok) {
            ID3D12CommandList *lists[]{list};
            r.commandQueue_->ExecuteCommandLists(1, lists);
            ok = SUCCEEDED(r.commandQueue_->Signal(fence, 1)) && SUCCEEDED(fence->SetEventOnCompletion(1, nullptr));
        }
        void *mapped = nullptr;
        if (ok && SUCCEEDED(readback->Map(0, nullptr, &mapped))) {
            auto decode = [&](int column, int row, uint8_t (&out)[4]) {
                const auto *texel = static_cast<const uint8_t *>(mapped) + footprint.Offset + size_t(row) * footprint.Footprint.RowPitch + size_t(column) * 4;
                if (desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
                    uint32_t packed = 0;
                    std::memcpy(&packed, texel, sizeof(packed));
                    for (int component = 0; component < 3; ++component)
                        out[component] = uint8_t((((packed >> (component * 10)) & 1023) * 255 + 511) / 1023);
                    out[3] = uint8_t((packed >> 30) * 85);
                } else {
                    std::memcpy(out, texel, 4);
                }
            };
            decode(x, y, rgba);
            // Text pixels: any bright texel in the glyph line region.
            textSeen = false;
            for (UINT row = 12; row < 60 && !textSeen; ++row)
                for (UINT column = 62; column < 250 && !textSeen; ++column) {
                    uint8_t texel[4]{};
                    decode(column, row, texel);
                    textSeen = texel[0] > 150 && texel[1] > 150;
                }
            readback->Unmap(0, nullptr);
        } else
            ok = false;
        for (IUnknown *object: {static_cast<IUnknown *>(fence), static_cast<IUnknown *>(list), static_cast<IUnknown *>(allocator), static_cast<IUnknown *>(readback),
                                static_cast<IUnknown *>(buffer)})
            if (object)
                object->Release();
        return ok;
    }

    // A game drains its queue before ResizeBuffers or releasing a swap chain.
    static void flush(D3DRenderer &r, ID3D12Device *device) {
        ID3D12Fence *fence = nullptr;
        if (SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            if (SUCCEEDED(r.commandQueue_->Signal(fence, 1)))
                fence->SetEventOnCompletion(1, nullptr);
            fence->Release();
        }
    }

    // Models the game's own frame: clear the back buffer before the overlay.
    static bool clearBackBuffer(D3DRenderer &r, ID3D12Device *device, IDXGISwapChain3 *swap, bool before = true, D3D12_CPU_DESCRIPTOR_HANDLE *target = nullptr, bool finish = true,
                                bool paint = true) {
        static ID3D12DescriptorHeap *heap = nullptr;
        static ID3D12CommandAllocator *allocator = nullptr;
        static ID3D12GraphicsCommandList *list = nullptr;
        static ID3D12Fence *fence = nullptr;
        static UINT64 value = 0;
        if (!heap && device) {
            D3D12_DESCRIPTOR_HEAP_DESC desc{};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = 1;
            if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap))) ||
                FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
                FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list))) || FAILED(list->Close()) ||
                FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
                return false;
        }
        if (!device) {
            for (IUnknown *object: {static_cast<IUnknown *>(fence), static_cast<IUnknown *>(list), static_cast<IUnknown *>(allocator), static_cast<IUnknown *>(heap)})
                if (object)
                    object->Release();
            heap = nullptr;
            return true;
        }
        ID3D12Resource *buffer = nullptr;
        if (FAILED(swap->GetBuffer(swap->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer))))
            return false;
        // Single allocator: wait for the previous clear before resetting it.
        if (value)
            fence->SetEventOnCompletion(value, nullptr);
        auto rtv = heap->GetCPUDescriptorHandleForHeapStart();
        if (target)
            *target = rtv;
        device->CreateRenderTargetView(buffer, nullptr, rtv);
        allocator->Reset();
        list->Reset(allocator, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffer;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        if (before) {
            list->ResourceBarrier(1, &barrier);
            const float black[]{0, 0, 0, 1};
            if (paint)
                list->ClearRenderTargetView(rtv, black, 0, nullptr);
        } else if (paint) {
            const float blue[]{0, 0, 1, 1};
            const D3D12_RECT corner{35, 35, 45, 45};
            list->ClearRenderTargetView(rtv, blue, 1, &corner);
        }
        if (!before && finish) {
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(1, &barrier);
        }
        bool ok = SUCCEEDED(list->Close());
        if (ok) {
            ID3D12CommandList *lists[]{list};
            r.commandQueue_->ExecuteCommandLists(1, lists);
            ok = SUCCEEDED(r.commandQueue_->Signal(fence, ++value));
        }
        buffer->Release();
        return ok;
    }

    static bool opacityCheck(D3DRenderer &r, ID3D12Device *device, IDXGISwapChain3 *swap) {
        using Microsoft::WRL::ComPtr;
        DXGI_SWAP_CHAIN_DESC desc{};
        swap->GetDesc(&desc);
        D3D12_VIEWPORT viewport{0, 0, float(desc.BufferDesc.Width), float(desc.BufferDesc.Height), 0, 1};
        ComPtr<ID3D12DescriptorHeap> rtvs, srvs;
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = 2;
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvs))))
            return false;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&srvs))))
            return false;
        std::array<ComPtr<ID3D12Resource>, 2> textures;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> targets;
        D3D12_HEAP_PROPERTIES memory{};
        memory.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width = desc.BufferDesc.Width;
        textureDesc.Height = desc.BufferDesc.Height;
        textureDesc.DepthOrArraySize = textureDesc.MipLevels = textureDesc.SampleDesc.Count = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        auto createTexture = [&](UINT i, const float (&color)[4]) {
            textures[i].Reset();
            D3D12_CLEAR_VALUE clear{};
            clear.Format = textureDesc.Format;
            std::copy(std::begin(color), std::end(color), clear.Color);
            if (FAILED(
                    device->CreateCommittedResource(&memory, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&textures[i]))))
                return false;
            targets[i] = rtvs->GetCPUDescriptorHandleForHeapStart();
            targets[i].ptr += i * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            device->CreateRenderTargetView(textures[i].Get(), nullptr, targets[i]);
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = textureDesc.Format;
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2D.MipLevels = 1;
            auto srv = srvs->GetCPUDescriptorHandleForHeapStart();
            srv.ptr += i * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            device->CreateShaderResourceView(textures[i].Get(), &view, srv);
            return true;
        };
        const float background[]{.2f, .4f, .8f, 1};
        if (!createTexture(0, background))
            return false;
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants.Num32BitValues = 24; // native DisplayMappingData, gamma = 1
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[1].DescriptorTable = {1, &range};
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 1;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rootDesc{2, parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        ComPtr<ID3DBlob> serialized, vs, ps;
        ComPtr<ID3D12RootSignature> root;
        if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, nullptr)) ||
            FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root))))
            return false;
        const char *vertex = R"(
            struct Output { float4 pos : SV_POSITION; float4 unused : TEXCOORD0; float2 uv : TEXCOORD1; };
            Output main(uint id : SV_VertexID) {
                Output o; o.uv = float2((id << 1) & 2, id & 2);
                o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1); o.unused = 0; return o;
            })";
        // Arithmetic independently transcribed from the native shader's DXIL,
        // not from the overlay implementation. Optionally use the local game's
        // actual GXSimple_BlendUI_SDR2SDR_F.ppo instead, without checking it in.
        const char *pixel = R"(
            cbuffer DisplayMappingData : register(b0) { float outputGammaForSDR; };
            Texture2D scene : register(t0); Texture2D ui : register(t1);
            SamplerState linearClamp : register(s1);
            float4 main(float4 pos : SV_POSITION, float4 unused : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target {
                float4 u = ui.Sample(linearClamp, uv);
                return float4(pow(scene.Sample(linearClamp, uv).rgb * u.a + u.rgb, outputGammaForSDR), 1);
            })";
        std::vector<char> nativeShader, nativeVertex;
        if (uiCompositorPath) {
            std::ifstream stream(uiCompositorPath, std::ios::binary);
            nativeShader.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
            std::ifstream vertexStream(uiCompositorVertexPath, std::ios::binary);
            nativeVertex.assign(std::istreambuf_iterator<char>(vertexStream), std::istreambuf_iterator<char>());
            if (nativeShader.empty() || nativeVertex.empty())
                return false;
        } else if (FAILED(D3DCompile(vertex, strlen(vertex), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs, nullptr)) ||
                   FAILED(D3DCompile(pixel, strlen(pixel), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps, nullptr)))
            return false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = root.Get();
        psoDesc.VS = uiCompositorPath ? D3D12_SHADER_BYTECODE{nativeVertex.data(), nativeVertex.size()} : D3D12_SHADER_BYTECODE{vs->GetBufferPointer(), vs->GetBufferSize()};
        psoDesc.PS = uiCompositorPath ? D3D12_SHADER_BYTECODE{nativeShader.data(), nativeShader.size()} : D3D12_SHADER_BYTECODE{ps->GetBufferPointer(), ps->GetBufferSize()};
        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = psoDesc.SampleDesc.Count = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
        ComPtr<ID3D12PipelineState> pipeline;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline))) ||
            FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
            FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))) || FAILED(list->Close()))
            return false;
        auto execute = [&] {
            if (FAILED(list->Close()))
                return false;
            ID3D12CommandList *commands[]{list.Get()};
            r.commandQueue_->ExecuteCommandLists(1, commands);
            flush(r, device);
            return true;
        };
        checkOpacity = true;
        plugins.front().layer = RenderLayer::BelowUI;
        bool ok = true;
        uint64_t sequence = 300;
        constexpr int samples[][2]{{15, 15}, {30, 30}, {120, 132}, {152, 112}, {152, 152}};
        // No native UI, half-transparent native UI, and opaque native UI.
        for (int transmission: {255, 128, 0}) {
            const float t = float(transmission) / 255;
            const float nativeUI[]{0, 0, 1 - t, t};
            if (!createTexture(1, nativeUI))
                return false;
            for (int alpha: {255, 229, 128, 51, 0}) {
                opacityAlpha = alpha;
                D3D12_CPU_DESCRIPTOR_HANDLE output{};
                ok &= clearBackBuffer(r, device, swap, true, &output, true, false);
                allocator->Reset();
                list->Reset(allocator.Get(), nullptr);
                list->ClearRenderTargetView(output, background, 0, nullptr);
                ok &= execute();
                // The upper UI_SCENE layer uses ordinary source-over blending.
                // Keep this GPU reference after removing the Present renderer.
                plugins.front().layer = RenderLayer::AboveUI;
                UiSceneFrame referenceFrame{device, r.commandQueue_, swap, output, DXGI_FORMAT_R10G10B10A2_UNORM, viewport, RenderLayer::AboveUI, sequence++};
                r.overlayScene(referenceFrame);
                plugins.front().layer = RenderLayer::BelowUI;
                ok &= clearBackBuffer(r, device, swap, false, nullptr, true, false);
                uint8_t reference[std::size(samples)][4]{};
                for (size_t i = 0; i < std::size(samples); ++i) {
                    ok &= readPixel(r, device, swap, samples[i][0], samples[i][1], reference[i]);
                    for (int c = 0; c < 3; ++c)
                        reference[i][c] = uint8_t(std::lround(float(reference[i][c]) * t + nativeUI[c] * 255));
                }
                allocator->Reset();
                list->Reset(allocator.Get(), nullptr);
                for (UINT i = 0; i < textures.size(); ++i) {
                    D3D12_RESOURCE_BARRIER barrier{};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition = {textures[i].Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                          D3D12_RESOURCE_STATE_RENDER_TARGET};
                    list->ResourceBarrier(1, &barrier);
                    list->ClearRenderTargetView(targets[i], i == 0 ? background : nativeUI, 0, nullptr);
                    if (i == 0) {
                        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
                        list->ResourceBarrier(1, &barrier);
                    }
                }
                ok &= execute();
                UiSceneFrame scene{device, r.commandQueue_, swap, targets[1], DXGI_FORMAT_R8G8B8A8_UNORM, viewport, RenderLayer::BelowUI, sequence++};
                r.overlayScene(scene);
                flush(r, device);
                ok &= clearBackBuffer(r, device, swap, true, &output, true, false);
                allocator->Reset();
                list->Reset(allocator.Get(), pipeline.Get());
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition = {textures[1].Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
                list->ResourceBarrier(1, &barrier);
                list->OMSetRenderTargets(1, &output, FALSE, nullptr);
                ID3D12DescriptorHeap *heaps[]{srvs.Get()};
                list->SetDescriptorHeaps(1, heaps);
                list->SetGraphicsRootSignature(root.Get());
                float constants[24]{1};
                list->SetGraphicsRoot32BitConstants(0, 24, constants, 0);
                list->SetGraphicsRootDescriptorTable(1, srvs->GetGPUDescriptorHandleForHeapStart());
                list->RSSetViewports(1, &viewport);
                D3D12_RECT scissor{0, 0, LONG(viewport.Width), LONG(viewport.Height)};
                list->RSSetScissorRects(1, &scissor);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                list->DrawInstanced(3, 1, 0, 0);
                ok &= execute();
                scene.target = output;
                scene.format = DXGI_FORMAT_R10G10B10A2_UNORM;
                scene.layer = RenderLayer::AboveUI;
                r.overlayScene(scene);
                ok &= clearBackBuffer(r, device, swap, false, nullptr, true, false);
                for (size_t i = 0; i < std::size(samples); ++i) {
                    uint8_t actual[4]{};
                    ok &= readPixel(r, device, swap, samples[i][0], samples[i][1], actual);
                    for (int c = 0; c < 3; ++c)
                        if (abs(int(actual[c]) - int(reference[i][c])) > 3) {
                            std::printf("FAIL opacity alpha=%d native-transmission=%d pixel=%zu component=%d: %u vs source-over %u\n", alpha, transmission, i, c, actual[c],
                                        reference[i][c]);
                            ok = false;
                        }
                    if (alpha == 229 && transmission == 255 && i == 0)
                        std::printf("SDR 90%%: native RGB=%u,%u,%u; source-over RGB=%u,%u,%u (%s compositor)\n", actual[0], actual[1], actual[2], reference[i][0], reference[i][1],
                                    reference[i][2], uiCompositorPath ? "game bytecode" : "native arithmetic");
                }
                if (!ok)
                    break;
            }
            if (!ok)
                break;
        }
        checkOpacity = false;
        plugins.front().layer = RenderLayer::AboveUI;
        if (ok)
            std::puts(
                "PASS: native SDR compositor matches source-over opacity at 100/90/50/20/0%; existing native UI stays above lower commands; overlapping draws and nested offscreen "
                "restore correctly.");
        return ok;
    }

    static bool layersCheck(D3DRenderer &r, ID3D12Device *device, IDXGISwapChain3 *swap, IDXGIFactory4 *factory, const WNDCLASSEXW &cls) {
        checkLayers = true;
        plugins.front().layer = RenderLayer::BelowUI;
        plugins.push_back({1, &aboveExports, false, RenderLayer::AboveUI});
        const auto generation = r.sceneGeneration_;
        DXGI_SWAP_CHAIN_DESC desc{};
        swap->GetDesc(&desc);
        D3D12_VIEWPORT viewport{0, 0, float(desc.BufferDesc.Width), float(desc.BufferDesc.Height), 0, 1};
        // Upload one green texel through the production DDS uploader. The
        // upper plugin retires it while building the shared frame; its SRV
        // must survive completion of the lower submission and the second draw.
        std::array<uint32_t, 38> dds{};
        dds[0] = 0x20534444;
        dds[1] = 124;
        dds[3] = dds[4] = dds[7] = 1;
        dds[19] = 32;
        dds[21] = 0x30315844;
        dds[32] = 28;
        dds[33] = 3;
        dds[35] = 1;
        dds[37] = 0xff00ff00;
        layerTexture = r.createDdsTexture(dds.data(), sizeof(dds));
        r.processTextureUploads();
        r.waitForFrames();
        r.processTextureUploads();
        ERTextureView textureView{};
        if (!layerTexture || r.pollTexture(layerTexture, textureView) != ER_TEXTURE_READY)
            return false;
        auto pixelIs = [&](IDXGISwapChain3 *source, int x, int y, int red, int green, int blue) {
            uint8_t pixel[4]{};
            if (!readPixel(r, device, source, x, y, pixel))
                return false;
            bool ok = abs(int(pixel[0]) - red) <= 5 && abs(int(pixel[1]) - green) <= 5 && abs(int(pixel[2]) - blue) <= 5;
            if (!ok)
                std::printf("FAIL layer pixel %d,%d: %u %u %u, expected %d %d %d\n", x, y, pixel[0], pixel[1], pixel[2], red, green, blue);
            return ok;
        };
        for (int i = 0; i < 4; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE target{};
            if (!clearBackBuffer(r, device, swap, true, &target))
                return false;
            int frames = ImGui::GetFrameCount(), lowerCalls = renderCalls, upperCalls = aboveCalls;
            int lowerCallbacks = lowerDrawCallbacks, upperCallbacks = upperDrawCallbacks;
            auto submissions = r.imguiFrame_;
            UiSceneFrame scene{device, r.commandQueue_, swap, target, DXGI_FORMAT_R10G10B10A2_UNORM, viewport, RenderLayer::BelowUI, uint64_t(100 + i)};
            r.overlayScene(scene);
            if (lowerDrawCallbacks != lowerCallbacks + 1 || upperDrawCallbacks != upperCallbacks) {
                std::printf("FAIL: callback-only lower list lost or crossed layers: lower=%d upper=%d\n", lowerDrawCallbacks - lowerCallbacks, upperDrawCallbacks - upperCallbacks);
                return false;
            }
            // Observe the actual lower submission before the native UI pass.
            if (!clearBackBuffer(r, device, swap, false, nullptr, true, false) || !pixelIs(swap, 45, 45, 255, 0, 0) || !pixelIs(swap, 280, 30, 255, 0, 0) ||
                !pixelIs(swap, 320, 30, 255, 0, 0) || !clearBackBuffer(r, device, swap, true, nullptr, true, false) || !clearBackBuffer(r, device, swap, false, nullptr, false))
                return false;
            scene.layer = RenderLayer::AboveUI;
            if (layerTexture && !r.nativeTextures_.contains(layerTexture))
                return false;
            r.overlayScene(scene);
            if (lowerDrawCallbacks != lowerCallbacks + 1 || upperDrawCallbacks != upperCallbacks + 1) {
                std::printf("FAIL: callback-only upper list lost or crossed layers: lower=%d upper=%d\n", lowerDrawCallbacks - lowerCallbacks, upperDrawCallbacks - upperCallbacks);
                return false;
            }
            if (!clearBackBuffer(r, device, swap, false, nullptr, true, false) || !pixelIs(swap, 30, 30, 255, 0, 0) || !pixelIs(swap, 40, 40, 0, 0, 255) ||
                !pixelIs(swap, 45, 45, 255, 255, 0) || !pixelIs(swap, 280, 30, 255, 255, 0) || !pixelIs(swap, 320, 30, 255, 255, 0) || ImGui::GetFrameCount() != frames + 1 ||
                renderCalls != lowerCalls + 1 || aboveCalls != upperCalls + 1 || r.imguiFrame_ != submissions + 2 || r.sceneGeneration_ != generation)
                return false;
            if (layerTexture) {
                if (!pixelIs(swap, 410, 110, 0, 255, 0))
                    return false;
                r.processTextureUploads();
                if (r.nativeTextures_.contains(layerTexture))
                    return false;
                layerTexture = 0;
            }
            swap->Present(0, 0);
        }
        // A frame may contain only custom draw callbacks. They still need
        // their command lists and, for the lower layer, a compositing target.
        {
            D3D12_CPU_DESCRIPTOR_HANDLE target{};
            if (!clearBackBuffer(r, device, swap, true, &target))
                return false;
            int lowerCallbacks = lowerDrawCallbacks, upperCallbacks = upperDrawCallbacks;
            int frames = ImGui::GetFrameCount();
            auto submissions = r.imguiFrame_;
            callbacksOnly = true;
            UiSceneFrame scene{device, r.commandQueue_, swap, target, DXGI_FORMAT_R10G10B10A2_UNORM, viewport, RenderLayer::BelowUI, 900};
            r.overlayScene(scene);
            bool ok = lowerDrawCallbacks == lowerCallbacks + 1 && upperDrawCallbacks == upperCallbacks;
            scene.layer = RenderLayer::AboveUI;
            r.overlayScene(scene);
            callbacksOnly = false;
            ok &= ImGui::GetDrawData()->TotalVtxCount == 0 && lowerDrawCallbacks == lowerCallbacks + 1 && upperDrawCallbacks == upperCallbacks + 1 &&
                  ImGui::GetFrameCount() == frames + 1 && r.imguiFrame_ == submissions + 2;
            if (!clearBackBuffer(r, device, swap, false, nullptr, true, false) || !ok)
                return false;
            std::puts("PASS: callback-only lists and zero-vertex frames execute once in their own render layer.");
            swap->Present(0, 0);
        }
        // Keep the GPU behind a fence while queueing a swap chain's worth of
        // two-stage frames. Each game frame needs two independent render slots;
        // a pool sized only by BufferCount drops the upper layer halfway in.
        {
            using Microsoft::WRL::ComPtr;
            ComPtr<ID3D12Fence> gate;
            D3D12_CPU_DESCRIPTOR_HANDLE target{};
            if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))) || !clearBackBuffer(r, device, swap, true, &target))
                return false;
            flush(r, device);
            auto submissions = r.imguiFrame_;
            int frames = ImGui::GetFrameCount();
            if (FAILED(r.commandQueue_->Wait(gate.Get(), 1)))
                return false;
            for (UINT i = 0; i < desc.BufferCount; ++i) {
                UiSceneFrame scene{device, r.commandQueue_, swap, target, DXGI_FORMAT_R10G10B10A2_UNORM, viewport, RenderLayer::BelowUI, 1000 + i};
                r.overlayScene(scene);
                scene.layer = RenderLayer::AboveUI;
                r.overlayScene(scene);
            }
            bool complete = r.imguiFrame_ == submissions + 2 * desc.BufferCount && ImGui::GetFrameCount() == frames + int(desc.BufferCount);
            gate->Signal(1); // Always release the GPU before cleanup or failure.
            flush(r, device);
            if (!clearBackBuffer(r, device, swap, false, nullptr, true, false))
                return false;
            std::printf("%s: queued %llu/%u layer submissions while GPU is pending, one ImGui frame per game frame.\n", complete ? "PASS" : "FAIL", r.imguiFrame_ - submissions,
                        2 * desc.BufferCount);
            if (!complete)
                return false;

            if (!clearBackBuffer(r, device, swap, true, &target))
                return false;
            flush(r, device);
            // Now let only the first lower submission complete. The ring has
            // one free slot, which must remain available to the upper plugin.
            for (UINT i = 0; i < desc.BufferCount; ++i) {
                UiSceneFrame scene{device, r.commandQueue_, swap, target, DXGI_FORMAT_R10G10B10A2_UNORM, viewport, RenderLayer::BelowUI, 1100 + i};
                r.overlayScene(scene);
                if (i == 0 && FAILED(r.commandQueue_->Wait(gate.Get(), 2)))
                    return false;
                scene.layer = RenderLayer::AboveUI;
                r.overlayScene(scene);
            }
            const UINT firstSlot = UINT(r.imguiFrame_ % r.frameCount_);
            HANDLE firstDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            bool firstCompleted =
                firstDone && SUCCEEDED(r.frameFence_->SetEventOnCompletion(r.allocatorFences_[firstSlot], firstDone)) && WaitForSingleObject(firstDone, 1000) == WAIT_OBJECT_0;
            if (firstDone)
                CloseHandle(firstDone);
            submissions = r.imguiFrame_;
            frames = ImGui::GetFrameCount();
            upperColor = IM_COL32(0, 255, 0, 255);
            UiSceneFrame scene{device, r.commandQueue_, swap, target, DXGI_FORMAT_R10G10B10A2_UNORM, viewport, RenderLayer::BelowUI, 1200};
            r.overlayScene(scene);
            scene.layer = RenderLayer::AboveUI;
            r.overlayScene(scene);
            upperColor = IM_COL32(255, 255, 0, 255);
            complete = firstCompleted && r.imguiFrame_ == submissions + 1 && ImGui::GetFrameCount() == frames + 1;
            gate->Signal(2);
            flush(r, device);
            if (!clearBackBuffer(r, device, swap, false, nullptr, true, false))
                return false;
            complete &= pixelIs(swap, 45, 45, 0, 255, 0);
            std::puts(complete ? "PASS: one remaining render slot is reserved for visible upper UI; no wait or second ImGui frame."
                               : "FAIL: lower layer consumed the upper layer's last available slot");
            if (!complete)
                return false;
        }
        // A borrowed RGBA8 scene target alternates with the RGB10A2 main
        // target while the window, plugins and renderer generation stay bound.
        HWND sceneWindow =
            CreateWindowW(cls.lpszClassName, L"scene target", WS_OVERLAPPEDWINDOW, 0, 0, desc.BufferDesc.Width, desc.BufferDesc.Height, nullptr, nullptr, cls.hInstance, nullptr);
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = desc.BufferDesc.Width;
        sd.Height = desc.BufferDesc.Height;
        sd.BufferCount = 2;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        IDXGISwapChain1 *scene1 = nullptr;
        IDXGISwapChain3 *sceneSwap = nullptr;
        bool ok = sceneWindow && SUCCEEDED(factory->CreateSwapChainForHwnd(r.commandQueue_, sceneWindow, &sd, nullptr, nullptr, &scene1)) &&
                  SUCCEEDED(scene1->QueryInterface(IID_PPV_ARGS(&sceneSwap)));
        if (ok) {
            D3D12_CPU_DESCRIPTOR_HANDLE target{};
            ok = clearBackBuffer(r, device, sceneSwap, true, &target);
            UiSceneFrame scene{device, r.commandQueue_, swap, target, DXGI_FORMAT_R8G8B8A8_UNORM, viewport, RenderLayer::BelowUI, 200};
            r.overlayScene(scene);
            ok &= clearBackBuffer(r, device, sceneSwap, false, nullptr, true, false) && pixelIs(sceneSwap, 45, 45, 255, 0, 0) && pixelIs(sceneSwap, 210, 210, 255, 255, 0) &&
                  clearBackBuffer(r, device, swap, true, &target);
            scene.target = target;
            scene.format = DXGI_FORMAT_R10G10B10A2_UNORM;
            scene.layer = RenderLayer::AboveUI;
            r.overlayScene(scene);
            ok &= clearBackBuffer(r, device, swap, false, nullptr, true, false) && pixelIs(swap, 45, 45, 255, 255, 0) && pixelIs(swap, 30, 30, 0, 0, 0) &&
                  r.sceneGeneration_ == generation;
            swap->Present(0, 0);
        }
        flush(r, device);
        if (sceneSwap)
            sceneSwap->Release();
        if (scene1)
            scene1->Release();
        DestroyWindow(sceneWindow);
        plugins.pop_back();
        plugins.front().layer = RenderLayer::AboveUI;
        checkLayers = false;
        if (ok)
            std::puts("PASS: one ImGui/input frame and one render per plugin; lower overlay -> native UI -> upper overlay, shared lists split correctly, RGBA8/RGB10A2 stages do "
                      "not rebuild renderers, retired upper texture survives both submissions.");
        return ok;
    }

    static int run() {
        if (!bridgeCheck())
            return 33;
        if (!boundaryCheck())
            return 37;
        auto directory = std::filesystem::absolute(L"build/native-checks/overlay-check");
        std::filesystem::create_directories(directory);
        // Old configuration files must not reactivate a removed renderer.
        std::ofstream(directory / L"EROverlay.ini") << "[common]\nrenderer=dxgi\ncharset=en\nfont_size=18\n";
        wcscpy_s(gModulePath, directory.wstring().c_str());
        gConfig.loadFile(L"EROverlay.ini");

        WNDCLASSEXW cls{sizeof(cls)};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"OverlayFrameCheck";
        RegisterClassExW(&cls);
        HWND hwnd = CreateWindowW(cls.lpszClassName, L"ELDEN RING™", WS_OVERLAPPEDWINDOW, 0, 0, 640, 360, nullptr, nullptr, cls.hInstance, nullptr);
        if (!hwnd)
            return 1;

        ID3D12Debug1 *debug = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            debug->SetEnableGPUBasedValidation(TRUE);
            debug->Release();
        } else
            return 31;
        ID3D12Device *device = nullptr;
        ID3D12CommandQueue *queue = nullptr;
        IDXGIFactory4 *factory = nullptr;
        IDXGISwapChain1 *swap1 = nullptr;
        IDXGISwapChain3 *swap = nullptr;
        ID3D12InfoQueue *info = nullptr;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.BufferCount = 3;
        sd.Width = 640;
        sd.Height = 360;
        const DXGI_FORMAT surfaceFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
        sd.Format = surfaceFormat;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) || FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))) ||
            FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &swap1)) ||
            FAILED(swap1->QueryInterface(IID_PPV_ARGS(&swap))))
            return 2;
        device->QueryInterface(IID_PPV_ARGS(&info));
        gInfo = info;
        if (!info)
            return 32;
        if (info) {
            info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
            info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        }
        std::puts("device ready");
        std::printf("main RTV format=%u, offscreen RTV format=%u\n", unsigned(surfaceFormat), unsigned(DXGI_FORMAT_R8G8B8A8_UNORM));
        std::puts("D3D12 debug layer and GPU-based validation enabled; warnings/errors fail the check.");

        if (MH_Initialize() != MH_OK)
            return 39;
        // This test executable has no Elden Ring signatures. Startup must
        // reject it promptly instead of repeatedly scanning an unsupported EXE.
        HANDLE startupDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!startupDone)
            return 39;
        std::atomic_bool timeout{false};
        std::thread watchdog([&] {
            if (WaitForSingleObject(startupDone, 1000) != WAIT_OBJECT_0) {
                timeout = true;
                gRunning = false;
            }
        });
        bool hooked = waitForRendererHook();
        SetEvent(startupDone);
        watchdog.join();
        CloseHandle(startupDone);
        gRunning = true;
        bool rejected = !hooked && !timeout && gD3DRenderer && !gD3DRenderer->uiScene_ && !gD3DRenderer->device_ && !gD3DRenderer->commandQueue_;
        MH_Uninitialize();
        if (!rejected || !ImGui::GetCurrentContext())
            return 39;
        std::puts("PASS: unsupported native image returns once with obsolete renderer=dxgi; no fallback or permanent startup retry.");
        auto &r = *gD3DRenderer;
        queue->AddRef();
        r.commandQueue_ = queue;
        plugins.push_back({0, &exports, true, RenderLayer::AboveUI});

        auto overlayOnly = [&] {
            D3D12_CPU_DESCRIPTOR_HANDLE target{};
            if (!clearBackBuffer(r, device, swap, true, &target))
                return false;
            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(swap->GetDesc(&desc)))
                return false;
            UiSceneFrame scene{device, queue, swap, target, desc.BufferDesc.Format, {0, 0, float(desc.BufferDesc.Width), float(desc.BufferDesc.Height), 0, 1}};
            r.overlayScene(scene);
            // Queue the game's later work without waiting for Overlay on the CPU.
            if (!clearBackBuffer(r, device, swap, false))
                return false;
            return true;
        };
        auto frame = [&]() -> HRESULT {
            if (!overlayOnly())
                return E_FAIL;
            return swap->Present(0, 0);
        };
        // First frame binds the swap chain and builds device objects.
        mode = 0;
        if (FAILED(frame()) || r.boundSwapChain_ != swap || !r.device_)
            return 4;
        // Empty frames: no ImGui render, no allocator use, no fence signal.
        auto imgui = r.imguiFrame_, value = r.frameValue_;
        for (int i = 0; i < 6; ++i)
            if (FAILED(frame()))
                return 5;
        if (r.imguiFrame_ != imgui || r.frameValue_ != value) {
            std::printf("FAIL empty frames submitted: imgui %llu->%llu fence %llu->%llu\n", imgui, r.imguiFrame_, value, r.frameValue_);
            return 6;
        }
        // Visible frames submit once each.
        mode = 1;
        for (int i = 0; i < 6; ++i)
            if (FAILED(frame()))
                return 7;
        if (r.imguiFrame_ != imgui + 6 || r.frameValue_ != value + 6) {
            std::printf("FAIL visible frames: imgui %llu fence %llu\n", r.imguiFrame_, r.frameValue_);
            return 8;
        }
        // Pixels of the current frame, read before Present.
        overlayOnly();
        uint8_t pixel[4]{}, clear[4]{};
        if (!readPixel(r, device, swap, 30, 30, pixel) || pixel[0] < 250 || pixel[1] > 5 || pixel[2] > 5 || textSeen) {
            std::printf("FAIL overlay pixel %u %u %u %u text=%d\n", pixel[0], pixel[1], pixel[2], pixel[3], textSeen);
            return 9;
        }
        swap->Present(0, 0);
        // New glyph sizes upload through ImGui on the overlay upload queue.
        mode = 2;
        for (glyphSize = 18; glyphSize < 60; glyphSize += 3)
            if (FAILED(frame()))
                return 10;
        // Translucent local offscreen composite, as used by the minimap.
        mode = 3;
        for (int i = 0; i < 6; ++i)
            if (FAILED(frame()))
                return 11;
        overlayOnly();
        if (!readPixel(r, device, swap, 132, 132, pixel) || pixel[1] < 120 || pixel[1] > 135 || pixel[0] > 5) {
            std::printf("FAIL offscreen pixel %u %u %u %u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
            return 12;
        }
        if (!readPixel(r, device, swap, 300, 300, clear) || clear[0] || clear[1] || clear[2]) {
            std::printf("FAIL clear pixel %u %u %u %u\n", clear[0], clear[1], clear[2], clear[3]);
            return 13;
        }
        auto *offscreenContext = static_cast<OffscreenContext *>(offscreen);
        if (!offscreenContext || offscreenContext->target.texture->GetDesc().Format != DXGI_FORMAT_R8G8B8A8_UNORM)
            return 34;
        // 20% alpha drawn into RGBA8, then composited at 50% opacity. This
        // catches RGB10A2's 2-bit alpha accidentally being used offscreen.
        if (!readPixel(r, device, swap, 152, 112, pixel) || pixel[0] > 1 || pixel[1] < 4 || pixel[1] > 6 || pixel[2] > 1) {
            std::printf("FAIL offscreen alpha precision pixel %u %u %u %u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
            return 35;
        }
        swap->Present(0, 0);
        // Allocate the native lower layer before resize, then repeat its
        // opacity checks after destroying/recreating the HWND and swap chain.
        if (!opacityCheck(r, device, swap)) {
            if (info)
                messages(info);
            return 37;
        }
        // A resize-style teardown rebuilds the binding and device objects.
        int fonts = ImGui::GetIO().Fonts->Fonts.Size;
        flush(r, device);
        auto generation = r.sceneGeneration_;
        // Deliberately leave the production renderer untouched.
        // ResizeBuffers must succeed without any DXGI resize hook notifying it.
        if (FAILED(swap->ResizeBuffers(2, 800, 450, DXGI_FORMAT_UNKNOWN, 0)))
            return 14;
        SetWindowPos(hwnd, nullptr, 0, 0, 800 + 16, 450 + 39, SWP_NOMOVE | SWP_NOZORDER);
        for (int i = 0; i < 6; ++i)
            if (FAILED(frame()))
                return 15;
        if (r.boundSwapChain_ != swap || r.sceneGeneration_ != generation + 1 || ImGui::GetIO().Fonts->Fonts.Size != fonts) {
            std::printf("FAIL rebuild fonts %d -> %d\n", fonts, ImGui::GetIO().Fonts->Fonts.Size);
            return 16;
        }
        if (r.sceneUnderlay_.texture)
            return 38; // Old-size underlay must be released with its generation.
        overlayOnly();
        if (!readPixel(r, device, swap, 30, 30, pixel) || pixel[0] < 250 || !textSeen) {
            std::printf("FAIL rebuilt overlay pixel %u %u %u %u text=%d\n", pixel[0], pixel[1], pixel[2], pixel[3], textSeen);
            return 17;
        }
        swap->Present(0, 0);
        {
            flush(r, device);
            generation = r.sceneGeneration_;
            DestroyWindow(hwnd);
            if (!r.windowDestroyed_.load())
                return 22;
            hwnd = CreateWindowW(cls.lpszClassName, L"ELDEN RING™", WS_OVERLAPPEDWINDOW, 0, 0, 720, 400, nullptr, nullptr, cls.hInstance, nullptr);
            if (!hwnd)
                return 23;
            swap->Release();
            swap1->Release();
            swap = nullptr;
            swap1 = nullptr;
            sd.Width = 720;
            sd.Height = 400;
            if (FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &swap1)) || FAILED(swap1->QueryInterface(IID_PPV_ARGS(&swap))))
                return 24;
            for (int i = 0; i < 6; ++i)
                if (FAILED(frame()))
                    return 25;
            if (r.gameWindow_ != hwnd || r.windowDestroyed_.load() || r.sceneGeneration_ != generation + 1 || ImGui::GetIO().Fonts->Fonts.Size != fonts)
                return 26;
            if (!overlayOnly() || !readPixel(r, device, swap, 30, 30, pixel) || pixel[0] < 250 || !textSeen)
                return 27;
            if (!readPixel(r, device, swap, 40, 40, pixel) || pixel[2] < 250 || pixel[0] > 5)
                return 28;
            if (!readPixel(r, device, swap, 210, 210, pixel) || pixel[0] < 250 || pixel[1] < 250 || pixel[2] > 5)
                return 29;
            if (!readPixel(r, device, swap, 132, 132, pixel) || pixel[1] < 120 || pixel[1] > 135 || pixel[0] > 5)
                return 30;
            swap->Present(0, 0);
            std::puts(
                "PASS: native ResizeBuffers without notification, destroyed/recreated HWND and swap chain, fonts retained, pre/overlay/post GPU order, offscreen RTV restoration.");
            if (!layersCheck(r, device, swap, factory, cls))
                return 36;
            if (!opacityCheck(r, device, swap)) {
                if (info)
                    messages(info);
                return 37;
            }
        }
        // Hidden again: retirements still complete without visible frames.
        mode = 0;
        destroy();
        value = r.frameValue_;
        for (int i = 0; i < 4; ++i)
            if (FAILED(frame()))
                return 18;
        r.processTextureUploads();
        if (!r.retiredTextures_.empty() && r.frameValue_ == value) {
            std::puts("FAIL retired textures were never fenced while hidden");
            return 19;
        }
        r.waitForFrames();
        r.processTextureUploads();
        if (!r.retiredTextures_.empty())
            return 20;

        flush(r, device);
        clearBackBuffer(r, nullptr, nullptr);
        // Also cover teardown after the window died, before a new scene binds.
        DestroyWindow(hwnd);
        hwnd = nullptr;
        r.unhook();
        gD3DRenderer.reset();
        swap->Release();
        swap1->Release();
        factory->Release();
        queue->Release();
        if (info)
            messages(info);
        unsigned errors = validationIssues.load();
        gInfo = nullptr;
        if (info)
            info->Release();
        device->Release();
        DestroyWindow(hwnd);
        if (errors)
            return 21;
        std::puts("PASS: overlay binds once, skips empty frames, submits visible frames, uploads atlas growth, composites offscreen, rebuilds after resize.");
        return 0;
    }
};
} // namespace er

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ui-compositor") == 0 && i + 2 < argc) {
            uiCompositorVertexPath = argv[++i];
            uiCompositorPath = argv[++i];
        } else
            return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("start");
    AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS *e) -> LONG {
        auto code = e->ExceptionRecord->ExceptionCode;
        if (code == 0x87D || code == 0x87A) {
            if (gInfo)
                er::NativeTextureVerifier::messages(gInfo);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (code != 0x406D1388 && code != 0xE06D7363 && code != DBG_PRINTEXCEPTION_C && code != 0x4001000A)
            std::printf("exception 0x%08lX at %p\n", code, e->ExceptionRecord->ExceptionAddress);
        return EXCEPTION_CONTINUE_SEARCH;
    });
    std::atexit([] { std::puts("atexit"); });
    int result = er::NativeTextureVerifier::run();
    std::printf("ui-scene-frame-check=%d\n", result);
    return result;
}
