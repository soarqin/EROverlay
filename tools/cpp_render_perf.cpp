// Controlled C++ comparison fixture, linked to the production renderer.
// Measures ImGui frame construction plus DX12 submission, never game code.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>
#include <dbghelp.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <malloc.h>
#include <new>
#include <vector>
#include "plugin.cpp"
#include "d3drenderer.hpp"
#include "uiscene.hpp"

using Microsoft::WRL::ComPtr;
namespace allocation_probe {
thread_local bool enabled = false;
thread_local uint64_t count = 0, bytes = 0;
void record(size_t size) { if (enabled) { ++count; bytes += size; } }
void *imgui_alloc(size_t size, void *) { record(size); return std::malloc(size); }
void imgui_free(void *pointer, void *) { std::free(pointer); }
}
// These counters cover this fixture/linked C++ new and ImGui allocations on
// the measured thread, not driver/COM/C runtime allocations or total residency.
void *operator new(size_t size) { allocation_probe::record(size); if (auto p = std::malloc(size ? size : 1)) return p; throw std::bad_alloc(); }
void *operator new[](size_t size) { return ::operator new(size); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }
void *operator new(size_t size, std::align_val_t alignment) { allocation_probe::record(size); if (auto p = _aligned_malloc(size ? size : 1, static_cast<size_t>(alignment))) return p; throw std::bad_alloc(); }
void *operator new[](size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void *p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void *p, size_t, std::align_val_t) noexcept { _aligned_free(p); }
namespace {
std::vector<std::array<ImVec2,4>> geometry;
bool shown=true;
float regionSize=320,opacity=1;
void *offscreen=nullptr;
bool drawFixture(){
    if(!shown)return false;
    ImGui::SetNextWindowPos({0,0});
    ImGui::SetNextWindowSize({1920,1080});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::Begin("fixture",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoInputs);
    auto *draw=ImGui::GetWindowDrawList();
    auto &r=*er::gD3DRenderer;
    if(r.BeginOffscreenRegion(static_cast<er::OffscreenContext*>(offscreen),40,40,regionSize,regionSize)){
        draw->PushClipRect({40,40},{40+regionSize,40+regionSize},true);
        auto old=draw->Flags;draw->Flags&=~ImDrawListFlags_AntiAliasedFill;
        for(auto &quad:geometry)draw->AddConvexPolyFilled(quad.data(),4,IM_COL32(0,255,0,204));
        draw->Flags=old;draw->PopClipRect();
        auto handle=r.EndOffscreen(static_cast<er::OffscreenContext*>(offscreen));
        if(handle){
            draw->PushTexture(reinterpret_cast<ImTextureID>(handle));
            auto first=draw->VtxBuffer.Size;
            draw->PathArcTo({40+regionSize*.5f,40+regionSize*.5f},regionSize*.5f,0,IM_PI*2,0);
            draw->PathFillConvex(IM_COL32(255,255,255,int(opacity*255)));
            ImGui::ShadeVertsLinearUV(draw,first,draw->VtxBuffer.Size,{40,40},{40+regionSize,40+regionSize},{0,0},{1,1},true);
            draw->PopTexture();
        }
    }
    ImGui::End();ImGui::PopStyleVar(2);return false;
}
int create(void*,void*,void*,void*){offscreen=er::gD3DRenderer->CreateOffscreen();return 0;}
void destroy(){er::gD3DRenderer->DestroyOffscreen(static_cast<er::OffscreenContext*>(offscreen));offscreen=nullptr;}
PluginExports exports{nullptr,nullptr,nullptr,create,destroy,drawFixture};
void makeGeometry(float size,int cells,bool rotated){
    geometry.clear();geometry.reserve(cells*cells);
    for(int row=0;row<cells;++row)for(int col=0;col<cells;++col){
        std::array<ImVec2,4> quad;int j=0;
        for(auto [dx,dy]:std::array<std::pair<float,float>,4>{{{0.f,0.f},{1.f,0.f},{1.f,1.f},{0.f,1.f}}}){
            float x=40+(col+dx)*size/cells,y=40+(row+dy)*size/cells;
            if(rotated){float c=40+size*.5f,tx=x-c,ty=y-c;x=c+(tx-ty)*.7071067812f;y=c+(tx+ty)*.7071067812f;}
            quad[j++]={x,y};
        }geometry.push_back(quad);
    }
}
double quantile(std::vector<double> &v,int p){std::sort(v.begin(),v.end());return v[(v.size()-1)*p/100];}
}

