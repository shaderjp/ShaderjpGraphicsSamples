param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$topics=@('Advanced20_ClusteredGeometry','Advanced21_ClusterTemplates','Advanced22_ClusterTessellation','Advanced23_PartitionedTLAS')
& (Join-Path $repo 'Tools/TestSamples.ps1') -Configuration $Configuration -Topics $topics -Apis Direct3D12 -CaptureDocs
$work=Join-Path $repo ".cache/cluster-docs/$Configuration";New-Item -ItemType Directory -Force -Path $work|Out-Null
$cases=@(@($topics[0],'clusters','clusters'),@($topics[0],'triangles','clusters'),@($topics[1],'clusters','templates'),
    @($topics[2],'lod','templates'),@($topics[2],'triangles','templates'),@($topics[3],'partition','ptlas'),@($topics[3],'activity','ptlas'))
foreach($case in $cases){
    $topic=$case[0];$name='Direct3D12_'+$topic;$label="$topic-$($case[1])";$log=Join-Path $work ($label+'.log')
    $image=Join-Path $repo "Docs/Direct3D12/Images/$label.png"
    $arguments=@('--frames','3','--log',$log,'--pause-animation','--animation-time','0.7','--no-ui','--cg-mode',$case[2],'--cg-view',$case[1],'--screenshot',$image)
    if($topic -match '23_'){$arguments+=@('--cg-partition','global','--cg-active-percent','10')}
    $process=Start-Process -FilePath (Join-Path $repo "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(180000)){$process.Kill();throw "Capture timeout: $label"}
    if($process.ExitCode -or (Get-Content -LiteralPath $log -Raw) -notmatch 'Validation errors: 0'){throw "Capture failed: $label"}
    Write-Output "Captured $label (1920x1080)"
}
