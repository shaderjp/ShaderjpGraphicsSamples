param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/deferred-failures/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
foreach($api in @('Direct3D12','Vulkan')){
    foreach($topic in @('17_DeferredPixel','18_DeferredCompute')){
        $name="${api}_${topic}";$source=Join-Path $repoRoot "bin/x64/$Configuration/$name"
        $cases=@('GBufferPixel',$(if($topic -eq '17_DeferredPixel'){'LightingPixel'}else{'LightingCompute'}))
        foreach($shader in $cases){
            $directory=Join-Path $work "$name-$shader"
            New-Item -ItemType Directory -Path $directory -Force|Out-Null
            Copy-Item -LiteralPath (Join-Path $source "$name.exe") -Destination $directory
            Copy-Item -LiteralPath (Join-Path $source 'Shaders') -Destination $directory -Recurse -Force
            if($api -eq 'Direct3D12'){Copy-Item -LiteralPath (Join-Path $source 'D3D12') -Destination $directory -Recurse -Force}
            $extension=if($api -eq 'Direct3D12'){'cso'}else{'spv'}
            Remove-Item -LiteralPath (Join-Path $directory "Shaders/$shader.$extension")
            $log=Join-Path $directory 'failure.log';$scene=Join-Path $repoRoot 'Assets/Scenes/Workshop/Workshop.json'
            $arguments=@('--frames','1','--no-ui','--scene',('"'+$scene+'"'),'--log',('"'+$log+'"'))
            $process=Start-Process -FilePath (Join-Path $directory "$name.exe") -ArgumentList $arguments -WorkingDirectory $directory -WindowStyle Hidden -PassThru
            if(-not $process.WaitForExit(60000)){$process.Kill();throw 'Failure test timed out'}
            $text=Get-Content -LiteralPath $log -Raw
            if($process.ExitCode -ne 1 -or $text -notmatch $shader -or $text -notmatch 'Validation errors: 0'){throw "$name missing $shader did not shut down cleanly: $text"}
            Write-Output "$name missing $shader passed"
        }
    }
}
