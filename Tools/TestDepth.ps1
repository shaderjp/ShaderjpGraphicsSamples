param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [ValidateSet('Direct3D12','Vulkan')][string]$Api,
    [switch]$Quick,
    [switch]$CaptureDocs,
    [string]$PythonPath='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
)
$ErrorActionPreference='Stop'
$repositoryRoot=Split-Path $PSScriptRoot -Parent
$arguments=@((Join-Path $repositoryRoot 'Tests/DepthGpuTests.py'),'--configuration',$Configuration)
if($Api){$arguments+=@('--api',$Api)}
if($Quick){$arguments+='--quick'}
if($CaptureDocs){$arguments+='--capture-docs'}
& $PythonPath @arguments
if($LASTEXITCODE){throw 'Depth GPU tests failed'}
