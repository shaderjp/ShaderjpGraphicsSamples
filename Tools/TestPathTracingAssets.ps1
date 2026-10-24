param([string]$BlenderPath='C:/Program Files/Blender Foundation/Blender 5.2/blender.exe',[switch]$SkipReexport)
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$source=Join-Path $repo 'SourceArt/Blender/PathTracing';$python=Join-Path (Split-Path $BlenderPath -Parent) '5.2/python/bin/python.exe'
$validator=Join-Path $repo '.cache/gltf-validator/gltf_validator.exe'
if(-not(Test-Path -LiteralPath $validator)){throw 'Run Tools/TestShaderBallAssets.ps1 to obtain the pinned glTF validator'}
$report=((& $validator -o -a (Join-Path $repo 'Assets/Models/PathTracing/PathTracing.gltf'))-join"`n")|ConvertFrom-Json
if($LASTEXITCODE-or$report.issues.numErrors-or$report.issues.numWarnings){throw ($report.issues|ConvertTo-Json -Depth 10)}
$saved=Join-Path $source 'PathTracing.blend';$hash=(Get-FileHash -LiteralPath $saved).Hash
$arguments=@((Join-Path $source 'validate.py'))
if(-not $SkipReexport){
    $isolated=Join-Path $repo ('.cache/pathtracing-reexport-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Force $isolated|Out-Null
    Copy-Item -LiteralPath $saved -Destination (Join-Path $isolated 'PathTracing.blend')
    & $BlenderPath --background (Join-Path $isolated 'PathTracing.blend') --python-exit-code 1 --python (Join-Path $source 'export.py') -- --output-dir (Join-Path $isolated 'Assets') *> (Join-Path $isolated 'export.log')
    if($LASTEXITCODE){throw "Isolated source export failed: $isolated"}
    $arguments+=@('--compare',(Join-Path $isolated 'Assets'))
}
& $python @arguments
if($LASTEXITCODE){throw 'PathTracing asset verification failed'}
if((Get-FileHash -LiteralPath $saved).Hash-ne$hash){throw 'Export changed saved source'}
Write-Output 'Saved Blender source preserved; isolated re-export PASS'
