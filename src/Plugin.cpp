#include "XPLMPlugin.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#include "XPLMMenus.h"
#include "NetworkMonitor.h"
#include "GPUMonitor.h"
#include "PDHMonitor.h"
#include "TempMonitor.h"
#include "Config.h"
#include <string>
#include <vector>
#include <fstream>
#include <cstring>
#include <cstdio>

// ---- monitors ----------------------------------------------------------------
static NetworkMonitor monitor;
static GPUMonitor     gpu;
static PDHMonitor     pdh;
static TempMonitor    temps;
static Config         cfg;
static Config         cfgEdit;

// ---- layout ------------------------------------------------------------------
static const int LINE_H      = 20;
static const int PAD         = 8;
static const int WIN_W       = 260;   // vertical mode width
static const int WIN_W_2COL  = 380;   // horizontal mode width (two columns)
static const int COL_W       = 180;   // width of each column in horizontal mode
static const int NIC_W       = 400;
static const int NIC_H       = 200;
static const int SETUP_W     = 360;
static const int SETUP_LINE_H = 22;

// ---- window handles ----------------------------------------------------------
static XPLMWindowID windowId      = nullptr;
static XPLMWindowID nicWindowId   = nullptr;
static XPLMWindowID setupWindowId = nullptr;
static bool nicWindowVisible   = false;
static bool setupWindowVisible = false;
static bool inVR               = false;

// ---- menu --------------------------------------------------------------------
static XPLMMenuID pluginMenuID = nullptr;
enum MenuItems { MENU_TOGGLE_WINDOW = 0, MENU_SELECT_NIC, MENU_SETUP };

// ---- FPS smoothing -----------------------------------------------------------
static const int kFpsSamples  = 30;
static float     gFpsBuf[30]  = {};
static int       gFpsBufPos   = 0;
static bool      gFpsBufFull  = false;
static float     gSmoothedFps = 0.0f;

// ---- datarefs ----------------------------------------------------------------
static XPLMDataRef gFrameRatePeriod = nullptr;
static XPLMDataRef gICAORef         = nullptr;
static XPLMDataRef gGpuTimeSec      = nullptr;  // sim/time/gpu_time_per_frame_sec_approx

static std::string gCurrentICAO = "DEFAULT";

// ---- colours -----------------------------------------------------------------
static float gCyan[3]   = { 0.40f, 1.00f, 0.85f };
static float gGreen[3]  = { 0.00f, 1.00f, 0.00f };
static float gYellow[3] = { 1.00f, 1.00f, 0.00f };
static float gOrange[3] = { 1.00f, 0.50f, 0.00f };
static float gRed[3]    = { 1.00f, 0.15f, 0.15f };
static float gWhite[3]  = { 1.00f, 1.00f, 1.00f };
static float gGrey[3]   = { 0.55f, 0.55f, 0.55f };
static float gMuted[3]  = { 0.50f, 0.70f, 0.65f };
static const XPLMFontID kFont = xplmFont_Proportional;

// ---- paths -------------------------------------------------------------------
static std::string gCfgPath;
static std::string gNICPrefPath;

static std::string GetPluginDir() {
    char path[512] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, path, nullptr, nullptr);
    std::string s(path);
    size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos) s = s.substr(0, slash + 1);
    return s;
}

static void SaveNICPref(const std::string& nicName) {
    std::ofstream f(gNICPrefPath);
    if (f.is_open()) f << nicName;
}
static std::string LoadNICPref() {
    std::ifstream f(gNICPrefPath);
    if (!f.is_open()) return "";
    std::string s;
    std::getline(f, s);
    return s;
}

// ---- geometry helpers --------------------------------------------------------
static int MainWindowRows() {
    // vertical: one row per enabled metric
    // horizontal: ceil(enabled / 2) rows
    int n = cfg.EnabledCount();
    if (cfg.layout == LAYOUT_HORIZONTAL)
        return (n + 1) / 2;
    return n;
}
static int MainWindowHeight() {
    return PAD + MainWindowRows() * LINE_H + PAD;
}
static int MainWindowWidth() {
    return (cfg.layout == LAYOUT_HORIZONTAL) ? WIN_W_2COL : WIN_W;
}
static int SetupWindowHeight() {
    return PAD + 30
         + METRIC_COUNT * SETUP_LINE_H
         + SETUP_LINE_H          // layout toggle row
         + PAD + SETUP_LINE_H + PAD;
}

