/**
 * @file MouseInput.cpp
 * @brief 鼠标输入实现 — RawInput + 一阶滞后滤波
 */

#include "MouseInput.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace mangke {

MouseInput::MouseInput() = default;

MouseInput::~MouseInput() {
    Stop();
}

bool MouseInput::Initialize(HWND hwnd, float smoothingFactor) {
    m_hwnd = hwnd;
    m_smoothingFactor = std::clamp(smoothingFactor, 0.01f, 1.0f);

    // 注册 RawInput 设备（如果提供了窗口句柄）
    if (hwnd) {
        RAWINPUTDEVICE rid = {};
        rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
        rid.usUsage     = 0x02; // HID_USAGE_GENERIC_MOUSE
        rid.dwFlags     = RIDEV_INPUTSINK;
        rid.hwndTarget  = hwnd;

        if (!RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE))) {
            std::cerr << "[MouseInput] RawInput 设备注册失败\n";
            return false;
        }
        std::cout << "[MouseInput] RawInput 事件模式已注册\n";
    }

    // 初始化平滑状态
    POINT pt;
    GetCursorPos(&pt);
    m_smoothedState.x = static_cast<float>(pt.x);
    m_smoothedState.y = static_cast<float>(pt.y);
    m_smoothedState.velocityX = 0.0f;
    m_smoothedState.velocityY = 0.0f;
    m_smoothedState.leftDown = false;
    m_smoothedState.rightDown = false;

    return true;
}

void MouseInput::SetStateCallback(MouseStateCallback callback) {
    std::lock_guard lock(m_callbackMutex);
    m_stateCallback = std::move(callback);
}

bool MouseInput::Start() {
    if (m_isRunning.load()) return false;

    m_isRunning.store(true);
    m_pollThread = std::thread(&MouseInput::PollThreadFunc, this);

    std::cout << "[MouseInput] 采样线程已启动 (1000Hz)\n";
    return true;
}

void MouseInput::Stop() {
    if (!m_isRunning.load()) return;

    m_isRunning.store(false);
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }

    std::cout << "[MouseInput] 采样线程已停止\n";
}

void MouseInput::HandleRawInput(HRAWINPUT hRawInput) {
    UINT dataSize = 0;
    GetRawInputData(hRawInput, RID_INPUT, nullptr, &dataSize, sizeof(RAWINPUTHEADER));

    std::vector<uint8_t> data(dataSize);
    if (GetRawInputData(hRawInput, RID_INPUT, data.data(), &dataSize, sizeof(RAWINPUTHEADER)) == dataSize) {
        auto* raw = reinterpret_cast<RAWINPUT*>(data.data());

        if (raw->header.dwType == RIM_TYPEMOUSE) {
            auto nowUs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count());

            POINT pt;
            GetCursorPos(&pt);

            MouseEvent event = {};
            event.x = pt.x;
            event.y = pt.y;
            event.timestampUs = nowUs;

            if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
                event.type = MouseEventType::LeftDown;
                m_smoothedState.leftDown = true;
            } else if (raw->data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP) {
                event.type = MouseEventType::LeftUp;
                m_smoothedState.leftDown = false;
            } else if (raw->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) {
                event.type = MouseEventType::RightDown;
                m_smoothedState.rightDown = true;
            } else if (raw->data.mouse.usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP) {
                event.type = MouseEventType::RightUp;
                m_smoothedState.rightDown = false;
            } else if (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL) {
                event.type = MouseEventType::Wheel;
                event.wheelDelta = static_cast<int16_t>(raw->data.mouse.usButtonData);
            } else {
                event.type = MouseEventType::Move;
            }

            auto smoothed = ApplySmoothing(event);
            UpdateVelocity(smoothed, nowUs);

            {
                std::lock_guard lock(m_stateMutex);
                m_smoothedState = smoothed;
            }

            {
                std::lock_guard lock(m_callbackMutex);
                if (m_stateCallback) {
                    m_stateCallback(smoothed);
                }
            }
        }
    }
}

SmoothedMouseState MouseInput::GetSmoothedState() const {
    std::lock_guard lock(m_stateMutex);
    return m_smoothedState;
}

void MouseInput::PollThreadFunc() {
    using Clock = std::chrono::high_resolution_clock;
    const auto interval = std::chrono::microseconds(1000); // 1000Hz

    while (m_isRunning.load()) {
        auto start = Clock::now();
        auto nowUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                start.time_since_epoch()).count());

        POINT pt;
        GetCursorPos(&pt);

        MouseEvent event = {};
        event.type = MouseEventType::Move;
        event.x = pt.x;
        event.y = pt.y;
        event.timestampUs = nowUs;

        // 检测按键状态
        event.type = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? MouseEventType::LeftDown : MouseEventType::Move;

        auto smoothed = ApplySmoothing(event);
        smoothed.leftDown  = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        smoothed.rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        smoothed.timestampUs = nowUs;

        UpdateVelocity(smoothed, nowUs);

        {
            std::lock_guard lock(m_stateMutex);
            m_smoothedState = smoothed;
        }

        {
            std::lock_guard lock(m_callbackMutex);
            if (m_stateCallback) {
                m_stateCallback(smoothed);
            }
        }

        auto elapsed = Clock::now() - start;
        auto sleepTime = interval - elapsed;
        if (sleepTime > std::chrono::microseconds(0)) {
            std::this_thread::sleep_for(sleepTime);
        }
    }
}

SmoothedMouseState MouseInput::ApplySmoothing(const MouseEvent& raw) {
    // 注意：调用者必须已持有 m_stateMutex 锁
    float alpha = m_smoothingFactor;
    SmoothedMouseState result = m_smoothedState;

    // 一阶滞后滤波: smoothed = alpha * raw + (1-alpha) * prev
    result.x = alpha * static_cast<float>(raw.x) + (1.0f - alpha) * result.x;
    result.y = alpha * static_cast<float>(raw.y) + (1.0f - alpha) * result.y;
    result.timestampUs = raw.timestampUs;

    return result;
}

void MouseInput::UpdateVelocity(SmoothedMouseState& state, uint64_t nowUs) {
    // 添加采样到历史
    m_sampleHistory.push_back({ state.x, state.y, nowUs });
    if (m_sampleHistory.size() > MAX_HISTORY) {
        m_sampleHistory.pop_front();
    }

    // 使用最近两个采样计算速度
    if (m_sampleHistory.size() >= 2) {
        const auto& prev = m_sampleHistory[m_sampleHistory.size() - 2];
        const auto& curr = m_sampleHistory.back();

        float dt = static_cast<float>(curr.timeUs - prev.timeUs) / 1'000'000.0f;
        if (dt > 0.0001f) {
            state.velocityX = (curr.x - prev.x) / dt;
            state.velocityY = (curr.y - prev.y) / dt;
        }
    }
}

} // namespace mangke
