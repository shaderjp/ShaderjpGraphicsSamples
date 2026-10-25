param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$dxv=Join-Path $repo 'packages/Microsoft.Direct3D.DXC.1.9.2609.5/build/native/bin/x64/dxv.exe'
$disassembler=Join-Path $env:VULKAN_SDK 'Bin/spirv-dis.exe';$validator=Join-Path $env:VULKAN_SDK 'Bin/spirv-val.exe'
foreach($shader in Get-ChildItem (Join-Path $repo "bin/x64/$Configuration/Direct3D12_Advanced19_BSDFMaterials/Shaders") -Filter *.cso){
 & $dxv $shader.FullName
 if($LASTEXITCODE){throw "DXIL invalid: $shader"}
}
foreach($shader in Get-ChildItem (Join-Path $repo "bin/x64/$Configuration/Vulkan_Advanced19_BSDFMaterials/Shaders") -Filter *.spv){
 & $validator --target-env vulkan1.3 $shader.FullName
 if($LASTEXITCODE){throw "SPIR-V invalid: $shader"}
 $text=(& $disassembler $shader.FullName)-join"`n"
 if($text-match'OpCapability RayQueryKHR|SPV_NV_ray_tracing'){throw "Unexpected RT extension: $shader"}
}
$path=Join-Path $repo "bin/x64/$Configuration/Vulkan_Advanced19_BSDFMaterials/Shaders/RayGeneration.spv"
$text=(& $disassembler $path)-join"`n"
foreach($pattern in @('OpDecorate %_runtimearr_Vertex ArrayStride 48','OpDecorate %_runtimearr_Instance ArrayStride 144','OpDecorate %_runtimearr_Material ArrayStride 112','OpDecorate %_runtimearr_AreaLight ArrayStride 64','OpMemberDecorate %Material 5 Offset 80','OpMemberDecorate %Material 6 Offset 96','OpTypeImage %float 2D 2 0 0 2 Rgba32f')){
 if(-not$text.Contains($pattern)){throw "Missing layout/format declaration: $pattern"}
}
foreach($offset in @(0,64,128,144,160,176,192,208,224,240)){
 if($text-notmatch"OpMemberDecorate %type_Scene \d+ Offset $offset(\r?\n|$)"){throw "Missing Scene offset: $offset"}
}
Write-Output 'BSDF DXIL/SPIR-V, KHR declarations, float32 accumulation and CPU/GPU layouts PASS'
