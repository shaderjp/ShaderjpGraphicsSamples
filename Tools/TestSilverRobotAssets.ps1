param(
    [string]$BlenderPath = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe',
    [string]$ValidatorPath,
    [switch]$SkipReexport
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$sourceDir = Join-Path $repoRoot 'SourceArt/Blender/SilverRobot'
$assetDir = Join-Path $repoRoot 'Assets/Models/SilverRobot'
$workDir = Join-Path $repoRoot '.cache/silver-robot'
New-Item -ItemType Directory -Path $workDir -Force | Out-Null
if (-not (Test-Path -LiteralPath $BlenderPath -PathType Leaf)) { throw 'Set -BlenderPath to Blender 5.2 LTS.' }
$validatorVersion = '2.0.0-dev.3.10'
if (-not $ValidatorPath) {
    $validatorDir = Join-Path $repoRoot '.cache/gltf-validator'
    $ValidatorPath = Join-Path $validatorDir 'gltf_validator.exe'
    if (-not (Test-Path -LiteralPath $ValidatorPath)) {
        New-Item -ItemType Directory -Path $validatorDir -Force | Out-Null
        $archive = Join-Path $validatorDir 'validator.zip'
        Invoke-WebRequest -Uri "https://github.com/KhronosGroup/glTF-Validator/releases/download/$validatorVersion/gltf_validator-$validatorVersion-win64.zip" -OutFile $archive
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'c5068f51205deedc28acc3529ee7e11ee60e853454f673093398eba80142202c') {
            throw 'Unexpected pinned glTF-Validator archive SHA-256.'
        }
        Expand-Archive -LiteralPath $archive -DestinationPath $validatorDir -Force
    }
}
$khronos = [ordered]@{}
foreach ($extension in @('gltf', 'glb')) {
    $text = & $ValidatorPath -o (Join-Path $assetDir "SilverRobot.$extension")
    if ($LASTEXITCODE -ne 0) { throw "Khronos validation failed: $extension" }
    $report = ($text -join "`n") | ConvertFrom-Json
    if ($report.validatorVersion -ne $validatorVersion -or $report.issues.numErrors -ne 0 -or $report.issues.numWarnings -ne 0) {
        throw "Use glTF-Validator $validatorVersion and fix all errors/warnings: $extension"
    }
    $khronos[$extension] = $report.issues
    ($text -join "`n") | Set-Content -LiteralPath (Join-Path $workDir "validator-$extension.json") -Encoding utf8
}
$sourcePath = Join-Path $sourceDir 'SilverRobot.blend'
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
$validationArgs = @('--blender-compare', '--report', (Join-Path $workDir 'structure.json'))
if (-not $SkipReexport) {
    $isolatedDir = Join-Path $workDir ('reexport-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $isolatedDir -Force | Out-Null
    $isolatedSource = Join-Path $isolatedDir 'SilverRobot.blend'
    Copy-Item -LiteralPath $sourcePath -Destination $isolatedSource
    $reexportDir = Join-Path $isolatedDir 'Assets'
    & $BlenderPath --background $isolatedSource --python-exit-code 1 --python (Join-Path $sourceDir 'export.py') -- --output-dir $reexportDir *> (Join-Path $workDir 'reexport.log')
    if ($LASTEXITCODE -ne 0) { throw "Saved source re-export failed. See $workDir/reexport.log" }
    $validationArgs += @('--compare', $reexportDir)
}
& $BlenderPath --background $sourcePath --python-exit-code 1 --python (Join-Path $sourceDir 'validate.py') -- @validationArgs *> (Join-Path $workDir 'validate.log')
if ($LASTEXITCODE -ne 0) { throw "Skin/profile/animation validation failed. See $workDir/validate.log" }
if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $sourceHash) { throw 'Authoring source changed during validation.' }
$versionText = & $BlenderPath --version
[ordered]@{
    result = 'PASS'
    blender = $versionText[0]
    blenderBuild = $versionText
    validator = $validatorVersion
    sourceSha256 = $sourceHash.ToLowerInvariant()
    savedSourceReexport = (-not $SkipReexport.IsPresent)
    khronos = $khronos
    profile = (Get-Content -LiteralPath (Join-Path $workDir 'structure.json') -Raw | ConvertFrom-Json)
} | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $workDir 'verification.json') -Encoding utf8
Write-Host "Silver Robot assets: PASS. Report: $workDir/verification.json"
