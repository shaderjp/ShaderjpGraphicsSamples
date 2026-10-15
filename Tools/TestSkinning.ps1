param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[switch]$Quick,[switch]$RootMotionOnly,[ValidateSet('Direct3D12','Vulkan')][string]$Api)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$python='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
$arguments=@((Join-Path $root 'Tests/SkinningGpuTests.py'),'--configuration',$Configuration)
if($Quick){$arguments+='--quick'}
if($RootMotionOnly){$arguments+='--root-only'}
if($Api){$arguments+=@('--api',$Api)}
& $python @arguments
if($LASTEXITCODE -ne 0){throw 'Skinning GPU comparison failed.'}
