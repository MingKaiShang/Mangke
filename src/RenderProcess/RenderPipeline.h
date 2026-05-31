/**
 * @file RenderPipeline.h
 * @brief 录制流水线 — 核心调度器
 *
 * 管理捕获 → 渲染 → 编码的完整流水线：
 * - 捕获线程：WGC 屏幕捕获
 * - AI 线程：（可选）NPU 推理增强
 * - 渲染线程：D3D11 变焦 + 鼠标/键盘叠加
 * - 编码线程：Media Foundation H.264 编码
 * - 磁盘写入线程：异步文件输出
 */

#pragma once

#include <CaptureLib/ScreenCapture.h>
#include <CaptureLib/MouseInput.h>
#include <CaptureLib/KeyboardInput.h>
#include <EncoderLib/VideoEncoder.h>
#include <AILib/NPUInferenceEngine.h>
#include "D3D11Renderer.h"
#include "SharedMemoryIPC.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>

namespace mangke {

/// 录制状态
enum class RecordingState : uint8_t {
    Idle,
    Recording,
    Paused,
    Stopping,
    Error,
};

/// 流水线配置
struct PipelineConfig {
    CaptureConfig      capture;
    EncoderConfig      encoder;
    ZoomConfig         zoom;
    MouseHighlightConfig highlight;
    KeyboardVisConfig  keyboard;
    std::wstring       outputPath;
};

/**
 * @class RenderPipeline
 * @brief 录制流水线管理器
 *
 * 协调所有线程和模块，实现端到端 < 16ms 延迟的录制流水线。
 */
class RenderPipeline {
public:
    RenderPipeline();
    ~RenderPipeline();

    RenderPipeline(const RenderPipeline&) = delete;
    RenderPipeline& operator=(const RenderPipeline&) = delete;

    /**
     * @brief 初始化流水线
     * @param config 流水线配置
     * @return 成功返回 true
     */
    bool Initialize(const PipelineConfig& config);

    /**
     * @brief 开始录制
     * @return 成功返回 true
     */
    bool StartRecording();

    /**
     * @brief 停止录制
     */
    void StopRecording();

    /**
     * @brief 暂停录制
     */
    void PauseRecording();

    /**
     * @brief 恢复录制
     */
    void ResumeRecording();

    /**
     * @brief 获取当前录制状态
     */
    RecordingState GetState() const { return m_state.load(); }

    /**
     * @brief 获取统计信息
     */
    float GetFPS() const { return m_capture ? m_capture->GetCurrentFPS() : 0.0f; }
    uint64_t GetRecordedFrames() const { return m_encoder ? m_encoder->GetEncodedFrames() : 0; }

    /**
     * @brief 运行主循环（处理 IPC 命令）
     * 由 ControlProcess 调用，或在无 UI 模式下阻塞运行
     */
    void RunIPCServer();

    /**
     * @brief 停止主循环
     */
    void StopIPCServer();
    void EnableKeyboard(bool enable); // 动态启用/禁用键盘钩子

    /**
     * @brief 是否正在运行
     */
    bool IsRunning() const { return m_isRunning.load(); }

private:
    /// 录制主循环
    void RecordingLoop();

    /// 处理 IPC 命令
    void ProcessIPCCommand(IPCCommand cmd);

    /// 更新状态到共享内存
    void UpdateSharedState();

private:
    PipelineConfig m_config;

    // 模块
    std::unique_ptr<ScreenCapture>  m_capture;
    std::unique_ptr<MouseInput>     m_mouseInput;
    std::unique_ptr<KeyboardInput>  m_keyboardInput;
    std::unique_ptr<D3D11Renderer>  m_renderer;
    std::unique_ptr<VideoEncoder>   m_encoder;
    std::unique_ptr<NPUInferenceEngine> m_aiEngine;

    // IPC
    SharedMemoryIPC m_ipc;

    // 状态
    std::atomic<RecordingState> m_state{RecordingState::Idle};
    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_ipcRunning{false};

    // 同步
    mutable std::mutex m_mutex;
    std::condition_variable m_condVar;

    // 鼠标状态
    SmoothedMouseState m_currentMouseState{};
    mutable std::mutex m_mouseMutex;

    // 键盘状态
    std::vector<KeyboardShortcut> m_activeShortcuts;
    mutable std::mutex m_keyboardMutex;
};

} // namespace mangke
