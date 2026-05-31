/**
 * @file CaptureCommon.h
 * @brief 公共类型定义与常量 — CaptureLib 模块
 *
 * 定义屏幕捕获、鼠标输入、键盘输入模块共用的数据结构。
 * 符合 C++20 规范。
 */

#pragma once

#include <cstdint>
#include <functional>
#include <Windows.h>

namespace mangke {

/// 鼠标事件类型
enum class MouseEventType : uint8_t {
    Move,
    LeftDown,
    LeftUp,
    RightDown,
    RightUp,
    MiddleDown,
    MiddleUp,
    Wheel,
    XButtonDown,
    XButtonUp
};

/// 鼠标事件数据（从 RawInput 采样）
struct MouseEvent {
    MouseEventType type;
    int32_t         x;          // 屏幕坐标 X
    int32_t         y;          // 屏幕坐标 Y
    int32_t         wheelDelta; // 滚轮增量（仅 Wheel 类型有效）
    uint64_t        timestampUs;// 微秒时间戳
};

/// 平滑后的鼠标状态（输出给渲染线程）
struct SmoothedMouseState {
    float    x;            // 平滑后 X（像素）
    float    y;            // 平滑后 Y（像素）
    float    velocityX;    // X 方向速度（像素/秒）
    float    velocityY;    // Y 方向速度（像素/秒）
    bool     leftDown;
    bool     rightDown;
    uint64_t timestampUs;
};

/// 键盘快捷键可视化数据
struct KeyboardShortcut {
    uint32_t vkCode;       // 虚拟键码
    bool     ctrl;
    bool     shift;
    bool     alt;
    bool     win;
    wchar_t  displayText[64]; // 显示文本，如 "Ctrl + C"
    uint64_t timestampUs;
    uint64_t durationUs;      // 按键持续时间
};

/// 捕获区域定义
struct CaptureRegion {
    bool     fullscreen = true;  // 全屏捕获
    int32_t  x = 0;
    int32_t  y = 0;
    uint32_t width  = 0;
    uint32_t height = 0;
    HMONITOR monitor = nullptr;  // 指定显示器（nullptr = 主显示器）
};

/// 捕获配置
struct CaptureConfig {
    uint32_t targetFps      = 60;
    uint32_t targetWidth    = 1920;
    uint32_t targetHeight   = 1080;
    bool     captureMouse   = true;
    bool     captureAudio   = true;
    int      monitorIndex   = 0;      // 显示器索引（0=主显示器）
    CaptureRegion region;
};

/// 鼠标高亮样式
enum class MouseHighlightStyle : uint8_t {
    None,           // 不高亮
    Circle,         // 圆形光圈
    Crosshair,      // 十字准星
    Spotlight       // 聚光灯
};

/// 鼠标高亮配置
struct MouseHighlightConfig {
    MouseHighlightStyle style     = MouseHighlightStyle::Circle;
    float               radius    = 30.0f;   // 像素
    float               opacity   = 0.4f;
    uint32_t            color     = 0xFF0071E3; // ARGB 默认蓝
    bool                showClick = true;     // 点击时显示光圈
    float               clickRadius = 50.0f;  // 点击光圈最大半径
};

/// 变焦（zoom）配置
struct ZoomConfig {
    bool  enabled         = true;
    float minZoom         = 1.0f;    // 最小倍率
    float maxZoom         = 3.0f;    // 最大倍率
    float currentZoom     = 1.5f;    // 当前目标倍率
    float smoothingFactor = 0.08f;   // 平滑系数 (0~1, 越小越平滑)
    float followSpeed     = 0.12f;   // 跟随速度
    bool  autoZoom        = true;    // 鼠标静止时自动放大
    uint32_t autoZoomDelayMs = 800;  // 静止多久后自动放大
};

/// 键盘可视化配置
struct KeyboardVisConfig {
    bool     enabled       = true;
    float    opacity       = 0.85f;
    uint32_t displayDurationMs = 2000; // 显示持续时间
    uint32_t maxComboKeys  = 4;       // 最多同时显示键数
    float    fontSize      = 18.0f;
};

} // namespace mangke
