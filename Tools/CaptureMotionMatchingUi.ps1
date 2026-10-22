$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$work=Join-Path $root '.cache/motion-matching/ui'
New-Item -ItemType Directory -Path $work -Force|Out-Null
$results=@()
foreach($api in @('Direct3D12','Vulkan')){
    foreach($topic in @('29_MotionMatching','30_TraversalController')){
        $name="${api}_$topic";$exe=Join-Path $root "bin/x64/Release/$name/$name.exe"
        $image=Join-Path $work "$name-ui.png";$log=Join-Path $work "$name-ui.log"
        $frames=if($topic.StartsWith('29_')){155}else{240}
        $process=Start-Process -FilePath $exe -ArgumentList @('--test-character','--frames',"$frames",'--fixed-dt','0.016666667','--screenshot',('"'+$image+'"'),'--log',('"'+$log+'"')) -WorkingDirectory $work -PassThru -WindowStyle Hidden
        $timer=[Diagnostics.Stopwatch]::StartNew()
        while(-not $process.WaitForExit(1000)){if($timer.Elapsed.TotalSeconds -gt 60){$process.Kill();throw 'UI capture timed out'}}
        if($process.ExitCode -ne 0 -or (Get-Content -LiteralPath $log -Raw) -notmatch 'Validation errors: 0'){throw 'UI capture failed'}
        Copy-Item -LiteralPath $image -Destination (Join-Path $root "Docs/$api/Images/$topic-ui.png")
        $results+=[ordered]@{sample=$name;frames=$frames;sha256=(Get-FileHash -LiteralPath $image).Hash.ToLowerInvariant()}
    }
}
$results|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $root 'Docs/MotionMatchingUiCaptures.json') -Encoding utf8
