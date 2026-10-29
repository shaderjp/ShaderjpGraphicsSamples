param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo ".cache/cluster-failures/$Configuration";New-Item -ItemType Directory -Path $work -Force|Out-Null
$topics=@('Advanced20_ClusteredGeometry','Advanced21_ClusterTemplates','Advanced22_ClusterTessellation','Advanced23_PartitionedTLAS')
foreach($topic in $topics) {
    $name='Direct3D12_'+$topic;$exe=Join-Path $repo "bin/x64/$Configuration/$name/$name.exe"
    $cases=@(@('unsupported',@('--cg-force-unsupported'),'unsupported'),@('mode',@('--cg-mode','invalid'),'cg-mode'),@('view',@('--cg-view','invalid'),'cg-view'),@('lod',@('--cg-lod','3'),'cg-lod'))
    if($topic -match '23_'){$cases+=,@('partition',@('--cg-partition','invalid'),'cg-partition')}
    foreach($case in $cases) {
        $log=Join-Path $work "$topic-$($case[0]).log"
        $p=Start-Process -FilePath $exe -ArgumentList (@('--frames','1','--log',$log)+$case[1]) -WindowStyle Hidden -PassThru
        if(-not $p.WaitForExit(90000)){$p.Kill();throw "Timeout: $topic $($case[0])"}
        if($p.ExitCode -eq 0 -or (Get-Content -LiteralPath $log -Raw) -notmatch $case[2]){throw "Invalid input was not rejected clearly: $topic $($case[0])"}
    }
    $settings=Get-Content -LiteralPath (Join-Path $repo 'Assets/Models/GeometryLab/GeometryLab.json') -Raw|ConvertFrom-Json
    $settings.binary=Join-Path $repo 'Assets/Models/GeometryLab/GeometryLab.bin';$settings.primitives[0].indexCount=4294967295
    $invalid=Join-Path $work "$topic-invalid.json";$settings|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $invalid
    $log=Join-Path $work "$topic-range.log"
    $p=Start-Process -FilePath $exe -ArgumentList @('--frames','1','--log',$log,'--scene',$invalid) -WindowStyle Hidden -PassThru
    if(-not $p.WaitForExit(90000)){$p.Kill();throw 'Invalid asset timeout'}
    if($p.ExitCode -eq 0 -or (Get-Content -LiteralPath $log -Raw) -notmatch 'invalid vertex/index range'){throw 'Invalid asset range accepted'}
    Write-Output "$Configuration $topic feature gates / invalid inputs PASS"
}
