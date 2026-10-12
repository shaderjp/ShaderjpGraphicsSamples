param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$spirvDis=Join-Path $env:VULKAN_SDK 'Bin/spirv-dis.exe'
$spirvVal=Join-Path $env:VULKAN_SDK 'Bin/spirv-val.exe'
$dxv=Join-Path $repo 'packages/Microsoft.Direct3D.DXC.1.9.2609.5/build/native/bin/x64/dxv.exe'
foreach($topic in @('Advanced01_RaytracingBasic','Advanced02_RaytracingShadows','Advanced03_RaytracingReflections')){
    foreach($file in Get-ChildItem (Join-Path $repo "bin/x64/$Configuration/Direct3D12_$topic/Shaders") -Filter *.cso){
        & $dxv $file.FullName
        if($LASTEXITCODE-ne 0){throw "DXIL validation failed: $file"}
    }
    foreach($file in Get-ChildItem (Join-Path $repo "bin/x64/$Configuration/Vulkan_$topic/Shaders") -Filter *.spv){
        & $spirvVal --target-env vulkan1.3 $file.FullName
        if($LASTEXITCODE-ne 0){throw "SPIR-V validation failed: $file"}
        $dis=(& $spirvDis $file.FullName)-join"`n"
        if($dis-match'OpCapability RayQueryKHR|SPV_NV_ray_tracing'){throw "Unexpected RT capability: $file"}
    }
    $dis=(& $spirvDis (Join-Path $repo "bin/x64/$Configuration/Vulkan_$topic/Shaders/ClosestHit.spv"))-join"`n"
    foreach($check in @('OpDecorate %_runtimearr_Vertex ArrayStride 48','OpDecorate %_runtimearr_Instance ArrayStride 144','OpDecorate %_runtimearr_Material ArrayStride 80','OpMemberDecorate %Material 3 Offset 48','OpMemberDecorate %Instance 2 Offset 128')){
        if(-not $dis.Contains($check)){throw "CPU/SPIR-V layout mismatch: $check"}
    }
    if($topic-ne'Advanced01_RaytracingBasic'){
        foreach($offset in @(0,64,128,144,160,176,192,208,224)){
            if($dis-notmatch"OpMemberDecorate %type_Scene \d+ Offset $offset(\r?\n|$)"){throw "Missing Scene offset $offset"}
        }
    }
    Write-Output "$topic DXIL/SPIR-V/KHR declarations and CPU layouts passed"
}
