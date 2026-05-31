/**
 * @file MonitorManager.cpp
 * @brief Multi-monitor enumeration using EnumDisplayMonitors
 */

#include "MonitorManager.h"
#include <iostream>

namespace mangke {

struct EnumData {
    std::vector<MonitorInfo>* monitors;
    int index;
};

BOOL CALLBACK MonitorManager::MonitorEnumProc(HMONITOR hMon, HDC hdc, LPRECT rect, LPARAM data) {
    auto* enumData = reinterpret_cast<EnumData*>(data);

    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(MONITORINFOEXW);
    if (GetMonitorInfoW(hMon, &mi)) {
        MonitorInfo info;
        info.hMonitor = hMon;
        info.x = mi.rcMonitor.left;
        info.y = mi.rcMonitor.top;
        info.width = mi.rcMonitor.right - mi.rcMonitor.left;
        info.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        info.deviceName = mi.szDevice;
        info.index = enumData->index++;
        enumData->monitors->push_back(info);
    }
    return TRUE;
}

std::vector<MonitorInfo> MonitorManager::EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumData data = { &monitors, 0 };
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&data));

    std::cout << "[Monitor] Found " << monitors.size() << " monitor(s):\n";
    for (const auto& m : monitors) {
        std::wcout << L"  [" << m.index << L"] " << m.deviceName
                   << L" " << m.width << L"x" << m.height
                   << L" at (" << m.x << L"," << m.y << L")"
                   << (m.isPrimary ? L" [PRIMARY]" : L"") << L"\n";
    }
    return monitors;
}

MonitorInfo MonitorManager::GetMonitor(int index) {
    auto monitors = EnumerateMonitors();
    if (index >= 0 && index < static_cast<int>(monitors.size())) {
        return monitors[index];
    }
    // Return primary monitor as fallback
    for (const auto& m : monitors) {
        if (m.isPrimary) return m;
    }
    return {};
}

int MonitorManager::GetMonitorCount() {
    return GetSystemMetrics(SM_CMONITORS);
}

} // namespace mangke
