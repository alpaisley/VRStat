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
    METRIC_COUNT      = 6
};

static const char* MetricNames[METRIC_COUNT] = {
    "NIC", "UPLOAD", "FPS", "FRAMETIME", "VRAM", "CPU"
};

static const char* MetricLabels[METRIC_COUNT] = {
    "NIC name",
    "Upload Mbps",
    "FPS",
    "Frame time ms",
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