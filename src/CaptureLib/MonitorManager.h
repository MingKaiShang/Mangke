/**
 * @file MonitorManager.h
 * @brief Multi-monitor enumeration and selection
 */

#pragma once

#include <Windows.h>
#include <vector>
#include <string>

namespace mangke {

struct MonitorInfo {
    HMONITOR hMonitor = nullptr;
    int x = 0, y = 0;
    int width = 0, height = 0;
    bool isPrimary = false;
    std::wstring deviceName;
    int index = 0;
};

class MonitorManager {
public:
    static std::vector<MonitorInfo> EnumerateMonitors();
    static MonitorInfo GetMonitor(int index);
    static int GetMonitorCount();

private:
    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC hdc, LPRECT rect, LPARAM data);
};

} // namespace mangke
