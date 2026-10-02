#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

#include "d3drenderer.hpp"

namespace er {
struct NativeTextureVerifier {
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
        // The production uploader budgets 8 MiB per frame; drive multiple
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
        if (renderer.pollTexture(token, view) != ER_TEXTURE_READY || view.width != 256 || view.height != 256)
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
        std::puts("PASS: production BC7 upload/readback matches DDS blocks, frame fences and SRV retirement.");
        std::puts("PASS: twelve BC1 atlases upload together, each GPU readback matches its DDS and all twelve descriptors retire.");
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
