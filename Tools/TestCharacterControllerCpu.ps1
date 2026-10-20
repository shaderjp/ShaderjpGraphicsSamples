$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$out = Join-Path $root '.cache/character/cpu'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$sources = @('Tests/CharacterControllerTests.cpp','Common/SkinnedGltf.cpp','Common/Application.cpp','ThirdParty/imgui/imgui.cpp','ThirdParty/imgui/imgui_draw.cpp','ThirdParty/imgui/imgui_tables.cpp','ThirdParty/imgui/imgui_widgets.cpp') | ForEach-Object { Join-Path $root $_ }
& cl.exe /nologo /std:c++20 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX ("/I" + (Join-Path $root 'Common')) ("/I" + (Join-Path $root 'ThirdParty/imgui')) ("/I" + (Join-Path $root 'ThirdParty/nlohmann-json/single_include')) ("/Fo" + $out + '\') ("/Fe" + (Join-Path $out 'CharacterControllerTests.exe')) @sources /link user32.lib shell32.lib ole32.lib xinput.lib imm32.lib
if ($LASTEXITCODE -ne 0) { throw 'Character CPU tests did not compile' }
& (Join-Path $out 'CharacterControllerTests.exe') $root
if ($LASTEXITCODE -ne 0) { throw 'Character CPU tests failed' }
