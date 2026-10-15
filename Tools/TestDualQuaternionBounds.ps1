$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$out=Join-Path $root '.cache/skinning/dqs-bounds'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$library=Join-Path $root 'bin/x64/Debug/MeshOptimizerCpu/MeshOptimizerCpu.lib'
if(-not (Test-Path -LiteralPath $library)){throw 'Build the Debug solution first (MeshOptimizerCpu is required).'}
$sources=@('Tests/DualQuaternionBoundsTests.cpp','Common/SkinnedGltf.cpp','Common/Application.cpp') | ForEach-Object {Join-Path $root $_}
& cl.exe /nologo /std:c++20 /EHsc /W4 /utf-8 /MDd /D_DEBUG /DUNICODE /D_UNICODE ("/I"+(Join-Path $root 'Common')) ("/I"+(Join-Path $root 'ThirdParty/meshoptimizer/src')) ("/I"+(Join-Path $root 'ThirdParty/nlohmann-json/single_include')) ("/Fo"+$out+'\') ("/Fe"+(Join-Path $out 'DualQuaternionBoundsTests.exe')) @sources /link $library user32.lib shell32.lib ole32.lib
if($LASTEXITCODE -ne 0){throw 'DQS bounds tests did not compile.'}
& (Join-Path $out 'DualQuaternionBoundsTests.exe') $root
if($LASTEXITCODE -ne 0){throw 'DQS bounds tests failed.'}
