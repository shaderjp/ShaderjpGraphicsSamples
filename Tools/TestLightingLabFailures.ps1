param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path -LiteralPath (Split-Path $PSScriptRoot -Parent)).Path
$work=Join-Path $repoRoot ('.cache/lab-failures/'+$Configuration+'-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force|Out-Null
foreach($api in @('Direct3D12','Vulkan')){
    $name=$api+'_13_ShadowMap'
    $source=Join-Path $repoRoot "bin/x64/$Configuration/$name"
    $fixture=Join-Path $work $name
    Copy-Item -LiteralPath $source -Destination $fixture -Recurse
    $fixture=(Resolve-Path -LiteralPath $fixture).Path
    foreach($case in @('scene','dds','shader')){
        $log=Join-Path $work "$api-$case.log"
        $arguments=@('--frames','8','--log',('"'+$log+'"'))
        $target=$null;$backup=$null
        try{
            if($case -eq 'scene'){$arguments+=@('--scene',('"'+(Join-Path $fixture 'missing-scene.json')+'"'));$expected='missing-scene.json'}
            else{
                $relative=if($case -eq 'dds'){'Assets/Environments/LightingLab/Orientation.dds'}else{'Shaders/Pixel.'+$(if($api -eq 'Vulkan'){'spv'}else{'cso'})}
                $target=[IO.Path]::GetFullPath((Join-Path $fixture $relative));$backup=$target+'.test-backup'
                if(-not $target.StartsWith($fixture+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Failure fixture outside workspace'}
                Move-Item -LiteralPath $target -Destination $backup
                $expected=[IO.Path]::GetFileName($target)
            }
            $old=$env:VK_LAYER_VALIDATE_SYNC
            try{
                if($Configuration -eq 'Debug' -and $api -eq 'Vulkan'){$env:VK_LAYER_VALIDATE_SYNC='1'}
                $p=Start-Process -FilePath (Join-Path $fixture "$name.exe") -ArgumentList $arguments -WorkingDirectory $work -PassThru -WindowStyle Hidden
            }finally{$env:VK_LAYER_VALIDATE_SYNC=$old}
            if(-not $p.WaitForExit(30000)){$p.Kill();throw 'Failure test timeout'}
            $text=Get-Content -LiteralPath $log -Raw
            if($p.ExitCode -ne 1 -or $text -notmatch [regex]::Escape($expected) -or $text -notmatch 'Validation errors: 0'){throw "$api $case did not fail cleanly: $text"}
            Write-Output "$api $Configuration missing ${case}: named error, exit 1, validation errors 0"
        }finally{if($backup -and (Test-Path -LiteralPath $backup)){Move-Item -LiteralPath $backup -Destination $target}}
    }
}
