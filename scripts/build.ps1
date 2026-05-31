#Requires -Version 5.1
<#
.SYNOPSIS
    Mangke 项目 MSBuild 构建脚本
.DESCRIPTION
    调用 Visual Studio 2022 的 MSBuild 编译整个解决方案。
    也可直接在 VS2022 IDE 中按 F5 构建。

.PARAMETER Config
    构建配置：Debug 或 Release（默认 Debug）

.PARAMETER Clean
    清理后再构建

.EXAMPLE
    .\build.ps1                  # Debug 构建
    .\build.ps1 -Config Release  # Release 构建
    .\build.ps1 -Clean           # 清理并构建
#>

param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [switch]$Clean
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Mangke 构建脚本 [$Config]" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 下载 AI 模型
$modelDir = Join-Path $PSScriptRoot ".." "models"
$modelFile = Join-Path $modelDir "yolov4-tiny-416.onnx"
if (-not (Test-Path $modelFile)) {
    Write-Host "[AI] 下载 YOLO 模型..." -ForegroundColor Yellow
    if (-not (Test-Path $modelDir)) { New-Item -ItemType Directory -Path $modelDir | Out-Null }
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri "https://github.com/onnx/models/raw/main/validated/vision/object_detection_segmentation/yolov4/model/yolov4-tiny-416.onnx" -OutFile $modelFile -TimeoutSec 300
        Write-Host "[AI] 模型下载完成" -ForegroundColor Green
    } catch {
        Write-Host "[AI] 模型下载失败（不影响编译，AI功能需模型文件）" -ForegroundColor Yellow
    }
} else {
    Write-Host "[AI] 模型已存在" -ForegroundColor Green
}

# 查找 MSBuild
function Find-MSBuild {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $installPath = & $vsWhere -version "[17.0,18.0)" -property installationPath -latest
        if ($installPath) {
            $msbuild = Join-Path $installPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path $msbuild) {
                return $msbuild
            }
        }
    }

    # 回退到 PATH 中的 MSBuild
    $msbuild = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($msbuild) {
        return $msbuild.Source
    }

    return $null
}

$msbuild = Find-MSBuild
if (-not $msbuild) {
    Write-Host "错误: 找不到 MSBuild，请安装 Visual Studio 2022" -ForegroundColor Red
    exit 1
}

Write-Host "MSBuild: $msbuild" -ForegroundColor Gray

$solutionFile = Join-Path $PSScriptRoot ".." "Mangke.sln"
$solutionFile = Resolve-Path $solutionFile

Write-Host "解决方案: $solutionFile" -ForegroundColor Gray
Write-Host ""

# 构建参数
$buildArgs = @(
    $solutionFile,
    "/p:Configuration=$Config",
    "/p:Platform=x64",
    "/m",                       # 并行构建
    "/verbosity:minimal"
)

if ($Clean) {
    Write-Host "清理..." -ForegroundColor Yellow
    & $msbuild $solutionFile /t:Clean /p:Configuration=$Config /p:Platform=x64 /verbosity:minimal
    if ($LASTEXITCODE -ne 0) {
        Write-Host "清理失败" -ForegroundColor Red
        exit 1
    }
    Write-Host ""
}

Write-Host "开始构建..." -ForegroundColor Yellow
Write-Host ""

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

& $msbuild @buildArgs

$exitCode = $LASTEXITCODE
$stopwatch.Stop()

Write-Host ""
if ($exitCode -eq 0) {
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "  构建成功！" -ForegroundColor Green
    Write-Host "  耗时: $($stopwatch.Elapsed.ToString('mm\:ss'))" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""

    $outputDir = Join-Path $PSScriptRoot ".." "x64" $Config
    Write-Host "输出目录: $outputDir" -ForegroundColor Cyan

    # 可选：构建安装包
    $issCompiler = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    if (Test-Path $issCompiler) {
        Write-Host "`n构建安装包..." -ForegroundColor Yellow
        $iss = Join-Path $PSScriptRoot "installer.iss"
        & $issCompiler $iss | Out-Host
        Write-Host "安装包完成" -ForegroundColor Green
    } else {
        Write-Host "`n[跳过] 未安装 Inno Setup，跳过安装包构建" -ForegroundColor Gray
        Write-Host "下载: https://jrsoftware.org/isdl.php" -ForegroundColor Gray
    }
    Write-Host ""
    Write-Host "  方式 1（IPC 模式）:" -ForegroundColor White
    Write-Host "    1. 启动 ControlProcess.exe" -ForegroundColor White
    Write-Host "    2. 点击 '开始录制'" -ForegroundColor White
    Write-Host ""
    Write-Host "  方式 2（无 IPC 模式）:" -ForegroundColor White
    Write-Host "    运行 RenderProcess.exe --no-ipc" -ForegroundColor White
    Write-Host "    按 Ctrl+C 停止录制" -ForegroundColor White
} else {
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "  构建失败 (错误码: $exitCode)" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
}

exit $exitCode
