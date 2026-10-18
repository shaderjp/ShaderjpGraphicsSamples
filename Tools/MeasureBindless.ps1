param([string[]]$Apis=@('Direct3D12','Vulkan'),[ValidateRange(1,5)][int]$Repeats=5,[switch]$FullMatrix,[switch]$Resume,[switch]$ListOnly,[switch]$VerifyRuntimeOnly,[string]$RuntimeRoot='',[string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$collectionStarted=[DateTime]::UtcNow
$work=if($OutputDirectory){[IO.Path]::GetFullPath($OutputDirectory)}else{Join-Path $repoRoot '.cache/bindless-performance'}
if(-not $RuntimeRoot){$RuntimeRoot=Join-Path $repoRoot 'bin/x64/Release'}
$RuntimeRoot=[IO.Path]::GetFullPath($RuntimeRoot)
$snapshotHash=$null
if(-not $ListOnly){
    $snapshotPath=Join-Path $RuntimeRoot 'Snapshot.json'
    if(-not (Test-Path -LiteralPath $snapshotPath)){throw 'Create a frozen runtime first with Tools/FreezeBindlessRuntime.py and pass -RuntimeRoot.'}
    $snapshot=Get-Content -LiteralPath $snapshotPath -Raw|ConvertFrom-Json
    foreach($program in $snapshot.programs.PSObject.Properties){foreach($entry in $program.Value.PSObject.Properties){
        $path=Join-Path (Join-Path $RuntimeRoot $program.Name) $entry.Name
        if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $entry.Value){throw "Frozen runtime changed: $path"}
    }}
    $snapshotHash=(Get-FileHash -LiteralPath $snapshotPath).Hash.ToLowerInvariant()
}
if($VerifyRuntimeOnly){
    if($ListOnly){throw 'VerifyRuntimeOnly cannot be combined with ListOnly'}
    Write-Output "Frozen runtime verification PASS: $snapshotHash"
    return
}
function Get-ForeignActivity {
    $roots=Get-ChildItem -LiteralPath (Join-Path $repoRoot '.cache') -Directory|Where-Object{$_.Name -notlike 'bindless*'}
    return @($roots|ForEach-Object{Get-ChildItem -LiteralPath $_.FullName -Recurse -File -Filter '*.log' -ErrorAction SilentlyContinue})
}
function Get-ForeignSamples {
    return @(Get-Process -Name 'Direct3D12_*','Vulkan_*','cl','dxc','MSBuild' -ErrorAction SilentlyContinue)
}
New-Item -ItemType Directory -Path $work -Force|Out-Null
if(-not $ListOnly){Copy-Item -LiteralPath $snapshotPath -Destination (Join-Path $work 'RuntimeSnapshot.json')}
. (Join-Path $PSScriptRoot 'BindlessEnvironment.ps1')
$environment=if(-not $ListOnly){Get-BindlessEnvironment $repoRoot $RuntimeRoot}else{@{}}
if(-not $ListOnly){$environment|ConvertTo-Json -Depth 12|Set-Content (Join-Path $work 'Environment.json')}
$topics=@('Advanced12_BindlessMaterials','Advanced13_BindlessMeshShader','Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid')
foreach($api in $Apis){foreach($topic in $topics){
    # The default records all techniques and the two independent sweeps in the introductory sample.
    $cases=,@(512,64)
    if($FullMatrix -or $topic -like 'Advanced12*'){$cases=@(@(128,64),@(512,64),@(4096,64),@(512,16),@(512,256))}
    foreach($case in $cases){for($repeat=0;$repeat -lt $Repeats;++$repeat){
        $modes=if($topic -like 'Advanced12*'){@('bound','textures')}else{@('bound','textures','resources')}
        $variants=@($modes|ForEach-Object{,@($_,$false)})
        if($topic -match '^Advanced1[35]'){$variants+=,@('textures',$true);$variants+=,@('resources',$true)}
        $rotated=@(0..($variants.Count-1)|ForEach-Object{,$variants[($_+$repeat)%$variants.Count]})
        foreach($variant in $rotated){
            $mode=$variant[0];$driven=$variant[1];$suffix=if($driven){'-driven'}else{''}
            $label="$api-$topic-$($case[0])-$($case[1])-$mode$suffix-$repeat"
            if($ListOnly){Write-Output $label;continue}
            $output=Join-Path $work "$label.json";$log=Join-Path $work "$label.log"
            if($Resume -and (Test-Path -LiteralPath $output)){
                $previous=Get-Content -LiteralPath $output -Raw|ConvertFrom-Json
                if($previous.measuredFrames -eq 1200 -and $previous.environment -and $previous.provenance.runtimeSnapshotSha256 -eq $snapshotHash){
                    $key="$api`_$topic"
                    $currentAsset=(Get-Content -LiteralPath (Join-Path $repoRoot 'Assets/Scenes/BindlessGallery/Manifest.json') -Raw|ConvertFrom-Json).assetSha256
                    if($previous.assetHash -ne $currentAsset -or $previous.environment.binaries.$key.executableSha256 -ne $environment.binaries[$key].executableSha256){throw "Cannot resume mixed assets or executables: $label"}
                    continue
                }
            }
            $name="$api`_$topic"
            $args=@('--benchmark','--gallery-preset','Standard','--objects',"$($case[0])",'--materials',"$($case[1])",'--binding',$mode,'--timings',$output,'--log',$log)
            if($driven){$args+='--gpu-driven'}
            $executable=Join-Path $RuntimeRoot "$name/$name.exe"
            $attempt=0
            while($true){
                while((Get-ForeignSamples).Count){Start-Sleep -Seconds 2}
                $expectedHash=$environment.binaries[$name].executableSha256
                if((Get-FileHash -LiteralPath $executable).Hash.ToLowerInvariant() -ne $expectedHash){throw "Executable changed before $label"}
                $started=[DateTime]::UtcNow
                $p=Start-Process -FilePath $executable -ArgumentList $args -WorkingDirectory $work -WindowStyle Hidden -PassThru
                $observed=@{}
                while(-not $p.WaitForExit(1000)){
                    foreach($other in (Get-ForeignSamples|Where-Object{$_.Id -ne $p.Id})){$observed[$other.Id]=$other.ProcessName}
                    if(([DateTime]::UtcNow-$started).TotalSeconds -ge 600){$p.Kill();throw "Benchmark timeout: $label"}
                }
                $foreign=@(Get-ForeignActivity|Where-Object{$_.LastWriteTimeUtc -ge $started})
                if($foreign.Count -or $observed.Count -or (Get-ForeignSamples|Where-Object{$_.Id -ne $p.Id}).Count){
                    ++$attempt
                    $rejected=Join-Path $work 'Rejected'
                    New-Item -ItemType Directory -Path $rejected -Force|Out-Null
                    foreach($path in @($output,$log,([IO.Path]::ChangeExtension($output,'.csv')))){
                        if(Test-Path -LiteralPath $path){Copy-Item -LiteralPath $path -Destination (Join-Path $rejected ("$attempt-"+[IO.Path]::GetFileName($path)))}
                    }
                    @{processes=$observed;logs=@($foreign.FullName);startedUtc=$started.ToString('o')}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $rejected "$attempt-$label-overlap.json")
                    Write-Output "$label retry: overlapping sample/build activity"
                    Start-Sleep -Seconds 2
                    continue
                }
                if((Get-FileHash -LiteralPath $executable).Hash.ToLowerInvariant() -ne $expectedHash){throw "Executable changed during $label"}
                if($p.ExitCode -or (Get-Content -LiteralPath $log -Raw) -notmatch 'Validation errors: 0'){throw "Benchmark failure: $label"}
                break
            }
            $report=Get-Content -LiteralPath $output -Raw|ConvertFrom-Json
            if($report.measuredFrames -ne 1200 -or $report.warmupFrames -ne 60){throw "Incomplete benchmark: $label"}
            $report|Add-Member -NotePropertyName environment -NotePropertyValue $environment -Force
            $report|Add-Member -NotePropertyName measurementConditions -NotePropertyValue @{
                vsync=$false;uiDraw=$false;cameraPath='BindlessGallery/CameraPath.json';fixedDeltaSeconds=(1.0/60)
                warmupFrames=60;measuredFrames=1200;framesInFlight=1
            } -Force
            $report|Add-Member -NotePropertyName provenance -NotePropertyValue @{
                runtimeSnapshotSha256=$snapshotHash;executableVerifiedBeforeAndAfter=$true;foreignActivityDetected=$false
                startedUtc=$started.ToString('o');finishedUtc=[DateTime]::UtcNow.ToString('o');attempts=($attempt+1)
                processPollIntervalMs=1000
                executable=$executable;arguments=$args
            } -Force
            $report|ConvertTo-Json -Depth 20|Set-Content -LiteralPath $output
            Write-Output "$label PASS"
        }
    }}
}}
if($ListOnly){return}
foreach($program in $snapshot.programs.PSObject.Properties){foreach($entry in $program.Value.PSObject.Properties){
    $path=Join-Path (Join-Path $RuntimeRoot $program.Name) $entry.Name
    if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $entry.Value){throw "Frozen runtime changed during collection: $path"}
}}
@{
    runtimeSnapshotSha256=$snapshotHash;snapshotVerifiedBeforeCollection=$true;snapshotVerifiedAfterCollection=$true
    startedUtc=$collectionStarted.ToString('o');finishedUtc=[DateTime]::UtcNow.ToString('o');processPollIntervalMs=1000
    scriptSha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant()
    rejectedAttempts=@(Get-ChildItem -LiteralPath (Join-Path $work 'Rejected') -Filter '*-overlap.json' -ErrorAction SilentlyContinue).Count
}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $work 'CollectionAudit.json')
$python=Join-Path 'C:\Program Files\Blender Foundation\Blender 5.2' '5.2/python/bin/python.exe'
& $python (Join-Path $repoRoot 'Tools/SummarizeBindless.py') $work
if($LASTEXITCODE){throw 'Benchmark summary failed'}
