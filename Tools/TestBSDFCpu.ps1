$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$out=Join-Path $repo '.cache/bsdf19/cpu';New-Item -ItemType Directory -Force $out|Out-Null
$sources=@('Tests/BSDFCpuTests.cpp','Common/Workshop.cpp','Common/Application.cpp','Common/Image.cpp')|ForEach-Object{Join-Path $repo $_}
& cl.exe /nologo /std:c++20 /EHsc /W4 /O2 /utf-8 /DUNICODE /D_UNICODE ("/I"+(Join-Path $repo 'Common')) ("/I"+(Join-Path $repo 'ThirdParty/nlohmann-json/single_include')) ("/Fo"+$out+'/') ("/Fe"+$out+'/BSDFCpu.exe') @sources /link user32.lib shell32.lib ole32.lib windowscodecs.lib
if($LASTEXITCODE){throw 'BSDF CPU build failed'}
& (Join-Path $out 'BSDFCpu.exe') $repo
if($LASTEXITCODE){throw 'BSDF CPU tests failed'}
