$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$dxv=Join-Path $repoRoot 'packages/microsoft.direct3d.dxc.1.9.2609.5/build/native/bin/x64/dxv.exe'
$count=0
foreach($configuration in @('Debug','Release')){foreach($api in @('Direct3D12','Vulkan')){
    $folders=Get-Item -Path (Join-Path $repoRoot "bin/x64/$configuration/$api`_Advanced1[2-5]_*/Shaders")
    foreach($folder in $folders){foreach($file in Get-ChildItem -LiteralPath $folder.FullName -File){
        if($api -eq 'Direct3D12'){$output=& $dxv $file.FullName 2>&1}
        else{$output=& "$env:VULKAN_SDK/Bin/spirv-val.exe" --target-env vulkan1.3 $file.FullName 2>&1}
        if($LASTEXITCODE){throw "Invalid shader $file : $output"}
        if($api -eq 'Vulkan' -and $file.BaseName -match '^(RayGeneration|Miss|Closest|Alpha)'){
            $assembly=& "$env:VULKAN_SDK/Bin/spirv-dis.exe" $file.FullName
            if($assembly -match 'OpCapability RayQuery|OpTypeRayQuery|OpRayQuery'){throw "Unexpected Ray Query: $file"}
        }
        ++$count
    }}
}}
if($count -lt 300){throw 'Missing shader outputs'}
Write-Output "$count offline DXIL/SPIR-V binaries validated; RT modules do not require Ray Query PASS"
