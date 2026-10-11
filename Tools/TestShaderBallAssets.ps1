param(
    [string]$BlenderPath = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe',
    [string]$ValidatorPath,
    [switch]$SkipReexport
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$sourceDir = Join-Path $repoRoot 'SourceArt/Blender/ShaderBall'
$assetDir = Join-Path $repoRoot 'Assets/Models/ShaderBall'
$workDir = Join-Path $repoRoot '.cache/shader-ball'
New-Item -ItemType Directory -Path $workDir -Force | Out-Null
if (-not (Test-Path -LiteralPath $BlenderPath -PathType Leaf)) {
    throw 'Blender 5.2 LTS was not found. Set -BlenderPath to blender.exe.'
}
$pythonPath = Join-Path (Split-Path $BlenderPath -Parent) '5.2/python/bin/python.exe'
if (-not (Test-Path -LiteralPath $pythonPath -PathType Leaf)) {
    throw 'The Blender 5.2 bundled Python runtime was not found.'
}
$validatorVersion = '2.0.0-dev.3.10'
if (-not $ValidatorPath) {
    $validatorDir = Join-Path $repoRoot '.cache/gltf-validator'
    $ValidatorPath = Join-Path $validatorDir 'gltf_validator.exe'
    if (-not (Test-Path -LiteralPath $ValidatorPath)) {
        New-Item -ItemType Directory -Path $validatorDir -Force | Out-Null
        $archive = Join-Path $validatorDir 'validator.zip'
        $url = "https://github.com/KhronosGroup/glTF-Validator/releases/download/$validatorVersion/gltf_validator-$validatorVersion-win64.zip"
        Invoke-WebRequest -Uri $url -OutFile $archive
        $expectedHash = 'c5068f51205deedc28acc3529ee7e11ee60e853454f673093398eba80142202c'
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
            throw 'The pinned Khronos glTF-Validator archive has an unexpected SHA-256.'
        }
        Expand-Archive -LiteralPath $archive -DestinationPath $validatorDir -Force
    }
}
$allReports = [ordered]@{}
foreach ($variant in @('Albedo', 'PBR')) {
    $gltfPath = Join-Path $assetDir "ShaderBall_$variant.gltf"
    $reportText = & $ValidatorPath -o -a $gltfPath
    if ($LASTEXITCODE -ne 0) { throw "Khronos glTF-Validator failed: $variant" }
    $report = ($reportText -join "`n") | ConvertFrom-Json
    if ($report.validatorVersion -ne $validatorVersion) { throw "Use glTF-Validator $validatorVersion." }
    if ($report.issues.numErrors -ne 0 -or $report.issues.numWarnings -ne 0) {
        throw "Khronos glTF-Validator reported errors or warnings: $variant"
    }
    ($reportText -join "`n") | Set-Content -LiteralPath (Join-Path $workDir "validator-$variant.json") -Encoding utf8
    $allReports[$variant] = $report.issues
}
$arguments = @((Join-Path $sourceDir 'validate_shader_ball.py'), $assetDir,
               '--report', (Join-Path $workDir 'structure.json'))
$sourcePath = Join-Path $sourceDir 'ShaderBall.blend'
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
if (-not $SkipReexport) {
    # Fresh directory avoids stale output without recursively deleting any path.
    $reexportRoot = Join-Path $workDir ('reexport-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $reexportRoot -Force | Out-Null
    $isolatedSource = Join-Path $reexportRoot 'ShaderBall.blend'
    Copy-Item -LiteralPath $sourcePath -Destination $isolatedSource
    $reexportDir = Join-Path $reexportRoot 'Assets'
    # No .generated images accompany this copy: prove the packed source alone
    # is sufficient, as it must be after cloning the repository.
    & $BlenderPath --background $isolatedSource --python-exit-code 1 --python (Join-Path $sourceDir 'export_shader_ball.py') -- --output-dir $reexportDir *> (Join-Path $workDir 'reexport.log')
    if ($LASTEXITCODE -ne 0) { throw "Blender re-export failed. See $workDir/reexport.log" }
    $arguments += @('--compare', $reexportDir)
    foreach ($variant in @('Albedo', 'PBR')) {
        $repeatedText = & $ValidatorPath -o (Join-Path $reexportDir "ShaderBall_$variant.gltf")
        if ($LASTEXITCODE -ne 0) { throw "Re-export validator failed: $variant" }
        $repeatedReport = ($repeatedText -join "`n") | ConvertFrom-Json
        if ($repeatedReport.issues.numErrors -ne 0 -or $repeatedReport.issues.numWarnings -ne 0) {
            throw "Re-export has errors or warnings: $variant"
        }
    }
}
& $pythonPath @arguments
if ($LASTEXITCODE -ne 0) { throw 'Shader Ball profile/texture/re-export verification failed.' }
if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $sourceHash) {
    throw 'Exporter unexpectedly modified the canonical .blend.'
}
$versionText = & $BlenderPath --version
[ordered]@{
    result = 'PASS'
    blender = $versionText[0]
    blenderBuild = $versionText
    validator = $validatorVersion
    sourceSha256 = $sourceHash.ToLowerInvariant()
    savedSourceReexport = (-not $SkipReexport.IsPresent)
    khronos = $allReports
    profile = (Get-Content -LiteralPath (Join-Path $workDir 'structure.json') -Raw | ConvertFrom-Json)
} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $workDir 'verification.json') -Encoding utf8
Write-Host "Shader Ball assets: PASS. Report: $workDir/verification.json"
