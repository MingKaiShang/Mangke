/**
 * @file KeyboardInput.cpp
 * @brief 键盘输入实现 — 低级键盘钩子
 */

#include "KeyboardInput.h"
#include <chrono>
#include <iostream>
#include <algorithm>

namespace mangke {

KeyboardInput* KeyboardInput::s_instance = nullptr;

KeyboardInput::KeyboardInput() {
    s_instance = this;
}

KeyboardInput::~KeyboardInput() {
    Uninitialize();
    s_instance = nullptr;
}

bool KeyboardInput::Initialize() {
    if (m_hook) return true;

    m_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        GetModuleHandleW(nullptr),
        0
    );

    if (!m_hook) {
        std::cerr << "[Keyboard] 安装键盘钩子失败 (错误: " << GetLastError() << ")\n";
        return false;
    }

    std::cout << "[Keyboard] 全局键盘钩子已安装\n";
    return true;
}

void KeyboardInput::SetEventCallback(KeyboardEventCallback callback) {
    std::lock_guard lock(m_callbackMutex);
    m_eventCallback = std::move(callback);
}

std::vector<KeyboardShortcut> KeyboardInput::GetActiveShortcuts() const {
    std::lock_guard lock(m_keysMutex);
    auto nowUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count());

    std::vector<KeyboardShortcut> result;
    for (const auto& key : m_activeKeys) {
        if (key.isDown) {
            KeyboardShortcut sc = {};
            sc.vkCode = key.vkCode;
            sc.ctrl   = m_ctrlDown;
            sc.shift  = m_shiftDown;
            sc.alt    = m_altDown;
            sc.win    = m_winDown;
            sc.timestampUs = key.pressTimeUs;
            sc.durationUs  = nowUs - key.pressTimeUs;

            // 构建显示文本
            std::wstring text;
            if (sc.ctrl)  text += L"Ctrl + ";
            if (sc.shift) text += L"Shift + ";
            if (sc.alt)   text += L"Alt + ";
            if (sc.win)   text += L"Win + ";
            text += VkToDisplayText(sc.vkCode);

            wcsncpy_s(sc.displayText, text.c_str(), _TRUNCATE);
            result.push_back(sc);
        }
    }
    return result;
}

void KeyboardInput::Uninitialize() {
    if (m_hook) {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
        std::cout << "[Keyboard] 键盘钩子已卸载\n";
    }
}

LRESULT CALLBACK KeyboardInput::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && s_instance) {
        auto* kbInfo = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        s_instance->ProcessKeyEvent(wParam, kbInfo);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void KeyboardInput::ProcessKeyEvent(WPARAM wParam, KBDLLHOOKSTRUCT* kbInfo) {
    bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isKeyUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);
    uint32_t vk = kbInfo->vkCode;

    auto nowUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count());

    // 更新修饰键状态
    switch (vk) {
        case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: m_ctrlDown  = isKeyDown; break;
        case VK_LSHIFT:   case VK_RSHIFT:   case VK_SHIFT:   m_shiftDown = isKeyDown; break;
        case VK_LMENU:    case VK_RMENU:    case VK_MENU:     m_altDown   = isKeyDown; break;
        case VK_LWIN:     case VK_RWIN:                       m_winDown   = isKeyDown; break;
    }

    // 跳过纯修饰键
    if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL ||
        vk == VK_LSHIFT   || vk == VK_RSHIFT   || vk == VK_SHIFT   ||
        vk == VK_LMENU    || vk == VK_RMENU    || vk == VK_MENU    ||
        vk == VK_LWIN     || vk == VK_RWIN) {
        return;
    }

    {
        std::lock_guard lock(m_keysMutex);

        if (isKeyDown) {
            // 检查是否已存在
            bool found = false;
            for (auto& key : m_activeKeys) {
                if (key.vkCode == vk) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_activeKeys.push_back({ vk, true, nowUs });
            }
        } else if (isKeyUp) {
            // 移除按键
            m_activeKeys.erase(
                std::remove_if(m_activeKeys.begin(), m_activeKeys.end(),
                    [vk](const KeyState& k) { return k.vkCode == vk; }),
                m_activeKeys.end()
            );
        }
    }

    // 仅在按键事件（非纯修饰键变化）时通知
    if (isKeyDown && (m_ctrlDown || m_shiftDown || m_altDown || m_winDown)) {
        KeyboardShortcut shortcut = {};
        shortcut.vkCode = vk;
        shortcut.ctrl   = m_ctrlDown;
        shortcut.shift  = m_shiftDown;
        shortcut.alt    = m_altDown;
        shortcut.win    = m_winDown;
        shortcut.timestampUs = nowUs;

        std::wstring text;
        if (m_ctrlDown)  text += L"Ctrl + ";
        if (m_shiftDown) text += L"Shift + ";
        if (m_altDown)   text += L"Alt + ";
        if (m_winDown)   text += L"Win + ";
        text += VkToDisplayText(vk);
        wcsncpy_s(shortcut.displayText, text.c_str(), _TRUNCATE);

        std::lock_guard lock(m_callbackMutex);
        if (m_eventCallback) {
            m_eventCallback(shortcut);
        }
    }
}

std::wstring KeyboardInput::VkToDisplayText(uint32_t vkCode) {
    // 特殊键映射
    switch (vkCode) {
        case VK_RETURN:   return L"Enter";
        case VK_BACK:     return L"Backspace";
        case VK_TAB:      return L"Tab";
        case VK_ESCAPE:   return L"Esc";
        case VK_SPACE:    return L"Space";
        case VK_LEFT:     return L"←";    // ←
        case VK_UP:       return L"↑";    // ↑
        case VK_RIGHT:    return L"→";    // →
        case VK_DOWN:     return L"↓";    // ↓
        case VK_DELETE:   return L"Delete";
        case VK_INSERT:   return L"Insert";
        case VK_HOME:     return L"Home";
        case VK_END:      return L"End";
        case VK_PRIOR:    return L"PageUp";
        case VK_NEXT:     return L"PageDown";
        case VK_SNAPSHOT: return L"PrtSc";
        case VK_F1:  return L"F1";
        case VK_F2:  return L"F2";
        case VK_F3:  return L"F3";
        case VK_F4:  return L"F4";
        case VK_F5:  return L"F5";
        case VK_F6:  return L"F6";
        case VK_F7:  return L"F7";
        case VK_F8:  return L"F8";
        case VK_F9:  return L"F9";
        case VK_F10: return L"F10";
        case VK_F11: return L"F11";
        case VK_F12: return L"F12";
        default: break;
    }

    // 字母键 A-Z
    if (vkCode >= 'A' && vkCode <= 'Z') {
        return std::wstring(1, static_cast<wchar_t>(vkCode));
    }

    // 数字键 0-9
    if (vkCode >= '0' && vkCode <= '9') {
        return std::wstring(1, static_cast<wchar_t>(vkCode));
    }

    // 未知键
    wchar_t buf[16];
    swprintf_s(buf, L"[%u]", vkCode);
    return buf;
}

} // namespace mangke
