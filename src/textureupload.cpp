#define NOMINMAX
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "d3drenderer.hpp"
#include "util/nativelog.hpp"

namespace er {
namespace {
template<typename T>
void releaseCom(T *&pointer) {
    if (pointer)
        pointer->Release();
    pointer = nullptr;
}
} // namespace

bool D3DRenderer::initializeTextureUpload() {
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&uploadQueue_))) || FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence_))) ||
        FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frameFence_))))
        return false;
    allocatorFences_.assign(buffersCounts_, 0);
    imguiFences_.assign(buffersCounts_, 0);
    return true;
}

uint64_t D3DRenderer::createDdsTexture(const void *bytes, uint64_t size) {
    if (!device_ || !uploadQueue_ || !bytes || !size || size > 32ull * 1024 * 1024 || queuedTextureBytes_ + size > 64ull * 1024 * 1024 || nativeTextures_.size() >= 768)
        return 0;
    util::DdsImage image;
    util::Bytes source(static_cast<const uint8_t *>(bytes), static_cast<size_t>(size));
    if (!util::parseDds(source, image))
        return 0;
    auto &texture = nativeTextures_[nextTextureToken_];
    texture.image = std::move(image);
    texture.bytes.assign(source.begin(), source.end());
    queuedTextureBytes_ += source.size();
    return nextTextureToken_++;
}

ERTextureStatus D3DRenderer::pollTexture(uint64_t token, ERTextureView &view) {
    view = {};
    auto it = nativeTextures_.find(token);
    if (it == nativeTextures_.end() || it->second.retired)
        return ER_TEXTURE_INVALID;
    auto &texture = it->second;
    if (texture.status == ER_TEXTURE_READY) {
        view = {reinterpret_cast<void *>(texture.gpu.ptr), texture.image.width, texture.image.height};
        // Views are requested while building this frame's ImGui draw list.
        texture.drawValue = std::max(texture.drawValue, frameValue_ + (drawingFrame_ ? 1 : 0));
    }
    return texture.status;
}

void D3DRenderer::retireTexture(uint64_t token) {
    auto it = nativeTextures_.find(token);
    if (it != nativeTextures_.end())
        it->second.retired = true;
}

bool D3DRenderer::submitTextureUpload(NativeTexture &texture) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = texture.image.width;
    desc.Height = texture.image.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = static_cast<UINT16>(texture.image.levels.size());
    desc.Format = static_cast<DXGI_FORMAT>(texture.image.format);
    desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    HeapDescriptorAlloc(&texture.cpu, &texture.gpu);
    if (!texture.cpu.ptr)
        return false;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture.texture))))
        return false;
    UINT count = static_cast<UINT>(texture.image.levels.size());
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(count);
    std::vector<UINT> rows(count);
    std::vector<UINT64> rowSizes(count);
    UINT64 total = 0;
    device_->GetCopyableFootprints(&desc, 0, count, 0, footprints.data(), rows.data(), rowSizes.data(), &total);
    if (!total || total > 64ull * 1024 * 1024)
        return false;
    D3D12_RESOURCE_DESC buffer = {};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = total;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&texture.upload))))
        return false;
    void *mapped = nullptr;
    D3D12_RANGE empty{0, 0};
    if (FAILED(texture.upload->Map(0, &empty, &mapped)))
        return false;
    bool ok = true;
    for (UINT i = 0; i < count; ++i) {
        const auto &level = texture.image.levels[i];
        if (rows[i] != level.rows || rowSizes[i] != level.rowBytes) {
            ok = false;
            break;
        }
        for (UINT row = 0; row < rows[i]; ++row) {
            std::memcpy(static_cast<uint8_t *>(mapped) + footprints[i].Offset + static_cast<size_t>(row) * footprints[i].Footprint.RowPitch,
                        texture.bytes.data() + level.offset + static_cast<size_t>(row) * level.rowBytes, level.rowBytes);
        }
    }
    texture.upload->Unmap(0, nullptr);
    if (!ok || FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&texture.allocator))) ||
        FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, texture.allocator, nullptr, IID_PPV_ARGS(&texture.list))))
        return false;
    for (UINT i = 0; i < count; ++i) {
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = texture.texture;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = i;
        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = texture.upload;
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprints[i];
        texture.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    texture.list->ResourceBarrier(1, &barrier);
    if (FAILED(texture.list->Close()))
        return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = count;
    device_->CreateShaderResourceView(texture.texture, &srv, texture.cpu);
    ID3D12CommandList *lists[] = {texture.list};
    uploadQueue_->ExecuteCommandLists(1, lists);
    texture.uploadValue = ++uploadValue_;
    HRESULT hr = uploadQueue_->Signal(uploadFence_, texture.uploadValue);
    if (FAILED(hr)) {
        deviceLost_ = true;
        return false;
    }
    return true;
}

