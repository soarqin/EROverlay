#define NOMINMAX
#include <algorithm>
#include <cstring>

#include "d3drenderer.hpp"

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
    acceptingTextures_.store(true, std::memory_order_release);
    return true;
}

uint64_t D3DRenderer::createDdsTexture(const void *bytes, uint64_t size) {
    if (!acceptingTextures_.load(std::memory_order_acquire) || !bytes || !size || size > 32ull * 1024 * 1024 ||
        queuedTextureBytes_.load(std::memory_order_relaxed) + size > 64ull * 1024 * 1024 || nativeTextureCount_.load(std::memory_order_relaxed) >= 768)
        return 0;
    util::DdsImage image;
    util::Bytes source(static_cast<const uint8_t *>(bytes), static_cast<size_t>(size));
    if (!util::parseDds(source, image))
        return 0;
    // Parse and copy on the requesting CPU thread. Publishing the prepared
    // request is short; the render thread alone owns GPU resources and fences.
    NativeTexture texture;
    texture.image = std::move(image);
    for (const auto &level: texture.image.levels) {
        texture.uploadEstimate = (texture.uploadEstimate + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) & ~size_t(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
        auto pitch = (level.rowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        texture.uploadEstimate += static_cast<size_t>(pitch) * level.rows;
    }
    texture.bytes.assign(source.begin(), source.end());
    std::lock_guard lock(textureQueueMutex_);
    if (!acceptingTextures_.load(std::memory_order_acquire) || queuedTextureBytes_.load(std::memory_order_relaxed) + size > 64ull * 1024 * 1024 ||
        nativeTextureCount_.load(std::memory_order_relaxed) >= 768)
        return 0;
    queuedTextures_.emplace(nextTextureToken_, std::move(texture));
    queuedTextureBytes_.fetch_add(source.size(), std::memory_order_relaxed);
    nativeTextureCount_.fetch_add(1, std::memory_order_relaxed);
    texturesQueued_.store(true, std::memory_order_release);
    return nextTextureToken_++;
}

ERTextureStatus D3DRenderer::pollTexture(uint64_t token, ERTextureView &view) {
    view = {};
    auto it = nativeTextures_.find(token);
    if (it == nativeTextures_.end()) {
        std::lock_guard lock(textureQueueMutex_);
        auto queued = queuedTextures_.find(token);
        return queued != queuedTextures_.end() && !queued->second.retired ? ER_TEXTURE_PENDING : ER_TEXTURE_INVALID;
    }
    if (it->second.retired)
        return ER_TEXTURE_INVALID;
    auto &texture = it->second;
    if (texture.status == ER_TEXTURE_READY) {
        view = {reinterpret_cast<void *>(texture.gpu.ptr), texture.image.width, texture.image.height};
        // Views are requested while building this frame's ImGui draw list.
        texture.drawValue = std::max(texture.drawValue, frameValue_ + (drawingFrame_ ? 1 : 0));
    }
    return texture.status;
}

uint64_t D3DRenderer::textureMemoryBytes(uint64_t token) const {
    auto found = nativeTextures_.find(token);
    return found == nativeTextures_.end() || found->second.retired ? 0 : found->second.allocationBytes;
}

void D3DRenderer::retireTexture(uint64_t token) {
    auto it = nativeTextures_.find(token);
    if (it != nativeTextures_.end()) {
        it->second.retired = true;
        if (!it->second.working) {
            it->second.working = true;
            textureWork_.push_back(token);
        }
    } else {
        std::lock_guard lock(textureQueueMutex_);
        auto queued = queuedTextures_.find(token);
        if (queued != queuedTextures_.end())
            queued->second.retired = true;
    }
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
    texture.allocationBytes = device_->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
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
    texture.uploadBytes = static_cast<size_t>(total);
    uploadingTextureBytes_ += texture.uploadBytes;
    ++uploadingTextureCount_;
    return true;
}

void D3DRenderer::processTextureUploads() {
    if (!uploadFence_ || !frameFence_ || deviceLost_)
        return;
    if (texturesQueued_.exchange(false, std::memory_order_acquire)) {
        std::lock_guard lock(textureQueueMutex_);
        for (auto &[token, texture]: queuedTextures_) {
            texture.working = true;
            textureWork_.push_back(token);
        }
        nativeTextures_.merge(queuedTextures_);
    }
    if (textureWork_.empty() && retiredTextures_.empty())
        return;
    uint64_t uploaded = uploadFence_->GetCompletedValue(), drawn = frameFence_->GetCompletedValue();
    size_t budget = 4 * 1024 * 1024;
    unsigned submissions = 0;
    size_t keep = 0;
    // Free completed staging before allocating the next batch, so completed
    // and new uploads do not briefly double the temporary memory usage.
    for (auto token: textureWork_) {
        auto &texture = nativeTextures_.at(token);
        if (!texture.uploadValue || texture.uploadValue > uploaded)
            continue;
        if (texture.uploadBytes) {
            uploadingTextureBytes_ -= texture.uploadBytes;
            --uploadingTextureCount_;
            texture.uploadBytes = 0;
        }
        releaseCom(texture.upload);
        releaseCom(texture.list);
        releaseCom(texture.allocator);
        if (texture.status == ER_TEXTURE_PENDING)
            texture.status = ER_TEXTURE_READY;
    }
    for (auto token: textureWork_) {
        auto it = nativeTextures_.find(token);
        if (it == nativeTextures_.end())
            continue;
        auto &texture = it->second;
        // A single larger atlas may exceed the byte budget, but it runs alone
        // so it cannot starve forever or accumulate with other uploads.
        if (texture.status == ER_TEXTURE_PENDING && !texture.uploadValue && !texture.retired && budget && submissions < 4 && (texture.bytes.size() <= budget || !submissions) &&
            uploadingTextureCount_ < 8 && (!uploadingTextureCount_ || uploadingTextureBytes_ + texture.uploadEstimate <= 16 * 1024 * 1024)) {
            budget = texture.bytes.size() >= budget ? 0 : budget - texture.bytes.size();
            ++submissions;
            if (!submitTextureUpload(texture))
                texture.status = ER_TEXTURE_FAILED;
        }
        if (!texture.bytes.empty() && (texture.uploadValue || texture.status == ER_TEXTURE_FAILED || texture.retired)) {
            queuedTextureBytes_.fetch_sub(texture.bytes.size(), std::memory_order_relaxed);
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
                nativeTextures_.erase(it);
                nativeTextureCount_.fetch_sub(1, std::memory_order_relaxed);
                continue;
            }
        }
        texture.working = texture.status == ER_TEXTURE_PENDING || texture.upload || texture.retired;
        if (texture.working)
            textureWork_[keep++] = token;
    }
    textureWork_.resize(keep);
    for (auto it = retiredTextures_.begin(); it != retiredTextures_.end();) {
        if (it->drawValue <= drawn) {
            releaseCom(it->texture);
            HeapDescriptorFree(it->cpu, it->gpu);
            it = retiredTextures_.erase(it);
        } else
            ++it;
    }
}

void D3DRenderer::finishTextureFrame(bool submitted) {
    drawingFrame_ = false;
    // A frame without overlay commands signals only to let pending texture
    // retirements complete; otherwise nothing is queued on the game's queue.
    if (!submitted && textureWork_.empty() && retiredTextures_.empty())
        return;
    if (FAILED(commandQueue_->Signal(frameFence_, frameValue_ + 1))) {
        deviceLost_ = true;
        return;
    }
    ++frameValue_;
    if (submitted) {
        allocatorFences_[currentBackBufferIndex_] = frameValue_;
        imguiFences_[imguiFrame_++ % imguiFences_.size()] = frameValue_;
    }
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
    acceptingTextures_.store(false, std::memory_order_release);
    for (auto &[token, texture]: nativeTextures_) {
        releaseCom(texture.upload);
        releaseCom(texture.list);
        releaseCom(texture.allocator);
        releaseCom(texture.texture);
        HeapDescriptorFree(texture.cpu, texture.gpu);
    }
    nativeTextures_.clear();
    {
        std::lock_guard lock(textureQueueMutex_);
        queuedTextures_.clear();
        texturesQueued_ = false;
        nativeTextureCount_ = 0;
        queuedTextureBytes_ = 0;
    }
    textureWork_.clear();
    uploadingTextureBytes_ = 0;
    uploadingTextureCount_ = 0;
    for (auto &texture: retiredTextures_) {
        releaseCom(texture.texture);
        HeapDescriptorFree(texture.cpu, texture.gpu);
    }
    retiredTextures_.clear();
    releaseCom(uploadQueue_);
    releaseCom(uploadFence_);
    releaseCom(frameFence_);
    uploadValue_ = frameValue_ = imguiFrame_ = 0;
    drawingFrame_ = false;
    allocatorFences_.clear();
    imguiFences_.clear();
}

} // namespace er
