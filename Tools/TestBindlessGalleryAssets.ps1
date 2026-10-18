param([string]$BlenderPath='C:\Program Files\Blender Foundation\Blender 5.2\blender.exe',[switch]$SkipReexport)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$sourceDir=Join-Path $repoRoot 'SourceArt/Blender/BindlessGallery'
$pythonPath=Join-Path (Split-Path $BlenderPath -Parent) '5.2/python/bin/python.exe'
$blend=Join-Path $sourceDir 'BindlessGallery.blend'
$original=(Get-FileHash -LiteralPath $blend).Hash
$validator=Join-Path $repoRoot '.cache/gltf-validator/gltf_validator.exe'
if(-not(Test-Path -LiteralPath $validator)){throw 'Pinned glTF-Validator is missing. See Tools/TestShaderBallAssets.ps1.'}
$result=((& $validator -o -a (Join-Path $repoRoot 'Assets/Models/BindlessGallery/BindlessGallery.gltf'))-join "`n")|ConvertFrom-Json
if($LASTEXITCODE -or $result.issues.numErrors -or $result.issues.numWarnings){throw 'glTF validation failed'}
& $pythonPath (Join-Path $sourceDir 'validate.py')
if($LASTEXITCODE){throw 'Gallery profile validation failed'}
& $pythonPath (Join-Path $repoRoot 'Tests/BindlessGalleryAssetTests.py')
if($LASTEXITCODE){throw 'Invalid asset fixtures failed'}
$cache=Join-Path $repoRoot ('.cache/bindless-assets-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $cache -Force|Out-Null
if(-not $SkipReexport){
    $isolated=Join-Path $cache 'BindlessGallery.blend'
    Copy-Item -LiteralPath $blend -Destination $isolated
    $out=Join-Path $cache 'Assets'
    & $BlenderPath --background $isolated --python-exit-code 1 --python (Join-Path $sourceDir 'export.py') -- --output-dir $out *> (Join-Path $cache 'export.log')
    if($LASTEXITCODE){throw "Isolated re-export failed: $cache"}
    foreach($folder in @('Models/BindlessGallery','Scenes/BindlessGallery')){
        $originalFolder=Join-Path $repoRoot "Assets/$folder"
        $relativeFiles=@(Get-ChildItem -LiteralPath $originalFolder -File -Recurse|ForEach-Object{$_.FullName.Substring($originalFolder.Length+1)})
        $copyFolder=Join-Path $out $folder
        $copyFiles=@(Get-ChildItem -LiteralPath $copyFolder -File -Recurse|ForEach-Object{$_.FullName.Substring($copyFolder.Length+1)})
        if(Compare-Object $relativeFiles $copyFiles){throw 'Re-export file set differs'}
        foreach($relative in $relativeFiles){
            if((Get-FileHash -LiteralPath (Join-Path $originalFolder $relative)).Hash -ne (Get-FileHash -LiteralPath (Join-Path $copyFolder $relative)).Hash){throw "Re-export mismatch: $relative"}
        }
    }
}
& $BlenderPath --background --factory-startup --python-exit-code 1 --python (Join-Path $sourceDir 'generate.py') *> (Join-Path $cache 'guard.log')
if($LASTEXITCODE -eq 0 -or -not(Select-String -LiteralPath (Join-Path $cache 'guard.log') -SimpleMatch 'Saved source exists' -Quiet)){throw 'Authoring overwrite guard failed'}
if((Get-FileHash -LiteralPath $blend).Hash -ne $original){throw 'Saved source changed'}
$references=Join-Path $repoRoot 'Docs/Assets/Images/BindlessGallery'
$report=Get-Content -LiteralPath (Join-Path $references 'References.json') -Raw | ConvertFrom-Json
if($report.sourceSha256.ToUpperInvariant() -ne $original){throw 'References are stale'}
foreach($name in @('Hero','Materials','Reflection','Occlusion','Stress')){
    $shot=$report.shots.$name
    if($shot.width -ne 1280 -or $shot.height -ne 720 -or $shot.samples -ne 64){throw "Reference settings: $name"}
    if((Get-FileHash -LiteralPath (Join-Path $references "BindlessGallery_$name.png")).Hash -ne $shot.pngSha256.ToUpperInvariant()){throw "Reference hash: $name"}
}
Write-Output 'BindlessGallery validator, numerical checks, invalid fixtures, isolated re-export, source guard and reference hashes PASS'
