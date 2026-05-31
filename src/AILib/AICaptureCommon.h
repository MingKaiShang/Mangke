/**
 * @file AICaptureCommon.h
 * @brief AI 模块公共类型定义
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>

namespace mangke {

/// 推理设备类型
enum class InferenceDevice : uint8_t {
    NPU,    // 专用神经网络处理器
    GPU,    // 通过 DirectML
    CPU,    // 通过 ONNX Runtime CPU
    Auto    // 自动选择最佳设备
};

/// 检测到的边界框
struct BoundingBox {
    float x;        // 左上角 X（归一化 0~1）
    float y;        // 左上角 Y
    float width;    // 宽度
    float height;   // 高度
    float confidence; // 置信度
    uint32_t classId; // 类别 ID
    char className[32]; // 类别名称
};

/// 兴趣区域（用于智能跟随）
struct InterestRegion {
    float centerX;   // 中心 X（归一化 0~1）
    float centerY;   // 中心 Y
    float width;     // 区域宽度（归一化）
    float height;    // 区域高度
    float confidence;
    uint64_t timestampUs;
};

/// AI 功能开关
struct AIFeatureFlags {
    bool contentFollowing = false;  // 智能内容跟随
    bool superResolution  = false;  // 实时超分辨率
    bool denoising        = false;  // 实时降噪
    bool backgroundBlur   = false;  // 背景虚化
    bool clipRecommendation = false; // 智能剪辑推荐
};

/// 推理设备信息
struct InferenceDeviceInfo {
    InferenceDevice type;
    std::wstring    name;
    uint64_t        dedicatedMemoryMB; // 专用显存
    bool            available;
    std::string     description;
};

} // namespace mangke
