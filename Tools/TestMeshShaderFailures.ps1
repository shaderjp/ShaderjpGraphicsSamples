param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo ('.cache/mesh-shader-failures/'+$Configuration+'-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force|Out-Null
$topics=@('Advanced04_MeshShaderBasic','Advanced05_GpuDrivenRendering','Advanced06_MeshletCulling','Advanced07_MeshShaderShadows','Advanced08_MeshShaderDeferred','Advanced09_MeshShaderTiledDeferred')
foreach($api in @('Direct3D12','Vulkan')){foreach($topic in $topics){
    $name="${api}_$topic";$source=Join-Path $repo "bin/x64/$Configuration/$name"
    foreach($case in @('full','path','shader')){
        $folder=Join-Path $work "$name-$case";New-Item -ItemType Directory -Path $folder|Out-Null
        if($case-eq'shader'){
            Copy-Item -LiteralPath (Join-Path $source "$name.exe") -Destination $folder
            foreach($dll in Get-ChildItem -LiteralPath $source -Filter '*.dll'){Copy-Item -LiteralPath $dll.FullName -Destination $folder}
            if(Test-Path (Join-Path $source 'D3D12')){Copy-Item -LiteralPath (Join-Path $source 'D3D12') -Destination $folder -Recurse}
            Copy-Item -LiteralPath (Join-Path $source 'Shaders') -Destination $folder -Recurse
            New-Item -ItemType Junction -Path (Join-Path $folder 'Assets') -Target (Join-Path $source 'Assets')|Out-Null
            $shader=if($topic-like'Advanced04*'){'Mesh'}else{'MeshletMesh'};$extension=if($api-eq'Direct3D12'){'cso'}else{'spv'}
            $target=[IO.Path]::GetFullPath((Join-Path $folder "Shaders/$shader.$extension"));if(-not $target.StartsWith([IO.Path]::GetFullPath($work)+[IO.Path]::DirectorySeparatorChar)){throw 'Unsafe shader fixture path'}
            Remove-Item -LiteralPath $target
            $exe=Join-Path $folder "$name.exe"
        }else{$exe=Join-Path $source "$name.exe"}
        $log=Join-Path $folder 'failure.log';$extra=if($case-eq'full'){@('--preset','Full')}elseif($case-eq'path'){@('--render-path',$(if($topic-like'Advanced05*'){'task'}else{'indirect'}))}else{@()}
        $arguments=@('--frames','2','--no-ui','--log',('"'+$log+'"'))+$extra
        $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $folder -WindowStyle Hidden -PassThru
        if(-not $process.WaitForExit(60000)){$process.Kill();throw "$name / $case timed out"}
        $text=Get-Content -LiteralPath $log -Raw
        if($process.ExitCode-ne 1-or$text-notmatch'Validation errors: 0'-or$text-match'(?m)^VALIDATION ERROR:| warning:'){throw "$name / $case failed: $text"}
        $pattern=if($case-eq'full'){'Opaque|BLEND|Full'}elseif($case-eq'path'){'render path|render-path'}else{'Shader|shader|Mesh'}
        if($text-notmatch$pattern){throw "$name / $case missing actionable error"}
        Write-Output "$name / $case partial initialization and teardown passed"
    }
}}
