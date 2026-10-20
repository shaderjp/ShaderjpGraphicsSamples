param([ValidateSet('Debug','Release')][string]$Configuration='Debug', [ValidateSet('Direct3D12','Vulkan')][string[]]$Api=@('Direct3D12','Vulkan'), [switch]$CaptureDocs)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$work=Join-Path $root ".cache/character/$Configuration"
New-Item -ItemType Directory -Path $work -Force | Out-Null
Add-Type -AssemblyName System.Drawing
if (-not ('CharacterImageChecks' -as [type])) {
    $drawingAssemblies = @([System.Drawing.Bitmap].Assembly.Location, [System.Drawing.Rectangle].Assembly.Location) + @(Get-ChildItem -Path (Join-Path $PSHOME 'System.Private.Windows*.dll') | ForEach-Object FullName)
    Add-Type -ReferencedAssemblies $drawingAssemblies -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class CharacterImageChecks {
    public static byte[] Read(string path) {
        using(var image = new Bitmap(path)) {
            if(image.Width!=1280 || image.Height!=720) throw new Exception("Capture must be 1280x720: "+path);
            var rect=new Rectangle(0,0,image.Width,image.Height);
            var bits=image.LockBits(rect,ImageLockMode.ReadOnly,PixelFormat.Format32bppArgb);
            try { var data=new byte[bits.Stride*image.Height]; Marshal.Copy(bits.Scan0,data,0,data.Length); return data; }
            finally { image.UnlockBits(bits); }
        }
    }
    public static int Difference(string a,string b) {
        var x=Read(a); var y=Read(b); int count=0;
        for(int i=0;i<x.Length;i+=4) if(x[i]!=y[i] || x[i+1]!=y[i+1] || x[i+2]!=y[i+2]) ++count;
        return count;
    }
}
'@
}
$results=@()
foreach($graphicsApi in $Api) {
    $name="${graphicsApi}_28_CharacterController"
    $exe=Join-Path $root "bin/x64/$Configuration/$name/$name.exe"
    $exeSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    $log=Join-Path $work "$name.log"
    $image=Join-Path $work "$name.png"
    $arguments=@('--frames','300','--test-character','--fixed-dt','0.016666667','--no-ui','--log',('"'+$log+'"'),'--screenshot',('"'+$image+'"'))
    $previous=$env:VK_LAYER_VALIDATE_SYNC
    try {
        if($graphicsApi -eq 'Vulkan' -and $Configuration -eq 'Debug') { $env:VK_LAYER_VALIDATE_SYNC='1' }
        $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
    } finally { $env:VK_LAYER_VALIDATE_SYNC=$previous }
    if(-not $process.WaitForExit(60000)) { $process.Kill(); throw "$name character test timed out" }
    $text=Get-Content -LiteralPath $log -Raw
    if($process.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0') { throw "$name failed: $text" }
    if($Configuration -eq 'Debug' -and $text -notmatch '(Debug Layer|Validation Layer) enabled') { throw "$name validation layer missing" }
    $trace=@(Get-Content -LiteralPath (Join-Path $work "$name.character.json") -Raw | ConvertFrom-Json)
    if($trace.Count -ne 300) { throw "$name trace length" }
    $states=@{1='Idle';75='Walk';120='Run';155='Walk';190='JumpLoop';222='JumpLand';270='Idle'}
    foreach($entry in $states.GetEnumerator()) {
        if($trace[$entry.Key-1].state -ne $entry.Value) { throw "$name state at $($entry.Key): $($trace[$entry.Key-1].state)" }
    }
    $peak=($trace | ForEach-Object { $_.position[1] } | Measure-Object -Maximum).Maximum
    if($peak -lt 1.2 -or $peak -gt 1.35) { throw "$name jump height: $peak" }
    if([math]::Abs($trace[119].speed-3) -gt .001 -or -not $trace[221].grounded) { throw "$name speed / landing" }
    if(($trace[269].position | Where-Object { $_ -ne 0 }).Count) { throw "$name reset position" }
    $baseline=Join-Path $work "$name-baseline.png"
    $reset=Join-Path $work "$name-reset.png"
    $resetPixels=[CharacterImageChecks]::Difference($baseline,$reset)
    if($resetPixels -ne 0) { throw "$name reset differs at $resetPixels pixels" }
    $changes=@{}
    foreach($state in @('walk','run','turn','jump','land')) {
        $capture=Join-Path $work "$name-$state.png"
        $pixels=[CharacterImageChecks]::Difference($baseline,$capture)
        if($pixels -lt 1000) { throw "$name $state changes only $pixels pixels" }
        $changes[$state]=$pixels
    }
    if($CaptureDocs) {
        $directory=Join-Path $root "Docs/$graphicsApi/Images"
        foreach($state in @('baseline','walk','run','turn','jump','land','reset')) {
            Copy-Item -LiteralPath (Join-Path $work "$name-$state.png") -Destination (Join-Path $directory "28_CharacterController-$state.png")
        }
    }
    $results+=@{api=$graphicsApi;configuration=$Configuration;executableSha256=$exeSha256;frames=300;peakMetres=$peak;resetDifferentPixels=$resetPixels;changedPixels=$changes;validationErrors=0;physicalGamepad='Not exercised by deterministic input test'}
    Write-Host "$name $Configuration PASS: move/run/orbit/jump/land, reset RGB exact, validation 0"
}
if ($Api.Count -eq 2) {
    $first=Get-Content -LiteralPath (Join-Path $work "$($Api[0])_28_CharacterController.character.json") -Raw
    $second=Get-Content -LiteralPath (Join-Path $work "$($Api[1])_28_CharacterController.character.json") -Raw
    if($first -cne $second) { throw 'The APIs produced different simulation traces' }
    Write-Host 'Direct3D12 / Vulkan simulation traces match exactly (300 frames)'
}
$results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $work 'results.json') -Encoding utf8
