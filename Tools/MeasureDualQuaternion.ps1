$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$work=Join-Path $root '.cache/skinning/dqs-performance'
New-Item -ItemType Directory -Path $work -Force | Out-Null
$summary=[ordered]@{}
foreach($api in @('Direct3D12','Vulkan')){
    foreach($actors in @(1,64)){
        foreach($mode in @('lbs','dqs')){
            $name=$api+'_24_DualQuaternionSkinning'
            $exe=Join-Path $root "bin/x64/Release/$name/$name.exe"
            $label="$api-$mode-$actors"
            $report=Join-Path $work "$label.json"
            $log=Join-Path $work "$label.log"
            $camera=if($actors -eq 64){'StressHero'}else{'Hero'}
            $arguments=@('--benchmark','--skinning',$mode,'--animation','UAL_Walk','--characters',"$actors",'--camera',$camera,'--timings',('"'+$report+'"'),'--log',('"'+$log+'"'))
            $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
            if(-not $process.WaitForExit(180000)){$process.Kill();throw "Benchmark timeout: $label"}
            if($process.ExitCode -ne 0){throw "Benchmark failed: $label"}
            $result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
            if($result.measuredFrames -ne 1200 -or $result.skinning.actors -ne $actors -or $result.skinning.deformation -ne $mode){throw "Incomplete benchmark: $label"}
            if((Get-Content -LiteralPath $log -Raw) -notmatch 'Validation errors: 0'){throw "Benchmark validation error: $label"}
            $summary[$label]=$result
            Write-Host "$label PASS (60 warmup + 1200 measured frames)."
        }
    }
}
$summary | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path $root 'Docs/DualQuaternionPerformance.json') -Encoding utf8
