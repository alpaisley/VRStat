#include "TempMonitor.h"
#include <windows.h>
#include <pdh.h>
#include <wbemidl.h>
#include <comdef.h>
#include <vector>
#include <string>
#include <cstring>
#include <cctype>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// ---- HWiNFO64 shared memory --------------------------------------------------
// Corrected layout per reverse-engineering (namazso/hwinfosharedmem.h)
// Key difference from old SDK docs: last_update is int64_t (8 bytes) not DWORD
#define HWINFO_SHARED_MEM_NAME "Global\\HWiNFO_SENS_SM2"

#pragma pack(push,1)
struct HWiNFO_HDR {
    uint32_t magic;                   // 0x00  "HWiS"
    uint32_t version;                 // 0x04
    uint32_t version2;                // 0x08
    int64_t  last_update;             // 0x0C  unix timestamp (8 bytes!)
    uint32_t sensor_section_offset;   // 0x14
    uint32_t sensor_element_size;     // 0x18
    uint32_t sensor_element_count;    // 0x1C
    uint32_t entry_section_offset;    // 0x20
    uint32_t entry_element_size;      // 0x24
    uint32_t entry_element_count;     // 0x28
};

struct HWiNFO_SENSOR {
    uint32_t id;                      // 0x00
    uint32_t instance;                // 0x04
    char     name_original[128];      // 0x08
    char     name_user[128];          // 0x88  -> total = 264 bytes
};

enum SensorType : uint32_t {
    ST_None=0, ST_Temp=1, ST_Volt=2, ST_Fan=3,
    ST_Current=4, ST_Power=5, ST_Clock=6, ST_Usage=7, ST_Other=8
};

struct HWiNFO_ENTRY {
    SensorType type;                  // 0x000
    uint32_t   sensor_index;          // 0x004
    uint32_t   id;                    // 0x008
    char       name_original[128];    // 0x00C
    char       name_user[128];        // 0x08C
    char       unit[16];              // 0x10C
    double     value;                 // 0x11C
    double     value_min;             // 0x124
    double     value_max;             // 0x12C
    double     value_avg;             // 0x134
    // newer HWiNFO versions extend this with extra fields — we use stride, not sizeof
};
#pragma pack(pop)

// ---- NVML --------------------------------------------------------------------
typedef int nvmlReturn_t; typedef void* nvmlDevice_t;
#define NVML_SUCCESS 0
#define NVML_TEMPERATURE_GPU 0
typedef nvmlReturn_t(*PFN_nvmlInit)();
typedef nvmlReturn_t(*PFN_nvmlShutdown)();
typedef nvmlReturn_t(*PFN_nvmlDeviceGetHandleByIndex)(unsigned int, nvmlDevice_t*);
typedef nvmlReturn_t(*PFN_nvmlDeviceGetTemperature)(nvmlDevice_t, unsigned int, unsigned int*);
static PFN_nvmlInit                   pfnNvmlInit                   = nullptr;
static PFN_nvmlShutdown               pfnNvmlShutdown               = nullptr;
static PFN_nvmlDeviceGetHandleByIndex pfnNvmlDeviceGetHandleByIndex = nullptr;
static PFN_nvmlDeviceGetTemperature   pfnNvmlDeviceGetTemperature   = nullptr;

// ---- Helpers -----------------------------------------------------------------
static std::string Lower(const std::string& s) {
    std::string r=s; for(char&c:r) c=(char)tolower((unsigned char)c); return r;
}
static bool Has(const std::string& s, const char* n) { return s.find(n)!=std::string::npos; }

static const HWiNFO_SENSOR* GetSensor(const BYTE* base, const HWiNFO_HDR* h, uint32_t idx) {
    return reinterpret_cast<const HWiNFO_SENSOR*>(
        base + h->sensor_section_offset + idx * h->sensor_element_size);
}
static const HWiNFO_ENTRY* GetEntry(const BYTE* base, const HWiNFO_HDR* h, uint32_t idx) {
    return reinterpret_cast<const HWiNFO_ENTRY*>(
        base + h->entry_section_offset + idx * h->entry_element_size);
}

