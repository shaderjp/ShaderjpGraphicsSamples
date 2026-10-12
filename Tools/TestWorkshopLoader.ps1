$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$outputDirectory = Join-Path $repositoryRoot '.cache\workshop-loader'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$sources = @('Tests\WorkshopRuntimeTests.cpp','Common\Workshop.cpp','Common\Application.cpp','Common\Image.cpp') | ForEach-Object { Join-Path $repositoryRoot $_ }
& cl.exe /nologo /std:c++20 /EHsc /W4 /O2 /utf-8 /DUNICODE /D_UNICODE ("/I" + (Join-Path $repositoryRoot 'Common')) ("/I" + (Join-Path $repositoryRoot 'ThirdParty\nlohmann-json\single_include')) ("/Fo" + $outputDirectory + '\') ("/Fe" + (Join-Path $outputDirectory 'WorkshopTests.exe')) @sources /link user32.lib shell32.lib ole32.lib windowscodecs.lib
if ($LASTEXITCODE -ne 0) { throw 'Workshop loader compilation failed' }
& (Join-Path $outputDirectory 'WorkshopTests.exe') $repositoryRoot
if ($LASTEXITCODE -ne 0) { throw 'Workshop loader tests failed' }
