#pragma once
#include <string>
#include <vector>

enum MetricID {
    METRIC_NIC        = 0,
    METRIC_UPLOAD     = 1,
    METRIC_FPS        = 2,
    METRIC_FRAMETIME  = 3,
    METRIC_CPU_FT     = 4,
    METRIC_GPU_FT     = 5,
    METRIC_VRAM       = 6,
    METRIC_CPU        = 7,
    METRIC_COUNT      = 8
};

static const char* MetricNames[METRIC_COUNT] = {
    "NIC", "UPLOAD", "FPS", "FRAMETIME", "CPU_FT", "GPU_FT", "VRAM", "CPU"
};

static const char* MetricLabels[METRIC_COUNT] = {
    "NIC name",
    "Upload Mbps",
    "FPS",
    "Frame time ms",
    "CPU frame time ms",
    "GPU frame time ms",
    "VRAM used",
    "CPU usage %"
};

struct Config {
    std::vector<MetricID> order;
    bool enabled[METRIC_COUNT];

    void SetDefaults();
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    int  EnabledCount() const;
};