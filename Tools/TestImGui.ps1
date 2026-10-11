param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [switch]$CaptureDocs
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$results = Join-Path $repositoryRoot ('.cache\imgui-tests\' + $Configuration)
New-Item -ItemType Directory -Path $results -Force | Out-Null
Add-Type -AssemblyName System.Drawing
$captureWidth = 1280
$captureHeight = 720
function ReadPixels([string]$Path) {
    $image = [System.Drawing.Bitmap]::new($Path)
    try {
        if ($image.Width -ne $captureWidth -or $image.Height -ne $captureHeight) { throw "Unexpected capture size: $Path" }
        $rectangle = [System.Drawing.Rectangle]::new(0, 0, $captureWidth, $captureHeight)
        $data = $image.LockBits($rectangle, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $pixels = [byte[]]::new($captureWidth * $captureHeight * 4)
            for ($row = 0; $row -lt $captureHeight; ++$row) {
                [System.Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0, $row * $data.Stride), $pixels, $row * $captureWidth * 4, $captureWidth * 4)
            }
            return ,$pixels
        } finally { $image.UnlockBits($data) }
    } finally { $image.Dispose() }
}
foreach ($api in @('Direct3D12','Vulkan')) {
    $name = $api + '_09_ImGui'
    $executable = Join-Path $repositoryRoot ("bin\x64\$Configuration\$name\$name.exe")
    $log = Join-Path $results ($name + '.log')
    $image = Join-Path $results ($name + '.png')
    $arguments = @('--frames','40','--test-ui','--screenshot',('"' + $image + '"'),'--log',('"' + $log + '"'))
    $oldSync = $env:VK_LAYER_VALIDATE_SYNC
    try {
        if ($Configuration -eq 'Debug') { $env:VK_LAYER_VALIDATE_SYNC = '1' }
        $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $results -PassThru -WindowStyle Hidden
        if (-not $process.WaitForExit(30000)) { $process.Kill(); throw "$name UI input test timed out" }
        $text = Get-Content -LiteralPath $log -Raw
        if ($process.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0' -or
            ([regex]::Matches($text, 'ImGui slider edited light intensity:').Count -ne 2) -or
            $text -notmatch 'ImGui reset button restored light intensity: 3.000000') {
            throw "$name UI input test failed: $text"
        }
        if ($Configuration -eq 'Debug' -and $text -notmatch '(Debug Layer|Validation Layer) enabled') {
            throw "$name validation layer was not enabled"
        }
    } finally { $env:VK_LAYER_VALIDATE_SYNC = $oldSync }
    $baseline = ReadPixels (Join-Path $results ($name + '-baseline.png'))
    $edited = ReadPixels (Join-Path $results ($name + '-edited.png'))
    $reset = ReadPixels (Join-Path $results ($name + '-reset.png'))
    $changed = 0
    # Exclude the UI. Verify shader output changes, then returns to its original image after Reset.
    for ($y = 0; $y -lt $captureHeight; ++$y) {
        for ($x = 350; $x -lt $captureWidth; ++$x) {
            $offset = ($y * $captureWidth + $x) * 4
            $different = $false
            for ($channel = 0; $channel -lt 3; ++$channel) {
                if ([Math]::Abs([int]$edited[$offset + $channel] - [int]$baseline[$offset + $channel]) -gt 2) { $different = $true }
                if ($reset[$offset + $channel] -ne $baseline[$offset + $channel]) { throw "$name Reset did not restore rendered scene at ($x,$y)" }
            }
            if ($different) { ++$changed }
        }
    }
    if ($changed -lt 1000) { throw "$name light intensity did not affect enough rendered pixels: $changed" }
    if ($CaptureDocs) {
        Copy-Item -LiteralPath (Join-Path $results ($name + '-edited.png')) -Destination (Join-Path $repositoryRoot "Docs\$api\Images\09_ImGui-intensity.png")
    }
    Write-Output "$name slider + Reset input, dynamic GPU constants, and image round-trip passed ($changed pixels changed)"
}
