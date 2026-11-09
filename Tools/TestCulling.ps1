param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [switch]$Quick,
    [switch]$UiOnly,
    [switch]$MotionOnly,
    [switch]$ResizeOnly,
    [string[]]$Cases,
    [ValidateSet('frustum','query','software','hiz')][string[]]$Modes,
    [ValidateSet('Direct3D12','Vulkan')][string]$Api,
    [string]$PythonPath='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
)
$ErrorActionPreference='Stop'
$repositoryRoot=Split-Path $PSScriptRoot -Parent
$arguments=@((Join-Path $repositoryRoot 'Tests/CullingGpuTests.py'),'--configuration',$Configuration)
if($Quick){$arguments+='--quick'}
if($UiOnly){$arguments+='--ui-only'}
if($MotionOnly){$arguments+='--motion-only'}
if($ResizeOnly){$arguments+='--resize-only'}
if($Cases){$arguments+='--cases';$arguments+=$Cases}
if($Modes){$arguments+='--modes';$arguments+=$Modes}
if($Api){$arguments+=@('--api',$Api)}
& $PythonPath @arguments
if($LASTEXITCODE -ne 0){throw 'Culling GPU verification failed'}