// ---- forward declarations ----------------------------------------------------
float FlightLoopCallback(float, float, int, void*);
void  DrawWindow(XPLMWindowID, void*);
int   HandleMouseClick(XPLMWindowID, int, int, int, void*);
int   HandleCursor(XPLMWindowID, int, int, void*);
void  DrawNICWindow(XPLMWindowID, void*);
int   HandleNICMouseClick(XPLMWindowID, int, int, int, void*);
int   HandleNICCursor(XPLMWindowID, int, int, void*);
void  DrawSetupWindow(XPLMWindowID, void*);
int   HandleSetupMouseClick(XPLMWindowID, int, int, int, void*);
int   HandleSetupCursor(XPLMWindowID, int, int, void*);
void  MenuHandler(void*, void*);

// ---- window creation ---------------------------------------------------------
static XPLMWindowID MakeWindow(int l, int t, int r, int b,
                                XPLMDrawWindow_f      drawFn,
                                XPLMHandleMouseClick_f clickFn,
                                XPLMHandleCursor_f    cursorFn)
{
    XPLMCreateWindow_t p;
    memset(&p, 0, sizeof(p));
    p.structSize               = sizeof(p);
    p.left = l; p.top = t; p.right = r; p.bottom = b;
    p.visible                  = 1;
    p.drawWindowFunc           = drawFn;
    p.handleMouseClickFunc     = clickFn;
    p.handleCursorFunc         = cursorFn;
    p.decorateAsFloatingWindow = xplm_WindowDecorationNone;
    p.layer                    = xplm_WindowLayerFloatingWindows;
    return XPLMCreateWindowEx(&p);
}

// All windows go VR when sim enters VR.
// NIC and setup are always xplm_WindowPositionFree (no VR attach) so the
// user can interact with them using the VR controller ray.
static void ApplyVRPositioning(bool vr) {
    // Main stats overlay: attach to VR compositor so it floats in world space
    XPLMSetWindowPositioningMode(windowId,
        vr ? xplm_WindowVR : xplm_WindowPositionFree, -1);

    // NIC and setup: keep as free-floating so controller ray can hit them
    // They are hidden on VR enter and opened fresh from the menu
    XPLMSetWindowPositioningMode(nicWindowId,   xplm_WindowPositionFree, -1);
    XPLMSetWindowPositioningMode(setupWindowId, xplm_WindowPositionFree, -1);
}

