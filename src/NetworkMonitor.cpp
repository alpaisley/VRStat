// winsock2.h MUST be included before windows.h to avoid type conflicts
#include <winsock2.h>
#include <ws2tcpip.h>
#include "NetworkMonitor.h"
#include <windows.h>
#include <iphlpapi.h>
#include <chrono>
#include <cstring>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

static std::chrono::steady_clock::time_point lastTime;

void NetworkMonitor::Initialize() {
    lastTime = std::chrono::steady_clock::now();
    BuildList();
}

void NetworkMonitor::Refresh() {
    BuildList();
}

void NetworkMonitor::BuildList() {
    nicList.clear();
    nicNames.clear();

    // GetAdaptersAddresses returns one entry per physical adapter
    // and provides FriendlyName (the alias e.g. "Ethernet", "Wi-Fi")
    ULONG bufLen = 20000;
    PIP_ADAPTER_ADDRESSES buf = nullptr;
    ULONG ret = ERROR_BUFFER_OVERFLOW;

    for (int attempt = 0; attempt < 3 && ret == ERROR_BUFFER_OVERFLOW; ++attempt) {
        free(buf);
        buf = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
        if (!buf) return;
        ret = GetAdaptersAddresses(
            AF_UNSPEC,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, buf, &bufLen);
    }

    if (ret != NO_ERROR) {
        free(buf);
        return;
    }

    for (PIP_ADAPTER_ADDRESSES a = buf; a != nullptr; a = a->Next) {
        // Skip software/loopback/tunnel adapters
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (a->IfType == IF_TYPE_TUNNEL)            continue;

        // Only keep Ethernet, WiFi, and PPP
        if (a->IfType != IF_TYPE_ETHERNET_CSMACD &&
            a->IfType != IF_TYPE_IEEE80211 &&
            a->IfType != IF_TYPE_PPP) continue;

        // Skip disconnected adapters
        if (a->OperStatus != IfOperStatusUp) continue;

        // Convert wide FriendlyName to UTF-8
        std::string friendly;
        if (a->FriendlyName) {
            int len = WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1,
                                          nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                friendly.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1,
                                    &friendly[0], len, nullptr, nullptr);
            }
        }

        // Convert Description as fallback
        std::string desc;
        if (a->Description) {
            int len = WideCharToMultiByte(CP_UTF8, 0, a->Description, -1,
                                          nullptr, 0, nullptr, nullptr);
            if (len > 0) {
                desc.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, a->Description, -1,
                                    &desc[0], len, nullptr, nullptr);
            }
        }

        if (friendly.empty()) friendly = desc;

        NICEntry entry;
        entry.friendlyName = friendly;
        entry.description  = desc;
        entry.ifIndex      = (unsigned long)a->IfIndex;
        nicList.push_back(entry);
        nicNames.push_back(friendly);
    }

    free(buf);

    if (selectedNICIndex >= nicList.size() && !nicList.empty())
        selectedNICIndex = 0;
}

void NetworkMonitor::Update() {
    if (selectedNICIndex >= nicList.size()) return;

    DWORD size = 0;
    GetIfTable(nullptr, &size, FALSE);
    PMIB_IFTABLE ifTable = (PMIB_IFTABLE)malloc(size);
    if (!ifTable) return;

    if (GetIfTable(ifTable, &size, FALSE) == NO_ERROR) {
        unsigned long targetIndex = nicList[selectedNICIndex].ifIndex;
        for (DWORD i = 0; i < ifTable->dwNumEntries; ++i) {
            if ((unsigned long)ifTable->table[i].dwIndex == targetIndex) {
                MIB_IFROW& row = ifTable->table[i];
                auto now = std::chrono::steady_clock::now();
                double seconds = std::chrono::duration<double>(now - lastTime).count();
                if (seconds > 0 && (lastRx != 0 || lastTx != 0)) {
                    rxMbps = ((row.dwInOctets - lastRx) * 8.0) / (seconds * 1e6);
                    txMbps = ((row.dwOutOctets - lastTx) * 8.0) / (seconds * 1e6);
                }
                lastRx = row.dwInOctets;
                lastTx = row.dwOutOctets;
                lastTime = now;
                break;
            }
        }
    }
    free(ifTable);
}

std::string NetworkMonitor::GetAdapterName() {
    if (selectedNICIndex < nicList.size())
        return nicList[selectedNICIndex].friendlyName;
    return "Unknown";
}

double NetworkMonitor::GetRxMbps() { return rxMbps; }
double NetworkMonitor::GetTxMbps() { return txMbps; }

void NetworkMonitor::SelectNIC(size_t index) {
    if (index < nicList.size()) {
        selectedNICIndex = index;
        lastRx = 0;
        lastTx = 0;
    }
}