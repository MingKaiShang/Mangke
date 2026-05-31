/**
 * @file KeyboardInput.h
 * @brief 键盘快捷键可视化输入捕获
 *
 * 捕获全局键盘事件，识别快捷键组合，
 * 输出可视化数据供渲染层显示。
 */

#pragma once

#include "CaptureCommon.h"
#include <Windows.h>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

namespace mangke {

/// 键盘事件回调
using KeyboardEventCallback = std::function<void(const KeyboardShortcut& shortcut)>;

/**
 * @class KeyboardInput
 * @brief 全局键盘输入捕获与快捷键识别
 */
class KeyboardInput {
public:
    KeyboardInput();
    ~KeyboardInput();

    KeyboardInput(const KeyboardInput&) = delete;
    KeyboardInput& operator=(const KeyboardInput&) = delete;

    /**
     * @brief 安装全局键盘钩子
     * @return 成功返回 true
     */
    bool Initialize();

    /**
     * @brief 设置键盘事件回调
     */
    void SetEventCallback(KeyboardEventCallback callback);

    /**
     * @brief 设置可视化配置
     */
    void SetConfig(const KeyboardVisConfig& config) { m_config = config; }
    const KeyboardVisConfig& GetConfig() const { return m_config; }

    /**
     * @brief 获取当前活动的快捷键列表
     */
    std::vector<KeyboardShortcut> GetActiveShortcuts() const;

    /**
     * @brief 卸载钩子
     */
    bool IsInitialized() const { return m_hook != nullptr; }
    void Uninitialize();

private:
    /// 键盘钩子回调
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    /// 处理按键事件
    void ProcessKeyEvent(WPARAM wParam, KBDLLHOOKSTRUCT* kbInfo);

    /// 将虚拟键码转为显示文本
    static std::wstring VkToDisplayText(uint32_t vkCode);

    /// 构建快捷键显示文本
    std::wstring BuildShortcutText(const KeyboardShortcut& shortcut);

private:
    static KeyboardInput* s_instance; // 全局钩子需要静态实例

    HHOOK m_hook = nullptr;
    KeyboardEventCallback m_eventCallback;
    std::mutex m_callbackMutex;

    KeyboardVisConfig m_config;

    // 当前按键状态
    struct KeyState {
        uint32_t vkCode;
        bool     isDown;
        uint64_t pressTimeUs;
    };
    std::vector<KeyState> m_activeKeys;
    mutable std::mutex    m_keysMutex;

    // 修饰键状态
    bool m_ctrlDown  = false;
    bool m_shiftDown = false;
    bool m_altDown   = false;
    bool m_winDown   = false;
};

} // namespace mangke
