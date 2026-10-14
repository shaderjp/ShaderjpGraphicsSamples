param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$disassembler=Join-Path $env:VULKAN_SDK 'Bin/spirv-dis.exe'
$validator=Join-Path $env:VULKAN_SDK 'Bin/spirv-val.exe'
$dxv=Join-Path $repo 'packages/Microsoft.Direct3D.DXC.1.9.2609.5/build/native/bin/x64/dxv.exe'
foreach($name in @('VisibilityVertex','VisibilityPixel','VisibilityCompute')){
    & $dxv (Join-Path $repo "bin/x64/$Configuration/Direct3D12_21_VisibilityBuffer/Shaders/$name.cso")
    if($LASTEXITCODE -ne 0){throw "$name DXIL validation failed"}
    & $validator --target-env vulkan1.3 (Join-Path $repo "bin/x64/$Configuration/Vulkan_21_VisibilityBuffer/Shaders/$name.spv")
    if($LASTEXITCODE -ne 0){throw "$name SPIR-V validation failed"}
}
$text=(& $disassembler (Join-Path $repo "bin/x64/$Configuration/Vulkan_21_VisibilityBuffer/Shaders/VisibilityCompute.spv"))-join"`n"
foreach($check in @(
    'OpMemberDecorate %VertexWords 0 Offset 0',
    'OpMemberDecorate %VertexWords 1 Offset 16',
    'OpMemberDecorate %VertexWords 2 Offset 32',
    'OpDecorate %_runtimearr_VertexWords ArrayStride 48',
    'OpMemberDecorate %InstanceData 2 Offset 128',
    'OpDecorate %_runtimearr_InstanceData ArrayStride 144',
    'OpMemberDecorate %MaterialData 3 Offset 48',
    'OpMemberDecorate %MaterialData 4 Offset 64',
    'OpDecorate %_runtimearr_MaterialData ArrayStride 80',
    'OpCapability SampledImageArrayNonUniformIndexing'
)){
    if(-not $text.Contains($check)){throw "Visibility CPU/SPIR-V contract mismatch: $check"}
}
Write-Output "Visibility $Configuration DXIL/SPIR-V, non-uniform indexing and shared 48/144/80-byte layouts passed"
