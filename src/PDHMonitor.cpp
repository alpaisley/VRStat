#include "PDHMonitor.h"
#include <windows.h>
#include <pdh.h>
#pragma comment(lib, "pdh.lib")

static const wchar_t* kCPUPath = L"\\Processor(_Total)\\% Processor Time";

bool PDHMonitor::Initialize() {
    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, (PDH_HQUERY*)&hQuery);
    if (status != ERROR_SUCCESS) return false;

    status = PdhAddEnglishCounterW((PDH_HQUERY)hQuery, kCPUPath, 0,
                                   (PDH_HCOUNTER*)&hCPU);
    if (status != ERROR_SUCCESS) {
        PdhCloseQuery((PDH_HQUERY)hQuery);
        hQuery = nullptr;
        return false;
    }

    PdhCollectQueryData((PDH_HQUERY)hQuery);
    warmupTicks = 0;
    return true;
}

void PDHMonitor::Update() {
    if (!hQuery) return;

    PDH_STATUS status = PdhCollectQueryData((PDH_HQUERY)hQuery);
    if (status != ERROR_SUCCESS) return;

    if (warmupTicks < 1) { ++warmupTicks; return; }

    PDH_FMT_COUNTERVALUE val = {};
    if (PdhGetFormattedCounterValue((PDH_HCOUNTER)hCPU,
            PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS)
        cpuPercent = (float)val.doubleValue;
}

void PDHMonitor::Shutdown() {
    if (hQuery) {
        PdhCloseQuery((PDH_HQUERY)hQuery);
        hQuery = nullptr;
        hCPU   = nullptr;
    }
}