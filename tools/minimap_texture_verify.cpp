#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>

#include <imgui_impl_dx12.h>

#include "d3drenderer.hpp"

namespace er {
struct NativeTextureVerifier {
    static bool verifyConcurrentQueue(D3DRenderer &renderer, util::Bytes bytes) {
        std::vector<uint64_t> tokens;
        std::atomic_bool done{false};
        std::thread producer([&] {
            for (unsigned i = 0; i < 128; ++i) {
                auto token = renderer.createDdsTexture(bytes.data(), bytes.size());
                if (!token)
                    break;
                tokens.push_back(token);
            }
            done.store(true, std::memory_order_release);
        });
        while (!done.load(std::memory_order_acquire)) {
            renderer.processTextureUploads();
            std::this_thread::yield();
        }
        producer.join();
        bool ready = false;
        for (unsigned i = 0; i < 128 && !ready; ++i) {
            auto before = renderer.uploadValue_;
            renderer.processTextureUploads();
            if (renderer.uploadValue_ - before > 4 || renderer.uploadingTextureCount_ > 8 || renderer.uploadingTextureBytes_ > 16 * 1024 * 1024)
                return false;
            renderer.waitForFrames();
            ready = std::all_of(tokens.begin(), tokens.end(), [&](auto token) {
                ERTextureView view;
                return renderer.pollTexture(token, view) == ER_TEXTURE_READY;
            });
        }
        renderer.processTextureUploads();
        if (tokens.size() != 128 || !ready || !renderer.textureWork_.empty() || renderer.queuedTextureBytes_.load() != 0)
            return false;
        for (auto token: tokens)
            renderer.retireTexture(token);
        renderer.processTextureUploads();
        return renderer.nativeTextures_.empty() && renderer.nativeTextureCount_.load() == 0 && renderer.freeDescriptors_.size() == 1024;
    }
    static bool verifyOffscreen(D3DRenderer &renderer) {
        // Exercise the stock ImGui DX12 backend, production local-target
        // callbacks, scissor translation, compositing and real GPU pixels.
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.DisplaySize = {640, 360};
        io.DeltaTime = 1.f / 60;
        io.IniFilename = io.LogFilename = nullptr;
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = 1026;
        if (FAILED(renderer.device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&renderer.rtvDescriptorHeap_))))
            return false;
        renderer.rtvDescriptorSize_ = renderer.device_->GetDescriptorHandleIncrementSize(heap.Type);
        renderer.currentRTV_ = renderer.rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC target{};
        target.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        target.Width = 640;
        target.Height = 360;
        target.DepthOrArraySize = target.MipLevels = 1;
        target.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        target.SampleDesc.Count = 1;
        target.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ID3D12Resource *mainTarget = nullptr;
        if (FAILED(renderer.device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &target, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&mainTarget))))
            return false;
        renderer.device_->CreateRenderTargetView(mainTarget, nullptr, renderer.currentRTV_);
        ImGui_ImplDX12_InitInfo info{};
        info.Device = renderer.device_;
        info.CommandQueue = renderer.commandQueue_;
        info.NumFramesInFlight = 2;
        info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        info.SrvDescriptorHeap = renderer.descriptorHeap_;
        info.UserData = &renderer;
        info.SrvDescriptorAllocFn = D3DRenderer::SrvDescriptorAlloc;
        info.SrvDescriptorFreeFn = D3DRenderer::SrvDescriptorFree;
        ID3D12CommandAllocator *allocator = nullptr;
        if (!ImGui_ImplDX12_Init(&info) || FAILED(renderer.device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
            FAILED(renderer.device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&renderer.commandList_))) ||
            FAILED(renderer.commandList_->Close()))
            return false;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows;
        UINT64 rowSize, total;
        renderer.device_->GetCopyableFootprints(&target, 0, 1, 0, &footprint, &rows, &rowSize, &total);
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = total;
        buffer.Height = 1;
        buffer.DepthOrArraySize = buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        properties.Type = D3D12_HEAP_TYPE_READBACK;
        ID3D12Resource *readback = nullptr;
        if (FAILED(renderer.device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))))
            return false;
        auto *offscreen = renderer.CreateOffscreen();
        auto *sibling = renderer.CreateOffscreen();
        struct Case {
            ImVec2 pos, size, viewport;
            bool local;
        };
        const Case cases[] = {{{500, 220}, {96, 96}, {0, 0}, true},
                              {{0, 0}, {80, 64}, {0, 0}, true},
                              {{120, 100}, {64, 80}, {100, 50}, true},
                              {{300, 150}, {128, 96}, {0, 0}, false},
                              {{400, 180}, {64, 64}, {0, 0}, true}};
        bool correct = true;
        for (size_t frame = 0; frame < std::size(cases); ++frame) {
            const auto &c = cases[frame];
            renderer.currentBackBufferIndex_ = static_cast<UINT>(frame % 2);
            renderer.waitForFrames();
            renderer.processTextureUploads();
            ImGui_ImplDX12_NewFrame();
            ImGui::NewFrame();
            auto *viewport = ImGui::GetMainViewport();
            viewport->Pos = viewport->WorkPos = c.viewport;
            ImGui::SetNextWindowPos(c.viewport);
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
            ImGui::Begin("offscreen-verification", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
            auto *draw = ImGui::GetWindowDrawList();
            renderer.drawingFrame_ = true;
            bool began = c.local ? renderer.BeginOffscreenRegion(offscreen, c.pos.x, c.pos.y, c.size.x, c.size.y) : renderer.BeginOffscreen(offscreen);
            if (!began)
                return false;
            draw->AddRectFilled({c.pos.x + 8, c.pos.y + 8}, {c.pos.x + c.size.x - 8, c.pos.y + c.size.y - 8}, IM_COL32(255, 0, 0, 255));
            draw->AddRectFilled({c.pos.x + 20, c.pos.y + 20}, {c.pos.x + 40, c.pos.y + 40}, IM_COL32(0, 255, 0, 255));
            void *handle = renderer.EndOffscreen(offscreen);
            ImVec2 uv0 = c.local ? ImVec2(0, 0) : ImVec2((c.pos.x - c.viewport.x) / 640, (c.pos.y - c.viewport.y) / 360);
            ImVec2 uv1 = c.local ? ImVec2(1, 1) : ImVec2((c.pos.x + c.size.x - c.viewport.x) / 640, (c.pos.y + c.size.y - c.viewport.y) / 360);
            draw->AddImage(reinterpret_cast<ImTextureID>(handle), c.pos, {c.pos.x + c.size.x, c.pos.y + c.size.y}, uv0, uv1, IM_COL32(255, 255, 255, 128));
            // A second plugin/context in the same frame must have independent
            // RTV/SRV descriptors and restore the main viewport/projection.
            ImVec2 second{c.viewport.x + 200, c.viewport.y + 20};
            if (!renderer.BeginOffscreenRegion(sibling, second.x, second.y, 32, 32))
                return false;
            draw->AddRectFilled(second, {second.x + 32, second.y + 32}, IM_COL32(255, 255, 0, 255));
            auto *other = renderer.EndOffscreen(sibling);
            draw->AddImage(reinterpret_cast<ImTextureID>(other), second, {second.x + 32, second.y + 32});
            draw->AddRectFilled({c.viewport.x + 240, c.viewport.y + 20}, {c.viewport.x + 256, c.viewport.y + 36}, IM_COL32(255, 0, 255, 255));
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::Render();
            auto &localTarget = offscreen->targets[offscreen->currentIndex];
            if (localTarget.width != (c.local ? int(c.size.x) : 640) || localTarget.height != (c.local ? int(c.size.y) : 360) ||
                localTarget.rtvCpuHandle.ptr == sibling->targets[sibling->currentIndex].rtvCpuHandle.ptr)
                return false;
            if (FAILED(allocator->Reset()) || FAILED(renderer.commandList_->Reset(allocator, nullptr)))
                return false;
            auto *list = renderer.commandList_;
            list->OMSetRenderTargets(1, &renderer.currentRTV_, FALSE, nullptr);
            list->SetDescriptorHeaps(1, &renderer.descriptorHeap_);
            const float blue[]{0, 0, 1, 1};
            list->ClearRenderTargetView(renderer.currentRTV_, blue, 0, nullptr);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), list);
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = mainTarget;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            list->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION destination{}, source{};
            destination.pResource = readback;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;
            source.pResource = mainTarget;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(1, &barrier);
            if (FAILED(list->Close()))
                return false;
            ID3D12CommandList *lists[]{list};
            renderer.commandQueue_->ExecuteCommandLists(1, lists);
            renderer.finishTextureFrame();
            renderer.waitForFrames();
            void *mapped = nullptr;
            D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
            if (FAILED(readback->Map(0, &range, &mapped)))
                return false;
            auto pixel = [&](float x, float y, std::array<uint8_t, 4> expected) {
                auto *actual = static_cast<uint8_t *>(mapped) + footprint.Offset + size_t(y) * footprint.Footprint.RowPitch + size_t(x) * 4;
                for (size_t channel = 0; channel < 4; ++channel)
                    if (std::abs(int(actual[channel]) - int(expected[channel])) > 2) {
                        std::fprintf(stderr, "FAIL offscreen frame=%zu pixel=%.0f,%.0f channel=%zu actual=%u expected=%u\n", frame, x, y, channel, actual[channel],
                                     expected[channel]);
                        return false;
                    }
                return true;
            };
            float x = c.pos.x - c.viewport.x, y = c.pos.y - c.viewport.y;
            correct &= pixel(x + 12, y + 12, {128, 0, 127, 255}) && pixel(x + 24, y + 24, {0, 128, 127, 255}) && pixel(x + 2, y + 2, {0, 0, 255, 255}) &&
                       pixel(212, 28, {255, 255, 0, 255}) && pixel(248, 28, {255, 0, 255, 255});
            readback->Unmap(0, nullptr);
        }
        renderer.drawingFrame_ = false;
        renderer.DestroyOffscreen(offscreen);
        renderer.DestroyOffscreen(sibling);
        renderer.processTextureUploads();
        ImGui_ImplDX12_Shutdown();
        ImGui::DestroyContext();
        mainTarget->Release();
        readback->Release();
        renderer.commandList_->Release();
        renderer.commandList_ = nullptr;
        allocator->Release();
        return correct && renderer.retiredTextures_.empty() && renderer.freeDescriptors_.size() == 1024;
    }
    static bool verifyMultipleAtlases(D3DRenderer &renderer) {
        std::ifstream stream("build/native-checks/mod-fixture/atlases.tpf", std::ios::binary | std::ios::ate);
        auto size = stream.tellg();
        if (size <= 0)
            return false;
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char *>(bytes.data()), size);
        std::vector<util::TpfEntry> entries;
        if (!util::parseTpf(bytes, entries) || entries.size() != 12)
            return false;
        std::vector<uint64_t> tokens;
        for (const auto &entry: entries) {
            auto token = renderer.createDdsTexture(entry.dds.data(), entry.dds.size());
            if (!token)
                return false;
            tokens.push_back(token);
        }
        // The production uploader budgets 4 MiB / four uploads per frame; drive multiple
        // frames until all twelve queued atlases have actually completed.
        bool ready = false;
        for (unsigned frame = 0; frame < 12 && !ready; ++frame) {
            renderer.processTextureUploads();
            renderer.waitForFrames();
            renderer.processTextureUploads();
            ready = std::all_of(tokens.begin(), tokens.end(), [&](auto token) {
                ERTextureView view;
                return renderer.pollTexture(token, view) == ER_TEXTURE_READY;
            });
        }
        if (!ready || renderer.freeDescriptors_.size() != 1012) {
            std::fprintf(stderr, "FAIL multi-atlas upload ready=%d free-descriptors=%zu\n", ready, renderer.freeDescriptors_.size());
            return false;
        }
        struct Copy {
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
            UINT rows;
        };
        std::vector<Copy> copies;
        UINT64 total = 0;
        for (auto token: tokens) {
            ERTextureView view;
            if (renderer.pollTexture(token, view) != ER_TEXTURE_READY || view.width != 2048 || view.height != 2048)
                return false;
            auto desc = renderer.nativeTextures_.at(token).texture->GetDesc();
            Copy copy{};
            UINT64 rowSize, count;
            total = (total + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~(UINT64(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT) - 1);
            renderer.device_->GetCopyableFootprints(&desc, 0, 1, total, &copy.footprint, &copy.rows, &rowSize, &count);
            total += count;
            copies.push_back(copy);
        }
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = total;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource *readback = nullptr;
        ID3D12CommandAllocator *allocator = nullptr;
        ID3D12GraphicsCommandList *list = nullptr;
        if (FAILED(renderer.device_->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))) ||
            FAILED(renderer.device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
            FAILED(renderer.device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list))))
            return false;
        for (size_t i = 0; i < tokens.size(); ++i) {
            auto *texture = renderer.nativeTextures_.at(tokens[i]).texture;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = texture;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            list->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION destination{}, source{};
            destination.pResource = readback;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = copies[i].footprint;
            source.pResource = texture;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            list->ResourceBarrier(1, &barrier);
        }
        if (FAILED(list->Close()))
            return false;
        ID3D12CommandList *lists[]{list};
        renderer.commandQueue_->ExecuteCommandLists(1, lists);
        renderer.drawingFrame_ = true;
        for (auto token: tokens) {
            ERTextureView view;
            if (renderer.pollTexture(token, view) != ER_TEXTURE_READY)
                return false;
        }
        renderer.finishTextureFrame();
        for (auto token: tokens)
            renderer.retireTexture(token);
        renderer.waitForFrames();
        void *mapped = nullptr;
        D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
        if (FAILED(readback->Map(0, &range, &mapped)))
            return false;
        bool same = true;
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto &level = renderer.nativeTextures_.at(tokens[i]).image.levels[0];
            for (UINT row = 0; row < copies[i].rows; ++row)
                if (std::memcmp(static_cast<uint8_t *>(mapped) + copies[i].footprint.Offset + size_t(row) * copies[i].footprint.Footprint.RowPitch,
                                entries[i].dds.data() + level.offset + size_t(row) * level.rowBytes, level.rowBytes))
                    same = false;
        }
        readback->Unmap(0, nullptr);
        list->Release();
        allocator->Release();
        readback->Release();
        renderer.processTextureUploads();
        return same && renderer.nativeTextures_.empty() && renderer.freeDescriptors_.size() == 1024;
    }
    static int run(D3DRenderer &renderer) {
        const auto *native = getEROverlayNativeAPI(1);
        if (!native || native->log)
            return 18;
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&renderer.device_))))
            return 1;
        D3D12_COMMAND_QUEUE_DESC queue{};
        queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(renderer.device_->CreateCommandQueue(&queue, IID_PPV_ARGS(&renderer.commandQueue_))))
            return 2;
        renderer.buffersCounts_ = 2;
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 1024;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(renderer.device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&renderer.descriptorHeap_))))
            return 3;
        renderer.srvDescriptorSize_ = renderer.device_->GetDescriptorHandleIncrementSize(heap.Type);
        for (uint32_t i = 1024; i > 0; --i)
            renderer.freeDescriptors_.push_back(i - 1);
        if (!renderer.initializeTextureUpload())
            return 4;
        std::ifstream stream("build/ida/probes/run-26836-32694765/surface-v8000.tpf", std::ios::binary | std::ios::ate);
        auto size = stream.tellg();
        if (size <= 0)
            return 5;
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char *>(bytes.data()), size);
        std::vector<util::TpfEntry> entries;
        if (!util::parseTpf(bytes, entries) || entries.size() != 1)
            return 6;
        auto token = renderer.createDdsTexture(entries[0].dds.data(), entries[0].dds.size());
        if (!token)
            return 7;
        renderer.processTextureUploads();
        renderer.waitForFrames();
        renderer.processTextureUploads();
        ERTextureView view;
        if (renderer.pollTexture(token, view) != ER_TEXTURE_READY || view.width != 256 || view.height != 256 || renderer.textureMemoryBytes(token) < 65536)
            return 8;
        auto &texture = renderer.nativeTextures_.at(token);
        auto desc = texture.texture->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows;
        UINT64 rowSize, total;
        renderer.device_->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowSize, &total);
        D3D12_HEAP_PROPERTIES props{};
        props.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = total;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource *readback = nullptr;
        ID3D12CommandAllocator *allocator = nullptr;
        ID3D12GraphicsCommandList *list = nullptr;
        if (FAILED(renderer.device_->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))) ||
            FAILED(renderer.device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
            FAILED(renderer.device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list))))
            return 9;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.texture;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION destination{}, source{};
        destination.pResource = readback;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        source.pResource = texture.texture;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &barrier);
        if (FAILED(list->Close()))
            return 10;
        ID3D12CommandList *lists[] = {list};
        renderer.commandQueue_->ExecuteCommandLists(1, lists);
        renderer.drawingFrame_ = true;
        renderer.finishTextureFrame();
        renderer.waitForFrames();
        void *mapped;
        D3D12_RANGE range{0, static_cast<SIZE_T>(total)};
        if (FAILED(readback->Map(0, &range, &mapped)))
            return 11;
        bool same = true;
        const auto &level = texture.image.levels[0];
        for (UINT row = 0; row < rows; ++row)
            if (std::memcmp(static_cast<uint8_t *>(mapped) + footprint.Offset + size_t(row) * footprint.Footprint.RowPitch,
                            entries[0].dds.data() + level.offset + size_t(row) * level.rowBytes, level.rowBytes))
                same = false;
        readback->Unmap(0, nullptr);
        list->Release();
        allocator->Release();
        readback->Release();
        renderer.retireTexture(token);
        renderer.processTextureUploads();
        bool gone = renderer.pollTexture(token, view) == ER_TEXTURE_INVALID && renderer.freeDescriptors_.size() == 1024;
        renderer.commandQueue_->Wait(renderer.frameFence_, renderer.frameValue_ + 1);
        auto held = renderer.createDdsTexture(entries[0].dds.data(), entries[0].dds.size());
        renderer.processTextureUploads();
        renderer.waitForFrames();
        renderer.processTextureUploads();
        renderer.drawingFrame_ = true;
        if (renderer.pollTexture(held, view) != ER_TEXTURE_READY)
            return 13;
        renderer.finishTextureFrame();
        renderer.retireTexture(held);
        renderer.processTextureUploads();
        bool delayed = renderer.nativeTextures_.contains(held) && renderer.freeDescriptors_.size() == 1023;
        renderer.frameFence_->Signal(renderer.frameValue_);
        renderer.waitForFrames();
        renderer.processTextureUploads();
        if (!delayed || renderer.nativeTextures_.contains(held) || renderer.freeDescriptors_.size() != 1024)
            return 14;
        auto second = renderer.createDdsTexture(entries[0].dds.data(), entries[0].dds.size());
        renderer.retireTexture(second);
        renderer.processTextureUploads();
        if (!same || !gone || !renderer.nativeTextures_.empty())
            return 12;
        if (!verifyMultipleAtlases(renderer))
            return 15;
        if (!verifyConcurrentQueue(renderer, entries[0].dds))
            return 16;
        if (!verifyOffscreen(renderer))
            return 17;
        D3D12_RESOURCE_DESC offscreen{};
        offscreen.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        offscreen.Width = 3840;
        offscreen.Height = 2160;
        offscreen.DepthOrArraySize = offscreen.MipLevels = 1;
        offscreen.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        offscreen.SampleDesc.Count = 1;
        offscreen.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        auto fullBytes = renderer.device_->GetResourceAllocationInfo(0, 1, &offscreen).SizeInBytes * 3;
        offscreen.Width = offscreen.Height = 648;
        auto localBytes = renderer.device_->GetResourceAllocationInfo(0, 1, &offscreen).SizeInBytes * 3;
        std::printf("MEMORY: 4K/648px circle/3 backbuffers: fullscreen=%llu local=%llu bytes (actual device allocation).\n", static_cast<unsigned long long>(fullBytes),
                    static_cast<unsigned long long>(localBytes));
        std::puts("PASS: production BC7 upload/readback matches DDS blocks, frame fences and SRV retirement.");
        std::puts("PASS: twelve BC1 atlases upload together, each GPU readback matches its DDS and all twelve descriptors retire.");
        std::puts("PASS: 128 concurrent CPU queue requests, bounded GPU submissions and no resident texture scan at steady state.");
        std::puts("PASS: real DX12 local/fullscreen offscreen pixels, transparency, nonzero viewport origins, resize, independent contexts and descriptor retirement.");
        return 0;
    }
};
} // namespace er
int main() {
    er::D3DRenderer renderer;
    int result = er::NativeTextureVerifier::run(renderer);
    std::printf("texture-verification=%d\n", result);
    return result;
}
