param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/bindless-failures/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
$original=Join-Path $repoRoot 'Assets/Scenes/BindlessGallery'
$invalid=Get-Content -LiteralPath (Join-Path $original 'BindlessGallery.json') -Raw|ConvertFrom-Json
$invalid.model=Join-Path $repoRoot 'Assets/Models/BindlessGallery/BindlessGallery.gltf'
$invalid.gallery.primitiveGeometry[0]=64
$invalid|ConvertTo-Json -Depth 30|Set-Content (Join-Path $work 'InvalidId.json')
Copy-Item -LiteralPath (Join-Path $original 'CameraPath.json') -Destination $work
Copy-Item -LiteralPath (Join-Path $original 'Manifest.json') -Destination $work
foreach($api in @('Direct3D12','Vulkan')){
    $name="$api`_Advanced12_BindlessMaterials";$directory=Join-Path $repoRoot "bin/x64/$Configuration/$name"
    $cases=@(@('objects',@('--objects','4097')),@('materials',@('--materials','257')),@('mode',@('--binding','resources')),@('driven',@('--gpu-driven','--binding','bound')),@('id',@('--scene',(Join-Path $work 'InvalidId.json'))))
    foreach($case in $cases){
        $log=Join-Path $work "$api-$($case[0]).log"
        $p=Start-Process -FilePath (Join-Path $directory "$name.exe") -ArgumentList (@('--frames','1','--gallery-preset','Mini','--log',$log)+$case[1]) -WorkingDirectory $work -WindowStyle Hidden -PassThru
        if(-not $p.WaitForExit(300000)){$p.Kill();throw 'Failure case timed out'}
        if($p.ExitCode -eq 0){throw "Accepted invalid input: $api $($case[0])"}
        if($case[0] -eq 'id' -and (Get-Content -LiteralPath $log -Raw) -notmatch 'Gallery geometry ID out of range'){throw 'Missing ID failure reason'}
        Write-Output "$api $($case[0]) rejected PASS"
    }
    $extension=if($api -eq 'Vulkan'){'spv'}else{'cso'}
    $shader=Join-Path $directory "Shaders/RasterVertex1.$extension";$backup="$shader.test-backup"
    if(Test-Path -LiteralPath $backup){throw 'A previous shader failure test needs recovery'}
    try{
        Move-Item -LiteralPath $shader -Destination $backup
        $log=Join-Path $work "$api-missing-shader.log"
        $p=Start-Process -FilePath (Join-Path $directory "$name.exe") -ArgumentList @('--frames','1','--gallery-preset','Mini','--log',$log) -WorkingDirectory $work -WindowStyle Hidden -PassThru
        if(-not $p.WaitForExit(300000)){$p.Kill();throw 'Missing shader timeout'}
        if($p.ExitCode -eq 0 -or (Get-Content -LiteralPath $log -Raw) -notmatch 'RasterVertex1'){throw 'Missing shader was not clearly rejected'}
        Write-Output "$api missing shader rejected PASS"
    }finally{if(Test-Path -LiteralPath $backup){Move-Item -LiteralPath $backup -Destination $shader}}
}