namespace er {
struct NativeTextureVerifier {
    static void drain(D3DRenderer &r){r.waitForFrames();}
    static int run(int samples){
        WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"RustCppPerfFixture";
        RegisterClassExW(&cls);
        auto hwnd=CreateWindowW(cls.lpszClassName,L"EROverlay comparison fixture",WS_OVERLAPPEDWINDOW,0,0,1920,1080,nullptr,nullptr,cls.hInstance,nullptr);
        if(!hwnd)return 1;
        ComPtr<IDXGIFactory6> factory;ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
        if(FAILED(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)))||FAILED(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)))||FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))return 2;
        DXGI_ADAPTER_DESC1 ad{};adapter->GetDesc1(&ad);std::printf("{\"kind\":\"environment\",\"implementation\":\"cpp_imgui_production\",\"adapter\":\"%ls\",\"width\":1920,\"height\":1080,\"samples\":%d,\"gpu_timing\":\"not_instrumented\",\"scope\":\"ImGui frame plus production renderer, synthetic quads; no native game/Minimap data/FFI\"}\n",ad.Description,samples);
        D3D12_COMMAND_QUEUE_DESC qd{};qd.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        if(FAILED(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue))))return 3;
        DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=1920;sd.Height=1080;sd.BufferCount=2;sd.Format=DXGI_FORMAT_R10G10B10A2_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.SampleDesc.Count=1;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> swap1;ComPtr<IDXGISwapChain3> swap;
        if(FAILED(factory->CreateSwapChainForHwnd(queue.Get(),hwnd,&sd,nullptr,nullptr,&swap1))||FAILED(swap1.As(&swap)))return 4;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;ComPtr<ID3D12DescriptorHeap> heap;
        if(FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))))return 5;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=1920;td.Height=1080;td.DepthOrArraySize=1;td.MipLevels=1;td.Format=sd.Format;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;ComPtr<ID3D12Resource> target;
        if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target))))return 6;
        auto rtv=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(target.Get(),nullptr,rtv);
        gD3DRenderer=std::make_unique<D3DRenderer>();auto &r=*gD3DRenderer;
        ImGui::SetAllocatorFunctions(allocation_probe::imgui_alloc,allocation_probe::imgui_free);
        ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().Fonts->AddFontDefault();
        // Bind the controlled fixture window directly; never find or touch a game window.
        r.gameWindow_=hwnd;if(!ImGui_ImplWin32_Init(hwnd))return 7;
        plugins.push_back({0,&exports,true,RenderLayer::BelowUI});
        UiSceneFrame frame{device.Get(),queue.Get(),swap.Get(),rtv,sd.Format,{0,0,1920,1080,0,1},RenderLayer::BelowUI,0};
        for(auto scenario:{"hidden","stable_64_quads","large_rotated_transparent_1024_quads","resize_region"}){
            const bool large=std::strcmp(scenario,"large_rotated_transparent_1024_quads")==0;
            shown=std::strcmp(scenario,"hidden")!=0;regionSize=large?1000.f:320.f;opacity=large?.4f:1.f;makeGeometry(regionSize,large?32:8,large);
            std::vector<double> cpu;cpu.reserve(samples);uint64_t submitted=0;allocation_probe::count=allocation_probe::bytes=0;
            auto wall=std::chrono::steady_clock::now();
            for(int i=0;i<samples+32;++i){
                if(std::strcmp(scenario,"resize_region")==0)regionSize=320.f+(i%32)*4.f;
                ++frame.sequence;frame.layer=RenderLayer::BelowUI;auto before=r.frameValue_;
                if(i==32)wall=std::chrono::steady_clock::now();
                allocation_probe::enabled=i>=32;
                auto begin=std::chrono::steady_clock::now();r.overlayScene(frame);frame.layer=RenderLayer::AboveUI;r.overlayScene(frame);auto end=std::chrono::steady_clock::now();
                allocation_probe::enabled=false;
                if(i>=32){cpu.push_back(std::chrono::duration<double,std::micro>(end-begin).count());submitted+=r.frameValue_-before;}
                drain(r);
            }
            auto desc=r.sceneUnderlay_.texture?r.sceneUnderlay_.texture->GetDesc():D3D12_RESOURCE_DESC{};
            auto underlay=r.sceneUnderlay_.texture?device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes:0;
            auto wallSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();
            std::printf("{\"kind\":\"scenario\",\"scenario\":\"%s\",\"cpu_p50_us\":%.3f,\"cpu_p95_us\":%.3f,\"cpu_p99_us\":%.3f,\"submissions_per_frame\":%.3f,\"underlay_committed_bytes\":%llu,\"allocator_count\":%u,\"cpp_new_and_imgui_allocations\":%llu,\"cpp_new_and_imgui_bytes\":%llu,\"sampled_wall_seconds\":%.6f}\n",scenario,quantile(cpu,50),quantile(cpu,95),quantile(cpu,99),double(submitted)/samples,underlay,r.frameCount_,allocation_probe::count,allocation_probe::bytes,wallSeconds);
        }
        drain(r);r.unhook();gD3DRenderer.reset();plugins.clear();DestroyWindow(hwnd);UnregisterClassW(cls.lpszClassName,cls.hInstance);return 0;
    }
};
}
int main(int argc,char**argv){
    setvbuf(stdout,nullptr,_IONBF,0);
    AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS *e)->LONG{
        if(e->ExceptionRecord->ExceptionCode!=0xc0000005)return EXCEPTION_CONTINUE_SEARCH;
        auto process=GetCurrentProcess();SymInitialize(process,nullptr,TRUE);void *frames[20]{};auto count=CaptureStackBackTrace(0,20,frames,nullptr);
        for(unsigned i=0;i<count;++i){char storage[sizeof(SYMBOL_INFO)+512]{};auto *symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(*symbol);symbol->MaxNameLen=511;DWORD64 displacement=0;if(SymFromAddr(process,reinterpret_cast<DWORD64>(frames[i]),&displacement,symbol))std::printf("exception frame %s+0x%llx\n",symbol->Name,displacement);}
        return EXCEPTION_CONTINUE_SEARCH;
    });
    int samples=argc>1?std::max(10,std::atoi(argv[1])):600;return er::NativeTextureVerifier::run(samples);
}
