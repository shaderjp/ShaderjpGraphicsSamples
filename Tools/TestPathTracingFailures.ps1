param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo ".cache/pathtracing-failures/$Configuration";New-Item -ItemType Directory -Force $work|Out-Null
foreach($api in @('Direct3D12','Vulkan')){
    $name="${api}_Advanced18_PathTracingBasic";$source=Join-Path $repo "bin/x64/$Configuration/$name"
    foreach($case in @('shader','asset','full','unknown-fixture','bounces','sampling','negative-seed')){
        $directory=Join-Path $work "$name-$case";New-Item -ItemType Directory -Force $directory|Out-Null
        Get-ChildItem -LiteralPath $source|Copy-Item -Destination $directory -Recurse -Force
        $extra=@();$expected=''
        switch($case){
            'shader'{$extension=if($api-eq'Direct3D12'){'cso'}else{'spv'};Remove-Item -LiteralPath (Join-Path $directory "Shaders/RayGeneration.$extension");$expected='RayGeneration'}
            'asset'{Remove-Item -LiteralPath (Join-Path $directory 'Assets/Models/PathTracing/PathTracing.bin');$expected='PathTracing.bin'}
            'full'{$extra=@('--preset','Full');$expected='Full'}
            'unknown-fixture'{$extra=@('--rt-test-scene','pt-unknown');$expected='Unknown --rt-test-scene'}
            'bounces'{$extra=@('--pt-max-bounces','0');$expected='Invalid path tracing value'}
            'sampling'{$extra=@('--pt-sampling','bad');$expected='--pt-sampling'}
            'negative-seed'{$extra=@('--pt-seed','-1');$expected='Invalid path tracing value'}
        }
        $stdout=Join-Path $directory 'stdout.txt';$log=Join-Path $directory 'failure.log'
        $arguments=@('--frames','2','--no-ui','--log',('"'+$log+'"'))+$extra
        $process=Start-Process -FilePath (Join-Path $directory "$name.exe") -ArgumentList $arguments -RedirectStandardOutput $stdout -WindowStyle Hidden -PassThru
        if(-not$process.WaitForExit(60000)){$process.Kill();throw "$api $case timed out"}
        $text=Get-Content -LiteralPath $stdout -Raw
        if($process.ExitCode-ne 1-or-not$text.Contains($expected)-or$text-notmatch'Validation errors: 0'){throw "$api ${case}: $text"}
        Write-Output "$Configuration $api $case rejected and cleaned up PASS"
    }
}
