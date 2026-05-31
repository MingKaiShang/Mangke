#Requires -Version 5.1
param([string]$Models="all")
$modelsDir = Join-Path $PSScriptRoot ".." "models"
if(!(Test-Path $modelsDir)){New-Item -ItemType Directory -Path $modelsDir|Out-Null}

$list = @(
    @{Name="YOLOv4-Tiny";File="yolov4-tiny-416.onnx";Url="https://github.com/onnx/models/raw/main/validated/vision/object_detection_segmentation/yolov4/model/yolov4-tiny-416.onnx";Size="23MB";Key="yolo"},
    @{Name="FSRCNN";File="fsrcnn-superres.onnx";Url="https://github.com/onnx/models/raw/main/validated/vision/super_resolution/sub_pixel_cnn_2016/model/super-resolution-10.onnx";Size="64KB";Key="fsrcnn"},
    @{Name="DeepLabV3";File="deeplabv3-mobilenetv2.onnx";Url="https://github.com/onnx/models/raw/main/validated/vision/object_detection_segmentation/deeplabv3/model/deeplab-mobilenetv2-10.onnx";Size="8MB";Key="deeplab"}
)

$downloadAll = $Models -eq "all"
foreach($m in $list){
    if($downloadAll -or $m.Key -in $Models){
        $out = Join-Path $modelsDir $m.File
        if(Test-Path $out){Write-Host "[SKIP] $($m.Name) exists"; continue}
        Write-Host "[DL] $($m.Name) ($($m.Size))..." -ForegroundColor Yellow
        try{[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $m.Url -OutFile $out -TimeoutSec 300
            Write-Host "[OK] $([math]::Round((Get-Item $out).Length/1MB,1)) MB" -ForegroundColor Green
        }catch{Write-Host "[FAIL] $($m.Name). Manual: $($m.Url)" -ForegroundColor Red}
    }
}
