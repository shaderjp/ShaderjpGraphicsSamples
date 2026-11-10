$ErrorActionPreference='Stop'
$repositoryRoot=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$outputDirectory=Join-Path $repositoryRoot '.cache/depth-cpu'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
& cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 /utf-8 (Join-Path $repositoryRoot 'Tests/DepthTests.cpp') ("/Fo$outputDirectory/") ("/Fe$outputDirectory/DepthTests.exe")
if($LASTEXITCODE){throw 'Depth CPU compilation failed'}
& "$outputDirectory/DepthTests.exe"
if($LASTEXITCODE){throw 'Depth CPU tests failed'}
