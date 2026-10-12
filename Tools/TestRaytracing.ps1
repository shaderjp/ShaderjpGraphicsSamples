param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [ValidateSet('Direct3D12','Vulkan')][string[]]$Apis=@('Direct3D12','Vulkan'),
    [switch]$CaptureDocs,
    [string]$PythonPath='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/raytracing-tests/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
$topics=@('Advanced01_RaytracingBasic','Advanced02_RaytracingShadows','Advanced03_RaytracingReflections')
function Run([string]$Api,[int]$Technique,[string]$Label,[string[]]$Extra=@(),[int]$Frames=2,[switch]$Ui){
    $topic=$topics[$Technique-1];$name="${Api}_${topic}";$png=Join-Path $work "$Api-$Label.png";$log=Join-Path $work "$Api-$Label.log"
    $arguments=@('--frames',"$Frames",'--log',('"'+$log+'"'),'--screenshot',('"'+$png+'"'),'--capture-gpu')
    if(-not $Ui){$arguments+='--no-ui'};$arguments+=$Extra
    $previous=$env:VK_LAYER_VALIDATE_SYNC
    try{if($Api-eq'Vulkan'-and$Configuration-eq'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $process=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
    }finally{$env:VK_LAYER_VALIDATE_SYNC=$previous}
    if(-not $process.WaitForExit(60000)){$process.Kill();throw "$name / $Label timed out"}
    $text=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode-ne 0-or$text-notmatch'Validation errors: 0'-or$text-match'(?m)^VALIDATION ERROR:|D3D12 warning:'){throw "$name / $Label failed: $text"}
    if($Configuration-eq'Debug'-and$text-notmatch'(Debug Layer|Validation Layer) enabled'){throw "$name missing validation layer"}
    if($CaptureDocs-and$Label-match'^(basic|shadows|reflections|no-shadow|no-reflections|mask|reflection-view)$'){
        Copy-Item -LiteralPath $png -Destination (Join-Path $repoRoot "Docs/$Api/Images/$topic-$Label.png")
    }
    Write-Output "$Api $Label passed"
}
foreach($api in $Apis){
    foreach($technique in 1..3){
        $label=@('basic','shadows','reflections')[$technique-1]
        Run $api $technique $label
        Run $api $technique "stress-$technique" @('--workshop-scene','Stress','--camera','StressHero')
        Run $api $technique "odd-$technique" @('--test-odd') -Frames 20
        Run $api $technique "ui-$technique" @('--test-ui','--rt-test-scene','point-shadow') -Frames 36 -Ui
    }
    Run $api 2 'no-shadow' @('--no-shadow')
    Run $api 3 'no-reflections' @('--no-reflections')
    Run $api 3 'reflection-view' @('--shading','reflection')
    Run $api 1 'mask' @('--camera','Occlusion')
    foreach($case in @('albedo','transform','mask','mask-back')){Run $api 1 "fixture-$case" @('--rt-test-scene',$case)}
    foreach($case in @('sun-shadow','point-shadow','point-beyond','mask-shadow-hole','mask-shadow-solid')){
        Run $api 2 "fixture-$case" @('--rt-test-scene',$case,'--no-ibl','--no-bloom')
        Run $api 2 "fixture-$case-clear" @('--rt-test-scene',$case,'--no-ibl','--no-bloom','--no-shadow')
    }
    foreach($case in @('reflection','reflection-moved','reflection-shadow','reflection-mask-hole','reflection-mask-solid')){
        Run $api 3 "fixture-$case" @('--rt-test-scene',$case,'--no-ibl','--no-bloom')
    }
    Run $api 3 'fixture-reflection-shadow-clear' @('--rt-test-scene','reflection-shadow','--no-ibl','--no-bloom','--no-shadow')
    Run $api 3 'fixture-environment' @('--rt-test-scene','environment','--no-bloom')
    Run $api 3 'fixture-environment-black' @('--rt-test-scene','environment','--no-bloom','--no-ibl')
    & $PythonPath (Join-Path $repoRoot 'Tests/RaytracingGpuTests.py') $work $api
    if($LASTEXITCODE-ne 0){throw "Raytracing GPU checks failed: $api"}
}
