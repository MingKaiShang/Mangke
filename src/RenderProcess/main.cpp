/**
 * @file main.cpp
 * @brief RenderProcess entry point
 *
 * IPC mode: waits for commands from ControlProcess (default)
 * No-IPC mode: starts recording immediately, waits for shutdown signal (--no-ipc)
 */

#include "RenderPipeline.h"
#include <CaptureLib/MonitorManager.h>
#include <iostream>
#include <fstream>
#include <string>
#include <csignal>

using namespace mangke;

static std::unique_ptr<RenderPipeline> g_pipeline;
static volatile bool g_running = true;
static std::ofstream g_log;

void SignalHandler(int signal) {
    g_running = false;
    if (g_pipeline) {
        g_pipeline->StopRecording();
        g_pipeline->StopIPCServer();
    }
}

int RunIPCMode() {
    g_log << "[IPC] Starting IPC server mode\n";
    g_log.flush();

    PipelineConfig config = {};
    auto monInfo = MonitorManager::GetMonitor(0);
    config.capture.targetFps = 60;
    config.capture.targetWidth = GetSystemMetrics(SM_CXSCREEN);
    config.capture.targetHeight = GetSystemMetrics(SM_CYSCREEN);
    config.capture.monitorIndex = 0;
    config.capture.captureMouse = true;
    config.capture.captureAudio = true;
    config.zoom.enabled = true;
    config.zoom.currentZoom = 1.5f;
    config.zoom.smoothingFactor = 0.08f;
    config.highlight.style = MouseHighlightStyle::Circle;
    config.highlight.radius = 30.0f;
    config.highlight.opacity = 0.4f;
    config.highlight.color = 0xFF0071E3;
    config.highlight.showClick = true;
    config.keyboard.enabled = false; // 默认禁用键盘钩子，避免输入法卡顿
    config.keyboard.displayDurationMs = 2000;
    config.encoder.bitrateKbps = 8000;
    config.encoder.codec = EncoderConfig::Codec::H264;

    g_pipeline = std::make_unique<RenderPipeline>();
    if (!g_pipeline->Initialize(config)) {
        g_log << "[IPC] Initialize FAILED\n";
        return 1;
    }

    HANDLE shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, L"MangkeShutdownEvent");
    g_pipeline->RunIPCServer();
    CloseHandle(shutdownEvent);
    g_pipeline.reset();
    return 0;
}

int RunNoIPCMode() {
    g_log << "[NoIPC] Starting direct recording mode\n";
    g_log.flush();

    PipelineConfig config = {};
    auto monInfo = MonitorManager::GetMonitor(0);
    config.capture.targetFps = 60;
    config.capture.targetWidth = GetSystemMetrics(SM_CXSCREEN);
    config.capture.targetHeight = GetSystemMetrics(SM_CYSCREEN);
    config.capture.monitorIndex = 0;
    config.capture.captureMouse = true;
    config.capture.captureAudio = true;
    config.zoom.enabled = true;
    config.zoom.currentZoom = 1.5f;
    config.zoom.smoothingFactor = 0.08f;
    config.highlight.style = MouseHighlightStyle::Circle;
    config.highlight.radius = 30.0f;
    config.highlight.opacity = 0.4f;
    config.highlight.color = 0xFF0071E3;
    config.highlight.showClick = true;
    config.keyboard.enabled = false; // 默认禁用键盘钩子，避免输入法卡顿
    config.keyboard.displayDurationMs = 2000;
    config.encoder.bitrateKbps = 8000;
    config.encoder.codec = EncoderConfig::Codec::H264;

    g_log << "[NoIPC] Config: " << monInfo.width << "x" << monInfo.height << "\n";
    g_log.flush();

    g_pipeline = std::make_unique<RenderPipeline>();
    if (!g_pipeline->Initialize(config)) {
        g_log << "[NoIPC] Initialize FAILED\n";
        return 1;
    }
    if (!g_pipeline->StartRecording()) {
        g_log << "[NoIPC] StartRecording FAILED\n";
        return 1;
    }

    // Wait for shutdown signal
    HANDLE shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, L"MangkeShutdownEvent");
    while (g_running) {
        HANDLE handles[] = { shutdownEvent };
        DWORD r = MsgWaitForMultipleObjects(1, handles, FALSE, 100, QS_ALLEVENTS);
        if (r == WAIT_OBJECT_0) {
            g_log << "[NoIPC] Shutdown event received\n";
            break;
        }
        static uint64_t lastLog = 0;
        auto now = GetTickCount64();
        if (now - lastLog > 2000) {
            g_log << "[Stats] FPS: " << g_pipeline->GetFPS()
                  << " Frames: " << g_pipeline->GetRecordedFrames() << "\n";
            lastLog = now;
        }
    }
    CloseHandle(shutdownEvent);

    g_pipeline->StopRecording();
    g_pipeline.reset();
    return 0;
}

int wmain(int argc, wchar_t* argv[]) {
    g_log.open("output\\render_log.txt", std::ios::out | std::ios::trunc);
    g_log << "[Main] Mangke RenderProcess\n";
    g_log.flush();

    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    SetConsoleCtrlHandler([](DWORD ctrlType) -> BOOL {
        if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
            g_running = false;
            if (g_pipeline) {
                g_pipeline->StopRecording();
                g_pipeline->StopIPCServer();
            }
            HANDLE evt = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"MangkeShutdownEvent");
            if (evt) { SetEvent(evt); CloseHandle(evt); }
            return TRUE;
        }
        return FALSE;
    }, TRUE);

    CreateDirectoryW(L"output", nullptr);

    bool noIPC = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--no-ipc") noIPC = true;
    }

    int result = 0;
    if (noIPC) {
        result = RunNoIPCMode();
    } else {
        result = RunIPCMode();
    }

    if (g_pipeline) {
        g_pipeline->StopRecording();
        g_pipeline.reset();
    }

    g_log << "[Main] Exit: " << result << "\n";
    g_log.close();
    VideoEncoder::ShutdownMF();
    return result;
}
