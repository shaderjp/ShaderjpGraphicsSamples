param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[switch]$CaptureDocs,[string]$PythonPath='C:\Program Files\Blender Foundation\Blender 5.2\5.2\python\bin\python.exe')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/post-tests/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
Add-Type -AssemblyName System.Drawing
if(-not ('PostPixelCompare' -as [type])) {
Add-Type -TypeDefinition @'
using System;
public static class PostPixelCompare {
 public static int[] Difference(byte[] a,byte[] b,int width,int height,int left) {
  int changed=0,max=0;
  for(int y=0;y<height;y++)for(int x=left;x<width;x++){
   int p=(y*width+x)*4,d=0;for(int c=0;c<3;c++)d=Math.Max(d,Math.Abs(a[p+c]-b[p+c]));
   if(d>2)changed++;max=Math.Max(max,d);
  }return new int[]{changed,max};
 }
}
'@
}
function Bytes([string]$Path){
 $b=[System.Drawing.Bitmap]::new($Path)
 try {$data=$b.LockBits([System.Drawing.Rectangle]::new(0,0,$b.Width,$b.Height),[System.Drawing.Imaging.ImageLockMode]::ReadOnly,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  try{$out=[byte[]]::new($b.Width*$b.Height*4);for($y=0;$y -lt $b.Height;$y++){[System.Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0,$y*$data.Stride),$out,$y*$b.Width*4,$b.Width*4)};return ,$out}finally{$b.UnlockBits($data)}
 }finally{$b.Dispose()}
}
function Difference($A,$B,[int]$Left=0){[PostPixelCompare]::Difference((Bytes $A),(Bytes $B),1280,720,$Left)}
function Run([string]$Api,[string]$Topic,[string]$Label,[string[]]$Extra=@(),[int]$Frames=8,[switch]$Ui,[switch]$Gpu){
 $name="${Api}_${Topic}";$png=Join-Path $work "$Api-$Label.png";$log=Join-Path $work "$Api-$Label.log"
 $arguments=@('--frames',"$Frames",'--fixed-dt','0.016666667','--log',('"'+$log+'"'),'--screenshot',('"'+$png+'"'))
 if(-not $Ui){$arguments+='--no-ui'};if($Gpu){$arguments+='--capture-gpu'};$arguments+=$Extra
 $old=$env:VK_LAYER_VALIDATE_SYNC
 try {if($Api -eq 'Vulkan' -and $Configuration -eq 'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
  $process=Start-Process -FilePath (Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
 }finally{$env:VK_LAYER_VALIDATE_SYNC=$old}
 if(-not $process.WaitForExit(45000)){$process.Kill();throw "$name timed out ($Label)"}
 $text=Get-Content -LiteralPath $log -Raw
 if($process.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0' -or $text -match '(?m)^VALIDATION ERROR:|D3D12 warning:') {throw "$name / $Label failed: $text"}
 if($Configuration -eq 'Debug' -and $text -notmatch '(Debug Layer|Validation Layer) enabled'){throw 'Debug validation disabled'}
 $articleLabels=@('14-final','14-grayscale','14-monitor','14-monitor-moved','15-input','15-final','15-extract','15-horizontal','15-bloom','16-final','16-paused','16-moving')
 if($CaptureDocs -and $Label -in $articleLabels){$dest=Join-Path $repoRoot "Docs/$Api/Images/$Topic-$Label.png";Copy-Item -LiteralPath $png -Destination $dest}
 return $png
}
$summary=@()
foreach($api in @('Direct3D12','Vulkan')){
 $final=Run $api '14_RenderTexture' '14-final' -Gpu
 $gray=Run $api '14_RenderTexture' '14-grayscale' @('--post-view','grayscale')
 $monitor=Run $api '14_RenderTexture' '14-monitor' @('--post-view','monitor') -Gpu
 $moved=Run $api '14_RenderTexture' '14-monitor-moved' @('--post-view','monitor','--occluder-x','1') -Gpu
 if((Difference $final $gray)[0] -lt 1000 -or (Difference $monitor $moved)[0] -lt 100){throw 'Post view or monitor did not change pixels'}
 $legacy=Run $api '13_ShadowMap' '13-reference'
 $sceneArgument='"'+(Join-Path $repoRoot 'Assets/Scenes/LightingLab/Exhibition.json')+'"'
 $compat=Run $api '14_RenderTexture' '14-reference' @('--scene',$sceneArgument)
 if((Difference $legacy $compat)[1] -gt 2){throw 'HDR post-process changed legacy lighting beyond half/8-bit rounding'}
 $inputImage=Run $api '15_ComputeBloom' '15-input' @('--post-view','input')
 $bloom=Run $api '15_ComputeBloom' '15-final'
 if((Difference $inputImage $bloom)[0] -lt 100){throw 'Bloom contribution missing'}
 foreach($view in @('extract','horizontal','bloom')){Run $api '15_ComputeBloom' "15-$view" @('--post-view',$view)|Out-Null}
 foreach($mode in @('black','constant','impulse')){Run $api '15_ComputeBloom' "15-$mode" @('--test-image',$mode) -Gpu|Out-Null}
 Run $api '15_ComputeBloom' '15-constant-odd' @('--test-image','constant','--test-odd') -Frames 20 -Gpu|Out-Null
 Run $api '16_GpuParticles' '16-paused' @('--particles','257','--pause') -Gpu|Out-Null
 Run $api '16_GpuParticles' '16-moving' @('--particles','257') -Gpu|Out-Null
 foreach($count in @('1','65536')){Run $api '16_GpuParticles' "16-count-$count" @('--particles',$count) -Gpu|Out-Null}
 Run $api '14_RenderTexture' '14-room' @('--environment','room')|Out-Null
 Run $api '16_GpuParticles' '16-final' -Frames 48|Out-Null
 $ui=Run $api '16_GpuParticles' '16-ui' @('--test-post') -Frames 36 -Ui -Gpu
 $baseline=$ui.Replace('.png','-baseline.png');$edited=$ui.Replace('.png','-edited.png');$reset=$ui.Replace('.png','-reset.png')
 if((Difference $baseline $edited 350)[0] -lt 30 -or (Difference $baseline $reset 350)[1] -ne 0){throw 'Particle Step or Reset pixels incorrect'}
 foreach($topic in @('14_RenderTexture','15_ComputeBloom')){
  $ui=Run $api $topic "$topic-ui" @('--test-ui') -Frames 36 -Ui
  if((Difference ($ui.Replace('.png','-baseline.png')) ($ui.Replace('.png','-edited.png')) 350)[0] -lt 1000 -or (Difference ($ui.Replace('.png','-baseline.png')) ($ui.Replace('.png','-reset.png')) 350)[1] -ne 0){throw 'Post UI parameter / Reset failed'}
 }
 $summary+=@{api=$api;hdrCompatibilityMaxByteError=(Difference $legacy $compat)[1];bloomChangedPixels=(Difference $inputImage $bloom)[0];particleUiReset='exact'}
 Write-Output "$api $Configuration post modes, live monitor, Bloom and particle UI PASS"
}
& $PythonPath (Join-Path $repoRoot 'Tests/VerifyPostProcess.py') $work
if($LASTEXITCODE){throw 'GPU numerical invariants failed'}
$summary|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $work 'summary.json') -Encoding utf8
