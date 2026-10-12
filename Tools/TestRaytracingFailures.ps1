param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo ".cache/raytracing-failures/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
foreach($api in @('Direct3D12','Vulkan')){
    foreach($topic in @('Advanced01_RaytracingBasic','Advanced02_RaytracingShadows','Advanced03_RaytracingReflections')){
        $name="${api}_${topic}";$source=Join-Path $repo "bin/x64/$Configuration/$name"
        foreach($case in @('shader','full','unknown-fixture')){
            $dest=Join-Path $work "$name-$case"
            New-Item -ItemType Directory -Path $dest -Force|Out-Null
            Get-ChildItem -LiteralPath $source|Copy-Item -Destination $dest -Recurse -Force
            $extra=@();$expected=''
            if($case-eq'shader'){
                $extension=if($api-eq'Direct3D12'){'cso'}else{'spv'}
                Remove-Item -LiteralPath (Join-Path $dest "Shaders/RayGeneration.$extension")
                $expected='RayGeneration'
            }elseif($case-eq'full'){$extra=@('--preset','Full');$expected='Full'}
            else{$extra=@('--rt-test-scene','unknown');$expected='Unknown --rt-test-scene'}
            $log=Join-Path $dest 'failure.log';$arguments=@('--frames','2','--no-ui','--log',('"'+$log+'"'))+$extra
            $previous=$env:VK_LAYER_VALIDATE_SYNC
            try{if($api-eq'Vulkan'-and$Configuration-eq'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
                $process=Start-Process -FilePath (Join-Path $dest "$name.exe") -ArgumentList $arguments -WindowStyle Hidden -PassThru
            }finally{$env:VK_LAYER_VALIDATE_SYNC=$previous}
            if(-not $process.WaitForExit(60000)){$process.Kill();throw "$name / $case timed out"}
            $text=Get-Content -LiteralPath $log -Raw
            if($process.ExitCode-ne 1-or-not $text.Contains($expected)-or$text-notmatch'Validation errors: 0'){throw "$name / $case failure handling: $text"}
            Write-Output "$name $case rejected and cleaned up"
        }
    }
}
