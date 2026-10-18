param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$work=Join-Path $repoRoot ".cache/bindless-cpu/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
$sources=@('Tests/BindlessCpuTests.cpp','Common/Workshop.cpp','Common/Gltf.cpp','Common/Application.cpp','Common/Image.cpp','Common/Mesh.cpp')|ForEach-Object{Join-Path $repoRoot $_}
$runtime=if($Configuration -eq 'Debug'){'/MDd'}else{'/MD'}
& cl.exe /nologo /std:c++20 /EHsc /W4 /O2 /utf-8 $runtime /DUNICODE /D_UNICODE /DGALLERY_TECHNIQUE=13 ("/I"+(Join-Path $repoRoot 'Common')) ("/I"+(Join-Path $repoRoot 'ThirdParty/nlohmann-json/single_include')) ("/I"+(Join-Path $repoRoot 'ThirdParty/meshoptimizer/src')) ("/Fo"+$work+'\') ("/Fe"+(Join-Path $work 'BindlessCpuTests.exe')) @sources /link (Join-Path $repoRoot "bin/x64/$Configuration/MeshOptimizerCpu/MeshOptimizerCpu.lib") user32.lib shell32.lib ole32.lib windowscodecs.lib
if($LASTEXITCODE){throw 'Bindless CPU tests compilation failed'}
& (Join-Path $work 'BindlessCpuTests.exe') $repoRoot
if($LASTEXITCODE){throw 'Bindless CPU tests failed'}
