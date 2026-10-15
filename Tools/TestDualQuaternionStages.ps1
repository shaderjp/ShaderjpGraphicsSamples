param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[switch]$Quick,[ValidateSet('Direct3D12','Vulkan')][string]$Api)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$python='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
$arguments=@((Join-Path $root 'Tests/DualQuaternionStagesGpuTests.py'),'--configuration',$Configuration)
if($Quick){$arguments+='--quick'}
if($Api){$arguments+=@('--api',$Api)}
& $python @arguments
if($LASTEXITCODE -ne 0){throw 'DQS VS / Compute / Mesh GPU comparison failed.'}
