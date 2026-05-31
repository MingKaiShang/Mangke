/**
 * @file RenderPipeline.cpp
 * @brief 录制流水线实现
 */

#include "RenderPipeline.h"
#include <iostream>
#include <chrono>

namespace mangke {

RenderPipeline::RenderPipeline() = default;

RenderPipeline::~RenderPipeline() {
    StopRecording();
    StopIPCServer();
    VideoEncoder::ShutdownMF();
}

bool RenderPipeline::Initialize(const PipelineConfig& config) {
    std::lock_guard lock(m_mutex);
    m_config = config;

    std::cout << "========================================\n";
    std::cout << "  Mangke 屏幕录制工具 - MVP\n";
    std::cout << "========================================\n";

    // 0. IPC 共享内存必须先创建，这样 ControlProcess 能立即检测到我们
    if (!m_ipc.CreateServer()) {
        std::cerr << "[Pipeline] IPC 创建失败\n";
        return false;
    }
    std::cout << "[IPC] 共享内存已创建，等待 ControlProcess 连接...\n";

    // 1. 初始化 Media Foundation
    if (!VideoEncoder::InitializeMF()) {
        std::cerr << "[Pipeline] Media Foundation 初始化失败\n";
        return false;
    }

    // 2. 初始化屏幕捕获
    m_capture = std::make_unique<ScreenCapture>();
    if (!m_capture->Initialize(config.capture)) {
        std::cerr << "[Pipeline] 屏幕捕获初始化失败\n";
        return false;
    }

    // 3. 初始化鼠标输入
    m_mouseInput = std::make_unique<MouseInput>();
    if (!m_mouseInput->Initialize(nullptr, config.zoom.smoothingFactor)) {
        std::cerr << "[Pipeline] 鼠标输入初始化失败\n";
        return false;
    }

    // 鼠标回调：仅存储状态，追踪由渲染线程统一处理
    m_mouseInput->SetStateCallback([this](const SmoothedMouseState& state) {
        std::lock_guard lock(m_mouseMutex);
        m_currentMouseState = state;
    });

    // 4. 键盘输入（用户无需键盘显示，禁用钩子提升输入法性能）
    m_keyboardInput = nullptr;


    // 6. 初始化 D3D11 设备（从捕获模块获取）
    ComPtr<ID3D11Device> d3dDevice = m_capture->GetD3DDevice();
    if (!d3dDevice) {
        std::cerr << "[Pipeline] 无法获取 D3D11 设备\n";
        return false;
    }

    // 7. 初始化渲染器
    m_renderer = std::make_unique<D3D11Renderer>();
    if (!m_renderer->Initialize(d3dDevice, config.capture.targetWidth, config.capture.targetHeight)) {
        std::cerr << "[Pipeline] 渲染器初始化失败\n";
        return false;
    }
    m_renderer->SetHighlightConfig(config.highlight);
    m_renderer->SetKeyboardConfig(config.keyboard);

    // 8. 初始化编码器
    m_encoder = std::make_unique<VideoEncoder>();

    // 9. 初始化 AI 引擎（可选）
    m_aiEngine = std::make_unique<NPUInferenceEngine>();
    m_aiEngine->Initialize(nullptr);

    m_state.store(RecordingState::Idle);
    std::cout << "\n[Pipeline] 流水线初始化完成，等待命令...\n";
    return true;
}

bool RenderPipeline::StartRecording() {
    // Guard: prevent re-entry or double-start
    auto currentState = m_state.load();
    if (currentState == RecordingState::Recording) {
        std::cerr << "[Pipeline] Already recording\n";
        return false;
    }
    if (currentState == RecordingState::Stopping) {
        std::cerr << "[Pipeline] Still stopping, ignore Start\n";
        return false;
    }

    std::lock_guard lock(m_mutex);

    // 配置编码器
    EncoderConfig encConfig = m_config.encoder;
    if (encConfig.outputPath.empty()) {
        // 生成默认输出路径
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        struct tm tm;
        localtime_s(&tm, &time);

        wchar_t path[MAX_PATH];
        swprintf_s(path, L"output/recording_%04d%02d%02d_%02d%02d%02d.mp4",
                   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec);
        encConfig.outputPath = path;
    }

    // 读取用户选择的显示器
    auto* ipcData = m_ipc.GetData();
    if (ipcData && ipcData->monitorIndex > 0) {
        m_config.capture.monitorIndex = ipcData->monitorIndex;
        std::cout << "[Pipeline] Monitor: " << m_config.capture.monitorIndex << "\n";
    }

    encConfig.width  = m_config.capture.targetWidth;
    encConfig.height = m_config.capture.targetHeight;
    encConfig.fps    = m_config.capture.targetFps;

    if (!m_encoder->Configure(encConfig) || !m_encoder->Start()) {
        std::cerr << "[Pipeline] Encoder start failed\n";
        return false;
    }
    std::cout << "[Pipeline] Encoder OK, setting callback...\n" << std::flush;

    // 设置捕获回调
    m_capture->SetFrameCallback([this](ID3D11Texture2D* frame, uint64_t frameIndex) {
        // Get mouse state for zoom tracking
        SmoothedMouseState mouseState;
        {
            std::lock_guard lock(m_mouseMutex);
            mouseState = m_currentMouseState;
        }

        // Render (zoom + effects)
        ID3D11Texture2D* outputFrame = nullptr;
        if (m_renderer) {
            m_renderer->RenderFrame(frame, mouseState, {}, &outputFrame);
        }
        if (!outputFrame) outputFrame = frame;

        // Encode
        if (m_encoder) {
            m_encoder->EncodeVideoFrame(outputFrame, frameIndex);
        }

        UpdateSharedState();
    });

    // 启动各个模块
    std::cout << "[Pipeline] Starting capture...\n" << std::flush;
    if (!m_capture->Start()) {
        std::cerr << "[Pipeline] Capture start failed\n";
        return false;
    }
    std::cout << "[Pipeline] Capture started OK\n" << std::flush;

    std::cout << "[Pipeline] Starting mouse input...\n" << std::flush;
    m_mouseInput->Start();
    std::cout << "[Pipeline] Mouse input started\n" << std::flush;

    // 音频暂禁用

    m_state.store(RecordingState::Recording);
    m_isRunning.store(true);
    UpdateSharedState(); // 立即通知 ControlProcess

    std::cout << "\n[Pipeline] ========== 录制已开始 ==========\n";
    std::cout << "[Pipeline] 输出: ";
    std::wcout << encConfig.outputPath << L"\n";
    std::cout << "[Pipeline] 分辨率: " << encConfig.width << "x" << encConfig.height << "\n";
    std::cout << "[Pipeline] 帧率: " << encConfig.fps << " fps\n";
    std::cout << "[Pipeline] 码率: " << encConfig.bitrateKbps << " kbps\n";
    std::cout << "[Pipeline] 按 Ctrl+C 停止录制\n\n";

    return true;
}

void RenderPipeline::StopRecording() {
    auto expected = RecordingState::Recording;
    if (!m_state.compare_exchange_strong(expected, RecordingState::Stopping)) {
        // Try Paused
        expected = RecordingState::Paused;
        if (!m_state.compare_exchange_strong(expected, RecordingState::Stopping)) {
            return;
        }
    }

    std::cout << "[Pipeline] 正在停止录制...\n";

    // 停止捕获
    if (m_capture) m_capture->Stop();
    if (m_mouseInput) m_mouseInput->Stop();
    // 完成编码
    if (m_encoder) {
        m_encoder->Finish();
    }

    m_state.store(RecordingState::Idle);
    m_isRunning.store(false);
    UpdateSharedState(); // 立即通知 ControlProcess 录制已停止

    uint64_t frames = m_encoder ? m_encoder->GetEncodedFrames() : 0;
    std::cout << "\n[Pipeline] ========== 录制完成 ==========\n";
    std::cout << "[Pipeline] 总帧数: " << frames << "\n";
    std::cout << "[Pipeline] 文件大小: " << (m_encoder ? m_encoder->GetOutputFileSize() / 1024 / 1024 : 0) << " MB\n\n";
}

void RenderPipeline::PauseRecording() {
    if (m_state.load() == RecordingState::Recording) {
        m_state.store(RecordingState::Paused);
        if (m_capture) m_capture->Stop();
        std::cout << "[Pipeline] 录制已暂停\n";
    }
}

void RenderPipeline::ResumeRecording() {
    if (m_state.load() == RecordingState::Paused) {
        if (m_capture) m_capture->Start();
        m_state.store(RecordingState::Recording);
        std::cout << "[Pipeline] 录制已恢复\n";
    }
}

void RenderPipeline::RunIPCServer() {
    m_ipcRunning.store(true);

    std::cout << "[Pipeline] IPC 服务器运行中...\n";
    std::cout << "[Pipeline] 等待 ControlProcess 连接...\n";

    while (m_ipcRunning.load()) {
        if (m_ipc.WaitForCommand(100)) {
            auto* data = m_ipc.GetData();
            if (data) {
                ProcessIPCCommand(data->command);
            }
        }
    }
}

void RenderPipeline::StopIPCServer() {
    m_ipcRunning.store(false);
}

void RenderPipeline::EnableKeyboard(bool enable) {
    if (enable && m_keyboardInput && !m_keyboardInput->IsInitialized()) {
        m_keyboardInput->Initialize();
        m_keyboardInput->SetEventCallback([this](const KeyboardShortcut& shortcut) {
            std::lock_guard lock(m_keyboardMutex);
            m_activeShortcuts.push_back(shortcut);
            if (m_activeShortcuts.size() > 5) m_activeShortcuts.erase(m_activeShortcuts.begin());
        });
        m_config.keyboard.enabled = true;
    } else if (!enable && m_keyboardInput) {
        m_keyboardInput->Uninitialize();
        m_config.keyboard.enabled = false;
    }
}

void RenderPipeline::ProcessIPCCommand(IPCCommand cmd) {
    // Clear the command immediately to prevent double-processing
    if (auto* d = m_ipc.GetData()) {
        d->command = IPCCommand::None;
    }

    switch (cmd) {
        case IPCCommand::StartRecording:
            StartRecording();
            break;
        case IPCCommand::StopRecording:
            std::cout << "[IPC] StopRecording received\n";
            StopRecording();
            break;
        case IPCCommand::PauseRecording:
            PauseRecording();
            break;
        case IPCCommand::ResumeRecording:
            ResumeRecording();
            break;
        case IPCCommand::UpdateZoomConfig: {
            auto* d = m_ipc.GetData();
            if (d) {
                m_config.zoom.enabled = d->zoomEnabled;
                m_config.zoom.currentZoom = d->zoomLevel;
                m_config.zoom.smoothingFactor = d->zoomSmoothing;
                d->currentZoom = m_config.zoom.currentZoom;
                if (m_renderer) {
                    float nx = d->mouseX / m_config.capture.targetWidth;
                    float ny = d->mouseY / m_config.capture.targetHeight;
                    m_renderer->UpdateZoomTarget(nx, ny, m_config.zoom.currentZoom);
                    // 也会携带光标/涟漪设置（防止 SyncAllSettings 双命令丢失）
                    m_renderer->SetConfig(d->cursorScale, d->zoomSmoothing, d->clickRippleEnabled, false);
                }
            }
            break;
        }
        case IPCCommand::UpdateHighlightConfig: {
            auto* data = m_ipc.GetData();
            if (data) {
                m_config.highlight.style = data->highlightEnabled ? MouseHighlightStyle::Circle : MouseHighlightStyle::None;
                m_config.highlight.radius = data->highlightRadius;
                m_config.highlight.color = data->highlightColor;
                if (m_renderer) {
                    m_renderer->SetHighlightConfig(m_config.highlight);
                }
            }
            break;
        }
        case IPCCommand::SetOutputPath: {
            auto* data = m_ipc.GetData();
            if (data) {
                m_config.encoder.outputPath = data->outputPath;
            }
            break;
        }
        case IPCCommand::UpdateCursorConfig: {
            auto* d = m_ipc.GetData();
            if (d) {
                if (m_renderer) {
                    m_renderer->SetConfig(d->cursorScale, d->zoomSmoothing, d->clickRippleEnabled, false);
                }
            }
            break;
        }
        case IPCCommand::Shutdown:
            StopRecording();
            StopIPCServer();
            break;
        default:
            break;
    }
}

void RenderPipeline::UpdateSharedState() {
    auto* data = m_ipc.GetData();
    if (!data) return;

    data->isRecording = m_state.load() == RecordingState::Recording;
    data->isPaused = m_state.load() == RecordingState::Paused;
    data->recordedFrames = m_encoder ? m_encoder->GetEncodedFrames() : 0;
    data->currentFPS = GetFPS();
    data->outputFileSize = m_encoder ? m_encoder->GetOutputFileSize() : 0;

    {
        std::lock_guard lock(m_mouseMutex);
        data->mouseX = m_currentMouseState.x;
        data->mouseY = m_currentMouseState.y;
    }

    data->currentZoom = m_config.zoom.currentZoom;
}

} // namespace mangke
