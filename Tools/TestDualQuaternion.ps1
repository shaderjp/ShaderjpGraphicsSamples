param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[switch]$Quick,[switch]$Resume,[switch]$KeepReadbacks)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$python='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
$arguments=@((Join-Path $root 'Tests/DualQuaternionGpuTests.py'),'--configuration',$Configuration)
if($Quick){$arguments+='--quick'}
if($Resume){$arguments+='--resume'}
if($KeepReadbacks){$arguments+='--keep-readbacks'}
& $python @arguments
if($LASTEXITCODE -ne 0){throw 'DQS GPU comparison failed.'}
