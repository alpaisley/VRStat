#pragma once
#include <string>
#include <vector>

enum MetricID {
    METRIC_NIC        = 0,
    METRIC_UPLOAD     = 1,
    METRIC_FPS        = 2,
    METRIC_FRAMETIME  = 3,
    METRIC_VRAM       = 4,
    METRIC_CPU        = 5,
    METRIC_GPU_FT     = 6,  // GPU frame time ms
    METRIC_CPU_TEMP   = 7,  // CPU temp C (WMI)
    METRIC_GPU_TEMP   = 8,  // GPU temp C (NVML)
    METRIC_COUNT      = 9
};

static const char* MetricNames[METRIC_COUNT] = {
    "NIC", "UPLOAD", "FPS", "FRAMETIME", "VRAM", "CPU", "GPU_FT", "CPU_TEMP", "GPU_TEMP"
};

static const char* MetricLabels[METRIC_COUNT] = {
    "NIC name",
    "Upload Mbps",
    "FPS",
    "Frame time ms",
    "VRAM used %",
    "CPU usage %",
    "GPU frame time ms",
    "CPU temp C",
    "GPU temp C"
};

enum LayoutMode {
    LAYOUT_VERTICAL   = 0,  // single column (original)
    LAYOUT_HORIZONTAL = 1   // two columns, metrics paired left/right
};

struct Config {
    std::vector<MetricID> order;
    bool       enabled[METRIC_COUNT];
    LayoutMode layout = LAYOUT_VERTICAL;

    void SetDefaults();
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    int  EnabledCount() const;
};