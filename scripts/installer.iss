[Setup]
AppName=Mangke 屏幕录制
AppVersion=1.0.0
AppPublisher=Mangke
DefaultDirName={autopf}\Mangke
DefaultGroupName=Mangke 屏幕录制
UninstallDisplayIcon={app}\ControlProcess.exe
Compression=lzma2
SolidCompression=yes
OutputDir={#SourcePath}\..\installer
OutputBaseFilename=Mangke_Setup
PrivilegesRequired=admin
[Files]
Source: "{#SourcePath}\..\x64\Debug\ControlProcess.exe"; DestDir: "{app}"
Source: "{#SourcePath}\..\x64\Debug\RenderProcess.exe"; DestDir: "{app}"
Source: "{#SourcePath}\..\x64\Debug\onnxruntime.dll"; DestDir: "{app}"
Source: "{#SourcePath}\..\x64\Debug\收款码\*"; DestDir: "{app}\收款码"

[Icons]
Name: "{group}\Mangke 屏幕录制"; Filename: "{app}\ControlProcess.exe"
Name: "{commondesktop}\Mangke 屏幕录制"; Filename: "{app}\ControlProcess.exe"
Name: "{group}\卸载 Mangke"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\ControlProcess.exe"; Description: "运行 Mangke"; Flags: postinstall nowait skipifsilent
