param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [string[]]$Apis=@('Direct3D12','Vulkan'),
    [switch]$CaptureDocs,
    [switch]$SkipCompleted
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/bindless-tests/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
$topics=@('Advanced12_BindlessMaterials','Advanced13_BindlessMeshShader','Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid')
foreach($api in $Apis){
    foreach($topic in $topics){
        $modes=if($topic -like 'Advanced12*'){@('bound','textures')}else{@('bound','textures','resources')}
        foreach($mode in $modes){
            $name="$api`_$topic";$label="$api-$topic-$mode"
            $log=Join-Path $work "$label.log";$image=Join-Path $work "$label.png"
            if($SkipCompleted -and (Test-Path -LiteralPath $image) -and (Test-Path -LiteralPath $log) -and (Get-Content -LiteralPath $log -Raw) -match 'Validation errors: 0'){continue}
            $args=@('--gallery-preset','Mini','--materials','64','--binding',$mode,'--frames','4','--no-ui','--capture-gpu','--log',$log,'--screenshot',$image,'--timings',(Join-Path $work "$label-timing.json"))
            $previous=$env:VK_LAYER_VALIDATE_SYNC
            try{
                if($Configuration -eq 'Debug' -and $api -eq 'Vulkan'){$env:VK_LAYER_VALIDATE_SYNC='1'}
                $process=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $args -WorkingDirectory $work -WindowStyle Hidden -PassThru
            }finally{$env:VK_LAYER_VALIDATE_SYNC=$previous}
            if(-not $process.WaitForExit(300000)){$process.Kill();throw "Timeout: $label"}
            if($process.ExitCode -or (Get-Content -LiteralPath $log -Raw) -notmatch 'Validation errors: 0'){throw "GPU failure: $label ($log)"}
            Write-Output "$label PASS"
            if($CaptureDocs -and $mode -eq 'textures'){
                Copy-Item -LiteralPath $image -Destination (Join-Path $repoRoot "Docs/$api/Images/$topic.png")
            }
        }
    }
}
$python=Join-Path 'C:\Program Files\Blender Foundation\Blender 5.2' '5.2/python/bin/python.exe'
& $python (Join-Path $repoRoot 'Tests/BindlessGpuTests.py') $work @Apis
if($LASTEXITCODE){throw 'Bindless numerical comparison failed'}
Write-Output 'Bindless API / binding mode comparisons PASS'