// ---- plugin lifecycle --------------------------------------------------------
PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    strcpy_s(outName, 256, "VRStat");
    strcpy_s(outSig,  256, "alanpaisley.vrstat");
    strcpy_s(outDesc, 256, "VR performance monitor - upload, FPS, VRAM, GPU, CPU.");

    std::string dir = GetPluginDir();
    gCfgPath     = dir + "vrstat_cfg.txt";
    gNICPrefPath = dir + "vrstat_nic.txt";

    if (!cfg.Load(gCfgPath)) cfg.SetDefaults();

    gFrameRatePeriod = XPLMFindDataRef("sim/operation/misc/frame_rate_period");
    gICAORef         = XPLMFindDataRef("sim/aircraft/view/acf_ICAO");

    // GPU frame time: probed below via DataRefs.txt scanner

    if (gICAORef) {
        char icao[8] = {};
        XPLMGetDatab(gICAORef, icao, 0, 7);
        if (icao[0]) gCurrentICAO = std::string(icao);
    }

    // GPU frame time — scan DataRefs.txt for gpu+time candidates, use first that resolves
    {
        char sysPath[512] = {};
        XPLMGetSystemPath(sysPath);
        std::string dataRefsTxt = std::string(sysPath) + "Resources/plugins/DataRefs.txt";

        std::vector<std::string> gpuCandidates;
        gpuCandidates.push_back("sim/operation/misc/gpu_time");
        gpuCandidates.push_back("sim/operation/misc/gpu_time_ms");
        gpuCandidates.push_back("sim/private/stats/frame_gpu_time");
        gpuCandidates.push_back("sim/private/stats/gpu_time");

        std::ifstream drf(dataRefsTxt);
        if (drf.is_open()) {
            std::string line;
            while (std::getline(drf, line)) {
                size_t tab = line.find('\t');
                std::string path = (tab != std::string::npos) ? line.substr(0, tab) : line;
                std::string lower = path;
                for (char& c : lower) c = (char)tolower((unsigned char)c);
                if (lower.find("gpu") != std::string::npos &&
                    lower.find("time") != std::string::npos)
                    gpuCandidates.push_back(path);
            }
        }

        for (const auto& c : gpuCandidates) {
            XPLMDataRef dr = XPLMFindDataRef(c.c_str());
            if (dr) { gGpuTimeSec = dr; break; }
        }
    }

    gpu.Initialize();
    pdh.Initialize();
    monitor.Initialize();

    // Log startup info to X-Plane's Log.txt
    {
        std::string msg = "VRStat: startup, dir=" + dir + "\n";
        XPLMDebugString(msg.c_str());
    }

    temps.Initialize(dir);
    XPLMDebugString(temps.GetLog().c_str());
    temps.ClearLog();

    // Ensure new metrics added since last save are enabled by default
    cfg.enabled[METRIC_CPU_TEMP] = cfg.enabled[METRIC_CPU_TEMP];
    cfg.enabled[METRIC_GPU_TEMP] = cfg.enabled[METRIC_GPU_TEMP];

    const std::vector<std::string>& nics = monitor.GetNICNames();
    if (!nics.empty()) {
        std::string saved = LoadNICPref();
        bool found = false;
        if (!saved.empty()) {
            for (size_t i = 0; i < nics.size(); ++i) {
                if (nics[i] == saved) { monitor.SelectNIC(i); found = true; break; }
            }
        }
        if (!found) monitor.SelectNIC(0);
    }

    // Create all windows hidden at a dummy position.
    // They will be properly positioned/shown when VR is entered or from the menu.
    int wh = MainWindowHeight();
    windowId = MakeWindow(0, wh, MainWindowWidth(), 0,
                          DrawWindow, HandleMouseClick, HandleCursor);
    XPLMSetWindowIsVisible(windowId, 0);

    nicWindowId = MakeWindow(0, NIC_H, NIC_W, 0,
                             DrawNICWindow, HandleNICMouseClick, HandleNICCursor);
    XPLMSetWindowIsVisible(nicWindowId, 0);

    int sh = SetupWindowHeight();
    setupWindowId = MakeWindow(0, sh, SETUP_W, 0,
                               DrawSetupWindow, HandleSetupMouseClick, HandleSetupCursor);
    XPLMSetWindowIsVisible(setupWindowId, 0);

    // Set initial positioning mode (non-VR until we get the message)
    ApplyVRPositioning(false);

    XPLMRegisterFlightLoopCallback(FlightLoopCallback, 0.5f, nullptr);

    int parentItem = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "VRStat", nullptr, 0);
    pluginMenuID   = XPLMCreateMenu("VRStat", XPLMFindPluginsMenu(),
                                    parentItem, MenuHandler, nullptr);
    XPLMAppendMenuItem(pluginMenuID, "Toggle Overlay",  (void*)MENU_TOGGLE_WINDOW, 0);
    XPLMAppendMenuItem(pluginMenuID, "Select NIC...",   (void*)MENU_SELECT_NIC,    0);
    XPLMAppendMenuItem(pluginMenuID, "Setup...",        (void*)MENU_SETUP,         0);

    return 1;
}

PLUGIN_API void XPluginStop(void) {
    cfg.Save(gCfgPath);
    XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);
    pdh.Shutdown();
    gpu.Shutdown();
    temps.Shutdown();
    if (windowId)      XPLMDestroyWindow(windowId);
    if (nicWindowId)   XPLMDestroyWindow(nicWindowId);
    if (setupWindowId) XPLMDestroyWindow(setupWindowId);
    if (pluginMenuID)  XPLMDestroyMenu(pluginMenuID);
}

PLUGIN_API void XPluginDisable(void) {}
PLUGIN_API int  XPluginEnable(void)  { return 1; }

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int msg, void*) {
    if (msg == XPLM_MSG_ENTERED_VR) {
        inVR = true;
        ApplyVRPositioning(true);
        // Show the overlay automatically when entering VR
        XPLMSetWindowIsVisible(windowId, 1);
        // Close NIC/setup if open
        XPLMSetWindowIsVisible(nicWindowId,   0);
        XPLMSetWindowIsVisible(setupWindowId, 0);
        nicWindowVisible   = false;
        setupWindowVisible = false;
    }
    else if (msg == XPLM_MSG_EXITING_VR) {
        inVR = false;
        ApplyVRPositioning(false);
        // Hide everything when leaving VR — plugin is VR-only
        XPLMSetWindowIsVisible(windowId,      0);
        XPLMSetWindowIsVisible(nicWindowId,   0);
        XPLMSetWindowIsVisible(setupWindowId, 0);
        nicWindowVisible   = false;
        setupWindowVisible = false;
    }
}

