param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [switch]$SkipCompleted,
    [string]$PythonPath='C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe'
)
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo ".cache/cluster-gpu/$Configuration";New-Item -ItemType Directory -Path $work -Force|Out-Null
$topics=@{20='ClusteredGeometry';21='ClusterTemplates';22='ClusterTessellation';23='PartitionedTLAS'}
function Run([int]$Technique,[string]$Label,[string]$Mode,[string[]]$Extra=@(),[int]$Frames=3,[switch]$Moving,[switch]$Ui,[switch]$Showcase) {
    $name="Direct3D12_Advanced${Technique}_$($topics[$Technique])";$log=Join-Path $work "$Label.log";$png=Join-Path $work "$Label.png"
    $executable=Join-Path $repo "bin/x64/$Configuration/$name/$name.exe"
    $runtimeFiles=@(Get-Item -LiteralPath $executable)+@(Get-ChildItem -LiteralPath (Join-Path (Split-Path $executable) 'Shaders') -File)
    $runtimeTime=($runtimeFiles|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 1).LastWriteTimeUtc
    if($SkipCompleted -and (Test-Path -LiteralPath $log) -and (Test-Path -LiteralPath ($png.Replace('.png','-gpu/metadata.json'))) -and (Get-Item -LiteralPath $log).LastWriteTimeUtc -ge $runtimeTime -and (Get-Content -LiteralPath $log -Raw) -match 'Validation errors: 0') {return}
    $arguments=@('--frames',"$Frames",'--log',$log,'--width','640','--height','360','--cg-mode',$Mode,'--fixed-dt','0.016666667','--animation-time','0.7','--capture-gpu','--screenshot',$png)
    if(-not $Moving){$arguments+='--pause-animation'}
    if(-not $Ui){$arguments+='--no-ui'}else{$arguments+='--test-ui'}
    if($Technique -ne 23 -and -not $Showcase){$arguments+=@('--cg-test','plane')}
    $process=Start-Process -FilePath (Join-Path $repo "bin/x64/$Configuration/$name/$name.exe") -ArgumentList ($arguments+$Extra) -WorkingDirectory $work -WindowStyle Hidden -PassThru
    if(-not $process.WaitForExit(180000)){$process.Kill();throw "Timeout: $Label"}
    $text=if(Test-Path -LiteralPath $log){Get-Content -LiteralPath $log -Raw}else{'Log missing'}
    if($process.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0' -or $text -match 'D3D12 warning:') {throw "$Label failed ($($process.ExitCode)): $text"}
    if($Configuration -eq 'Debug' -and $text -notmatch 'Debug Layer enabled'){throw 'Debug layer missing'}
    Write-Output "$Configuration $Label PASS"
}
foreach($technique in @(20,21,22,23)) {
    $modes=switch($technique){20{@('clusters','rebuild')}21{@('templates','direct','rebuild','refit')}22{@('templates','direct','rebuild')}23{@('ptlas','rebuild','refit')}}
    foreach($mode in $modes){Run $technique "$technique-$mode" $mode}
}
foreach($rate in @(2,4,8)) {foreach($mode in @('templates','rebuild')){Run 22 "22-lod$rate-$mode" $mode @('--cg-lod',"$rate")}}
Run 21 '21-showcase' 'templates' -Showcase
foreach($policy in @('spatial','global')) {
    Run 23 "23-$policy-moving" 'ptlas' @('--cg-partition',$policy) -Frames 5 -Moving
    Run 23 "23-$policy-reference" 'rebuild' @('--cg-partition',$policy) -Frames 5 -Moving
}
Run 23 '23-inactive' 'ptlas' @('--cg-active-percent','0') -Frames 5 -Moving
Run 23 '23-all-active' 'ptlas' @('--cg-active-percent','100') -Frames 5 -Moving
foreach($technique in @(21,22,23)){Run $technique "$technique-ui" $(if($technique -eq 23){'ptlas'}else{'templates'}) -Frames 16 -Ui}
foreach($technique in @(20,21,22,23)) {
    $mode=switch($technique){20{'clusters'}21{'templates'}22{'templates'}23{'ptlas'}}
    Run $technique "$technique-stress" $mode @('--workshop-scene','Stress') -Frames 5 -Moving -Showcase
}
& $PythonPath (Join-Path $repo 'Tests/ClusterGeometryGpuTests.py') $work
if($LASTEXITCODE){throw 'Cluster GPU numerical tests failed'}
