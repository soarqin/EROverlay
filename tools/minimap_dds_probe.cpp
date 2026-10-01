// Standalone sampling of CPU DDS bytes; no game process or texture handles.
// See minimap-probes.md.
#define WIN32_LEAN_AND_MEAN
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

bool check(HRESULT result, const char *action) {
    if (SUCCEEDED(result))
        return true;
    fprintf(stderr, "%s HRESULT=%08lx\n", action, result);
    return false;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

int wmain(int argc, wchar_t **argv) {
    if (argc != 3 && argc != 4)
        return 1;
    FILE *input = nullptr;
    _wfopen_s(&input, argv[1], L"rb");
    if (!input)
        return 2;
    fseek(input, 0, SEEK_END);
    size_t size = static_cast<size_t>(ftell(input));
    fseek(input, 0, SEEK_SET);
    std::vector<unsigned char> tpf(size);
    if (fread(tpf.data(), 1, size, input) != size) {
        fclose(input);
        return 3;
    }
    fclose(input);
    if (size < 36 || memcmp(tpf.data(), "TPF", 3) != 0 || tpf[3] != 0 || tpf[12] != 0 || tpf[13] != 3 || tpf[15] & 1)
        return 4;
    uint32_t dataOffset = 0, dataSize = 0;
    uint32_t textureCount = 0;
    memcpy(&textureCount, tpf.data() + 8, 4);
    size_t cursor = 16;
    bool found = false;
    for (uint32_t index = 0; index < textureCount && cursor + 20 <= size; ++index) {
        uint32_t nameOffset = 0, extraCount = 0;
        memcpy(&nameOffset, tpf.data() + cursor + 12, 4);
        memcpy(&extraCount, tpf.data() + cursor + 16, 4);
        bool matches = argc == 3 && index == 0;
        if (argc == 4 && nameOffset < size && !(nameOffset & 1)) {
            size_t length = 0;
            const auto name = reinterpret_cast<const wchar_t *>(tpf.data() + nameOffset);
            while (nameOffset + (length + 1) * 2 <= size && name[length])
                ++length;
            matches = nameOffset + (length + 1) * 2 <= size && length == wcslen(argv[3]) && wmemcmp(name, argv[3], length) == 0;
        }
        if (matches) {
            memcpy(&dataOffset, tpf.data() + cursor, 4);
            memcpy(&dataSize, tpf.data() + cursor + 4, 4);
            found = true;
            break;
        }
        cursor += 20;
        for (uint32_t extra = 0; extra < extraCount && cursor + 8 <= size; ++extra) {
            uint32_t extraSize = 0;
            memcpy(&extraSize, tpf.data() + cursor + 4, 4);
            cursor += extraSize + 8;
        }
    }
    if (!found)
        return 5;
    if (dataOffset > size || dataSize > size - dataOffset || dataSize < 148)
        return 5;
    auto dds = tpf.data() + dataOffset;
    uint32_t width = 0, height = 0, format = 0;
    memcpy(&height, dds + 12, 4);
    memcpy(&width, dds + 16, 4);
    memcpy(&format, dds + 128, 4);
    if (memcmp(dds, "DDS ", 4) || memcmp(dds + 84, "DX10", 4) || !width || !height || width > 4096 || height > 4096 || width % 16 || height % 16 || format != DXGI_FORMAT_BC7_UNORM)
        return 6;

    ComPtr<IDXGIFactory6> factory;
    if (!check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "factory"))
        return 7;
    ComPtr<IDXGIAdapter1> adapter;
    if (!check(factory->EnumAdapters1(0, &adapter), "adapter"))
        return 8;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    adapter->GetDesc1(&adapterDesc);
    wprintf(L"adapter=%s input=%ux%u format=%u\n", adapterDesc.Description, width, height, format);
    ComPtr<ID3D12Device> device;
    if (!check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "device"))
        return 9;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{DXGI_FORMAT_BC7_UNORM};
    if (!check(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)), "BC7 support"))
        return 10;
    if (!(support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) || !(support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD))
        return 11;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    if (!check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "queue") ||
        !check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "allocator") ||
        !check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "list"))
        return 12;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = static_cast<DXGI_FORMAT>(format);
    textureDesc.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> texture;
    if (!check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)), "BC7 texture"))
        return 13;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowBytes = 0, total = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
    if (rows * rowBytes != dataSize - 148)
        return 14;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = total;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> upload;
    if (!check(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)), "upload"))
        return 15;
    void *mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    if (!check(upload->Map(0, &noRead, &mapped), "upload map"))
        return 16;
    for (UINT row = 0; row < rows; ++row)
        memcpy(static_cast<unsigned char *>(mapped) + footprint.Offset + row * footprint.Footprint.RowPitch, dds + 148 + row * rowBytes, static_cast<size_t>(rowBytes));
    upload->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    destination.pResource = texture.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    auto barrier = transition(texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    list->ResourceBarrier(1, &barrier);

    D3D12_RESOURCE_DESC outputDesc = textureDesc;
    outputDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    outputDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> output;
    if (!check(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &outputDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output)),
               "RGBA output"))
        return 17;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 2;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    if (!check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)), "SRV heap"))
        return 18;
    auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
    auto increment = device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = static_cast<DXGI_FORMAT>(format);
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(texture.Get(), &srv, cpu);
    cpu.ptr += increment;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = outputDesc.Format;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(output.Get(), nullptr, &uav, cpu);
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    D3D12_ROOT_PARAMETER parameters[2]{};
    for (int index = 0; index < 2; ++index) {
        ranges[index].NumDescriptors = 1;
        ranges[index].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[index].DescriptorTable.NumDescriptorRanges = 1;
        parameters[index].DescriptorTable.pDescriptorRanges = &ranges[index];
    }
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = parameters;
    ComPtr<ID3DBlob> rootBlob, error;
    if (!check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &error), "root serialize"))
        return 19;
    ComPtr<ID3D12RootSignature> root;
    if (!check(device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&root)), "root"))
        return 20;
    const char shader[] = "Texture2D<float4> image:register(t0); RWTexture2D<float4> result:register(u0); [numthreads(16,16,1)] void main(uint3 p:SV_DispatchThreadID) { "
                          "result[p.xy]=image.Load(int3(p.xy,0)); }";
    ComPtr<ID3DBlob> shaderBlob;
    if (!check(D3DCompile(shader, sizeof(shader) - 1, "byte-probe", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shaderBlob, &error), "shader"))
        return 21;
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = root.Get();
    psoDesc.CS = {shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pso;
    if (!check(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso)), "pipeline"))
        return 22;
    ID3D12DescriptorHeap *heaps[] = {heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(root.Get());
    list->SetPipelineState(pso.Get());
    list->SetComputeRootDescriptorTable(0, gpu);
    gpu.ptr += increment;
    list->SetComputeRootDescriptorTable(1, gpu);
    list->Dispatch(width / 16, height / 16, 1);
    barrier = transition(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->ResourceBarrier(1, &barrier);
    device->GetCopyableFootprints(&outputDesc, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
    bufferDesc.Width = total;
    D3D12_HEAP_PROPERTIES readHeap{};
    readHeap.Type = D3D12_HEAP_TYPE_READBACK;
    ComPtr<ID3D12Resource> readback;
    if (!check(device->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "readback"))
        return 23;
    source.pResource = output.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    if (!check(list->Close(), "close"))
        return 24;
    ComPtr<ID3D12Fence> fence;
    if (!check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "fence"))
        return 25;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event)
        return 26;
    ID3D12CommandList *lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    if (!check(queue->Signal(fence.Get(), 1), "signal") || !check(fence->SetEventOnCompletion(1, event), "completion") || WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
        return 27;
    CloseHandle(event);
    if (!check(readback->Map(0, nullptr, &mapped), "readback map"))
        return 28;
    std::vector<unsigned char> pixels(width * height * 4);
    for (UINT row = 0; row < height; ++row) {
        auto src = static_cast<unsigned char *>(mapped) + footprint.Offset + row * footprint.Footprint.RowPitch;
        auto dst = pixels.data() + row * width * 4;
        for (UINT column = 0; column < width; ++column) {
            dst[column * 4] = src[column * 4 + 2];
            dst[column * 4 + 1] = src[column * 4 + 1];
            dst[column * 4 + 2] = src[column * 4];
            dst[column * 4 + 3] = src[column * 4 + 3];
        }
    }
    readback->Unmap(0, &noRead);
    BITMAPFILEHEADER bmp{};
    bmp.bfType = 0x4D42;
    bmp.bfOffBits = sizeof(bmp) + sizeof(BITMAPINFOHEADER);
    bmp.bfSize = bmp.bfOffBits + static_cast<DWORD>(pixels.size());
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = -static_cast<LONG>(height);
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biSizeImage = static_cast<DWORD>(pixels.size());
    FILE *out = nullptr;
    _wfopen_s(&out, argv[2], L"wb");
    if (!out)
        return 29;
    fwrite(&bmp, sizeof(bmp), 1, out);
    fwrite(&info, sizeof(info), 1, out);
    fwrite(pixels.data(), 1, pixels.size(), out);
    fclose(out);
    printf("own BC7 resource + own SRV + own DIRECT queue sampled successfully; pixels=%zu\n", pixels.size());
    return 0;
}
