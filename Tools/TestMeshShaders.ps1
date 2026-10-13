param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [ValidateSet('Direct3D12','Vulkan')][string[]]$Apis=@('Direct3D12','Vulkan'),
    [switch]$CaptureDocs,[switch]$BenchmarkOnly,[switch]$StressLifecycleOnly,
    [string]$PythonPath='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo ".cache/mesh-shader-tests/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
$topics=@{4='Advanced04_MeshShaderBasic';5='Advanced05_GpuDrivenRendering';6='Advanced06_MeshletCulling';7='Advanced07_MeshShaderShadows';8='Advanced08_MeshShaderDeferred';9='Advanced09_MeshShaderTiledDeferred'}
function Run([string]$Api,[int]$Topic,[string]$Label,[string[]]$Extra=@(),[int]$Frames=2,[switch]$Ui){
    $name="${Api}_$($topics[$Topic])";$png=Join-Path $work "$Api-$Label.png";$log=Join-Path $work "$Api-$Label.log"
    $arguments=@('--frames',"$Frames",'--log',('"'+$log+'"'),'--screenshot',('"'+$png+'"'))
    if($Topic-ne 4){$arguments+='--capture-gpu'}
    if(-not $Ui){$arguments+='--no-ui'};$arguments+=$Extra
    $oldSync=$env:VK_LAYER_VALIDATE_SYNC
    try{if($Api-eq'Vulkan'-and$Configuration-eq'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $process=Start-Process -FilePath (Join-Path $repo "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
    }finally{$env:VK_LAYER_VALIDATE_SYNC=$oldSync}
    if(-not $process.WaitForExit(60000)){$process.Kill();throw "$name / $Label timed out"}
    $text=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode-ne 0-or$text-notmatch'Validation errors: 0'-or$text-match'(?m)^VALIDATION ERROR:| warning:'){throw "$name / $Label failed: $text"}
    if($Configuration-eq'Debug'-and$text-notmatch'(Debug Layer|Validation Layer) enabled'){throw "$name validation disabled"}
    if($CaptureDocs-and$Label-match'^([4-9])-(base|meshlets|off|shadow-off|gbuffer|light-count|overflow-view|overflow|stress)$'){
        Copy-Item -LiteralPath $png -Destination (Join-Path $repo "Docs/$Api/Images/$($topics[$Topic])-$($Matches[2]).png")
    }
    Write-Output "$Api $Label passed"
}
foreach($api in $Apis){
    if($StressLifecycleOnly){
        foreach($topic in 5..9){Run $api $topic "$topic-stress" @('--workshop-scene','Stress','--camera','StressHero','--test-odd') -Frames 48}
        & $PythonPath (Join-Path $repo 'Tests/MeshShaderGpuTests.py') $work $api
        if($LASTEXITCODE-ne 0){throw "$api Stress lifecycle numeric verification failed"}
        continue
    }
    if(-not $BenchmarkOnly){
        Run $api 4 '4-base';Run $api 4 '4-vertex' @('--render-path','vertex')
        Run $api 4 '4-uv' @('--mesh-test-scene','uv');Run $api 4 '4-transform' @('--mesh-test-scene','transform')
        Run $api 4 '4-ui' @('--test-ui') -Frames 36 -Ui
        Run $api 4 '4-odd' @('--test-odd') -Frames 20
        foreach($topic in 5..9){
            Run $api $topic "$topic-base"
            Run $api $topic "$topic-stress" @('--workshop-scene','Stress','--camera','StressHero','--test-odd') -Frames 48
            Run $api $topic "$topic-vertex" @('--render-path','vertex')
            Run $api $topic "$topic-mesh" @('--render-path','mesh')
            Run $api $topic "$topic-off" @('--meshlet-culling','off')
            Run $api $topic "$topic-frustum" @('--meshlet-culling','frustum')
            Run $api $topic "$topic-meshlets" @('--meshlet-view','meshlets','--no-bloom')
            Run $api $topic "$topic-odd" @('--test-odd') -Frames 20
            Run $api $topic "$topic-ui" @('--test-ui','--mesh-test-scene','uv') -Frames 36 -Ui
            foreach($fixture in @('empty','all-visible','backface','mask','mask-back')){Run $api $topic "$topic-$fixture" @('--mesh-test-scene',$fixture,'--no-bloom')}
        }
        Run $api 7 '7-shadow-off' @('--no-shadow')
        Run $api 7 '7-shadow-fixture' @('--mesh-test-scene','shadow','--no-ibl','--no-bloom')
        Run $api 7 '7-shadow-fixture-off' @('--mesh-test-scene','shadow','--no-shadow','--no-ibl','--no-bloom')
        Run $api 8 '8-gbuffer' @('--shading','normal')
        Run $api 9 '9-all' @('--light-culling','all')
        Run $api 9 '9-light-count' @('--light-view','count')
        Run $api 9 '9-overflow' @('--list-capacity','1')
        Run $api 9 '9-overflow-view' @('--list-capacity','1','--light-view','overflow')
        Run $api 9 '9-maximum' @('--workshop-scene','Stress','--camera','StressHero','--lights-per-room','512')
        & $PythonPath (Join-Path $repo 'Tests/MeshShaderGpuTests.py') $work $api
        if($LASTEXITCODE-ne 0){throw "$api GPU numeric verification failed"}
    }
    if($BenchmarkOnly){
        if($Configuration-ne'Release'){throw 'Performance measurements require Release'}
        $measurements=Join-Path $repo 'Docs/Measurements/MeshShader';New-Item -ItemType Directory -Path $measurements -Force|Out-Null
        foreach($topic in 5..9){foreach($scene in @('Standard','Stress')){
            $camera=if($scene-eq'Stress'){'StressHero'}else{'Hero'}
            $cases=if($topic-eq5){@(@('vertex','off'),@('mesh','off'),@('indirect','off'),@('indirect','frustum-cone'))}else{@(@('task','off'),@('task','frustum-cone'))}
            if($topic-eq9){$cases+=,@('task','frustum-cone','all')}
            foreach($case in $cases){
                $name="${api}_$($topics[$topic])";$label="$api-$topic-$scene-$($case[0])-$($case[1])";if($case.Count-eq3){$label+='-all'};$json=Join-Path $measurements "$label.json";$log=Join-Path $work "$label.log"
                $running=@(Get-Process -Name 'Direct3D12_*','Vulkan_*' -ErrorAction SilentlyContinue);if($running.Count){throw 'Another graphics sample is running; benchmark must be isolated'}
                $arguments=@('--frames','1260','--benchmark','--no-ui','--fixed-dt','0.0166666667','--camera-path','--workshop-scene',$scene,'--camera',$camera,'--render-path',$case[0],'--meshlet-culling',$case[1],'--timings',('"'+$json+'"'),'--log',('"'+$log+'"'))
                if($case.Count-eq3){$arguments+=@('--light-culling','all')}
                $process=Start-Process -FilePath (Join-Path $repo "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
                if(-not $process.WaitForExit(120000)){$process.Kill();throw "$label timed out"}
                $text=Get-Content -LiteralPath $log -Raw;if($process.ExitCode-ne 0-or$text-notmatch'Validation errors: 0'){throw "$label failed: $text"}
                $report=Get-Content $json -Raw|ConvertFrom-Json;if($report.measuredFrames-ne1200){throw "$label missing measurements"};Write-Output "$label measured"
            }
        }}
    }
}
