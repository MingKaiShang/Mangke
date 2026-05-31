$root = Split-Path $PSScriptRoot -Parent
$b = Join-Path $root "x64/Debug"
$out = Join-Path $root "dist/Mangke"
if(Test-Path $out){Remove-Item -Recurse -Force $out}
New-Item -ItemType Directory -Path $out -Force > $null
New-Item -ItemType Directory -Path "$out/收款码" -Force > $null
Copy-Item "$b/ControlProcess.exe" $out -Force
Copy-Item "$b/RenderProcess.exe" $out -Force
if(Test-Path "$b/onnxruntime.dll"){Copy-Item "$b/onnxruntime.dll" $out -Force}
if(Test-Path "$b/收款码/微信.jpg"){Copy-Item "$b/收款码/*" "$out/收款码/" -Force}
Write-Host "Packaged: $out"
$s = (Get-ChildItem -Recurse $out | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("Size: {0:N1} MB" -f $s)
