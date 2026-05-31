#pragma once
#include <SharedLib/SharedMemoryIPC.h>
#include <Windows.h>
#include <vector>
#include <thread>
#include <atomic>

namespace mangke {

class ControlPanel {
public:
    ControlPanel();
    ~ControlPanel();
    ControlPanel(const ControlPanel&)=delete;
    ControlPanel& operator=(const ControlPanel&)=delete;
    bool Create(HINSTANCE,int);
    int Run();
    bool ConnectToRenderProcess();

private:
    bool RegisterWindowClass(HINSTANCE);
    bool CreateMainWindow(HINSTANCE,int);
    void CreateControls(HWND);
    void OnTimer();
    void OnCommand(WORD);
    void SyncAllSettings();
    void PopulateMonitorList();
    void SaveConfig();
    void LoadConfig();
    static LRESULT CALLBACK WndProc(HWND,UINT,WPARAM,LPARAM);
    bool LaunchRenderProcess();

    HWND m_hwnd=nullptr;
    HWND m_hwndStartBtn=nullptr,m_hwndStopBtn=nullptr,m_hwndPauseBtn=nullptr;
    HWND m_hwndStatusText=nullptr;
    HWND m_zoomTrack=nullptr,m_zoomLabel=nullptr;
    HWND m_easingTrack=nullptr,m_easingLabel=nullptr;
    HWND m_cursorTrack=nullptr,m_cursorLabel=nullptr;
    HWND m_sharpTrack=nullptr,m_sharpLabel=nullptr;
    HWND m_bitrateTrack=nullptr,m_bitrateLabel=nullptr;
    HWND m_outputEdit=nullptr;
    HWND m_exportCombo=nullptr;
    HWND m_hwndHighlightCheck=nullptr,m_hwndRippleCheck=nullptr;

    HFONT m_fontNormal=nullptr,m_fontBold=nullptr,m_fontMono=nullptr;
    HBRUSH m_brushBg=nullptr;

    SharedMemoryIPC m_ipc;
    std::atomic<bool> m_connected{false};
    std::thread m_updateThread;
    std::atomic<bool> m_running{false};
    HANDLE m_renderProcess=nullptr;
    bool m_isRecording=false,m_isPaused=false;
    NOTIFYICONDATA m_trayData={};
    HMENU m_trayMenu=nullptr;
};

} // namespace mangke
