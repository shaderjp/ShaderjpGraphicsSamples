param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [string[]]$Topics=@('13_ShadowMap','14_RenderTexture','15_ComputeBloom','16_GpuParticles','17_DeferredPixel','18_DeferredCompute','19_TiledDeferred','20_ClusteredShading','21_VisibilityBuffer','Advanced01_RaytracingBasic','Advanced02_RaytracingShadows','Advanced03_RaytracingReflections','Advanced04_MeshShaderBasic','Advanced05_GpuDrivenRendering','Advanced06_MeshletCulling','Advanced07_MeshShaderShadows','Advanced08_MeshShaderDeferred','Advanced09_MeshShaderTiledDeferred'),
    [string]$PythonPath='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe'
)
$ErrorActionPreference='Stop'
$repositoryRoot=Split-Path $PSScriptRoot -Parent
$results=Join-Path $repositoryRoot ".cache/camera-navigation/$Configuration"
New-Item -ItemType Directory -Force -Path $results | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
public static class CameraPixels {
    public static long[] Compare(byte[] baseline, byte[] move, byte[] look, byte[] reset) {
        long moved=0, turned=0, resetMismatch=-1;
        for(int i=0;i<baseline.Length;i+=4) {
            bool m=false,t=false;
            for(int c=0;c<3;c++) {
                if(System.Math.Abs(move[i+c]-baseline[i+c])>2)m=true;
                if(System.Math.Abs(look[i+c]-move[i+c])>2)t=true;
                if(reset[i+c]!=baseline[i+c] && resetMismatch<0)resetMismatch=i+c;
            }
            if(m)moved++;if(t)turned++;
        }
        return new long[]{moved,turned,resetMismatch};
    }
}
'@
function ReadPixels([string]$path) {
    $image=[System.Drawing.Bitmap]::new($path)
    try {
        if($image.Width -ne 1280 -or $image.Height -ne 720){throw "Unexpected capture size: $path"}
        $data=$image.LockBits([System.Drawing.Rectangle]::new(0,0,1280,720),[System.Drawing.Imaging.ImageLockMode]::ReadOnly,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $pixels=[byte[]]::new(1280*720*4)
            for($row=0;$row -lt 720;$row++) {
                [System.Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0,$row*$data.Stride),$pixels,$row*1280*4,1280*4)
            }
            return ,$pixels
        } finally {$image.UnlockBits($data)}
    } finally {$image.Dispose()}
}
foreach($api in @('Direct3D12','Vulkan')) {
    foreach($topic in $Topics) {
        $name="$api`_$topic"
        $exe=Join-Path $repositoryRoot "bin/x64/$Configuration/$name/$name.exe"
        $log=Join-Path $results "$name.log"
        $image=Join-Path $results "$name.png"
        $arguments=@('--frames','32','--test-camera','--no-ui','--pause','--fixed-dt','0.016666667','--log',('"'+$log+'"'),'--screenshot',('"'+$image+'"'))
        $numericReset=$topic -in @('Advanced05_GpuDrivenRendering','Advanced09_MeshShaderTiledDeferred','21_VisibilityBuffer')
        if($numericReset){$arguments+='--capture-gpu'}
        $oldSync=$env:VK_LAYER_VALIDATE_SYNC
        try {
            if($Configuration -eq 'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
            $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $results -WindowStyle Hidden -PassThru
            if(-not $process.WaitForExit(60000)){$process.Kill();throw "$name timed out"}
            $text=Get-Content -LiteralPath $log -Raw
            if($process.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0'){throw "$name failed: $text"}
            if($Configuration -eq 'Debug' -and $text -notmatch '(Debug Layer|Validation Layer) enabled'){throw "$name validation disabled"}
        } finally {$env:VK_LAYER_VALIDATE_SYNC=$oldSync}
        $baseline=ReadPixels (Join-Path $results "$name-camera-baseline.png")
        $move=ReadPixels (Join-Path $results "$name-camera-move.png")
        $look=ReadPixels (Join-Path $results "$name-camera-look.png")
        $reset=ReadPixels (Join-Path $results "$name-camera-reset.png")
        $comparison=[CameraPixels]::Compare($baseline,$move,$look,$reset)
        $moved=$comparison[0];$turned=$comparison[1]
        $resetDescription='exact Reset'
        if($numericReset){
            # GPU work/light ordering can change depth ties and FP accumulation.
            # Verify exact scene constants plus independent visibility/HDR instead.
            $numericScript=if($topic -eq '21_VisibilityBuffer'){'Tests/VisibilityGpuTests.py'}else{'Tests/MeshShaderGpuTests.py'}
            & $PythonPath (Join-Path $repositoryRoot $numericScript) $results $name 'camera'
            if($LASTEXITCODE -ne 0){throw "$name camera GPU Reset failed"}
            $resetDescription='GPU constants/visibility/HDR Reset'
        } elseif($comparison[2] -ge 0){throw "$name camera Reset failed at byte $($comparison[2])"}
        if($moved -lt 1000 -or $turned -lt 1000){throw "$name input did not change rendering: moved=$moved turned=$turned"}
        Write-Output "$name passed (W movement: $moved pixels, right drag: $turned pixels, $resetDescription, validation errors: 0)"
    }
}