void D3DRenderer::processTextureUploads() {
    if (!uploadFence_ || !frameFence_ || deviceLost_)
        return;
    uint64_t uploaded = uploadFence_->GetCompletedValue(), drawn = frameFence_->GetCompletedValue();
    size_t budget = 8 * 1024 * 1024;
    for (auto it = nativeTextures_.begin(); it != nativeTextures_.end();) {
        auto &texture = it->second;
        if (texture.status == ER_TEXTURE_PENDING && !texture.uploadValue && !texture.retired && budget) {
            budget = texture.bytes.size() >= budget ? 0 : budget - texture.bytes.size();
            if (!submitTextureUpload(texture))
                texture.status = ER_TEXTURE_FAILED;
        }
        if (texture.uploadValue && texture.uploadValue <= uploaded) {
            releaseCom(texture.upload);
            releaseCom(texture.list);
            releaseCom(texture.allocator);
            if (texture.status == ER_TEXTURE_PENDING) {
                texture.status = ER_TEXTURE_READY;
                util::nativeLog("texture-ready token=%llu size=%ux%u format=%u\n", static_cast<unsigned long long>(it->first), texture.image.width, texture.image.height,
                                texture.image.format);
            }
        }
        if (!texture.bytes.empty() && (texture.uploadValue || texture.status == ER_TEXTURE_FAILED || texture.retired)) {
            queuedTextureBytes_ -= texture.bytes.size();
            std::vector<uint8_t>().swap(texture.bytes);
        }
        if ((texture.retired || texture.status == ER_TEXTURE_FAILED) && (!texture.uploadValue || texture.uploadValue <= uploaded) && texture.drawValue <= drawn) {
            releaseCom(texture.upload);
            releaseCom(texture.list);
            releaseCom(texture.allocator);
            releaseCom(texture.texture);
            HeapDescriptorFree(texture.cpu, texture.gpu);
            texture.cpu = {};
            texture.gpu = {};
            if (texture.retired) {
                it = nativeTextures_.erase(it);
                continue;
            }
        }
        ++it;
    }
    for (auto it = retiredTextures_.begin(); it != retiredTextures_.end();) {
        if (it->drawValue <= drawn) {
            releaseCom(it->texture);
            HeapDescriptorFree(it->cpu, it->gpu);
            it = retiredTextures_.erase(it);
        } else
            ++it;
    }
}

void D3DRenderer::finishTextureFrame() {
    HRESULT hr = commandQueue_->Signal(frameFence_, ++frameValue_);
    drawingFrame_ = false;
    if (FAILED(hr)) {
        deviceLost_ = true;
        return;
    }
    allocatorFences_[currentBackBufferIndex_] = frameValue_;
    imguiFences_[(frameValue_ - 1) % imguiFences_.size()] = frameValue_;
    if (frameValue_ % 600 == 1)
        util::nativeLog("overlay-frame=%llu\n", static_cast<unsigned long long>(frameValue_));
}

void D3DRenderer::waitForFrames() {
    // Waiting is allowed only during teardown/resize, never the Present path.
    if (!device_ || FAILED(device_->GetDeviceRemovedReason()))
        return;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    for (auto [fence, value]: {std::pair{uploadFence_, uploadValue_}, std::pair{frameFence_, frameValue_}}) {
        if (!fence || !value || fence->GetCompletedValue() >= value)
            continue;
        bool waitable = event && SUCCEEDED(fence->SetEventOnCompletion(value, event));
        while (fence->GetCompletedValue() < value && SUCCEEDED(device_->GetDeviceRemovedReason())) {
            if (waitable)
                WaitForSingleObject(event, 50);
            else
                Sleep(50);
        }
    }
    if (event)
        CloseHandle(event);
}

void D3DRenderer::deferTexture(ID3D12Resource *texture, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
    if (texture)
        retiredTextures_.push_back({texture, cpu, gpu, frameValue_ + (drawingFrame_ ? 1 : 0)});
    else
        HeapDescriptorFree(cpu, gpu);
}

void D3DRenderer::releaseTextureUploads() {
    for (auto &[token, texture]: nativeTextures_) {
        releaseCom(texture.upload);
        releaseCom(texture.list);
        releaseCom(texture.allocator);
        releaseCom(texture.texture);
        HeapDescriptorFree(texture.cpu, texture.gpu);
    }
    nativeTextures_.clear();
    for (auto &texture: retiredTextures_) {
        releaseCom(texture.texture);
        HeapDescriptorFree(texture.cpu, texture.gpu);
    }
    retiredTextures_.clear();
    releaseCom(uploadQueue_);
    releaseCom(uploadFence_);
    releaseCom(frameFence_);
    uploadValue_ = frameValue_ = 0;
    queuedTextureBytes_ = 0;
    drawingFrame_ = false;
    allocatorFences_.clear();
    imguiFences_.clear();
}

} // namespace er
