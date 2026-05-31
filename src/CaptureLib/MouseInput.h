/**
 * @file MouseInput.h
 * @brief 基于 RawInput 的高频鼠标输入模块
 *
 * 使用 RawInput API 以 1000Hz 频率采样鼠标坐标。
 * 实现一阶滞后滤波 + 线性外推，消除抖动并补偿系统捕获延迟。
 */

#pragma once

#include "CaptureCommon.h"
#include <Windows.h>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>

namespace mangke {

/// 鼠标状态更新回调
using MouseStateCallback = std::function<void(const SmoothedMouseState& state)>;

/**
 * @class MouseInput
 * @brief 高频鼠标输入采集与平滑
 */
class MouseInput {
public:
    MouseInput();
    ~MouseInput();

    MouseInput(const MouseInput&) = delete;
    MouseInput& operator=(const MouseInput&) = delete;

    /**
     * @brief 初始化鼠标输入模块
     * @param hwnd 接收 RawInput 的窗口句柄（nullptr 则使用轮询模式）
     * @param smoothingFactor 平滑系数 (0.01~1.0, 推荐 0.08)
     */
    bool Initialize(HWND hwnd = nullptr, float smoothingFactor = 0.08f);

    /**
     * @brief 设置鼠标状态回调
     */
    void SetStateCallback(MouseStateCallback callback);

    /**
     * @brief 启动鼠标采样线程（轮询模式）
     */
    bool Start();

    /**
     * @brief 停止采样
     */
    void Stop();

    /**
     * @brief 处理 WM_INPUT 消息（事件驱动模式）
     */
    void HandleRawInput(HRAWINPUT hRawInput);

    /**
     * @brief 获取当前平滑后的鼠标状态
     */
    SmoothedMouseState GetSmoothedState() const;

    /**
     * @brief 获取鼠标高亮配置
     */
    const MouseHighlightConfig& GetHighlightConfig() const { return m_highlightConfig; }
    void SetHighlightConfig(const MouseHighlightConfig& config) { m_highlightConfig = config; }

    /**
     * @brief 获取变焦配置
     */
    const ZoomConfig& GetZoomConfig() const { return m_zoomConfig; }
    void SetZoomConfig(const ZoomConfig& config) { m_zoomConfig = config; }

private:
    /// 轮询采样线程
    void PollThreadFunc();

    /// 一阶滞后滤波
    SmoothedMouseState ApplySmoothing(const MouseEvent& raw);

    /// 计算鼠标速度
    void UpdateVelocity(SmoothedMouseState& state, uint64_t nowUs);

private:
    HWND    m_hwnd = nullptr;
    float   m_smoothingFactor = 0.08f;
    std::atomic<bool> m_isRunning{false};
    std::thread       m_pollThread;

    // 回调
    MouseStateCallback m_stateCallback;
    std::mutex         m_callbackMutex;

    // 平滑状态
    SmoothedMouseState m_smoothedState{};
    mutable std::mutex m_stateMutex;

    // 历史采样（用于速度计算）
    struct SamplePoint {
        float    x, y;
        uint64_t timeUs;
    };
    std::deque<SamplePoint> m_sampleHistory;
    static constexpr size_t MAX_HISTORY = 10;

    // 配置
    MouseHighlightConfig m_highlightConfig;
    ZoomConfig           m_zoomConfig;
};

} // namespace mangke
