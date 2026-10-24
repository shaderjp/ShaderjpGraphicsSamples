param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [ValidateSet('Direct3D12','Vulkan')][string[]]$Apis=@('Direct3D12','Vulkan'),
    [switch]$CaptureDocs,
    [string]$PythonPath='C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe'
)
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo ".cache/pathtracing-tests/$Configuration";New-Item -ItemType Directory -Force $work|Out-Null
function Run([string]$Api,[string]$Label,[int]$Spp,[string[]]$Extra=@(),[switch]$Ui,[int]$Frames=0){
    $name="${Api}_Advanced18_PathTracingBasic";$png=Join-Path $work "$Api-$Label.png";$log=Join-Path $work "$Api-$Label.log"
    if(-not $Frames){$Frames=$Spp}
    $arguments=@('--frames',"$Frames",'--pt-target-spp',"$Spp",'--capture-gpu','--screenshot',('"'+$png+'"'),'--log',('"'+$log+'"'))+$Extra
    if(-not $Ui){$arguments+='--no-ui'}
    $previous=$env:VK_LAYER_VALIDATE_SYNC
    try{if($Api-eq'Vulkan'-and$Configuration-eq'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $process=Start-Process -FilePath (Join-Path $repo "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
    }finally{$env:VK_LAYER_VALIDATE_SYNC=$previous}
    if(-not $process.WaitForExit(240000)){$process.Kill();throw "$Api $Label timed out"}
    $text=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode-ne 0-or$text-notmatch'Validation errors: 0'-or$text-match'(?m)^VALIDATION ERROR:|D3D12 warning:'){throw "$Api $Label failed: $text"}
    if($Configuration-eq'Debug'-and$text-notmatch'(Debug Layer|Validation Layer) enabled'){throw 'Validation layer missing'}
    if($CaptureDocs-and$Label-match'^(room-(1|16|256)|room-bsdf|room-direct|workshop)$'){
        Copy-Item -LiteralPath $png -Destination (Join-Path $repo "Docs/$Api/Images/Advanced18_PathTracingBasic-$Label.png")
    }
    $captureMetadata=Get-Content -LiteralPath (Join-Path $work "$Api-$Label-gpu/metadata.json") -Raw|ConvertFrom-Json
    Write-Output "$Configuration $Api $Label ($($captureMetadata.spp) spp) PASS"
}
foreach($api in $Apis){
    foreach($spp in @(1,16,256)){Run $api "room-$spp" $spp}
    Run $api 'room-bsdf' 256 @('--pt-sampling','bsdf')
    Run $api 'room-direct' 256 @('--pt-max-bounces','1')
    Run $api 'workshop' 128 @('--pt-scene','workshop')
    Run $api 'stress' 8 @('--pt-scene','workshop','--workshop-scene','Stress')
    foreach($case in @('emission','dark','occluded','mask','transformed')){
        Run $api "fixture-$case" 64 @('--rt-test-scene',"pt-$case",'--pt-max-bounces','1')
    }
    foreach($seed in @(1,7,19)){
        Run $api "area-mis-$seed" 128 @('--rt-test-scene','pt-area','--pt-max-bounces','1','--pt-seed',"$seed")
        Run $api "area-bsdf-$seed" 128 @('--rt-test-scene','pt-area','--pt-max-bounces','1','--pt-seed',"$seed",'--pt-sampling','bsdf')
    }
    Run $api 'area-reference' 1024 @('--rt-test-scene','pt-area','--pt-max-bounces','1')
    Run $api 'repeat' 16
    Run $api 'no-nee-mis' 16 @('--rt-test-scene','pt-area','--pt-max-bounces','1','--no-direct')
    Run $api 'no-nee-bsdf' 16 @('--rt-test-scene','pt-area','--pt-max-bounces','1','--no-direct','--pt-sampling','bsdf')
    Run $api 'ui' 7 @('--test-ui') -Ui -Frames 40
    Run $api 'ui-flow' 0 @('--test-ui') -Ui -Frames 52
    Run $api 'paused' 0 @('--pause') -Frames 8
    Run $api 'camera' 4 @('--test-camera') -Frames 32
    Run $api 'odd' 0 @('--test-odd') -Frames 20
    if($api-eq'Vulkan'){Run $api 'capture-minimized' 0 @('--test-window') -Frames 24}
    & $PythonPath (Join-Path $repo 'Tests/PathTracingGpuTests.py') $work $api
    if($LASTEXITCODE){throw "$api numerical checks failed"}
}
