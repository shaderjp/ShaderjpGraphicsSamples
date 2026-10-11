$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Install Visual Studio 2026 Desktop development with C++' }
Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$outputDirectory = Join-Path $repositoryRoot '.cache\loader-tests'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$jsonInclude = Join-Path $repositoryRoot 'ThirdParty\nlohmann-json\single_include'
if (-not (Test-Path -LiteralPath (Join-Path $jsonInclude 'nlohmann\json.hpp'))) { throw 'Run git submodule update --init --recursive' }
$sources = @('Tests\LoaderTests.cpp','Tests\GltfTests.cpp','Common\Application.cpp','Common\Mesh.cpp','Common\Gltf.cpp','Common\Image.cpp') | ForEach-Object { Join-Path $repositoryRoot $_ }
& cl.exe /nologo /std:c++20 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE ("/I" + (Join-Path $repositoryRoot 'Common')) ("/I" + $jsonInclude) ("/Fo" + $outputDirectory + '\') ("/Fe" + (Join-Path $outputDirectory 'LoaderTests.exe')) @sources /link user32.lib shell32.lib ole32.lib windowscodecs.lib
if ($LASTEXITCODE -ne 0) { throw 'Loader test compilation failed' }
& (Join-Path $outputDirectory 'LoaderTests.exe') $repositoryRoot
if ($LASTEXITCODE -ne 0) { throw 'Loader tests failed' }
