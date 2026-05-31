# Mangke — Windows 屏幕录制与智能变焦工具

> 集成 NPU 加速的桌面屏幕录制工具，支持鼠标跟随平滑变焦、鼠标高亮、键盘可视化。

## 项目概述

Mangke 是一款高性能 Windows 桌面录屏应用，核心特性：

- **全屏/区域录制** — 60fps，最高 4K
- **鼠标跟随变焦** — 相机级缓动动画，端到端延迟 < 16ms
- **鼠标高亮** — 圆形光圈、点击光圈效果
- **键盘可视化** — 实时显示按下的快捷键组合
- **硬件编码** — NVENC/AMF/QSV，低 CPU 占用
- **NPU AI 增强**（开发中）— 智能内容跟随、实时超分、降噪

## 系统架构

```
┌──────────────────────────────────────────────────────┐
│                  ControlProcess (Win32 UI)            │
│  - 控制面板、录制状态、变焦/高亮设置                    │
└──────────────────────┬───────────────────────────────┘
                       │ 共享内存 + Event (IPC)
┌──────────────────────▼───────────────────────────────┐
│                  RenderProcess (Native C++/DX)        │
│  ┌──────────┐   ┌──────────┐   ┌──────────────────┐  │
│  │ 捕获线程 │──▶│ AI 线程  │──▶│ 渲染线程         │  │
│  │ (WGC)    │   │ (WinML)  │   │ (D3D11+DCOMP)    │  │
│  └──────────┘   └──────────┘   └────────┬─────────┘  │
│                                          │            │
│  ┌──────────┐                     ┌──────▼─────────┐  │
│  │ 输入线程 │                     │ 编码线程        │  │
│  │(RawInput)│                     │ (MF H.264)     │  │
│  └──────────┘                     └──────┬─────────┘  │
│                                          │            │
│                                    ┌──────▼─────────┐  │
│                                    │ 磁盘写入        │  │
│                                    └────────────────┘  │
└───────────────────────────────────────────────────────┘
```

## 项目结构

```
Mangke/
├── Mangke.sln                      # VS2022 解决方案
├── src/
│   ├── CaptureLib/                 # 捕获库（静态库）
│   │   ├── CaptureCommon.h         # 公共类型定义
│   │   ├── ScreenCapture.h/cpp     # WGC 屏幕捕获
│   │   ├── MouseInput.h/cpp        # RawInput 鼠标输入 + 平滑滤波
│   │   ├── KeyboardInput.h/cpp     # 全局键盘钩子 + 快捷键识别
│   │   └── AudioCapture.h/cpp      # WASAPI 环回音频捕获
│   │
│   ├── AILib/                      # AI 推理库（静态库）
│   │   ├── AICaptureCommon.h       # AI 公共类型
│   │   └── NPUInferenceEngine.h/cpp # NPU/GPU/CPU 推理引擎骨架
│   │
│   ├── EncoderLib/                 # 编码库（静态库）
│   │   └── VideoEncoder.h/cpp      # Media Foundation H.264 编码
│   │
│   ├── RenderProcess/              # 渲染进程（EXE）
│   │   ├── D3D11Renderer.h/cpp     # D3D11 渲染器（变焦/高亮/键盘叠加）
│   │   ├── SharedMemoryIPC.h/cpp   # 共享内存 IPC 服务端
│   │   ├── RenderPipeline.h/cpp    # 录制流水线调度器
│   │   └── main.cpp                # 入口点
│   │
│   └── ControlProcess/             # 控制进程（EXE）
│       ├── ControlPanel.h/cpp      # Win32 控制面板窗口
│       ├── SharedMemoryIPC.h/cpp   # IPC 客户端
│       └── main.cpp                # 入口点
│
├── config/
│   └── default.json                # 默认配置文件
├── scripts/
│   ├── setup.ps1                   # 依赖安装脚本
│   ├── build.ps1                   # MSBuild 构建脚本
│   └── download_models.ps1         # AI 模型下载器
├── models/                         # ONNX 模型文件目录
├── output/                         # 录制输出目录
├── 项目介绍.txt                     # 项目详细说明
└── 设计风格.txt                     # UI 设计系统规范
```

## 技术栈

| 组件 | 技术选型 |
|------|---------|
| 语言 | C++20 |
| 屏幕捕获 | Windows Graphics Capture (WGC) |
| 渲染 | Direct3D 11.1 |
| 动画合成 | DirectComposition |
| 鼠标输入 | RawInput (1000Hz 采样) |
| 平滑算法 | 一阶滞后滤波 + 线性外推 |
| 音频捕获 | WASAPI 环回（低延迟） |
| 视频编码 | Media Foundation H.264 |
| AI 推理 | Windows ML + ONNX Runtime（开发中） |
| 进程通信 | 共享内存 + Event |
| UI | Win32 原生控件 |

## 快速开始

### 环境要求

