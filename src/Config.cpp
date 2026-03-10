#include "Config.h"
#include <fstream>
#include <sstream>

void Config::SetDefaults() {
    order.clear();
    for (int i = 0; i < METRIC_COUNT; ++i) {
        order.push_back((MetricID)i);
        enabled[i] = true;
    }
    enabled[METRIC_CPU] = false;
}

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

static MetricID NameToMetric(const std::string& name) {
    for (int i = 0; i < METRIC_COUNT; ++i)
        if (name == MetricNames[i]) return (MetricID)i;
    return METRIC_COUNT;
}

bool Config::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    SetDefaults();
    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key   = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));

        bool matched = false;
        for (int i = 0; i < METRIC_COUNT; ++i) {
            if (key == MetricNames[i]) {
                enabled[i] = (value == "1");
                matched = true;
                break;
            }
        }
        if (matched) continue;

        if (key == "ORDER") {
            std::vector<MetricID> newOrder;
            std::stringstream ss(value);
            std::string token;
            while (std::getline(ss, token, ',')) {
                MetricID m = NameToMetric(Trim(token));
                if (m != METRIC_COUNT) newOrder.push_back(m);
            }
            if (!newOrder.empty()) order = newOrder;
        }
    }
    return true;
}

bool Config::Save(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "# VRStat configuration\n";
    for (int i = 0; i < METRIC_COUNT; ++i)
        f << MetricNames[i] << "=" << (enabled[i] ? "1" : "0") << "\n";

    f << "\nORDER=";
    for (size_t i = 0; i < order.size(); ++i) {
        if (i > 0) f << ",";
        f << MetricNames[order[i]];
    }
    f << "\n";
    return true;
}

int Config::EnabledCount() const {
    int count = 0;
    for (size_t i = 0; i < order.size(); ++i)
        if (enabled[order[i]]) ++count;
    return count;
}