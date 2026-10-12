$ErrorActionPreference='Stop'
$repositoryRoot=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$output=Join-Path $repositoryRoot '.cache/camera-navigation-cpu'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$sources=@('Tests/CameraNavigationTests.cpp','ThirdParty/imgui/imgui.cpp','ThirdParty/imgui/imgui_draw.cpp','ThirdParty/imgui/imgui_tables.cpp','ThirdParty/imgui/imgui_widgets.cpp') | ForEach-Object {Join-Path $repositoryRoot $_}
& cl.exe /nologo /std:c++20 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE ("/I"+(Join-Path $repositoryRoot 'Common')) ("/I"+(Join-Path $repositoryRoot 'ThirdParty/imgui')) ("/Fo"+$output+'\') ("/Fe"+(Join-Path $output 'CameraNavigationTests.exe')) @sources /link user32.lib imm32.lib
if($LASTEXITCODE -ne 0){throw 'Camera CPU test compilation failed'}
& (Join-Path $output 'CameraNavigationTests.exe')
if($LASTEXITCODE -ne 0){throw 'Camera CPU tests failed'}
