param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo ".cache/bsdf19/failures/$Configuration";New-Item -ItemType Directory -Force $work|Out-Null
foreach($api in @('Direct3D12','Vulkan')){
 $name="${api}_Advanced19_BSDFMaterials";$source=Join-Path $repo "bin/x64/$Configuration/$name"
 foreach($case in @('shader','overlay-bin','base-scene','mode','preset','material','fixture','ior','roughness','distance','bounces','sampling','negative-seed')){
  $directory=Join-Path $work "$name-$case";New-Item -ItemType Directory -Force $directory|Out-Null
  Get-ChildItem -LiteralPath $source|Copy-Item -Destination $directory -Recurse -Force
  $extra=@();$expected=''
  switch($case){
   'shader'{$extension=if($api-eq'Direct3D12'){'cso'}else{'spv'};Remove-Item -LiteralPath (Join-Path $directory "Shaders/RayGeneration.$extension");$expected='RayGeneration'}
   'overlay-bin'{Remove-Item -LiteralPath (Join-Path $directory 'Assets/Models/BSDFMaterials/BSDFMaterials.bin');$expected='BSDFMaterials.bin'}
   'base-scene'{Remove-Item -LiteralPath (Join-Path $directory 'Assets/Scenes/PathTracing/PathTracing.json');$expected='PathTracing.json'}
   'mode'{$extra=@('--bsdf-mode','bad');$expected='--bsdf-mode'}
   'preset'{$extra=@('--bsdf-preset','bad');$expected='--bsdf-preset'}
   'material'{$extra=@('--bsdf-material','material:Missing');$expected='--bsdf-material'}
   'fixture'{$extra=@('--bsdf-test','unknown');$expected='--bsdf-test'}
   'ior'{$extra=@('--bsdf-ior','0.9');$expected='invalid BSDF material override'}
   'roughness'{$extra=@('--bsdf-roughness','nan');$expected='invalid BSDF material override'}
   'distance'{$extra=@('--bsdf-absorption-distance','-1');$expected='invalid BSDF material override'}
   'bounces'{$extra=@('--pt-max-bounces','0');$expected='Invalid path tracing value'}
   'sampling'{$extra=@('--pt-sampling','bad');$expected='--pt-sampling'}
   'negative-seed'{$extra=@('--pt-seed','-1');$expected='Invalid path tracing value'}
  }
  $stdout=Join-Path $directory 'stdout.txt';$log=Join-Path $directory 'failure.log'
  $arguments=@('--frames','2','--no-ui','--log',('"'+$log+'"'))+$extra
  $process=Start-Process -FilePath (Join-Path $directory "$name.exe") -ArgumentList $arguments -RedirectStandardOutput $stdout -WindowStyle Hidden -PassThru
  if(-not$process.WaitForExit(60000)){$process.Kill();throw "$api $case timed out"}
  $text=Get-Content -LiteralPath $stdout -Raw
  if($process.ExitCode-ne 1-or-not$text.Contains($expected)-or$text-notmatch'Validation errors: 0'){throw "$api ${case}: $text"}
  Write-Output "$Configuration $api $case rejected and cleaned up PASS"
 }
}
