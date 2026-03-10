#pragma once
#include <string>
#include <vector>

struct NICEntry {
    std::string   friendlyName;
    std::string   description;
    unsigned long ifIndex;
};

class NetworkMonitor {
public:
    void Initialize();
    void Update();
    void Refresh();
    std::string GetAdapterName();
    double GetRxMbps();
    double GetTxMbps();
    const std::vector<std::string>& GetNICNames() const { return nicNames; }
    void SelectNIC(size_t index);

private:
    void BuildList();
    unsigned long lastRx = 0;
    unsigned long lastTx = 0;
    double rxMbps = 0.0;
    double txMbps = 0.0;
    std::vector<NICEntry>    nicList;
    std::vector<std::string> nicNames;
    size_t selectedNICIndex = 0;
};