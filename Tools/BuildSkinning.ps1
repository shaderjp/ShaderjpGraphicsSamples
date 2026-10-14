param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[string]$Project,[switch]$Restore)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$work=Join-Path $root '.cache/skinning'
New-Item -ItemType Directory -Path $work -Force | Out-Null
if($Restore){& msbuild.exe (Join-Path $root 'ShaderjpGraphicsSamples.sln') -t:Restore -p:RestorePackagesConfig=true -v:minimal *> (Join-Path $work 'restore.log');if($LASTEXITCODE -ne 0){throw 'Restore failed; see .cache/skinning/restore.log'}}
$target=if($Project){Join-Path $root $Project}else{Join-Path $root 'ShaderjpGraphicsSamples.sln'}
& msbuild.exe $target -m:4 -p:Configuration=$Configuration -p:Platform=x64 -v:minimal *> (Join-Path $work "build-$Configuration.log")
if($LASTEXITCODE -ne 0){Get-Content (Join-Path $work "build-$Configuration.log") -Tail 35;throw 'Skinning build failed.'}
Write-Host "Build $Configuration PASS. Log: .cache/skinning/build-$Configuration.log"
