#pragma once

class PDHMonitor {
public:
    bool Initialize();
    void Update();
    void Shutdown();
    float GetCPUPercent() const { return cpuPercent; }

private:
    float cpuPercent  = 0.0f;
    int   warmupTicks = 0;
    void* hQuery      = nullptr;
    void* hCPU        = nullptr;
};