param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [ValidateSet('Direct3D12','Vulkan')][string[]]$Apis=@('Direct3D12','Vulkan'),
    [switch]$CaptureDocs,[switch]$Benchmark,[switch]$BenchmarkOnly,[switch]$FixturesOnly,[switch]$SkipCompleted,
    [string]$PythonPath='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/inline-tests/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
$basic26='26_InlineRaytracingShadows';$basic27='27_InlineRaytracingReflections'
$hybrid='Advanced16_BindlessInlineHybrid';$many='Advanced17_MeshShaderTiledInlineShadows'
function WaitForExternalBenchmarks {
    $announced=$false;$quietSince=$null
    do {
        $active=@(Get-CimInstance Win32_Process | Where-Object {
            ($_.Name-match'^(Direct3D12|Vulkan)_' -and $_.CommandLine-match'--benchmark') -or
            ($_.Name-eq'pwsh.exe' -and $_.CommandLine-match'[/\\]MeasureBindless\.ps1')
        })
        if($active.Count){
            if(!$announced){Write-Output 'Waiting for the existing GPU benchmark to finish';$announced=$true}
            $quietSince=$null
        }else{
            if(!$announced){return}
            if(!$quietSince){$quietSince=Get-Date}
            if(((Get-Date)-$quietSince).TotalSeconds-ge10){return}
        }
        Start-Sleep -Seconds 1
    }while($true)
}
function Run([string]$Api,[string]$Topic,[string]$Label,[string[]]$Extra=@(),[int]$Frames=3,[switch]$Ui,[switch]$NoGpu) {
    $name=$Api+'_'+$Topic;$log=Join-Path $work "$Api-$Label.log";$image=Join-Path $work "$Api-$Label.png"
    if($SkipCompleted-and(Test-Path -LiteralPath $image)-and(Test-Path -LiteralPath $log)){
        $oldLog=Get-Content -LiteralPath $log -Raw
        $gpuDirectory=Join-Path $work "$Api-$Label-gpu"
        if($oldLog-match'Validation errors: 0'-and$oldLog-notmatch'ERROR:|VALIDATION ERROR:|D3D12 warning:'-and($NoGpu-or(Test-Path -LiteralPath (Join-Path $gpuDirectory 'metadata.json')))){
            Write-Output "$Api $Label cached PASS";return
        }
    }
    WaitForExternalBenchmarks
    $arguments=@('--frames',"$Frames",'--log',$log,'--screenshot',$image)+$Extra
    if(!$Ui){$arguments+='--no-ui'};if(!$NoGpu){$arguments+='--capture-gpu'}
    $previous=$env:VK_LAYER_VALIDATE_SYNC
    try{
        if($Api-eq'Vulkan'-and$Configuration-eq'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $process=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
    }finally{$env:VK_LAYER_VALIDATE_SYNC=$previous}
    if(!$process.WaitForExit(300000)){$process.Kill();throw "$name timeout: $Label"}
    $text=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -or $text-notmatch'Validation errors: 0' -or $text-match'VALIDATION ERROR:|D3D12 warning:'){throw "$name failed: $Label ($log)"}
    if($Configuration-eq'Debug'-and$text-notmatch'(Debug Layer|Validation Layer) enabled'){throw "Validation disabled: $Label"}
    if($CaptureDocs-and$Topic-in@($basic26,$basic27,$hybrid,$many)-and$Label-match'^(basic26|map|no-shadow|basic27|reflection-off|reflection|fixture-reflection|fixture-reflection-mask-hole|fixture-reflection-mask-solid|fixture-reflection-sun-shadow|fixture-reflection-sun-shadow-clear|fixture-environment|fixture-environment-black|many-tile8|many-all8|many-map0|many-all|hybrid-textures)$'){
        Copy-Item -LiteralPath $image -Destination (Join-Path $repoRoot "Docs/$Api/Images/$Topic-$Label.png")
    }
    Write-Output "$Api $Label PASS"
}
function Failure([string]$Api,[string]$Topic,[string]$Label,[string[]]$Extra,[string]$Expected) {
    $name=$Api+'_'+$Topic;$log=Join-Path $work "$Api-invalid-$Label.log"
    $process=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList (@('--frames','1','--no-ui','--log',$log)+$Extra) -WorkingDirectory $work -WindowStyle Hidden -PassThru
    if(!$process.WaitForExit(30000)){$process.Kill();throw "Failure test timeout: $Label"}
    if(!$process.ExitCode-or(Get-Content -LiteralPath $log -Raw)-notmatch$Expected){throw "Invalid input accepted: $Label"}
    Write-Output "$Api invalid $Label PASS"
}
if($FixturesOnly){foreach($api in $Apis){
    foreach($case in @('reflection','reflection-moved','reflection-mask-hole','reflection-mask-solid','reflection-sun-shadow')){
        Run $api $basic27 "fixture-$case" @('--rt-test-scene',$case,'--shading','reflection','--no-bloom','--no-ibl')
    }
    Run $api $basic27 'fixture-reflection-sun-shadow-clear' @('--rt-test-scene','reflection-sun-shadow','--shading','reflection','--no-shadow','--no-bloom','--no-ibl')
}}
if(!$BenchmarkOnly-and!$FixturesOnly){foreach($api in $Apis){
    Run $api $basic26 'basic26'
    Run $api $basic26 'map' @('--sun-shadow','map')
    Run $api '18_DeferredCompute' 'deferred18'
    Run $api $basic26 'no-shadow' @('--no-shadow')
    Run $api $basic26 'moved' @('--occluder-x','1')
    Run $api $basic26 'stress26' @('--workshop-scene','Stress','--camera','StressHero')
    Run $api $basic27 'basic27'
    Run $api $basic27 'reflection-off' @('--no-reflections')
    Run $api $basic27 'reflection' @('--shading','reflection')
    Run $api $basic26 'fixture-sun-shadow' @('--rt-test-scene','sun-shadow','--no-ibl','--no-bloom')
    Run $api $basic26 'fixture-sun-shadow-clear' @('--rt-test-scene','sun-shadow','--no-ibl','--no-bloom','--no-shadow')
    Run $api $basic26 'fixture-sun-unoccluded' @('--rt-test-scene','sun-unoccluded','--no-ibl','--no-bloom')
    Run $api $basic26 'fixture-sun-unoccluded-clear' @('--rt-test-scene','sun-unoccluded','--no-ibl','--no-bloom','--no-shadow')
    foreach($case in @('reflection','reflection-moved','reflection-mask-hole','reflection-mask-solid','reflection-mask-back','reflection-single-back','environment','reflection-sun-shadow')){
        $fixture=@('--rt-test-scene',$case,'--shading','reflection','--no-bloom')
        if($case-ne'environment'){$fixture+='--no-ibl'}
        Run $api $basic27 "fixture-$case" $fixture
    }
    Run $api $basic27 'fixture-environment-black' @('--rt-test-scene','environment','--shading','reflection','--no-ibl','--no-bloom')
    Run $api $basic27 'fixture-reflection-sun-shadow-clear' @('--rt-test-scene','reflection-sun-shadow','--shading','reflection','--no-shadow','--no-bloom','--no-ibl')
    foreach($mode in @('bound','textures','resources')){
        $gallery=@('--gallery-preset','Mini','--materials','64','--binding',$mode)
        Run $api $hybrid "hybrid-$mode" $gallery
        Run $api 'Advanced15_BindlessHybrid' "pipeline-$mode" $gallery
    }
    Run $api $hybrid 'hybrid-driven' @('--gallery-preset','Mini','--materials','64','--gpu-driven')
    Run $api 'Advanced15_BindlessHybrid' 'pipeline-driven' @('--gallery-preset','Mini','--materials','64','--gpu-driven')
    Run $api $hybrid 'hybrid-driven-single' @('--gallery-preset','Mini','--materials','64','--objects','1','--gpu-driven')
    Run $api 'Advanced15_BindlessHybrid' 'pipeline-driven-single' @('--gallery-preset','Mini','--materials','64','--objects','1','--gpu-driven')
    foreach($mode in @('all','tile')){
        Run $api $many "many-$($mode)8" @('--light-culling',$mode,'--shadow-point-lights','8')
    }
    Run $api $many 'many-map0' @('--sun-shadow','map','--shadow-point-lights','0')
    Run $api 'Advanced09_MeshShaderTiledDeferred' 'mesh09'
    Run $api $many 'many-all' @('--shadow-point-lights','all')
    Run $api $many 'many-overflow' @('--list-capacity','1')
    Run $api $many 'many-moving' @('--animate-lights')
    Run $api $many 'many-empty' @('--mesh-test-scene','empty')
    Run $api $many 'many-stress' @('--workshop-scene','Stress','--camera','StressHero')
    Run $api $many 'many-ray-count' @('--shading','ray-count')
    foreach($case in @('point-shadow','point-beyond','mask-shadow-hole','mask-shadow-solid')){
        $fixture=@('--rt-test-scene',$case,'--lights-per-room','0','--shadow-point-lights','all','--no-ibl','--no-bloom')
        Run $api $many "fixture-$case" $fixture
        Run $api $many "fixture-$case-clear" ($fixture+@('--no-shadow'))
    }
    foreach($entry in @(@($basic26,'26'),@($basic27,'27'),@($hybrid,'16'),@($many,'17'))){
        Run $api $entry[0] "odd-$($entry[1])" @('--test-odd') -Frames 20 -NoGpu
        $uiExtra=@('--test-ui');$uiLabel="ui-$($entry[1])-controls"
        if($entry[1]-eq'17'){$uiExtra+=@('--mesh-test-scene','uv');$uiLabel='ui-17-uv-hdr'}
        Run $api $entry[0] $uiLabel $uiExtra -Frames 36 -Ui -NoGpu:($entry[1]-ne'17')
    }
    Failure $api $basic26 'sun' @('--sun-shadow','invalid') 'sun-shadow'
    Failure $api $many 'count' @('--shadow-point-lights','-1') 'shadow-point-lights'
    Failure $api $many 'capacity' @('--shadow-point-lights','4681') 'shadow-point-lights'
    Failure $api $many 'cluster' @('--light-culling','cluster') 'all / tile'
    Failure $api $basic26 'blend' @('--preset','Full') 'OPAQUE/MASK'
    Failure $api $hybrid 'gallery-blend' @('--preset','Full') 'OPAQUE/MASK'
    & $PythonPath (Join-Path $repoRoot 'Tests/InlineRaytracingGpuTests.py') $work $api
    if($LASTEXITCODE){throw "$api numeric verification failed"}
    if($CaptureDocs){Copy-Item (Join-Path $work "$api-pipeline-textures.png") (Join-Path $repoRoot "Docs/$api/Images/$hybrid-pipeline-textures.png")}
}}
if($Benchmark-or$BenchmarkOnly){
    if($Configuration-ne'Release'){throw 'Benchmark requires Release'}
    . (Join-Path $PSScriptRoot 'BindlessEnvironment.ps1')
    $environment=Get-BindlessEnvironment $repoRoot
    $environment.binaries=@{}
    $runtimeHashes=[ordered]@{}
    foreach($api in $Apis){foreach($topic in @($basic26,$basic27,$hybrid,$many,'Advanced15_BindlessHybrid')){
        $name=$api+'_'+$topic;$directory=Join-Path $repoRoot "bin/x64/Release/$name"
        $binary=@{executableSha256=(Get-FileHash (Join-Path $directory "$name.exe")).Hash.ToLowerInvariant();shaders=@{}}
        $runtimeHashes[(Join-Path $directory "$name.exe")]=$binary.executableSha256
        foreach($shader in Get-ChildItem (Join-Path $directory 'Shaders') -File|Sort-Object Name){
            $hash=(Get-FileHash $shader.FullName).Hash.ToLowerInvariant()
            $binary.shaders[$shader.Name]=$hash;$runtimeHashes[$shader.FullName]=$hash
        }
        $environment.binaries[$name]=$binary
    }}
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=(& $vswhere -latest -products '*' -format json -utf8|ConvertFrom-Json)[0]
    $environment.visualStudio=@{version=$vs.installationVersion;displayVersion=$vs.catalog.productDisplayVersion}
    $environment.spirvTools=(& "$env:VULKAN_SDK/Bin/spirv-val.exe" --version)-join "`n"
    $environment | ConvertTo-Json -Depth 12 | Set-Content (Join-Path (Split-Path $work -Parent) 'Environment.json') -Encoding utf8
    function AssertRuntimeUnchanged {
        foreach($entry in $runtimeHashes.GetEnumerator()){
            if((Get-FileHash -LiteralPath $entry.Key).Hash.ToLowerInvariant()-ne$entry.Value){throw "Benchmark runtime changed: $($entry.Key)"}
        }
    }
    $cases=@(
        @{topic=$basic26;label=$basic26;extra=@()},
        @{topic=$basic26;label='ShadowMap';extra=@('--sun-shadow','map')},
        @{topic=$basic27;label=$basic27;extra=@()},
        @{topic=$hybrid;label=$hybrid;extra=@('--gallery-preset','Standard','--objects','512','--materials','64','--binding','textures')},
        @{topic='Advanced15_BindlessHybrid';label='PipelineHybrid';extra=@('--gallery-preset','Standard','--objects','512','--materials','64','--binding','textures')},
        @{topic=$many;label=$many;extra=@()},
        @{topic=$many;label='ManyAll8';extra=@('--light-culling','all')},
        @{topic=$many;label='ManyTile32';extra=@('--shadow-point-lights','32')}
    )
    foreach($api in $Apis){foreach($case in $cases){foreach($iteration in 1..5){
        $name=$api+'_'+$case.topic;$label="$api-$($case.label)-benchmark-$iteration"
        $seal=Join-Path $work "$label-runtime.json"
        if($SkipCompleted-and(Test-Path $seal)-and(Test-Path (Join-Path $work "$label.json"))){
            $oldSeal=Get-Content $seal -Raw|ConvertFrom-Json
            if($oldSeal.runtimeHashes-eq($runtimeHashes|ConvertTo-Json -Compress)){
                $oldReport=Get-Content (Join-Path $work "$label.json") -Raw|ConvertFrom-Json
                if($oldReport.measuredFrames-eq1200-and(Test-Path (Join-Path $work "$label.csv"))){Write-Output "$label cached PASS";continue}
            }
        }
        $args=@('--benchmark','--fixed-dt','0.0166666667','--timings',(Join-Path $work "$label.json"),'--log',(Join-Path $work "$label.log"))+$case.extra
        do{
            AssertRuntimeUnchanged
            WaitForExternalBenchmarks
            while(@(Get-Process -Name Direct3D12_*,Vulkan_* -ErrorAction SilentlyContinue).Count){Start-Sleep -Seconds 5}
            $process=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/Release/$name/$name.exe") -ArgumentList $args -WorkingDirectory $work -WindowStyle Hidden -PassThru
            $clean=$true;$started=Get-Date
            while(!$process.WaitForExit(2000)){
                if(@(Get-Process -Name Direct3D12_*,Vulkan_* -ErrorAction SilentlyContinue | Where-Object {$_.Id-ne$process.Id}).Count){$clean=$false}
                if(((Get-Date)-$started).TotalSeconds-gt600){$process.Kill();throw "Benchmark timeout: $label"}
            }
            if($process.ExitCode){throw "Benchmark failed: $label"}
            if(!$clean){Write-Output "$label overlapped another sample; retrying"}
        }while(!$clean)
        AssertRuntimeUnchanged
        $text=Get-Content (Join-Path $work "$label.log") -Raw
        if($text-notmatch'Validation errors: 0'-or$text-match'VALIDATION ERROR:|D3D12 warning:|ERROR:'){throw "Benchmark log failed: $label"}
        $result=Get-Content (Join-Path $work "$label.json") -Raw|ConvertFrom-Json
        if($result.measuredFrames-ne1200){throw "Benchmark frame count: $label"}
        @{runtimeHashes=($runtimeHashes|ConvertTo-Json -Compress);arguments=$args;completedUtc=[DateTime]::UtcNow.ToString('o')}|
            ConvertTo-Json -Depth 5|Set-Content $seal -Encoding utf8
        Write-Output "$label PASS"
    }}}
}
