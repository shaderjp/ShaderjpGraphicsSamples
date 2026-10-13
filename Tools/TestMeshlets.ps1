param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repositoryRoot=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$work=Join-Path $repositoryRoot ".cache/meshlet-cpu/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
$sources=@('Tests/MeshletTests.cpp','Common/Workshop.cpp','Common/Gltf.cpp','Common/Application.cpp','Common/Image.cpp')|ForEach-Object{Join-Path $repositoryRoot $_}
$runtime=if($Configuration -eq 'Debug'){'/MDd'}else{'/MD'}
& cl.exe /nologo /std:c++20 /EHsc /W4 /O2 /utf-8 $runtime /DUNICODE /D_UNICODE /DLIGHT_CULLING=1 /DMESH_TECHNIQUE=6 ("/I"+(Join-Path $repositoryRoot 'Common')) ("/I"+(Join-Path $repositoryRoot 'ThirdParty/nlohmann-json/single_include')) ("/I"+(Join-Path $repositoryRoot 'ThirdParty/meshoptimizer/src')) ("/Fo"+$work+'\') ("/Fe"+(Join-Path $work 'MeshletTests.exe')) @sources /link (Join-Path $repositoryRoot "bin/x64/$Configuration/MeshOptimizerCpu/MeshOptimizerCpu.lib") user32.lib shell32.lib ole32.lib windowscodecs.lib
if($LASTEXITCODE-ne 0){throw 'Meshlet CPU test compilation failed'}
& (Join-Path $work 'MeshletTests.exe') $repositoryRoot
if($LASTEXITCODE-ne 0){throw 'Meshlet CPU tests failed'}
