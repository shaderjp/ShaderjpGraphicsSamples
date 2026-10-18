param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$topics=@('26_InlineRaytracingShadows','27_InlineRaytracingReflections','Advanced16_BindlessInlineHybrid','Advanced17_MeshShaderTiledInlineShadows')
$dxv=Join-Path $repoRoot 'packages/Microsoft.Direct3D.DXC.1.9.2609.5/build/native/bin/x64/dxv.exe'
foreach($topic in $topics){
    foreach($api in @('Direct3D12','Vulkan')){
        $folder=Join-Path $repoRoot "bin/x64/$Configuration/$($api)_$topic/Shaders"
        $queries=0
        foreach($file in Get-ChildItem -LiteralPath $folder -File){
            if($api-eq'Direct3D12'){$output=& $dxv $file.FullName 2>&1}
            else{$output=& "$env:VULKAN_SDK/Bin/spirv-val.exe" --target-env vulkan1.3 $file.FullName 2>&1}
            if($LASTEXITCODE){throw "Invalid shader $file : $output"}
            if($api-eq'Vulkan'){
                $assembly=(& "$env:VULKAN_SDK/Bin/spirv-dis.exe" $file.FullName)-join [Environment]::NewLine
                if($assembly-match'OpCapability RayTracingKHR|SPV_NV_ray_tracing'){throw "Unexpected RT Pipeline/NV capability: $file"}
                if($file.BaseName-match'^Inline'){
                    if($assembly-notmatch'OpCapability RayQueryKHR' -or $assembly-notmatch'OpRayQueryProceedKHR'){throw "Missing actual RayQuery traversal: $file"}
                    ++$queries
                    if($file.BaseName-eq'InlineLightingCompute'){
                        foreach($layout in @('InlineVertex ArrayStride 48','InlinePrimitive ArrayStride 16','InlineInstance ArrayStride 144','InlineMaterial ArrayStride 80','type_InlineSettings 1 Offset 16')){
                            if(!$assembly.Contains($layout)){throw "CPU/SPIR-V layout mismatch: $layout ($file)"}
                        }
                    }
                }
            }
        }
        if($api-eq'Vulkan'-and$queries-ne$(if($topic-like'Advanced16*'){3}else{1})){throw "Missing inline shader outputs: $topic"}
    }
    Write-Output "$Configuration $topic offline DXIL/SPIR-V and RayQuery capability PASS"
}
foreach($mode in 0..2){
    $reference=Join-Path $repoRoot "bin/x64/$Configuration/Vulkan_Advanced15_BindlessHybrid/Shaders/RayGeneration$mode.spv"
    $assembly=(& "$env:VULKAN_SDK/Bin/spirv-dis.exe" $reference)-join [Environment]::NewLine
    if($LASTEXITCODE -or $assembly-notmatch'OpCapability RayTracingKHR' -or $assembly-match'OpCapability RayQueryKHR'){
        throw "RT Pipeline reference has unexpected capabilities: $reference"
    }
}
Write-Output "$Configuration existing RT Pipeline and new RayQuery declarations are distinct PASS"