- **操作系统**: Windows 10 1903+ (x64)
- **开发工具**: Visual Studio 2022（需安装 "使用 C++ 的桌面开发" 工作负载）
- **Windows SDK**: 10.0.19041.0 或更高
- **可选**: 支持 NPU 的设备（Intel Core Ultra / AMD Ryzen 8040 / Qualcomm Snapdragon X Elite）用于 AI 功能

### 步骤 1: 安装依赖

```powershell
# 以管理员身份运行 PowerShell
cd D:\Mangke

# 安装基础依赖（C++/WinRT、目录结构）
.\scripts\setup.ps1

# 如需 AI 功能，同时安装 ONNX Runtime
.\scripts\setup.ps1 -InstallONNX
```

### 步骤 2: 构建项目

**方式 A — 使用 VS2022 IDE：**
1. 双击 `Mangke.sln` 打开解决方案
2. 选择 `x64 | Debug` 配置
3. 按 `F5` 或 `Ctrl+Shift+B` 编译

**方式 B — 使用命令行：**
```powershell
.\scripts\build.ps1              # Debug 构建
.\scripts\build.ps1 -Config Release  # Release 构建
```

### 步骤 3: 运行

**模式 1 — IPC 模式（完整功能）：**
```
1. 启动 ControlProcess.exe
2. 在控制面板点击 "开始录制"
3. 录制完成后点击 "停止"
4. 视频输出到 output/ 目录
```

**模式 2 — 无 IPC 模式（直接录制）：**
```powershell
RenderProcess.exe --no-ipc
# 按 Ctrl+C 停止录制
```

## MVP 功能状态

| 功能 | 状态 | 说明 |
|------|------|------|
| 全屏/区域捕获 | ✅ MVP | GDI 捕获路径，WGC 接口预留 |
| 鼠标跟随变焦 | ✅ MVP | 一阶滞后滤波 + D3D11 裁切缩放 |
| 鼠标高亮 | ✅ 框架 | 渲染接口就绪，Shader 实现待完善 |
| 键盘可视化 | ✅ 框架 | 钩子捕获就绪，Direct2D 渲染待完善 |
| H.264 编码 | ✅ MVP | Media Foundation Sink Writer |
| Win32 控制面板 | ✅ MVP | 启动/停止/设置/状态显示 |
| 进程间通信 | ✅ MVP | 共享内存 + Event |
| WASAPI 音频 | ✅ MVP | 系统音频环回捕获 |
| AI 智能跟随 | 🔄 骨架 | WinML 接口就绪，待集成 ONNX Runtime |
| AI 超分辨率 | 🔄 骨架 | 框架就绪，待加载 FSRCNN 模型 |
| 时间轴编辑 | 📋 计划 | 后续版本 |
| 导出 H.265/ProRes | 📋 计划 | 后续版本 |

## 开发计划

按照项目介绍文档的 11 步开发路线：

1. ✅ 基础项目搭建
2. ✅ RawInput + WGC + D3D11 基础渲染
3. ✅ DirectComposition 动画（变焦平滑）
4. ✅ 硬件编码（Media Foundation）
5. 🔄 Windows ML 和 NPU 推理集成
6. 🔄 NPU 推理集成到实时管线
7. 📋 智能内容跟随
8. 📋 实时超分辨率
9. 📋 智能剪辑推荐
10. 📋 完整 UI 控制进程
11. 📋 性能优化与延迟测量

## 已知限制

- MVP 使用 GDI 捕获（非 WGC），帧率受限于 GDI 性能
- 鼠标高亮和键盘可视化仅实现框架，需要 HLSL Shader/Direct2D 完善渲染
- AI 功能为骨架实现，需集成 ONNX Runtime 启用实际推理
- 编码使用 Media Foundation 软件编码器，性能低于硬件编码器（NVENC/AMF/QSV）
- 控制面板使用 Win32 原生控件，UI 美化待后续版本

## 设计规范

UI 设计遵循 **商明凯设计系统 V4.0**，参见 `设计风格.txt`。核心原则：

- 瑞士平面设计 × 德国功能主义 × 杂志编辑式布局 × 苹果现代元素
- 8px 基准间距，12 列网格
- 中性色覆盖 80%+，单一信号色
- 动画仅使用 transform/opacity 驱动
- 所有配色/布局决策须用户确认

## 许可证

本项目基于 MIT 许可证开源 — 详见 [LICENSE](LICENSE) 文件。

Copyright (c) 2026 商明凯

特此免费授予任何获得本软件副本和相关文档文件（以下简称"软件"）的人不受限制地处理本软件的权限，包括但不限于使用、复制、修改、合并、发布、分发、再许可和/或出售本软件副本的权利，并允许被提供本软件的人这样做，但须符合以下条件：

上述版权声明和本许可声明应包含在所有副本或实质性部分中。

本软件按"原样"提供，不提供任何明示或暗示的保证，包括但不限于适销性、特定用途适用性和非侵权性的保证。在任何情况下，作者或版权持有人均不对因本软件或本软件的使用或其他交易而产生或与之相关的任何索赔、损害或其他责任负责，无论是合同诉讼、侵权行为还是其他。