// ---- flight loop -------------------------------------------------------------
float FlightLoopCallback(float, float, int, void*) {
    monitor.Update();
    gpu.Update();
    pdh.Update();

    // Temps only need updating every 2 seconds
    static int tempSkip = 0;
    if (++tempSkip >= 4) { temps.Update(); tempSkip = 0; }

    // Track aircraft changes
    if (gICAORef) {
        char icao[8] = {};
        XPLMGetDatab(gICAORef, icao, 0, 7);
        if (icao[0]) {
            std::string newICAO(icao);
            if (newICAO != gCurrentICAO)
                gCurrentICAO = newICAO;
        }
    }

    // FPS smoothing
    if (gFrameRatePeriod) {
        float period = XPLMGetDataf(gFrameRatePeriod);
        if (period > 0.0f) {
            gFpsBuf[gFpsBufPos] = 1.0f / period;
            gFpsBufPos = (gFpsBufPos + 1) % kFpsSamples;
            if (gFpsBufPos == 0) gFpsBufFull = true;
            int count = gFpsBufFull ? kFpsSamples : gFpsBufPos;
            float sum = 0.0f;
            for (int i = 0; i < count; ++i) sum += gFpsBuf[i];
            gSmoothedFps = sum / (float)count;
        }
    }

    return 0.5f;
}

