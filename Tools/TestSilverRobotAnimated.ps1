param([string]$BlenderPath='C:\Program Files\Blender Foundation\Blender 5.2\blender.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$source=Join-Path $root 'SourceArt/Blender/SilverRobot'
$assets=Join-Path $root 'Assets/Models/SilverRobotAnimated'
$work=Join-Path $root '.cache/skinning/animated-assets'
New-Item -ItemType Directory -Path $work -Force | Out-Null
$validator=Join-Path $root '.cache/gltf-validator/gltf_validator.exe'
if(-not(Test-Path -LiteralPath $validator)){throw 'Run Tools/TestSilverRobotAssets.ps1 first to obtain the pinned validator.'}
$original=Join-Path $source 'SilverRobot.blend'
$animated=Join-Path $source 'SilverRobotAnimated.blend'
$originalHash=(Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
$animatedHash=(Get-FileHash -LiteralPath $animated -Algorithm SHA256).Hash
$issues=[ordered]@{}
foreach($extension in @('gltf','glb')){
    $text=& $validator -o (Join-Path $assets "SilverRobot.$extension")
    if($LASTEXITCODE -ne 0){throw "Khronos validation failed: $extension"}
    $report=($text -join "`n") | ConvertFrom-Json
    if($report.validatorVersion -ne '2.0.0-dev.3.10' -or $report.issues.numErrors -ne 0 -or $report.issues.numWarnings -ne 0){throw "Validator issues: $extension"}
    $issues[$extension]=$report.issues
    ($text -join "`n") | Set-Content -LiteralPath (Join-Path $work "validator-$extension.json") -Encoding utf8
}
$isolated=Join-Path $work ('reexport-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $isolated -Force | Out-Null
Copy-Item -LiteralPath $animated -Destination (Join-Path $isolated 'SilverRobotAnimated.blend')
& $BlenderPath --background (Join-Path $isolated 'SilverRobotAnimated.blend') --python-exit-code 1 --python (Join-Path $source 'export_animated.py') -- --output-dir (Join-Path $isolated 'Assets') *> (Join-Path $work 'reexport.log')
if($LASTEXITCODE -ne 0){throw 'Animated re-export failed.'}
foreach($name in @('SilverRobot.gltf','SilverRobot.bin','SilverRobot.glb','Humanoid.json','Animations.json','Quaternius-CC0.txt')){
    if((Get-FileHash -LiteralPath (Join-Path $assets $name)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $isolated "Assets/$name")).Hash){throw "Re-export mismatch: $name"}
}
& $BlenderPath --background $animated --python-exit-code 1 --python (Join-Path $source 'verify_animated.py') *> (Join-Path $work 'blender-compare.log')
if($LASTEXITCODE -ne 0){throw 'Blender / independent glTF comparison failed.'}
if((Get-FileHash -LiteralPath $original).Hash -ne $originalHash -or (Get-FileHash -LiteralPath $animated).Hash -ne $animatedHash){throw 'Verification changed a saved Blender source.'}
$hashes=[ordered]@{}
Get-ChildItem -LiteralPath $assets -File | Sort-Object Name | ForEach-Object {$hashes[$_.Name]=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
[ordered]@{result='PASS';validator='2.0.0-dev.3.10';khronos=$issues;originalSourceSha256=$originalHash.ToLowerInvariant();animatedSourceSha256=$animatedHash.ToLowerInvariant();savedSourceReexport='byte-identical';assetSha256=$hashes} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $root 'Docs/Assets/SilverRobotAnimatedAssets.json') -Encoding utf8
Write-Host 'Animated Blender assets: PASS (Khronos, byte-identical re-export, Blender deformation).'
