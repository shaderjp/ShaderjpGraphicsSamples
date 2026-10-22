param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[switch]$CaptureDocs)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$work=Join-Path $root ".cache/motion-matching/gpu/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
Add-Type -AssemblyName System.Drawing
function ImageHash([string]$Path) {
    $bitmap=[System.Drawing.Bitmap]::new($Path)
    try {
        if($bitmap.Width -ne 1280 -or $bitmap.Height -ne 720){throw "Wrong image size: $Path"}
        $bits=$bitmap.LockBits([System.Drawing.Rectangle]::new(0,0,1280,720),[System.Drawing.Imaging.ImageLockMode]::ReadOnly,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {$bytes=[byte[]]::new($bits.Stride*720);[System.Runtime.InteropServices.Marshal]::Copy($bits.Scan0,$bytes,0,$bytes.Length)}finally{$bitmap.UnlockBits($bits)}
        return [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($bytes))
    }finally{$bitmap.Dispose()}
}
$topics=@('29_MotionMatching','30_TraversalController')
$results=@();$traces=@{}
foreach($api in @('Direct3D12','Vulkan')){
    foreach($topic in $topics){
        $name="${api}_$topic";$exe=Join-Path $root "bin/x64/$Configuration/$name/$name.exe"
        $image=Join-Path $work "$name.png";$log=Join-Path $work "$name.log"
        $frames=if($topic.StartsWith('29_')){300}else{900}
        $previousSync=$env:VK_LAYER_VALIDATE_SYNC;$previousRecord=$env:SHADERJP_CHARACTER_FRAMES
        try {
            if($Configuration -eq 'Debug' -and $api -eq 'Vulkan'){$env:VK_LAYER_VALIDATE_SYNC='1'}
            $env:SHADERJP_CHARACTER_FRAMES=$null
            $arguments=@('--test-character','--frames',"$frames",'--fixed-dt','0.016666667','--no-ui','--screenshot',('"'+$image+'"'),'--log',('"'+$log+'"'))
            $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $work -PassThru -WindowStyle Hidden
        } finally {$env:VK_LAYER_VALIDATE_SYNC=$previousSync;$env:SHADERJP_CHARACTER_FRAMES=$previousRecord}
        $timer=[Diagnostics.Stopwatch]::StartNew()
        while(-not $process.WaitForExit(1000)){if($timer.Elapsed.TotalSeconds -gt 180){$process.Kill();throw "$name timed out"}}
        if($process.ExitCode -ne 0){throw "$name failed: $(Get-Content -LiteralPath $log -Raw)"}
        $text=Get-Content -LiteralPath $log -Raw
        if($text -notmatch 'Validation errors: 0'){throw "$name validation errors"}
        if($Configuration -eq 'Debug' -and $text -notmatch '(Debug Layer|Validation Layer) enabled'){throw "$name validation layer disabled"}
        $trace=Get-Content -LiteralPath (Join-Path $work "$name.character.json") -Raw|ConvertFrom-Json
        if($trace.Count -ne $frames){throw "$name missing trace frames"}
        if(($trace.searches|Measure-Object -Maximum).Maximum -lt 10){throw "$name did not search"}
        if(($trace.footError|Measure-Object -Maximum).Maximum -gt .05){throw "$name foot error exceeds 5 cm"}
        if(($trace.penetration|Measure-Object -Maximum).Maximum -gt .01){throw "$name penetrated level"}
        if($topic -eq '30_TraversalController'){
            if(@($trace|Where-Object{-not $_.grounded}).Count -lt 20){throw "$name did not jump"}
            if(-not $trace[224].grounded){throw "$name did not land"}
            if($trace[599].position[2] -lt -5.1){throw "$name crossed the blocking wall"}
        }
        $baseline=Join-Path $work "$name-baseline.png";$reset=Join-Path $work "$name-reset.png"
        if((ImageHash $baseline) -ne (ImageHash $reset)){throw "$name Reset pixels differ"}
        if((ImageHash $baseline) -eq (ImageHash (Join-Path $work "$name-walk.png"))){throw "$name scene pixels did not change"}
        $canonical=@($trace|ForEach-Object{$item=$_|Select-Object * -ExcludeProperty searchMs;$item|ConvertTo-Json -Compress}) -join "`n"
        if($api -eq 'Direct3D12'){$traces[$topic]=$canonical}elseif($traces[$topic] -ne $canonical){throw "$topic CPU trace differs between APIs"}
        $captures=@{}
        foreach($capture in Get-ChildItem -LiteralPath $work -Filter "$name*.png"){
            $captures[$capture.Name]=(Get-FileHash -LiteralPath $capture.FullName).Hash.ToLowerInvariant()
            if($CaptureDocs){Copy-Item -LiteralPath $capture.FullName -Destination (Join-Path $root "Docs/$api/Images/$($capture.Name.Replace($api+'_',''))")}
        }
        $results+=[ordered]@{name=$name;configuration=$Configuration;frames=$frames;validationErrors=0;resetRgb='identical';traceMatchesOtherApi=$true;maxFootError=($trace.footError|Measure-Object -Maximum).Maximum;maxPenetration=($trace.penetration|Measure-Object -Maximum).Maximum;executableSha256=(Get-FileHash -LiteralPath $exe).Hash.ToLowerInvariant();captures=$captures}
        Write-Output "$name $Configuration PASS ($frames frames, validation 0, Reset RGB identical)"
    }
}
$results|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root "Docs/MotionMatchingGpu$Configuration.json") -Encoding utf8
