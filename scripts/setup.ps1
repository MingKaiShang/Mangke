#Requires -Version 5.1
<#
.SYNOPSIS
    Mangke 项目依赖安装脚本
.DESCRIPTION
    自动下载和安装项目所需的开发依赖：
    - Visual Studio 2022 Build Tools
    - Windows SDK
    - C++/WinRT 投影头文件
    - ONNX Runtime（可选，用于 AI 功能）

.NOTES
    需要以管理员权限运行
#>

param(
    [switch]$InstallONNX,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Mangke 依赖安装脚本" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 检查管理员权限
function Test-Admin {
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# 检查 Visual Studio 2022
function Test-VS2022 {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $install = & $vsWhere -version "[17.0,18.0)" -format json | ConvertFrom-Json
        return $install.Count -gt 0
    }
    return $false
}

# 检查 Windows SDK
function Test-WindowsSDK {
    $sdkPath = "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots"
    if (Test-Path $sdkPath) {
        $kits = Get-ItemProperty $sdkPath
        return $null -ne $kits.KitsRoot10
    }
    return $false
}

# 安装 C++/WinRT 投影头文件
function Install-CppWinRT {
    Write-Host "[1/4] 安装 C++/WinRT 投影头文件..." -ForegroundColor Yellow

    $nugetPath = "nuget.exe"
    if (-not (Get-Command $nugetPath -ErrorAction SilentlyContinue)) {
        Write-Host "  下载 NuGet CLI..." -ForegroundColor Gray
        Invoke-WebRequest -Uri "https://dist.nuget.org/win-x86-commandline/latest/nuget.exe" -OutFile ".\nuget.exe"
        $nugetPath = ".\nuget.exe"
    }

    $packagesDir = ".\packages"
    if (-not (Test-Path $packagesDir)) {
        New-Item -ItemType Directory -Path $packagesDir | Out-Null
    }

    & $nugetPath install Microsoft.Windows.CppWinRT -OutputDirectory $packagesDir

    Write-Host "  C++/WinRT 安装完成" -ForegroundColor Green
}

# 安装 ONNX Runtime
function Install-ONNXRuntime {
    Write-Host "[2/4] 安装 ONNX Runtime..." -ForegroundColor Yellow

    $nugetPath = "nuget.exe"
    if (-not (Get-Command $nugetPath -ErrorAction SilentlyContinue)) {
        $nugetPath = ".\nuget.exe"
    }

    $packagesDir = ".\packages"
    & $nugetPath install Microsoft.ML.OnnxRuntime.DirectML -OutputDirectory $packagesDir
    & $nugetPath install Microsoft.ML.OnnxRuntime -OutputDirectory $packagesDir

    Write-Host "  ONNX Runtime 安装完成" -ForegroundColor Green
}

# 下载 AI 模型
function Download-Models {
    Write-Host "[3/4] 下载 AI 模型..." -ForegroundColor Yellow

    $modelsDir = ".\models"
    if (-not (Test-Path $modelsDir)) {
        New-Item -ItemType Directory -Path $modelsDir | Out-Null
    }

    # YOLOv4-Tiny（目标检测）
    $yoloUrl = "https://github.com/onnx/models/raw/main/validated/vision/object_detection_segmentation/yolov4/model/yolov4-tiny-416.onnx"
    $yoloPath = Join-Path $modelsDir "yolov4-tiny-416.onnx"
    if (-not (Test-Path $yoloPath)) {
        Write-Host "  下载 YOLOv4-Tiny..." -ForegroundColor Gray
        try {
            Invoke-WebRequest -Uri $yoloUrl -OutFile $yoloPath -TimeoutSec 120
        } catch {
            Write-Host "  警告: YOLOv4-Tiny 下载失败，需要手动下载" -ForegroundColor Yellow
        }
    }

    # FSRCNN（超分辨率）
    $fsrcnnUrl = "https://github.com/onnx/models/raw/main/validated/vision/super_resolution/sub_pixel_cnn_2016/model/super-resolution-10.onnx"
    $fsrcnnPath = Join-Path $modelsDir "fsrcnn-superres.onnx"
    if (-not (Test-Path $fsrcnnPath)) {
        Write-Host "  下载 FSRCNN 超分模型..." -ForegroundColor Gray
        try {
            Invoke-WebRequest -Uri $fsrcnnUrl -OutFile $fsrcnnPath -TimeoutSec 120
        } catch {
            Write-Host "  警告: FSRCNN 下载失败，需要手动下载" -ForegroundColor Yellow
        }
    }

    Write-Host "  模型下载完成（放置在 models/ 目录）" -ForegroundColor Green
}

# 创建项目目录结构
function Initialize-ProjectDirs {
    Write-Host "[4/4] 创建项目目录..." -ForegroundColor Yellow

    $dirs = @("output", "models", "packages", "build")
    foreach ($dir in $dirs) {
        if (-not (Test-Path $dir)) {
            New-Item -ItemType Directory -Path $dir | Out-Null
        }
    }

    Write-Host "  目录结构已创建" -ForegroundColor Green
}

# 主流程
Write-Host "检查开发环境..." -ForegroundColor Cyan

if (-not (Test-VS2022)) {
    Write-Host "  警告: 未检测到 Visual Studio 2022" -ForegroundColor Yellow
    Write-Host "  请安装 VS2022 并勾选 '使用 C++ 的桌面开发' 工作负载" -ForegroundColor Yellow
    Write-Host "  下载地址: https://visualstudio.microsoft.com/downloads/" -ForegroundColor Yellow
    Write-Host ""
}

if (-not (Test-WindowsSDK)) {
    Write-Host "  警告: 未检测到 Windows SDK" -ForegroundColor Yellow
    Write-Host "  请安装 Windows 10 SDK 或 Windows 11 SDK" -ForegroundColor Yellow
    Write-Host ""
}

Write-Host ""
Install-CppWinRT

if ($InstallONNX) {
    Install-ONNXRuntime
    Download-Models
} else {
    Write-Host "[2/4] 跳过 ONNX Runtime 安装（使用 -InstallONNX 启用）" -ForegroundColor Gray
    Write-Host "[3/4] 跳过 AI 模型下载" -ForegroundColor Gray
}

Initialize-ProjectDirs

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  依赖安装完成！" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "下一步：" -ForegroundColor Cyan
Write-Host "  1. 打开 Mangke.sln（使用 VS2022）" -ForegroundColor White
Write-Host "  2. 选择 x64 | Debug 配置" -ForegroundColor White
Write-Host "  3. 按 F5 编译并运行" -ForegroundColor White
Write-Host ""
