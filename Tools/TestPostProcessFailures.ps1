param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path -LiteralPath (Split-Path $PSScriptRoot -Parent)).Path
$work=Join-Path $repoRoot ('.cache/post-failures/'+$Configuration+'-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force|Out-Null
foreach($api in @('Direct3D12','Vulkan')){
    $name=$api+'_16_GpuParticles'
    $fixture=Join-Path $work $name
    Copy-Item -LiteralPath (Join-Path $repoRoot "bin/x64/$Configuration/$name") -Destination $fixture -Recurse
    $fixture=(Resolve-Path -LiteralPath $fixture).Path
    foreach($shader in @('PostPixel','Extract','ParticleUpdate')){
        $extension=if($api -eq 'Vulkan'){'spv'}else{'cso'}
        $target=[IO.Path]::GetFullPath((Join-Path $fixture "Shaders/$shader.$extension"))
        if(-not $target.StartsWith($fixture+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Failure fixture outside test directory'}
        $backup=$target+'.test-backup';$log=Join-Path $work "$api-$shader.log"
        try{
            Move-Item -LiteralPath $target -Destination $backup
            $arguments=@('--frames','8','--log',('"'+$log+'"'))
            $old=$env:VK_LAYER_VALIDATE_SYNC
            try{
                if($Configuration -eq 'Debug' -and $api -eq 'Vulkan'){$env:VK_LAYER_VALIDATE_SYNC='1'}
                $p=Start-Process -FilePath (Join-Path $fixture "$name.exe") -ArgumentList $arguments -WorkingDirectory $work -PassThru -WindowStyle Hidden
            }finally{$env:VK_LAYER_VALIDATE_SYNC=$old}
            if(-not $p.WaitForExit(30000)){$p.Kill();throw 'Post failure test timeout'}
            $text=Get-Content -LiteralPath $log -Raw
            if($p.ExitCode -ne 1 -or $text -notmatch [regex]::Escape("$shader.$extension") -or $text -notmatch 'Validation errors: 0' -or $text -match '(?m)^VALIDATION ERROR:|D3D12 warning:'){throw "$api $shader did not fail cleanly: $text"}
            Write-Output "$api $Configuration missing ${shader}: named error, exit 1, validation errors 0"
        }finally{if(Test-Path -LiteralPath $backup){Move-Item -LiteralPath $backup -Destination $target}}
    }
}
