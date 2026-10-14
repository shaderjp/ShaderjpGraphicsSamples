param([ValidateSet('Direct3D12','Vulkan')][string]$Api,
      [ValidateSet('Vertex','Compute','MeshDirect','MeshTask')][string[]]$Techniques=@('Vertex','Compute','MeshDirect','MeshTask'))
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$work=Join-Path $root '.cache/skinning/performance'
New-Item -ItemType Directory -Path $work -Force | Out-Null
$apis=if($Api){@($Api)}else{@('Direct3D12','Vulkan')}
foreach($apiName in $apis){
    foreach($actors in @(1,64)){
        foreach($technique in $Techniques){
            $topic=switch($technique){'Vertex'{'22_VertexSkinning'} 'Compute'{'23_ComputeSkinningCache'} default{'Advanced10_MeshShaderSkinning'}}
            $name="$apiName`_$topic"
            $exe=Join-Path $root "bin/x64/Release/$name/$name.exe"
            $label="$apiName-$technique-$actors"
            $report=Join-Path $work "$label.json"
            $log=Join-Path $work "$label.log"
            $camera=if($actors -eq 64){'StressHero'}else{'Hero'}
            $arguments=@('--benchmark','--animation','UAL_Walk','--characters',"$actors",'--camera',$camera,'--timings',('"'+$report+'"'),'--log',('"'+$log+'"'))
            if($technique -eq 'MeshDirect'){$arguments+=@('--render-path','mesh','--meshlet-culling','off')}
            if($technique -eq 'MeshTask'){$arguments+=@('--render-path','task','--meshlet-culling','frustum')}
            $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
            if(-not $process.WaitForExit(180000)){$process.Kill();throw "Benchmark timeout: $label"}
            if($process.ExitCode -ne 0){throw "Benchmark failed: $label"}
            $result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
            if($result.measuredFrames -ne 1200 -or $result.skinning.actors -ne $actors){throw "Incomplete benchmark: $label"}
            if((Get-Content -LiteralPath $log -Raw) -notmatch 'Validation errors: 0'){throw "Benchmark validation error: $label"}
            Write-Host "$label PASS (60 warmup + 1200 measured frames)."
        }
    }
}
$summary=[ordered]@{}
Get-ChildItem -LiteralPath $work -Filter '*.json' -File | Sort-Object Name | ForEach-Object {$summary[$_.BaseName]=Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json}
$summary | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path $root 'Docs/SkinningPerformance.json') -Encoding utf8