// ---- main overlay ------------------------------------------------------------
void DrawWindow(XPLMWindowID id, void*) {
    int l, t, r, b;
    XPLMGetWindowGeometry(id, &l, &t, &r, &b);
    XPLMDrawTranslucentDarkBox(l, t, r, b);

    // Build list of enabled metrics in order
    std::vector<MetricID> active;
    for (size_t i = 0; i < cfg.order.size(); ++i)
        if (cfg.enabled[cfg.order[i]]) active.push_back(cfg.order[i]);

    // Helper lambda: format a metric into buf and return its colour
    auto FormatMetric = [&](MetricID m, char* buf, size_t bufSz) -> float* {
        float* col = gCyan;
        switch (m) {
        case METRIC_NIC: {
            std::string name = monitor.GetAdapterName();
            if (name.length() > 22) name = name.substr(0, 22) + "..";
            snprintf(buf, bufSz, "NIC:  %s", name.c_str());
            break;
        }
        case METRIC_UPLOAD: {
            double mbps = monitor.GetTxMbps();
            snprintf(buf, bufSz, "Up:   %.2f Mbps", mbps);
            if      (mbps > 130.0) col = gGreen;
            else if (mbps > 100.0) col = gOrange;
            else                   col = gRed;
            break;
        }
        case METRIC_FPS:
            if (gSmoothedFps > 0.0f) snprintf(buf, bufSz, "FPS:  %.0f", gSmoothedFps);
            else                     snprintf(buf, bufSz, "FPS:  ---");
            if      (gSmoothedFps >= 40.0f) col = gGreen;
            else if (gSmoothedFps >= 25.0f) col = gYellow;
            else                            col = gOrange;
            break;
        case METRIC_FRAMETIME: {
            if (gFrameRatePeriod) {
                float ft = XPLMGetDataf(gFrameRatePeriod) * 1000.0f;
                snprintf(buf, bufSz, "FT:   %.1f ms", ft);
                if      (ft < 25.0f) col = gGreen;   // >40fps
                else if (ft < 33.3f) col = gOrange;  // >30fps
                else                 col = gRed;      // <30fps
            } else {
                snprintf(buf, bufSz, "FT:   ---");
            }
            break;
        }
        case METRIC_VRAM: {
            float used   = gpu.GetVRAMUsedGB();
            float budget = gpu.GetVRAMBudgetGB();
            if (budget > 0.0f) {
                float pct = used / budget * 100.0f;
                snprintf(buf, bufSz, "VRAM: %.0f%%", pct);
                if      (pct < 60.0f) col = gGreen;
                else if (pct < 80.0f) col = gOrange;
                else                  col = gRed;
            } else {
                snprintf(buf, bufSz, "VRAM: ---");
            }
            break;
        }
        case METRIC_CPU: {
            float cpu = pdh.GetCPUPercent();
            snprintf(buf, bufSz, "CPU:  %.0f%%", cpu);
            if      (cpu < 50.0f) col = gGreen;
            else if (cpu < 75.0f) col = gOrange;
            else                  col = gRed;
            break;
        }
        case METRIC_GPU_FT: {
            if (gGpuTimeSec) {
                float ft = XPLMGetDataf(gGpuTimeSec) * 1000.0f;
                snprintf(buf, bufSz, "GPU-FT: %.1f ms", ft);
                if      (ft < 25.0f) col = gGreen;
                else if (ft < 33.3f) col = gOrange;
                else                 col = gRed;
            } else {
                snprintf(buf, bufSz, "GPU-FT: ---");
            }
            break;
        }
        case METRIC_CPU_TEMP: {
            if (temps.HasCPUTemp()) {
                float t = temps.GetCPUTempC();
                snprintf(buf, bufSz, "CPU-T: %.0f C", t);
                if      (t <= 60.0f) col = gGreen;
                else if (t <= 75.0f) col = gOrange;
                else                 col = gRed;
            } else {
                snprintf(buf, bufSz, "CPU-T: ---");
            }
            break;
        }
        case METRIC_GPU_TEMP: {
            if (temps.HasGPUTemp()) {
                float t = temps.GetGPUTempC();
                snprintf(buf, bufSz, "GPU-T: %.0f C", t);
                if      (t <= 60.0f) col = gGreen;
                else if (t <= 75.0f) col = gOrange;
                else                 col = gRed;
            } else {
                snprintf(buf, bufSz, "GPU-T: ---");
            }
            break;
        }
        default: break;
        }
        return col;
    };

    if (cfg.layout == LAYOUT_VERTICAL) {
        int y = t - PAD - 10;
        for (size_t i = 0; i < active.size(); ++i) {
            char buf[64] = {};
            float* col = FormatMetric(active[i], buf, sizeof(buf));
            XPLMDrawString(col, l + PAD, y, buf, nullptr, kFont);
            y -= LINE_H;
        }
    } else {
        // Horizontal: two columns, metrics paired left/right
        int y = t - PAD - 10;
        for (size_t i = 0; i < active.size(); i += 2) {
            // Left column
            char bufL[64] = {};
            float* colL = FormatMetric(active[i], bufL, sizeof(bufL));
            XPLMDrawString(colL, l + PAD, y, bufL, nullptr, kFont);

            // Right column (if a metric exists for this row)
            if (i + 1 < active.size()) {
                char bufR[64] = {};
                float* colR = FormatMetric(active[i + 1], bufR, sizeof(bufR));
                XPLMDrawString(colR, l + PAD + COL_W, y, bufR, nullptr, kFont);
            }
            y -= LINE_H;
        }
    }
}

// No dragging — VR windows are repositioned by the VR controller
int HandleMouseClick(XPLMWindowID, int, int, int, void*) { return 1; }
int HandleCursor(XPLMWindowID, int, int, void*)          { return xplm_CursorDefault; }

// ---- NIC picker --------------------------------------------------------------
void DrawNICWindow(XPLMWindowID id, void*) {
    int l, t, r, b;
    XPLMGetWindowGeometry(id, &l, &t, &r, &b);
    if ((r - l) != NIC_W || (t - b) != NIC_H)
        XPLMSetWindowGeometry(id, l, b + NIC_H, l + NIC_W, b);

    XPLMDrawTranslucentDarkBox(l, t, r, b);
    XPLMDrawString(gCyan,   l + 8, t - 14, "[ Select NIC ]",        nullptr, kFont);
    XPLMDrawString(gOrange, r - 24, t - 14, "[X]",                  nullptr, kFont);
    XPLMDrawString(gYellow, l + 8, t - 34, "Click a NIC to select:", nullptr, kFont);

    const std::vector<std::string>& nics = monitor.GetNICNames();
    int y = t - 54;
    for (size_t i = 0; i < nics.size(); ++i) {
        std::string line = std::to_string(i + 1) + ": " + nics[i];
        if (line.length() > 52) line = line.substr(0, 52) + "..";
        XPLMDrawString(gWhite, l + 8, y, line.c_str(), nullptr, kFont);
        y -= 20;
        if (y < b + 8) break;
    }
}

