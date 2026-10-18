param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[switch]$SkipCompleted)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/bindless-features/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
foreach($api in @('Direct3D12','Vulkan')){
    $cases=@(
        @('Advanced12_BindlessMaterials','ui',@('--test-ui','--frames','36')),
        @('Advanced12_BindlessMaterials','material',@('--gallery-view','material')),
        @('Advanced12_BindlessMaterials','geometry',@('--gallery-view','geometry')),
        @('Advanced12_BindlessMaterials','uv',@('--gallery-view','uv')),
        @('Advanced14_BindlessRaytracing','material',@('--gallery-view','material','--binding','resources')),
        @('Advanced14_BindlessRaytracing','geometry',@('--gallery-view','geometry','--binding','resources')),
        @('Advanced14_BindlessRaytracing','uv',@('--gallery-view','uv','--binding','resources'))
    )
    foreach($topic in @('Advanced13_BindlessMeshShader','Advanced15_BindlessHybrid')){
        $cases+=,@($topic,'driven-textures',@('--binding','textures','--materials','64','--gpu-driven'))
        $cases+=,@($topic,'driven-resources',@('--binding','resources','--materials','64','--gpu-driven'))
        $cases+=,@($topic,'empty',@('--binding','resources','--gpu-driven','--mesh-test-scene','offscreen'))
        $cases+=,@($topic,'all-tail',@('--binding','resources','--gpu-driven','--meshlet-culling','off','--objects','65'))
    }
    foreach($case in $cases){
        $topic=$case[0];$tag=$case[1];$label="$api-$topic-$tag";$name="$api`_$topic"
        $log=Join-Path $work "$label.log";$image=Join-Path $work "$label.png";$timing=Join-Path $work "$label-timing.json"
        if($SkipCompleted -and (Test-Path -LiteralPath $timing) -and (Test-Path -LiteralPath $log) -and (Get-Content -LiteralPath $log -Raw) -match 'Validation errors: 0'){continue}
        $args=@('--gallery-preset','Mini','--frames','4','--capture-gpu','--screenshot',$image,'--timings',$timing,'--log',$log)
        if($tag -ne 'ui'){$args+='--no-ui'}
        $args+=$case[2]
        $oldSync=$env:VK_LAYER_VALIDATE_SYNC
        try{
            if($api -eq 'Vulkan' -and $Configuration -eq 'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
            $p=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $args -WorkingDirectory $work -WindowStyle Hidden -PassThru
        }finally{$env:VK_LAYER_VALIDATE_SYNC=$oldSync}
        if(-not $p.WaitForExit(300000)){$p.Kill();throw "Feature timeout: $label"}
        $text=Get-Content -LiteralPath $log -Raw
        if($p.ExitCode -or $text -notmatch 'Validation errors: 0'){throw "Feature failure: $label ($log)"}
        if($tag -eq 'ui' -and ($text -notmatch 'Bindless UI binding switch PASS' -or $text -notmatch 'Bindless UI Reset PASS')){throw "UI failure: $label"}
        $d=Get-Content -LiteralPath $timing -Raw|ConvertFrom-Json
        if($tag -eq 'empty' -and ($d.visibility.meshletsGpu -ne 0 -or $d.visibility.primaryInstances -ne 0)){throw "Empty list failure: $label"}
        if($d.gpuDriven -and $d.visibility.meshletsGpu -ne $d.visibility.frustumMeshletsCpuReference){throw "GPU culling differs from CPU reference: $label"}
        Write-Output "$label PASS"
    }

}
$python='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
& $python (Join-Path $repoRoot 'Tests/BindlessFeatureTests.py') $work (Join-Path $repoRoot ".cache/bindless-tests/$Configuration")
if($LASTEXITCODE){throw 'Feature image comparisons failed'}
Write-Output 'Binding UI, Reset, ID/UV, GPU driven, empty/all-visible/tail and invalid input PASS'
