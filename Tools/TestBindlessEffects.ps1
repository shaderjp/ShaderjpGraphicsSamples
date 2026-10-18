param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string[]]$Apis=@('Direct3D12','Vulkan'))
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/bindless-effects/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
foreach($api in $Apis){foreach($topic in @('Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid')){foreach($effect in @('shadow','reflections')){
    $name="$api`_$topic";$label="$api-$topic-no-$effect";$image=Join-Path $work "$label.png";$log=Join-Path $work "$label.log"
    $oldSync=$env:VK_LAYER_VALIDATE_SYNC
    try{
        if($api -eq 'Vulkan' -and $Configuration -eq 'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $p=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList @('--gallery-preset','Mini','--materials','64','--binding','textures','--frames','4','--no-ui','--capture-gpu','--screenshot',$image,'--log',$log,"--no-$effect") -WorkingDirectory $work -WindowStyle Hidden -PassThru
    }finally{$env:VK_LAYER_VALIDATE_SYNC=$oldSync}
    if(-not $p.WaitForExit(300000)){$p.Kill();throw 'RT effect test timeout'}
    if($p.ExitCode -or (Get-Content -LiteralPath $log -Raw) -notmatch 'Validation errors: 0'){throw "RT effect test failed: $label"}
    Write-Output "$label PASS"
}}}
& 'C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe' (Join-Path $repoRoot 'Tests/BindlessEffectTests.py') $work (Join-Path $repoRoot ".cache/bindless-tests/$Configuration")
if($LASTEXITCODE){throw 'RT effect pixels did not change correctly'}
