cmake_minimum_required(VERSION 3.20)
project(VRStat)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(XPLANE_SDK "C:/XPlaneSDK")

add_definitions(
    -DIBM=1
    -DWIN32
    -DXPLM200=1
    -DXPLM210=1
    -DXPLM300=1
    -DXPLM301=1
    -DXPLM302=1
    -DXPLM303=1
    -DXPLM400=1
    -D_CRT_SECURE_NO_WARNINGS
)

include_directories(
    ${XPLANE_SDK}/CHeaders/XPLM
    ${XPLANE_SDK}/CHeaders/Widgets
)

add_library(VRStat SHARED
    src/Plugin.cpp
    src/NetworkMonitor.cpp
    src/GPUMonitor.cpp
    src/PDHMonitor.cpp
    src/Config.cpp
)

target_link_libraries(VRStat
    ws2_32
    iphlpapi
    dxgi
    pdh
    "${XPLANE_SDK}/Libraries/Win/XPLM_64.lib"
    "${XPLANE_SDK}/Libraries/Win/XPWidgets_64.lib"
)

set_target_properties(VRStat PROPERTIES
    PREFIX ""
    OUTPUT_NAME "VRStat"
    SUFFIX ".xpl"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/64/win_x64"
)