static IWbemServices* ConnectWMI(const wchar_t* ns) {
    IWbemLocator* pL=nullptr; IWbemServices* pS=nullptr;
    if(FAILED(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,
        IID_IWbemLocator,(void**)&pL))) return nullptr;
    HRESULT hr=pL->ConnectServer(_bstr_t(ns),nullptr,nullptr,nullptr,0,nullptr,nullptr,&pS);
    pL->Release(); if(FAILED(hr)) return nullptr;
    CoSetProxyBlanket(pS,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,
        RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE);
    return pS;
}

// ---- TempMonitor -------------------------------------------------------------
bool TempMonitor::Initialize(const std::string& dir) {
    pluginDir=dir; initLog.clear();
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    CoInitializeSecurity(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);
    InitHWiNFO(); InitNVML();
    if(!cpuAvailable) InitCPUFallback();
    Update(); return true;
}

void TempMonitor::InitHWiNFO() {
    initLog += "VRStat TempMonitor:\n";
    hHWiNFOMap = OpenFileMappingA(FILE_MAP_READ,FALSE,HWINFO_SHARED_MEM_NAME);
    if(!hHWiNFOMap) {
        char b[64]; sprintf_s(b,"  HWiNFO: not running (err=%lu)\n",GetLastError());
        initLog+=b; return;
    }
    pHWiNFOData = MapViewOfFile(hHWiNFOMap,FILE_MAP_READ,0,0,0);
    if(!pHWiNFOData) {
        char b[64]; sprintf_s(b,"  HWiNFO: map failed (err=%lu)\n",GetLastError());
        initLog+=b; CloseHandle(hHWiNFOMap); hHWiNFOMap=nullptr; return;
    }

    const HWiNFO_HDR* h  = reinterpret_cast<const HWiNFO_HDR*>(pHWiNFOData);
    const BYTE*       base = reinterpret_cast<const BYTE*>(pHWiNFOData);

    char b[256];
    sprintf_s(b,"  HWiNFO connected: sensors=%u readings=%u\n",
        h->sensor_element_count, h->entry_element_count);
    initLog+=b;

    // Scan all temperature entries — no verbose dump, just find CPU and GPU
    for(uint32_t i=0;i<h->entry_element_count;++i) {
        const HWiNFO_ENTRY* e=GetEntry(base,h,i);
        if(e->type!=ST_Temp) continue;

        const HWiNFO_SENSOR* s=nullptr;
        if(e->sensor_index<h->sensor_element_count)
            s=GetSensor(base,h,e->sensor_index);

        const char* sname = s ? s->name_user : "?";
        std::string sl=Lower(sname), ll=Lower(e->name_user);
        bool isCPU=Has(sl,"cpu")||Has(sl,"ryzen")||Has(sl,"core")||Has(sl,"intel");
        bool isGPU=Has(sl,"gpu")||Has(sl,"nvidia")||Has(sl,"radeon")||Has(sl,"geforce");

        if(isCPU&&cpuReadingIdx<0)
            if(Has(ll,"package")||Has(ll,"tdie")||Has(ll,"cpu temp")||Has(ll,"temperature"))
                { cpuSensorIdx=(int)e->sensor_index; cpuReadingIdx=(int)i;
                  sprintf_s(b,"  CPU temp: '%s' = %.1f C\n",e->name_user,e->value);
                  initLog+=b; }
        if(isGPU&&gpuReadingIdx<0)
            if(Has(ll,"gpu temp")||Has(ll,"temperature")||Has(ll,"core"))
                { gpuSensorIdx=(int)e->sensor_index; gpuReadingIdx=(int)i;
                  sprintf_s(b,"  GPU temp: '%s' = %.1f C\n",e->name_user,e->value);
                  initLog+=b; }
    }
    if(cpuReadingIdx<0) initLog+="  CPU temp: not found\n";
    if(gpuReadingIdx<0) initLog+="  GPU temp: not found\n";

    hwInfoAvailable=true;
    if(cpuReadingIdx>=0) cpuAvailable=true;
    if(gpuReadingIdx>=0) gpuAvailable=true;
}

