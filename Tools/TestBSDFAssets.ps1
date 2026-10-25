param([string]$BlenderPath='C:/Program Files/Blender Foundation/Blender 5.2/blender.exe',[switch]$SkipReexport)
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$source=Join-Path $repo 'SourceArt/Blender/BSDFMaterials';$python=Join-Path (Split-Path $BlenderPath -Parent) '5.2/python/bin/python.exe'
$validator=Join-Path $repo '.cache/gltf-validator/gltf_validator.exe'
if(-not(Test-Path -LiteralPath $validator)){throw 'Run Tools/TestShaderBallAssets.ps1 for the pinned glTF validator'}
$report=((& $validator -o -a (Join-Path $repo 'Assets/Models/BSDFMaterials/BSDFMaterials.gltf'))-join"`n")|ConvertFrom-Json
if($LASTEXITCODE-or$report.issues.numErrors-or$report.issues.numWarnings){throw ($report.issues|ConvertTo-Json -Depth 10)}
$saved=Join-Path $source 'BSDFMaterials.blend';$hash=(Get-FileHash -LiteralPath $saved).Hash
$arguments=@((Join-Path $source 'validate.py'))
if(-not $SkipReexport){
 $isolated=Join-Path $repo ('.cache/bsdf19/reexport-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $isolated|Out-Null
 Copy-Item -LiteralPath $saved -Destination (Join-Path $isolated 'BSDFMaterials.blend')
 & $BlenderPath --background (Join-Path $isolated 'BSDFMaterials.blend') --python-exit-code 1 --python (Join-Path $source 'export.py') -- --output-dir (Join-Path $isolated 'Assets') *> (Join-Path $isolated 'export.log')
 if($LASTEXITCODE){throw "Isolated export failed: $isolated"};$arguments+=@('--compare',(Join-Path $isolated 'Assets'))
}
& $python @arguments
if($LASTEXITCODE){throw 'BSDF asset verification failed'}
if((Get-FileHash -LiteralPath $saved).Hash-ne$hash){throw 'Export changed saved Blender source'}
Write-Output 'BSDF saved source preserved; isolated source export PASS'
