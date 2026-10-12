param([string]$BlenderPath='C:\Program Files\Blender Foundation\Blender 5.2\blender.exe',[switch]$SkipReexport)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$sourceDir=Join-Path $repoRoot 'SourceArt/Blender/Workshop'
$assets=Join-Path $repoRoot 'Assets'
$pythonPath=Join-Path (Split-Path $BlenderPath -Parent) '5.2/python/bin/python.exe'
$validator=Join-Path $repoRoot '.cache/gltf-validator/gltf_validator.exe'
if(-not(Test-Path -LiteralPath $validator)){throw 'Run Tools/TestShaderBallAssets.ps1 to obtain pinned glTF-Validator 2.0.0-dev.3.10'}
$model=Join-Path $assets 'Models/Workshop/Workshop.gltf'
$report=((& $validator -o -a $model)-join "`n")|ConvertFrom-Json
if($LASTEXITCODE -ne 0 -or $report.issues.numErrors -or $report.issues.numWarnings){throw "glTF validation failed: $($report.issues|ConvertTo-Json -Depth 12)"}
$sourceHashes=@{}
foreach($file in Get-ChildItem -LiteralPath $sourceDir -Filter *.blend){$sourceHashes[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
$arguments=@((Join-Path $sourceDir 'validate.py'),'--assets',$assets)
if(-not $SkipReexport){
    $temporary=Join-Path $repoRoot ('.cache/workshop-reexport-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporary -Force|Out-Null
    # Copy only the saved blend: packed textures must suffice without .generated files.
    $isolatedSource=Join-Path $temporary 'Workshop.blend'
    Copy-Item -LiteralPath (Join-Path $sourceDir 'Workshop.blend') -Destination $isolatedSource
    $out=Join-Path $temporary 'Assets'
    & $BlenderPath --background $isolatedSource --python-exit-code 1 --python (Join-Path $sourceDir 'export.py') -- --output-dir $out *> (Join-Path $temporary 'export.log')
    if($LASTEXITCODE){throw "Saved-source export failed: $temporary"}
    $arguments+=@('--compare',$out)
}
& $pythonPath @arguments
if($LASTEXITCODE){throw 'Workshop profile, geometry, sharing, texture, camera, DDS or hash validation failed'}
& $pythonPath (Join-Path $repoRoot 'Tests/WorkshopAssetTests.py')
if($LASTEXITCODE){throw 'Workshop invalid-profile fixtures failed'}
& $BlenderPath --background --factory-startup --python-exit-code 1 --python (Join-Path $sourceDir 'verify_references.py')
if($LASTEXITCODE){throw 'Workshop EXR/PNG reference checks failed'}
# Initial generation must stop before touching an existing saved source.
$guardLog=Join-Path $repoRoot '.cache/workshop-initial-guard.log'
& $BlenderPath --background --factory-startup --python-exit-code 1 --python (Join-Path $sourceDir 'generate.py') *> $guardLog
if($LASTEXITCODE -eq 0 -or -not(Select-String -LiteralPath $guardLog -SimpleMatch 'Saved source exists' -Quiet)){throw 'Workshop initial-generation overwrite guard failed'}
foreach($path in $sourceHashes.Keys){if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $sourceHashes[$path]){throw "Saved source modified: $path"}}
Write-Output 'Workshop glTF validation, isolated re-export and source preservation PASS'
