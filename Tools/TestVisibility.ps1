param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [ValidateSet('Direct3D12','Vulkan')][string[]]$Apis=@('Direct3D12','Vulkan'),
    [switch]$CaptureDocs,[switch]$BenchmarkOnly,
    [string]$PythonPath='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/visibility-tests/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
$fixture=Join-Path $work 'near-scene'
New-Item -ItemType Directory -Path $fixture -Force|Out-Null
$settings=Get-Content (Join-Path $repoRoot 'Assets/Scenes/Workshop/Workshop.json') -Raw | ConvertFrom-Json
$settings.model=Join-Path $repoRoot 'Assets/Models/Workshop/Workshop.gltf'
$settings.environment.prefix=Join-Path $repoRoot 'Assets/Environments/Workshop/WorkshopSky'
$settings.cameras.Materials.near=1.0
$settings | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $fixture 'Workshop.json') -Encoding utf8
Copy-Item (Join-Path $repoRoot 'Assets/Scenes/Workshop/CameraPath.json') (Join-Path $fixture 'CameraPath.json') -Force
function WaitForIdleGpu {
    if(-not (Get-Command nvidia-smi -ErrorAction SilentlyContinue)){return $null}
    $stable=0;$maximum=0;$deadline=(Get-Date).AddMinutes(3)
    while($stable -lt 3){
        $reading=& nvidia-smi --id=0 --query-gpu=utilization.gpu --format=csv,noheader,nounits
        if($LASTEXITCODE -ne 0){throw 'GPU utilization query failed'}
        $usage=[int]($reading | Select-Object -First 1)
        if($usage -le 20){$stable++;$maximum=[Math]::Max($maximum,$usage)}else{$stable=0;$maximum=0}
        if((Get-Date) -gt $deadline){throw 'GPU is busy with another workload; stop that workload before benchmarking'}
        if($stable -lt 3){Start-Sleep -Milliseconds 1000}
    }
    return $maximum
}
function RecordIdleCondition([string]$Path,$Percent){
    if($null -eq $Percent){return}
    $report=Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $report | Add-Member -NotePropertyName idleGpuUtilizationMaxPercent -NotePropertyValue $Percent
    $report | Add-Member -NotePropertyName idleGpuSamples -NotePropertyValue 3
    $report | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $Path -Encoding utf8
}
function Run([string]$Api,[string]$Label,[string[]]$Extra=@(),[string]$Topic='21_VisibilityBuffer',[int]$Frames=3,[switch]$NoReadback) {
    $name="${Api}_${Topic}";$png=Join-Path $work "$Api-$Label.png";$log=Join-Path $work "$Api-$Label.log"
    $arguments=@('--frames',"$Frames",'--no-ui','--log',('"'+$log+'"'),'--screenshot',('"'+$png+'"'))
    if(-not $NoReadback){$arguments+='--capture-gpu'};$arguments+=$Extra
    $previous=$env:VK_LAYER_VALIDATE_SYNC
    try {
        if($Api -eq 'Vulkan' -and $Configuration -eq 'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $process=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
    } finally {$env:VK_LAYER_VALIDATE_SYNC=$previous}
    $deadline=(Get-Date).AddMinutes(3)
    while(-not $process.WaitForExit(250)){
        if((Get-Date) -gt $deadline){$process.Kill();throw "$name / $Label timed out"}
        if($Extra -contains '--benchmark' -and (Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $process.Id -and $_.ProcessName -match '^(Direct3D12_|Vulkan_)' })){
            $process.Kill();throw 'Benchmark requires exclusive tutorial GPU execution'
        }
    }
    $text=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0' -or $text -match '(?m)^VALIDATION ERROR:|D3D12 warning:'){throw "$name / $Label failed: $text"}
    if($CaptureDocs -and $Topic -eq '21_VisibilityBuffer' -and $Label -in @('base','stress','instance-id','triangle-id','barycentrics','uv-gradient','full')){
        Copy-Item -LiteralPath $png -Destination (Join-Path $repoRoot "Docs/$Api/Images/21_VisibilityBuffer-$Label.png")
    }
    Write-Output "$Api $Label passed"
}
foreach($api in $Apis){
    if(-not $BenchmarkOnly){
        Run $api 'base'
        Run $api 'all' @('--light-culling','all')
        Run $api 'cluster' @('--light-culling','cluster')
        Run $api 'overflow' @('--list-capacity','1')
        Run $api 'stress' @('--workshop-scene','Stress','--camera','StressHero')
        Run $api 'odd' @('--test-odd') -Frames 20
        Run $api 'full' @('--preset','Full','--camera','Transparency')
        Run $api 'forward' @('--forward')
        Run $api 'moving' @('--occluder-x','1')
        Run $api 'near' @('--scene',(Join-Path $fixture 'Workshop.json'),'--camera','Materials')
        Run $api 'near-barycentrics' @('--scene',(Join-Path $fixture 'Workshop.json'),'--camera','Materials','--shading','barycentrics')
        Run $api 'backface' @('--camera','Occlusion')
        Run $api 'black' @('--test-image','black')
        foreach($view in @('instance-id','triangle-id','barycentrics','uv-gradient')){
            Run $api $view @('--shading',$view)
        }
        Run $api 'chapter19' @() -Topic '19_TiledDeferred'
        Run $api 'chapter20' @() -Topic '20_ClusteredShading'
        & $PythonPath (Join-Path $repoRoot 'Tests/VisibilityGpuTests.py') $work $api
        if($LASTEXITCODE -ne 0){throw "$api Visibility Buffer numeric checks failed"}
    } else {
        if($Configuration -ne 'Release'){throw 'Benchmark requires Release'}
        $measure=Join-Path $repoRoot 'Docs/Measurements/Visibility';New-Item -ItemType Directory -Path $measure -Force|Out-Null
        foreach($scene in @('Standard','Stress')){
            foreach($topic in @('19_TiledDeferred','20_ClusteredShading','21_VisibilityBuffer')){
                $json=Join-Path $measure "$api-$scene-$topic.json"
                if(Get-Process -ErrorAction SilentlyContinue | Where-Object ProcessName -Match '^(Direct3D12_|Vulkan_)'){throw 'Another tutorial is using the GPU'}
                $idle=WaitForIdleGpu
                Run $api "benchmark-$scene-$topic" @('--benchmark','--workshop-scene',$scene,'--timings',('"'+$json+'"')) -Topic $topic -Frames 1260 -NoReadback
                RecordIdleCondition $json $idle
            }
            $json=Join-Path $measure "$api-$scene-21-Clustered.json"
            $idle=WaitForIdleGpu
            Run $api "benchmark-$scene-21-Clustered" @('--benchmark','--workshop-scene',$scene,'--light-culling','cluster','--timings',('"'+$json+'"')) -Frames 1260 -NoReadback
            RecordIdleCondition $json $idle
        }
    }
}
