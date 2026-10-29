param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$output=Join-Path $repo ".cache/cluster-cpu/$Configuration";New-Item -ItemType Directory -Path $output -Force|Out-Null
$sources=@('Tests/ClusterGeometryTests.cpp','Common/Application.cpp','Common/Gltf.cpp','Common/Workshop.cpp','Common/Image.cpp')|ForEach-Object{Join-Path $repo $_}
$runtime=if($Configuration -eq 'Debug'){@('/MDd','/Od','/D_DEBUG')}else{@('/MD','/O2','/DNDEBUG')}
& cl.exe /nologo /std:c++20 /EHsc /W4 /utf-8 @runtime /DUNICODE /D_UNICODE ('/I'+(Join-Path $repo 'Common')) ('/I'+(Join-Path $repo 'ThirdParty/nlohmann-json/single_include')) ('/I'+(Join-Path $repo 'ThirdParty/meshoptimizer/src')) ('/Fo'+$output+'/') ('/Fe'+$output+'/ClusterGeometryTests.exe') @sources /link (Join-Path $repo "bin/x64/$Configuration/MeshOptimizerCpu/MeshOptimizerCpu.lib") user32.lib shell32.lib ole32.lib windowscodecs.lib
if($LASTEXITCODE){throw 'Cluster CPU compilation failed'}
& (Join-Path $output 'ClusterGeometryTests.exe') $repo
if($LASTEXITCODE){throw 'Cluster CPU tests failed'}
