param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64' -or -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
}
$out = Join-Path $root ".cache/motion-matching/cpu/$Configuration"
New-Item -ItemType Directory -Path $out -Force | Out-Null
$builderProject=Join-Path $root 'Samples/Support/MotionDatabaseBuilder/MotionDatabaseBuilder.vcxproj'
& msbuild.exe $builderProject -p:Configuration=$Configuration -p:Platform=x64 -v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Motion database builder did not build' }
$motionDirectory=Join-Path $root 'Assets/Models/SilverRobotMotionSet'
& (Join-Path $root "bin/x64/$Configuration/MotionDatabaseBuilder/MotionDatabaseBuilder.exe") $motionDirectory (Join-Path $motionDirectory 'Locomotion.mmdb')
if ($LASTEXITCODE -ne 0) { throw 'Motion database rebuild failed' }
$sources = @('Tests/MotionMatchingTests.cpp','Common/SkinnedGltf.cpp','Common/Application.cpp') | ForEach-Object { Join-Path $root $_ }
$compileFlags=if($Configuration -eq 'Debug'){@('/MDd','/Od','/Zi','/D_DEBUG')}else{@('/MD','/O2','/DNDEBUG')}
& cl.exe /nologo /std:c++20 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX @compileFlags ("/I" + (Join-Path $root 'Common')) ("/I" + (Join-Path $root 'ThirdParty/nlohmann-json/single_include')) ("/Fo" + $out + '\') ("/Fe" + (Join-Path $out 'MotionMatchingTests.exe')) @sources /link user32.lib shell32.lib ole32.lib
if ($LASTEXITCODE -ne 0) { throw 'Motion CPU tests did not compile' }
& (Join-Path $out 'MotionMatchingTests.exe') $root
if ($LASTEXITCODE -ne 0) { throw 'Motion CPU tests failed' }
