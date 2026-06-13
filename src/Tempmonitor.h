#pragma once
#include <string>

class TempMonitor {
public:
    bool Initialize(const std::string& pluginDir);
    void Update();
    void Shutdown();

    float GetCPUTempC()  const { return cpuTempC; }
    float GetGPUTempC()  const { return gpuTempC; }
    bool  HasCPUTemp()   const { return cpuAvailable; }
    bool  HasGPUTemp()   const { return gpuAvailable; }
    const std::string& GetLog() const { return initLog; }
    void ClearLog() { initLog.clear(); initLog.shrink_to_fit(); }

private:
    std::string pluginDir;
    std::string initLog;   // accumulates init diagnostics, read by Plugin.cpp via GetLog()
    float cpuTempC      = 0.0f;
    float gpuTempC      = 0.0f;
    bool  cpuAvailable  = false;
    bool  gpuAvailable  = false;

    // HWiNFO64 shared memory (primary for both CPU and GPU temp)
    void* hHWiNFOMap    = nullptr;  // HANDLE from OpenFileMapping
    void* pHWiNFOData   = nullptr;  // MapViewOfFile pointer
    bool  hwInfoAvailable = false;
    int   cpuSensorIdx  = -1;       // index into HWiNFO sensor array for CPU temp
    int   cpuReadingIdx = -1;       // index into HWiNFO reading array
    int   gpuSensorIdx  = -1;
    int   gpuReadingIdx = -1;

    // NVML fallback for GPU temp (NVIDIA only)
    void* hNvml         = nullptr;
    void* nvmlDevice    = nullptr;
    bool  nvmlAvailable = false;

    // PDH/WMI fallback for CPU temp
    void* hPdhQuery     = nullptr;
    void* hPdhCounter   = nullptr;
    bool  pdhAvailable  = false;
    int   pdhWarmup     = 0;
    void* pSvcCPU       = nullptr;

    void InitHWiNFO();
    void InitNVML();
    void InitCPUFallback();
    void UpdateFromHWiNFO();
    void UpdateCPUFallback();
    void UpdateGPUFallback();
};