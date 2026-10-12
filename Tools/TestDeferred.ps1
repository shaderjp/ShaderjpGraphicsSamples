param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [ValidateSet('Direct3D12','Vulkan')][string[]]$Apis=@('Direct3D12','Vulkan'),
    [switch]$CaptureDocs,
    [switch]$Benchmark,
    [switch]$BenchmarkOnly,
    [string]$PythonPath='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/deferred-tests/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
function Run([string]$Api,[string]$Topic,[string]$Label,[string[]]$Extra=@(),[int]$Frames=3,[switch]$Ui,[switch]$Gpu) {
    $name="${Api}_${Topic}";$png=Join-Path $work "$Api-$Label.png";$log=Join-Path $work "$Api-$Label.log"
    $arguments=@('--frames',"$Frames",'--log',('"'+$log+'"'),'--screenshot',('"'+$png+'"'))
    if(-not $Ui){$arguments+='--no-ui'};if($Gpu){$arguments+='--capture-gpu'};$arguments+=$Extra
    $previous=$env:VK_LAYER_VALIDATE_SYNC
    try {if($Api -eq 'Vulkan' -and $Configuration -eq 'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $process=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
    }finally{$env:VK_LAYER_VALIDATE_SYNC=$previous}
    if(-not $process.WaitForExit(60000)){$process.Kill();throw "$name / $Label timed out"}
    $text=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0' -or $text -match '(?m)^VALIDATION ERROR:|D3D12 warning:'){throw "$name / $Label failed: $text"}
    if($CaptureDocs -and $Label -match '^(ps-base|cs-base|normal|depth|material|stress|transparent|extract|shadow-map)$'){
        $dest=Join-Path $repoRoot "Docs/$Api/Images/$Topic-$Label.png";Copy-Item -LiteralPath $png -Destination $dest
    }
    Write-Output "$Api $Label passed"
}
foreach($api in $Apis){
    if(-not $BenchmarkOnly){
    foreach($entry in @(@('17_DeferredPixel','ps'),@('18_DeferredCompute','cs'))){
        $topic=$entry[0];$kind=$entry[1]
        Run $api $topic "$kind-base" -Gpu
        Run $api $topic "$kind-stress" @('--workshop-scene','Stress','--camera','StressHero') -Gpu
        Run $api $topic "$kind-full" @('--preset','Full','--camera','Transparency') -Gpu
        Run $api $topic "$kind-forward" @('--forward') -Gpu
        Run $api $topic "$kind-odd" @('--test-odd') -Frames 20 -Gpu
        Run $api $topic "$kind-ui" @('--test-ui') -Frames 36 -Ui
    }
    $topic='17_DeferredPixel'
    Run $api $topic 'no-shadow' @('--no-shadow') -Gpu
    Run $api $topic 'hard-shadow' @('--hard-shadow') -Gpu
    Run $api $topic 'moved' @('--occluder-x','1') -Gpu
    Run $api $topic 'opaque' @('--preset','Opaque') -Gpu
    Run $api $topic 'no-bloom' @('--no-bloom')
    Run $api $topic 'black' @('--test-image','black') -Gpu
    Run $api $topic 'constant-odd' @('--test-image','constant','--test-odd') -Frames 20 -Gpu
    Run $api $topic 'material' @('--camera','Materials')
    Run $api $topic 'occlusion' @('--camera','Occlusion')
    Run $api $topic 'transparent' @('--preset','Full','--camera','Transparency')
    Run $api $topic 'stress' @('--workshop-scene','Stress','--camera','StressHero')
    foreach($view in @('normal','normal-packed','base','ao','metallic','roughness','emissive','depth','shadow-depth','direct','diffuse','specular','shadow','extract','horizontal','bloom')){Run $api $topic $view @('--shading',$view)}
    & $PythonPath (Join-Path $repoRoot 'Tests/DeferredGpuTests.py') $work $api $repoRoot
    if($LASTEXITCODE -ne 0){throw "Deferred numeric verification failed: $api"}
    }
    if($Benchmark -or $BenchmarkOnly){
        if($Configuration -ne 'Release'){throw 'Benchmark requires Release'}
        foreach($scene in @('Standard','Stress')){foreach($topic in @('17_DeferredPixel','18_DeferredCompute')){
            $name="${api}_${topic}";$log=Join-Path $work "$name-$scene-benchmark.log";$output=Join-Path $work "$name-$scene-timings.json"
            $arguments=@('--benchmark','--workshop-scene',$scene,'--timings',('"'+$output+'"'),'--log',('"'+$log+'"'))
            $process=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
            if(-not $process.WaitForExit(180000)){$process.Kill();throw 'Benchmark timed out'}
            $result=Get-Content -LiteralPath $output -Raw|ConvertFrom-Json
            if($process.ExitCode -ne 0 -or $result.measuredFrames -ne 1200 -or $result.passes.total.averageMs -le 0){throw 'Benchmark result invalid'}
            $dxc=if($api -eq 'Direct3D12'){Join-Path $repoRoot 'packages/Microsoft.Direct3D.DXC.1.9.2609.5/build/native/bin/x64/dxc.exe'}else{Join-Path $env:VULKAN_SDK 'Bin/dxc.exe'}
            $result|Add-Member dxcVersion ((& $dxc --version|Out-String).Trim())
            $result|Add-Member vulkanSdk $(if($api -eq 'Vulkan'){Split-Path $env:VULKAN_SDK -Leaf}else{$null})
            $result|Add-Member driverVersion ((Get-CimInstance Win32_VideoController|Where-Object Name -eq $result.gpu|Select-Object -First 1).DriverVersion)
            $result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $output -Encoding utf8
            Write-Output "$name $scene benchmark passed"
        }}
    }
}
