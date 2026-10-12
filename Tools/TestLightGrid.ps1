param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [ValidateSet('Direct3D12','Vulkan')][string[]]$Apis=@('Direct3D12','Vulkan'),
    [switch]$CaptureDocs,[switch]$Benchmark,[switch]$BenchmarkOnly,
    [string]$PythonPath='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/light-grid-tests/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
function Run([string]$Api,[string]$Label,[string[]]$Extra=@(),[string]$Topic='19_TiledDeferred',[int]$Frames=3,[switch]$Gpu,[switch]$Ui) {
    $name="${Api}_${Topic}";$png=Join-Path $work "$Api-$Label.png";$log=Join-Path $work "$Api-$Label.log"
    $arguments=@('--frames',"$Frames",'--log',('"'+$log+'"'),'--screenshot',('"'+$png+'"'))
    if(-not $Ui){$arguments+='--no-ui'};if($Gpu){$arguments+='--capture-gpu'};$arguments+=$Extra
    $previous=$env:VK_LAYER_VALIDATE_SYNC
    try {if($Api -eq 'Vulkan' -and $Configuration -eq 'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $process=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
    }finally{$env:VK_LAYER_VALIDATE_SYNC=$previous}
    $conflicts=[Collections.Generic.HashSet[string]]::new()
    $deadline=(Get-Date).AddMinutes(3)
    while(-not $process.WaitForExit(250)){
        if((Get-Date) -gt $deadline){$process.Kill();throw "$name / $Label timed out"}
        if($Extra -contains '--benchmark'){
            Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $process.Id -and $_.ProcessName -match '^(Direct3D12_|Vulkan_)' } | ForEach-Object { [void]$conflicts.Add($_.ProcessName) }
        }
    }
    $text=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0' -or $text -match '(?m)^VALIDATION ERROR:|D3D12 warning:'){throw "$name / $Label failed: $text"}
    if($conflicts.Count){throw "Benchmark GPU conflict: $($conflicts -join ', ')"}
    if($CaptureDocs -and $Label -in @('tile-base','cluster-base','tile-stress','cluster-stress','tile-count','cluster-count','cluster-slice','overflow-view')){
        $chapter=if($Label -like 'tile-*'){'19_TiledDeferred'}else{'20_ClusteredShading'}
        Copy-Item -LiteralPath $png -Destination (Join-Path $repoRoot "Docs/$Api/Images/$chapter-$Label.png")
    }
    Write-Output "$Api $Label passed"
}
foreach($api in $Apis){
    if(-not $BenchmarkOnly){
        foreach($case in @('base','stress','odd','full','inside','maximum','moving')){
            $extra=@()
            if($case -eq 'stress'){$extra=@('--workshop-scene','Stress','--camera','StressHero')}
            if($case -eq 'odd'){$extra=@('--test-odd');$frames=20}else{$frames=3}
            if($case -eq 'full'){$extra=@('--preset','Full','--camera','Transparency')}
            if($case -eq 'inside'){$extra=@('--camera','Materials','--light-radius','3')}
            if($case -eq 'maximum'){$extra=@('--workshop-scene','Stress','--camera','StressHero','--lights-per-room','512','--light-radius','1')}
            if($case -eq 'moving'){$extra=@('--animate-lights','--fixed-dt','0.0166666667');$frames=8}
            foreach($mode in @('all','tile','cluster')){Run $api "$mode-$case" ($extra+@('--light-culling',$mode)) -Frames $frames -Gpu}
        }
        Run $api 'overflow-base' @('--list-capacity','1') -Gpu
        Run $api 'chapter20-base' @() -Topic '20_ClusteredShading' -Gpu
        Run $api 'tile-count' @('--light-view','count')
        Run $api 'cluster-count' @('--light-view','count') -Topic '20_ClusteredShading'
        Run $api 'cluster-slice' @('--light-view','slice','--camera','Materials') -Topic '20_ClusteredShading'
        Run $api 'overflow-view' @('--list-capacity','1','--light-view','overflow') -Topic '20_ClusteredShading'
        Run $api 'reset-ui' @('--test-ui') -Frames 36 -Ui
        Run $api 'odd-lifecycle' @('--test-odd') -Frames 48 -Topic '20_ClusteredShading'
        Run $api 'switch-live' @('--test-post') -Frames 36 -Gpu
        Run $api 'switch20-live' @('--test-post') -Frames 36 -Gpu -Topic '20_ClusteredShading'
        & $PythonPath (Join-Path $repoRoot 'Tests/LightGridGpuTests.py') $work $api
        if($LASTEXITCODE -ne 0){throw "$api light-list numeric checks failed"}
    }
    if($Benchmark -or $BenchmarkOnly){
        if($Configuration -ne 'Release'){throw 'Benchmark requires Release'}
        $measure=Join-Path $repoRoot 'Docs/Measurements/LightGrid';New-Item -ItemType Directory -Path $measure -Force|Out-Null
        foreach($scene in @('Standard','Stress')){foreach($mode in @('all','tile','cluster')){
            $json=Join-Path $measure "$api-$scene-$mode.json"
            $valid=$false
            for($attempt=0;$attempt -lt 3 -and -not $valid;$attempt++){
                $deadline=(Get-Date).AddMinutes(10)
                $idleSince=Get-Date
                while($true){
                    if(Get-Process -ErrorAction SilentlyContinue | Where-Object ProcessName -Match '^(Direct3D12_|Vulkan_)'){$idleSince=Get-Date}
                    elseif(((Get-Date)-$idleSince).TotalSeconds -ge 10){break}
                    if((Get-Date) -gt $deadline){throw 'Benchmark needs other tutorial GPU processes to finish'}
                    Start-Sleep -Milliseconds 1000
                }
                try {Run $api "benchmark-$scene-$mode" @('--benchmark','--workshop-scene',$scene,'--light-culling',$mode,'--timings',('"'+$json+'"')) -Frames 1260;$valid=$true}
                catch {if($_.Exception.Message -notlike 'Benchmark GPU conflict:*'){throw};Write-Output "Repeating $api $scene $mode after concurrent GPU work"}
            }
            if(-not $valid){throw 'Benchmark could not obtain an uncontended run'}
        }}
    }
}
