/**
 * @file SharedMemoryIPC.h
 * @brief Shared memory IPC module (shared between RenderProcess and ControlProcess)
 *
 * ControlProcess and RenderProcess communicate via shared memory + Event.
 * Command delivery latency < 1us (nanosecond level).
 */

#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <mutex>

namespace mangke {

/// IPC command type
enum class IPCCommand : uint32_t {
    None = 0,
    StartRecording,
    StopRecording,
    PauseRecording,
    ResumeRecording,
    UpdateZoomConfig,
    UpdateHighlightConfig,
    UpdateKeyboardConfig,
    SetCaptureRegion,
    SetOutputPath,
    UpdateCursorConfig,
    Shutdown,
};

/// Shared memory data layout
#pragma pack(push, 8)
struct SharedMemoryData {
    // === Control -> Render (commands) ===
    volatile IPCCommand command;
    volatile uint64_t   commandId;

    // Capture region
    volatile int32_t  captureX;
    volatile int32_t  captureY;
    volatile uint32_t captureWidth;
    volatile uint32_t captureHeight;
    volatile bool     captureFullscreen;

    // Display selection
    volatile int32_t  monitorIndex;

    // Zoom parameters
    volatile float    zoomLevel;
    volatile float    zoomSmoothing;
    volatile bool     zoomEnabled;

    // Mouse highlight
    volatile bool     highlightEnabled;
    volatile float    highlightRadius;
    volatile uint32_t highlightColor;

    // Keyboard visualization
    volatile bool     keyboardVisEnabled;

    // Cursor settings
    volatile float    cursorScale;
    volatile bool     clickRippleEnabled;

    // Recording settings
    volatile uint32_t encoderBitrate;

    // Output path
    wchar_t outputPath[260];

    // === Render -> Control (status) ===
    volatile bool     isRecording;
    volatile bool     isPaused;
    volatile uint64_t recordedFrames;
    volatile float    currentFPS;
    volatile float    captureFPS;
    volatile uint64_t outputFileSize;
    volatile float    cpuUsage;
    volatile float    gpuUsage;
    volatile float    npuUsage;

    // Mouse state (written by render, read by control)
    volatile float    mouseX;
    volatile float    mouseY;
    volatile float    currentZoom;

    uint8_t reserved[252];
};
#pragma pack(pop)

/**
 * @class SharedMemoryIPC
 * @brief Shared memory IPC communication
 */
class SharedMemoryIPC {
public:
    SharedMemoryIPC();
    ~SharedMemoryIPC();

    SharedMemoryIPC(const SharedMemoryIPC&) = delete;
    SharedMemoryIPC& operator=(const SharedMemoryIPC&) = delete;

    bool CreateServer();
    bool OpenClient();
    void SendCommand(IPCCommand cmd);
    bool WaitForCommand(uint32_t timeoutMs = 100);

    SharedMemoryData* GetData() { return m_data; }
    const SharedMemoryData* GetData() const { return m_data; }

    void Close();
    bool IsConnected() const { return m_data != nullptr; }

private:
    HANDLE m_mapFile  = nullptr;
    HANDLE m_cmdEvent = nullptr;
    SharedMemoryData* m_data = nullptr;
    bool m_isServer = false;
    uint64_t m_lastCommandId = 0;
};

} // namespace mangke