int HandleNICMouseClick(XPLMWindowID id, int x, int y, int isDown, void*) {
    if (!isDown) return 1;
    int l, t, r, b;
    XPLMGetWindowGeometry(id, &l, &t, &r, &b);

    // Close button
    if (x >= r - 24 && x <= r - 4 && y >= t - 20 && y <= t - 4) {
        XPLMSetWindowIsVisible(nicWindowId, 0);
        nicWindowVisible = false;
        return 1;
    }

    const std::vector<std::string>& nics = monitor.GetNICNames();
    int yPos = t - 54;
    for (size_t i = 0; i < nics.size(); ++i) {
        if (y <= yPos + 4 && y >= yPos - 14) {
            monitor.SelectNIC(i);
            SaveNICPref(monitor.GetAdapterName());
            XPLMSetWindowIsVisible(nicWindowId, 0);
            nicWindowVisible = false;
            break;
        }
        yPos -= 20;
        if (yPos < b + 8) break;
    }
    return 1;
}
int HandleNICCursor(XPLMWindowID, int, int, void*) { return xplm_CursorDefault; }

// ---- setup window ------------------------------------------------------------
static int SetupRowY(int t, int row) {
    return t - PAD - 30 - row * SETUP_LINE_H;
}

void DrawSetupWindow(XPLMWindowID id, void*) {
    int l, t, r, b;
    XPLMGetWindowGeometry(id, &l, &t, &r, &b);
    int sh = SetupWindowHeight();
    if ((r - l) != SETUP_W || (t - b) != sh)
        XPLMSetWindowGeometry(id, l, b + sh, l + SETUP_W, b);

    XPLMDrawTranslucentDarkBox(l, t, r, b);
    XPLMDrawString(gCyan,   l + 8,  t - 14, "[ VRStat Setup ]",          nullptr, kFont);
    XPLMDrawString(gOrange, r - 24, t - 14, "[X]",                        nullptr, kFont);
    XPLMDrawString(gMuted,  l + 8,  t - 28, "Toggle and reorder. Save to apply.", nullptr, kFont);

    for (int row = 0; row < METRIC_COUNT; ++row) {
        if (row >= (int)cfgEdit.order.size()) break;  // safety: should never happen
        MetricID m = cfgEdit.order[row];
        int y = SetupRowY(t, row);

        const char* box = cfgEdit.enabled[m] ? "[X]" : "[ ]";
        float* boxCol   = cfgEdit.enabled[m] ? gGreen : gGrey;
        XPLMDrawString(boxCol, l + 8,  y, box,            nullptr, kFont);
        XPLMDrawString(gWhite, l + 36, y, MetricLabels[m], nullptr, kFont);

        float* upCol = (row > 0)               ? gCyan : gGrey;
        float* dnCol = (row < METRIC_COUNT - 1) ? gCyan : gGrey;
        XPLMDrawString(upCol, r - 38, y, "[^]", nullptr, kFont);
        XPLMDrawString(dnCol, r - 18, y, "[v]", nullptr, kFont);
    }

    // Layout toggle row (below metrics)
    int layoutY = SetupRowY(t, METRIC_COUNT);
    XPLMDrawString(gMuted,  l + 8,   layoutY, "Layout:", nullptr, kFont);
    float* vertCol  = (cfgEdit.layout == LAYOUT_VERTICAL)   ? gGreen : gGrey;
    float* horizCol = (cfgEdit.layout == LAYOUT_HORIZONTAL) ? gGreen : gGrey;
    XPLMDrawString(vertCol,  l + 70,  layoutY, "[Vertical]",   nullptr, kFont);
    XPLMDrawString(horizCol, l + 180, layoutY, "[Horizontal]", nullptr, kFont);

    int btnY = b + PAD + 4;
    XPLMDrawString(gGreen,  l + 40,        btnY, "[ Save ]",   nullptr, kFont);
    XPLMDrawString(gOrange, l + SETUP_W/2, btnY, "[ Cancel ]", nullptr, kFont);
}

