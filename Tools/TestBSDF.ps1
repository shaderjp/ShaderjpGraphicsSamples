param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [ValidateSet('Direct3D12','Vulkan')][string[]]$Apis=@('Direct3D12','Vulkan'),
 [switch]$Quick,[switch]$CaptureDocs,[switch]$SkipNumerical,[switch]$ExtrasOnly,[switch]$PanelOnly,[switch]$UiOnly,
 [string]$PythonPath='C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe'
)
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo ".cache/bsdf19/gpu/$Configuration";New-Item -ItemType Directory -Force $work|Out-Null
function Run([string]$Api,[string]$Label,[int]$Spp,[string[]]$Extra=@(),[switch]$Ui,[int]$Frames=0){
 $name="${Api}_Advanced19_BSDFMaterials";$png=Join-Path $work "$Api-$Label.png";$log=Join-Path $work "$Api-$Label.log"
 if(-not $Frames){$Frames=$Spp}
 $arguments=@('--frames',"$Frames",'--pt-target-spp',"$Spp",'--capture-gpu','--screenshot',('"'+$png+'"'),'--log',('"'+$log+'"'))+$Extra
 if(-not $Ui){$arguments+='--no-ui'}
 $previous=$env:VK_LAYER_VALIDATE_SYNC
 try{if($Api-eq'Vulkan'-and$Configuration-eq'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
  $process=Start-Process -FilePath (Join-Path $repo "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
 }finally{$env:VK_LAYER_VALIDATE_SYNC=$previous}
 if(-not $process.WaitForExit(600000)){$process.Kill();throw "$Api $Label timed out: $log"}
 $text=Get-Content -LiteralPath $log -Raw
 if($process.ExitCode-ne 0-or$text-notmatch'Validation errors: 0'-or$text-match'(?m)^VALIDATION ERROR:|D3D12 warning:'){throw "$Api $Label failed: $text"}
 if($Configuration-eq'Debug'-and$text-notmatch'(Debug Layer|Validation Layer) enabled'){throw 'Validation layer missing'}
 if($CaptureDocs-and$Label-match'^(full|reflection|legacy|thickness|inside|ior|roughness|full-(1|16|256))$'){
  Copy-Item -LiteralPath $png -Destination (Join-Path $repo "Docs/$Api/Images/Advanced19_BSDFMaterials-$Label.png")
 }
 if($CaptureDocs-and$Label-eq'panel'){Copy-Item -LiteralPath $png -Destination (Join-Path $repo "Docs/$Api/Images/Advanced19_BSDFMaterials.png")}
 $m=Get-Content -LiteralPath (Join-Path $work "$Api-$Label-gpu/metadata.json") -Raw|ConvertFrom-Json
 if($m.technique-ne 19){throw "$Api ${Label}: incorrect sample metadata label"}
 Write-Output "$Configuration $Api $Label ($($m.spp) spp, $($m.width)x$($m.height)) PASS"
}
foreach($api in $Apis){
 if($PanelOnly){Run $api 'panel' 24 -Ui;continue}
 if($UiOnly){
  Run $api 'ui' 4 @('--test-ui') -Ui -Frames 74
  Run $api 'ui-custom' 4 @('--test-ui','--bsdf-mode','reflection','--bsdf-preset','thickness','--camera','ThicknessOblique','--bsdf-material','material:AbsorbingGlass','--bsdf-ior','1.8','--bsdf-roughness','0.15','--pt-max-bounces','6','--pt-sampling','bsdf') -Ui -Frames 40
  Run $api 'ui-flow' 0 @('--test-ui') -Ui -Frames 52
  Run $api 'camera' 0 @('--test-camera') -Frames 48
  if(-not $SkipNumerical){& $PythonPath (Join-Path $repo 'Tests/BSDFGpuTests.py') $work $api --ui-only;if($LASTEXITCODE){throw "$api BSDF widget tests failed"}}
  continue
 }
 if($ExtrasOnly){
  Run $api 'ui-flow' 0 @('--test-ui') -Ui -Frames 52
  Run $api 'camera' 0 @('--test-camera') -Frames 48
  if(-not $SkipNumerical){& $PythonPath (Join-Path $repo 'Tests/BSDFGpuTests.py') $work $api --extras-only;if($LASTEXITCODE){throw "$api BSDF extra UI/camera tests failed"}}
  continue
 }
 if($Quick){
  Run $api 'quick-full' 4;Run $api 'quick-reflection' 4 @('--bsdf-mode','reflection');Run $api 'quick-legacy' 4 @('--bsdf-mode','legacy-pbr');Run $api 'optics-full' 1 @('--bsdf-test','optics');continue
 }
 foreach($mode in @('full','reflection','legacy-pbr')){Run $api "optics-$mode" 1 @('--bsdf-test','optics','--bsdf-mode',$mode)}
 $referenceSpp=if($CaptureDocs-or$Configuration-eq'Release'){1024}else{256}
 Run $api 'full' $referenceSpp;Run $api 'reflection' $referenceSpp @('--bsdf-mode','reflection');Run $api 'legacy' $referenceSpp @('--bsdf-mode','legacy-pbr')
 foreach($spp in @(1,16,256)){Run $api "full-$spp" $spp}
 Run $api 'thickness' $referenceSpp @('--bsdf-preset','thickness')
 Run $api 'ior' $referenceSpp @('--bsdf-ior','2.2');Run $api 'roughness' $referenceSpp @('--bsdf-roughness','0.3')
 Run $api 'panel' 24 -Ui
 Run $api 'inside' 256 @('--camera','InsideClear')
 Run $api 'dark' 16 @('--bsdf-test','dark')
 Run $api 'slab' 16 @('--bsdf-test','slab');Run $api 'scaled' 16 @('--bsdf-test','scaled')
 foreach($seed in @(1,7,19)){
  Run $api "furnace-$seed" 256 @('--bsdf-test','furnace','--pt-seed',"$seed")
  Run $api "rough-mis-$seed" 128 @('--pt-seed',"$seed")
  Run $api "rough-bsdf-$seed" 128 @('--pt-seed',"$seed",'--pt-sampling','bsdf')
 }
 Run $api 'ui' 4 @('--test-ui') -Ui -Frames 74
 Run $api 'ui-custom' 4 @('--test-ui','--bsdf-mode','reflection','--bsdf-preset','thickness','--camera','ThicknessOblique','--bsdf-material','material:AbsorbingGlass','--bsdf-ior','1.8','--bsdf-roughness','0.15','--pt-max-bounces','6','--pt-sampling','bsdf') -Ui -Frames 40
 Run $api 'ui-flow' 0 @('--test-ui') -Ui -Frames 52
 Run $api 'camera' 0 @('--test-camera') -Frames 48
 Run $api 'odd' 0 @('--test-odd') -Frames 20
 Run $api 'lifecycle' 4 @('--test-window') -Frames 48
 Run $api 'repeat' 16
 if($api-eq'Vulkan'){Run $api 'capture-minimized' 0 @('--test-window') -Frames 24}
 if(-not $SkipNumerical){& $PythonPath (Join-Path $repo 'Tests/BSDFGpuTests.py') $work $api;if($LASTEXITCODE){throw "$api BSDF numerical tests failed"}}
}
