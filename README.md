# VRStat

A lightweight X-Plane 12 plugin that displays a real-time system stats overlay — designed for VR use where you can't alt-tab to check performance.

---

## Features

- **FPS** — smoothed frame rate with colour coding (green ≥40, yellow ≥25, orange <25)
- **Frame Time** — total frame period in ms
- **GPU Frame Time** — GPU time per frame in ms (via `sim/time/gpu_time_per_frame_sec_approx`)
- **VRAM** — GPU memory used in GB
- **CPU Usage** — system CPU % with colour coding (green <50%, orange 50–80%, red >80%)
- **Upload Bandwidth** — NIC transmit rate in Mbps with colour coding (red <100, orange 100–130, green >130)
- **NIC Name** — active network adapter name

## Layout Modes

The overlay supports two layout modes, switchable from the Setup window:

- **Vertical** — single column, one metric per row (default)
- **Horizontal** — two columns, metrics paired left/right, compact footprint

## Setup Window

Open via the **Plugins → VRStat → Setup** menu. From here you can:

- Toggle individual metrics on/off
- Reorder metrics using the up/down arrows
- Switch between Vertical and Horizontal layout
- Save settings (persisted to `vrstat_cfg.txt` next to the `.xpl` file)

---

## Installation

1. Download the latest release
2. Copy the `VRStat` folder into:
   ```
   X-Plane 12\Resources\plugins\
   ```
   So the final path is:
   ```
   X-Plane 12\Resources\plugins\VRStat\64\win_x64\VRStat.xpl
   ```
3. Launch X-Plane 12 — the overlay appears automatically

---

## Building from Source

**Requirements:**
- Windows 10/11
- Visual Studio 2019 or 2022 (Desktop C++ workload)
- [X-Plane SDK](https://developer.x-plane.com/sdk/) extracted to `C:\XPlaneSDK`
- CMake 3.16+

**Steps:**
```bash
git clone https://github.com/alpaisley/VRStat.git
cd VRStat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build/out/64/win_x64/VRStat.xpl`

If your SDK is not at `C:\XPlaneSDK`:
```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DXPLANE_SDK="D:\path\to\XPlaneSDK"
```

---

## Configuration File

Settings are saved to `vrstat_cfg.txt` in the plugin folder. You can edit it manually:

```
NIC=1
UPLOAD=1
FPS=1
FRAMETIME=1
VRAM=1
CPU=1
GPU_FT=1

ORDER=NIC,UPLOAD,FPS,FRAMETIME,VRAM,CPU,GPU_FT
LAYOUT=VERTICAL
```

---

## Compatibility

- X-Plane 12 (Windows only)
- VR and 2D modes supported