int HandleSetupMouseClick(XPLMWindowID id, int x, int y, int isDown, void*) {
    if (!isDown) return 1;
    int l, t, r, b;
    XPLMGetWindowGeometry(id, &l, &t, &r, &b);

    // Close [X]
    if (x >= r - 24 && x <= r - 4 && y >= t - 20 && y <= t - 4) {
        XPLMSetWindowIsVisible(setupWindowId, 0);
        setupWindowVisible = false;
        return 1;
    }

    // Save / Cancel
    int btnY = b + PAD + 4;
    if (y >= btnY - 4 && y <= btnY + 14) {
        if (x >= l + 40 && x <= l + 120) {
            // Save - apply config then immediately resize overlay anchored to top
            cfg = cfgEdit;
            cfg.Save(gCfgPath);
            int wh = MainWindowHeight();
            int ww = MainWindowWidth();
            int wl, wt, wr, wb;
            XPLMGetWindowGeometry(windowId, &wl, &wt, &wr, &wb);
            XPLMSetWindowGeometry(windowId, wl, wt, wl + ww, wt - wh);
            XPLMSetWindowIsVisible(setupWindowId, 0);
            setupWindowVisible = false;
            return 1;
        }
        if (x >= l + SETUP_W/2 && x <= l + SETUP_W - 10) {
            // Cancel
            XPLMSetWindowIsVisible(setupWindowId, 0);
            setupWindowVisible = false;
            return 1;
        }
    }

    // Layout toggle row
    int layoutY = SetupRowY(t, METRIC_COUNT);
    if (y >= layoutY - 4 && y <= layoutY + 14) {
        if (x >= l + 70 && x <= l + 170)
            cfgEdit.layout = LAYOUT_VERTICAL;
        else if (x >= l + 180 && x <= l + 310)
            cfgEdit.layout = LAYOUT_HORIZONTAL;
        return 1;
    }

    // Row toggle / reorder
    for (int row = 0; row < METRIC_COUNT; ++row) {
        int rowY = SetupRowY(t, row);
        if (y > rowY + 4 || y < rowY - SETUP_LINE_H + 4) continue;

        if (x >= l + 8 && x <= l + 32) {
            cfgEdit.enabled[cfgEdit.order[row]] = !cfgEdit.enabled[cfgEdit.order[row]];
            return 1;
        }
        if (x >= r - 40 && x <= r - 22 && row > 0) {
            std::swap(cfgEdit.order[row], cfgEdit.order[row - 1]);
            return 1;
        }
        if (x >= r - 20 && x <= r - 2 && row < METRIC_COUNT - 1) {
            std::swap(cfgEdit.order[row], cfgEdit.order[row + 1]);
            return 1;
        }
    }
    return 1;
}
int HandleSetupCursor(XPLMWindowID, int, int, void*) { return xplm_CursorDefault; }

// ---- menu --------------------------------------------------------------------
void MenuHandler(void*, void* itemRef) {
    intptr_t item = (intptr_t)itemRef;

    if (item == MENU_TOGGLE_WINDOW) {
        bool vis = XPLMGetWindowIsVisible(windowId) != 0;
        if (!vis) {
            // Reapply correct size when showing in case config changed
            int wh = MainWindowHeight();
            int ww = MainWindowWidth();
            int wl, wt, wr, wb;
            XPLMGetWindowGeometry(windowId, &wl, &wt, &wr, &wb);
            XPLMSetWindowGeometry(windowId, wl, wt, wl + ww, wt - wh);
        }
        XPLMSetWindowIsVisible(windowId, vis ? 0 : 1);

    } else if (item == MENU_SELECT_NIC) {
        nicWindowVisible = !nicWindowVisible;
        if (nicWindowVisible) {
            monitor.Refresh();
            // Always hide first to reset VR compositor state, then reshow
            XPLMSetWindowIsVisible(nicWindowId, 0);
            XPLMSetWindowPositioningMode(nicWindowId,
                inVR ? xplm_WindowVR : xplm_WindowPositionFree, -1);
        }
        XPLMSetWindowIsVisible(nicWindowId, nicWindowVisible ? 1 : 0);

    } else if (item == MENU_SETUP) {
        setupWindowVisible = !setupWindowVisible;
        if (setupWindowVisible) {
            cfgEdit = cfg;
            XPLMSetWindowIsVisible(setupWindowId, 0);
            XPLMSetWindowPositioningMode(setupWindowId,
                inVR ? xplm_WindowVR : xplm_WindowPositionFree, -1);
        }
        XPLMSetWindowIsVisible(setupWindowId, setupWindowVisible ? 1 : 0);
    }
}