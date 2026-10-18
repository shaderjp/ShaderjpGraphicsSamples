param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[switch]$SkipCompleted)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/bindless-features/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
$topics=@('Advanced12_BindlessMaterials','Advanced13_BindlessMeshShader','Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid')
foreach($api in @('Direct3D12','Vulkan')){foreach($topic in $topics){
    $name="$api`_$topic";$label="$api-$topic-ui";$log=Join-Path $work "$label.log";$image=Join-Path $work "$label.png"
    if($SkipCompleted -and (Test-Path -LiteralPath $log) -and (Get-Content -LiteralPath $log -Raw) -match 'Bindless UI Reset PASS' -and (Get-Content -LiteralPath $log -Raw) -match 'Validation errors: 0'){continue}
    $mode=if($topic -like 'Advanced12*'){'textures'}else{'resources'}
    $oldSync=$env:VK_LAYER_VALIDATE_SYNC
    try{
        if($api -eq 'Vulkan' -and $Configuration -eq 'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $p=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList @('--gallery-preset','Mini','--binding',$mode,'--test-ui','--frames','36','--capture-gpu','--screenshot',$image,'--log',$log) -WorkingDirectory $work -WindowStyle Hidden -PassThru
    }finally{$env:VK_LAYER_VALIDATE_SYNC=$oldSync}
    if(-not $p.WaitForExit(300000)){$p.Kill();throw "UI timeout: $label"}
    $text=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -or $text -notmatch 'Bindless UI binding switch PASS' -or $text -notmatch 'Bindless UI Reset PASS' -or $text -notmatch 'Validation errors: 0'){throw "UI/Reset failure: $label ($log)"}
    Write-Output "$label input / Reset PASS"
}}
$python='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
& $python (Join-Path $repoRoot 'Tests/BindlessUiTests.py') $work
if($LASTEXITCODE){throw 'UI HDR round trip failed'}