void TempMonitor::InitNVML() {
    static const wchar_t* kP[]={L"nvml.dll",
        L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll",nullptr};
    for(int i=0;kP[i];++i){hNvml=LoadLibraryW(kP[i]);if(hNvml)break;}
    if(!hNvml){initLog+="  NVML: not found\n";return;}
    pfnNvmlInit=(PFN_nvmlInit)GetProcAddress((HMODULE)hNvml,"nvmlInit");
    pfnNvmlShutdown=(PFN_nvmlShutdown)GetProcAddress((HMODULE)hNvml,"nvmlShutdown");
    pfnNvmlDeviceGetHandleByIndex=(PFN_nvmlDeviceGetHandleByIndex)GetProcAddress((HMODULE)hNvml,"nvmlDeviceGetHandleByIndex");
    pfnNvmlDeviceGetTemperature=(PFN_nvmlDeviceGetTemperature)GetProcAddress((HMODULE)hNvml,"nvmlDeviceGetTemperature");
    if(!pfnNvmlInit||!pfnNvmlDeviceGetHandleByIndex||!pfnNvmlDeviceGetTemperature)
        {FreeLibrary((HMODULE)hNvml);hNvml=nullptr;initLog+="  NVML: proc fail\n";return;}
    if(pfnNvmlInit()!=NVML_SUCCESS)
        {FreeLibrary((HMODULE)hNvml);hNvml=nullptr;initLog+="  NVML: init fail\n";return;}
    nvmlDevice_t dev=nullptr;
    if(pfnNvmlDeviceGetHandleByIndex(0,&dev)==NVML_SUCCESS)
        {nvmlDevice=dev;nvmlAvailable=true;if(!gpuAvailable)gpuAvailable=true;
         initLog+="  NVML: GPU found\n";}
}

void TempMonitor::InitCPUFallback() {
    PDH_STATUS s=PdhOpenQueryW(nullptr,0,(PDH_HQUERY*)&hPdhQuery);
    if(s==ERROR_SUCCESS) {
        s=PdhAddEnglishCounterW((PDH_HQUERY)hPdhQuery,
            L"\\Thermal Zone Information(*)\\Temperature",0,(PDH_HCOUNTER*)&hPdhCounter);
        if(s==ERROR_SUCCESS) {
            PdhCollectQueryData((PDH_HQUERY)hPdhQuery);
            DWORD bSz=0,cnt=0;
            PdhCollectQueryData((PDH_HQUERY)hPdhQuery);
            PdhGetFormattedCounterArrayW((PDH_HCOUNTER)hPdhCounter,PDH_FMT_DOUBLE,&bSz,&cnt,nullptr);
            if(cnt>0){pdhAvailable=true;cpuAvailable=true;
                initLog+="  CPU: PDH OK\n";return;}
        }
        PdhCloseQuery((PDH_HQUERY)hPdhQuery);hPdhQuery=nullptr;hPdhCounter=nullptr;
    }
    pSvcCPU=ConnectWMI(L"ROOT\\WMI");
    if(pSvcCPU){cpuAvailable=true;initLog+="  CPU: WMI fallback\n";}
    else initLog+="  CPU: no source\n";
}

void TempMonitor::Update() {
    if(hwInfoAvailable) UpdateFromHWiNFO();
    if(!hwInfoAvailable||gpuReadingIdx<0) UpdateGPUFallback();
    if(!hwInfoAvailable||cpuReadingIdx<0) UpdateCPUFallback();
}

void TempMonitor::UpdateFromHWiNFO() {
    if(!pHWiNFOData) return;
    const HWiNFO_HDR*   h    = reinterpret_cast<const HWiNFO_HDR*>(pHWiNFOData);
    const BYTE*         base = reinterpret_cast<const BYTE*>(pHWiNFOData);
    if(cpuReadingIdx>=0&&(uint32_t)cpuReadingIdx<h->entry_element_count)
        cpuTempC=(float)GetEntry(base,h,cpuReadingIdx)->value;
    if(gpuReadingIdx>=0&&(uint32_t)gpuReadingIdx<h->entry_element_count)
        gpuTempC=(float)GetEntry(base,h,gpuReadingIdx)->value;
}

void TempMonitor::UpdateCPUFallback() {
    if(!cpuAvailable) return;
    if(pdhAvailable&&hPdhQuery&&hPdhCounter) {
        PdhCollectQueryData((PDH_HQUERY)hPdhQuery);
        if(pdhWarmup<1){++pdhWarmup;return;}
        DWORD bSz=0,cnt=0;
        PdhGetFormattedCounterArrayW((PDH_HCOUNTER)hPdhCounter,PDH_FMT_DOUBLE,&bSz,&cnt,nullptr);
        if(!bSz) return;
        std::vector<BYTE> buf(bSz);
        PDH_FMT_COUNTERVALUE_ITEM_W* items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
        if(PdhGetFormattedCounterArrayW((PDH_HCOUNTER)hPdhCounter,PDH_FMT_DOUBLE,&bSz,&cnt,items)==ERROR_SUCCESS) {
            float tot=0;int n=0;
            for(DWORD i=0;i<cnt;++i){float c=(float)items[i].FmtValue.doubleValue/10.0f-273.15f;
                if(c>0&&c<150){tot+=c;++n;}}
            if(n>0) cpuTempC=tot/(float)n;
        } return;
    }
    if(!pSvcCPU) return;
    IWbemServices* pS=(IWbemServices*)pSvcCPU;
    IEnumWbemClassObject* pE=nullptr;
    if(FAILED(pS->ExecQuery(_bstr_t(L"WQL"),
        _bstr_t(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
        WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&pE))||!pE) return;
    IWbemClassObject* pO=nullptr;ULONG ret=0;float tot=0;int n=0;
    while(pE->Next(WBEM_INFINITE,1,&pO,&ret)==S_OK) {
        VARIANT v;
        if(SUCCEEDED(pO->Get(L"CurrentTemperature",0,&v,nullptr,nullptr)))
            {float c=(float)v.lVal/10.0f-273.15f;if(c>0&&c<150){tot+=c;++n;}VariantClear(&v);}
        pO->Release();
    } pE->Release(); if(n>0) cpuTempC=tot/(float)n;
}

void TempMonitor::UpdateGPUFallback() {
    if(!nvmlAvailable||!nvmlDevice) return;
    unsigned int t=0;
    if(pfnNvmlDeviceGetTemperature(nvmlDevice,NVML_TEMPERATURE_GPU,&t)==NVML_SUCCESS)
        gpuTempC=(float)t;
}

void TempMonitor::Shutdown() {
    if(pHWiNFOData){UnmapViewOfFile(pHWiNFOData);pHWiNFOData=nullptr;}
    if(hHWiNFOMap) {CloseHandle(hHWiNFOMap);hHWiNFOMap=nullptr;}
    if(hPdhQuery)  {PdhCloseQuery((PDH_HQUERY)hPdhQuery);hPdhQuery=nullptr;}
    if(pSvcCPU)    {((IWbemServices*)pSvcCPU)->Release();pSvcCPU=nullptr;}
    if(nvmlAvailable&&pfnNvmlShutdown) pfnNvmlShutdown();
    if(hNvml){FreeLibrary((HMODULE)hNvml);hNvml=nullptr;}
    CoUninitialize();
}