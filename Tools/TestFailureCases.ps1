param([ValidateSet('05_Model','06_Gltf','07_Lighting','08_PBR')][string[]]$Topics = @('05_Model','06_Gltf','07_Lighting','08_PBR'))
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$resultsDirectory = Join-Path $repositoryRoot '.cache\failure-tests'
New-Item -ItemType Directory -Path $resultsDirectory -Force | Out-Null
function ExpectFailure([string]$Executable, [string[]]$ExtraArguments, [string]$Log, [string]$ExpectedText) {
    $arguments = @('--frames','1','--log',('"' + $Log + '"')) + $ExtraArguments
    $process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory $resultsDirectory -PassThru -WindowStyle Hidden
    if (-not $process.WaitForExit(15000)) { $process.Kill(); throw 'Failure-case process timed out' }
    $text = Get-Content -LiteralPath $Log -Raw
    if ($process.ExitCode -ne 1 -or $text -notmatch [regex]::Escape($ExpectedText) -or $text -notmatch 'Validation errors: 0') { throw "Unexpected failure result: $text" }
}
foreach ($api in @('Direct3D12','Vulkan')) {
  foreach ($topic in $Topics) {
    $name = $api + '_' + $topic
    $folder = Join-Path $repositoryRoot ("bin\x64\Debug\$name")
    $executable = Join-Path $folder ($name + '.exe')
    ExpectFailure $executable @('--texture',('"' + (Join-Path $resultsDirectory 'missing.png') + '"')) (Join-Path $resultsDirectory ($name + '-texture.log')) 'missing.png'
    $modelName = if ($topic -eq '05_Model') { 'missing.obj' } else { 'missing.gltf' }
    ExpectFailure $executable @('--model',('"' + (Join-Path $resultsDirectory $modelName) + '"')) (Join-Path $resultsDirectory ($name + '-model.log')) $modelName
    $extension = if ($api -eq 'Direct3D12') { 'cso' } else { 'spv' }
    $shader = Join-Path $folder ("Shaders\Pixel.$extension")
    $saved = $shader + '.failure-test-backup'
    try {
        Move-Item -LiteralPath $shader -Destination $saved
        ExpectFailure $executable @() (Join-Path $resultsDirectory ($name + '-shader.log')) ("Pixel.$extension")
    } finally { if (Test-Path -LiteralPath $saved) { Move-Item -LiteralPath $saved -Destination $shader } }
    Write-Output "$name missing texture/model/shader diagnostics passed"
  }
}
