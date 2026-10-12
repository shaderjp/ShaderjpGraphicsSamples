param([string]$BlenderPath='C:\Program Files\Blender Foundation\Blender 5.2\blender.exe',[switch]$SkipReexport)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$pythonPath=Join-Path (Split-Path $BlenderPath -Parent) '5.2/python/bin/python.exe'
$sourceDir=Join-Path $repoRoot 'SourceArt/Blender/LightingLab'
$validator=Join-Path $repoRoot '.cache/gltf-validator/gltf_validator.exe'
if(-not(Test-Path -LiteralPath $validator)){throw 'Run Tools/TestShaderBallAssets.ps1 first to obtain pinned glTF-Validator 2.0.0-dev.3.10'}
$assetDir=Join-Path $repoRoot 'Assets'
foreach($file in Get-ChildItem -LiteralPath (Join-Path $assetDir 'Models/LightingLab') -Recurse -Filter *.gltf){
    $report=((& $validator -o -a $file.FullName)-join "`n")|ConvertFrom-Json
    if($LASTEXITCODE -ne 0 -or $report.issues.numErrors -or $report.issues.numWarnings){throw "glTF validation failed: $($file.FullName): $($report.issues|ConvertTo-Json -Depth 12)"}
}
$hashes=@{};foreach($file in Get-ChildItem -LiteralPath $sourceDir -Filter *.blend){$hashes[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
$arguments=@((Join-Path $sourceDir 'validate.py'),'--assets',$assetDir)
if(-not $SkipReexport){
    $temporary=Join-Path $repoRoot ('.cache/lightinglab-reexport-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporary -Force|Out-Null
    & $BlenderPath --background (Join-Path $sourceDir 'LightingLab.blend') --python-exit-code 1 --python (Join-Path $sourceDir 'export.py') -- --output-dir $temporary *> (Join-Path $temporary 'export.log')
    if($LASTEXITCODE){throw "Saved-source export failed: $temporary"}
    $arguments+=@('--compare',$temporary)
}
& $pythonPath @arguments
if($LASTEXITCODE){throw 'LightingLab profile, texture, DDS or numeric IBL validation failed'}
foreach($path in $hashes.Keys){if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $hashes[$path]){throw "Saved source modified: $path"}}
Write-Output 'LightingLab glTF validation, re-export, source preservation and numerical IBL checks PASS'
