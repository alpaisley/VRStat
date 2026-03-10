#pragma once

class GPUMonitor {
public:
    bool Initialize();
    void Update();
    void Shutdown();
    float GetVRAMUsedGB() const   { return vramUsedGB; }
    float GetVRAMBudgetGB() const { return vramBudgetGB; }

private:
    float vramUsedGB   = 0.0f;
    float vramBudgetGB = 0.0f;
    void* dxgiAdapter  = nullptr;
};