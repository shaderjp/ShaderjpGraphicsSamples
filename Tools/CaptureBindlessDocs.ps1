# Capture the normal article preset after the numerical Mini tests.
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot '.cache/bindless-docs'
New-Item -ItemType Directory -Path $work -Force|Out-Null
$shots=@()
foreach($api in @('Direct3D12','Vulkan')){foreach($topic in @('Advanced12_BindlessMaterials','Advanced13_BindlessMeshShader','Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid')){
    $name="$api`_$topic";$label="$api-$topic";$image=Join-Path $repoRoot "Docs/$api/Images/$topic.png";$log=Join-Path $work "$label.log";$timings=Join-Path $work "$label.json"
    $p=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/Release/$name/$name.exe") -ArgumentList @('--gallery-preset','Standard','--binding','textures','--frames','4','--no-ui','--screenshot',$image,'--timings',$timings,'--log',$log) -WorkingDirectory $work -WindowStyle Hidden -PassThru
    if(-not $p.WaitForExit(300000)){$p.Kill();throw "Article capture timeout: $label"}
    if($p.ExitCode -or (Get-Content -LiteralPath $log -Raw) -notmatch 'Validation errors: 0'){throw "Article capture failed: $label"}
    $d=Get-Content -LiteralPath $timings -Raw|ConvertFrom-Json
    $shots+=@{api=$api;topic=$topic;configuration='Release';preset='Standard';camera='StandardHero';binding=$d.binding;objects=$d.objects;materials=$d.residentMaterials;images=$d.images;assetHash=$d.assetHash;sha256=(Get-FileHash -LiteralPath $image).Hash.ToLowerInvariant();path="Docs/$api/Images/$topic.png";width=1280;height=720}
    Write-Output "$label article capture PASS"
}}
foreach($api in @('Direct3D12','Vulkan')){
    $directory=Join-Path $repoRoot '.cache/bindless-features/Release'
    foreach($stage in @(@('baseline','textures'),@('switched','bound'))){
        $from=Join-Path $directory "$api-Advanced12_BindlessMaterials-ui-$($stage[0]).png"
        $to=Join-Path $repoRoot "Docs/$api/Images/Advanced12_Binding-$($stage[1]).png"
        Copy-Item -LiteralPath $from -Destination $to
    }
}
$shots|ConvertTo-Json -Depth 10|Set-Content (Join-Path $repoRoot 'Docs/BindlessCaptures.json')
