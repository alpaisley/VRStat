#include "GPUMonitor.h"
#include <windows.h>
#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")

static IDXGIAdapter3* gAdapter = nullptr;

bool GPUMonitor::Initialize() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return false;

    IDXGIAdapter1* bestAdapter = nullptr;
    SIZE_T         bestVRAM    = 0;
    IDXGIAdapter1* adapter     = nullptr;

    for (UINT i = 0;
         factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++i) {
        DXGI_ADAPTER_DESC1 desc = {};
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            continue;
        }

        if (desc.DedicatedVideoMemory > bestVRAM) {
            if (bestAdapter) bestAdapter->Release();
            bestAdapter = adapter;
            bestVRAM    = desc.DedicatedVideoMemory;
        } else {
            adapter->Release();
        }
    }

    factory->Release();

    if (!bestAdapter) return false;

    IDXGIAdapter3* adapter3 = nullptr;
    HRESULT hr = bestAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&adapter3);
    bestAdapter->Release();

    if (FAILED(hr)) return false;

    gAdapter    = adapter3;
    dxgiAdapter = adapter3;

    Update();
    return true;
}

void GPUMonitor::Update() {
    if (!gAdapter) return;

    DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
    if (SUCCEEDED(gAdapter->QueryVideoMemoryInfo(
            0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        // Values are in bytes as UINT64 - divide by 1024^3 for GB
        vramUsedGB   = (float)((double)info.CurrentUsage / (1024.0 * 1024.0 * 1024.0));
        vramBudgetGB = (float)((double)info.Budget        / (1024.0 * 1024.0 * 1024.0));
    }
}

void GPUMonitor::Shutdown() {
    if (gAdapter) {
        gAdapter->Release();
        gAdapter    = nullptr;
        dxgiAdapter = nullptr;
    }
}