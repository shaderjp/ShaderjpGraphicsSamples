param([string]$BlenderPath='C:\Program Files\Blender Foundation\Blender 5.2\blender.exe')
$ErrorActionPreference='Stop';$repoRoot=Split-Path $PSScriptRoot -Parent
$source=Join-Path $repoRoot 'SourceArt/Blender/PostProcess/Monitor.blend'
$hash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$validator=Join-Path $repoRoot '.cache/gltf-validator/gltf_validator.exe'
if(-not(Test-Path -LiteralPath $validator)){throw 'Run Tools/TestShaderBallAssets.ps1 to obtain the pinned glTF validator'}
$model=Join-Path $repoRoot 'Assets/Models/PostProcess/Monitor/Monitor.gltf'
$report=((& $validator -o -a $model)-join "`n")|ConvertFrom-Json
if($LASTEXITCODE -or $report.issues.numErrors -or $report.issues.numWarnings){throw ($report.issues|ConvertTo-Json -Depth 10)}
$temp=Join-Path $repoRoot ('.cache/monitor-reexport-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $temp|Out-Null
& $BlenderPath --background $source --python-exit-code 1 --python (Join-Path $repoRoot 'SourceArt/Blender/PostProcess/monitor.py') -- --output $temp *> (Join-Path $temp 'export.log')
if($LASTEXITCODE){throw 'Monitor re-export failed'}
foreach($file in Get-ChildItem -LiteralPath (Split-Path $model -Parent) -Recurse -File){
 $relative=$file.FullName.Substring($repoRoot.Length+8);$copy=Join-Path $temp $relative
 if(-not(Test-Path -LiteralPath $copy) -or (Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $copy).Hash){throw "Re-export mismatch: $relative"}
}
if((Get-FileHash -LiteralPath $source).Hash -ne $hash){throw 'Saved Blender source changed during export'}
Write-Output 'Monitor glTF validator errors/warnings 0; saved-source re-export and source hash PASS'
