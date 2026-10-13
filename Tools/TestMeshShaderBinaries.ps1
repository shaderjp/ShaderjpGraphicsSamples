param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$dxv=Join-Path $repo 'packages/Microsoft.Direct3D.DXC.1.9.2609.5/build/native/bin/x64/dxv.exe'
$val=Join-Path $env:VULKAN_SDK 'Bin/spirv-val.exe';$disassembler=Join-Path $env:VULKAN_SDK 'Bin/spirv-dis.exe'
foreach($project in Get-ChildItem (Join-Path $repo 'Samples/Direct3D12') -Directory | Where-Object Name -Match '^Advanced0[4-9]_'){
    foreach($file in Get-ChildItem (Join-Path $repo "bin/x64/$Configuration/Direct3D12_$($project.Name)/Shaders") -Filter '*.cso'){& $dxv $file.FullName;if($LASTEXITCODE){throw "DXIL failed: $file"}}
    $vk=Join-Path $repo "bin/x64/$Configuration/Vulkan_$($project.Name)/Shaders"
    foreach($file in Get-ChildItem $vk -Filter '*.spv'){& $val --target-env vulkan1.3 $file.FullName;if($LASTEXITCODE){throw "SPIR-V failed: $file"};$text=(& $disassembler $file.FullName)-join"`n";if($text-match'OpCapability MeshShadingNV'){throw "NV mesh capability must not be used: $file"}}
    if($project.Name -notlike 'Advanced04*'){
        $layoutShader=if($project.Name -like 'Advanced05*'){'CullMeshlets.spv'}else{'MeshletTask.spv'}
        $text=(& $disassembler (Join-Path $vk $layoutShader))-join"`n"
        if($LASTEXITCODE){throw "SPIR-V disassembly failed: $layoutShader"}
        $text+="`n"+((& $disassembler (Join-Path $vk 'MeshletMesh.spv'))-join"`n")
        if($LASTEXITCODE){throw 'SPIR-V disassembly failed: MeshletMesh.spv'}
        $layouts=@{MeshVertex=@(48,0,16,32);Meshlet=@(64,0,16,32,48);MeshInstance=@(144,0,64,128);MeshPrimitive=@(32,0,16);MeshMaterial=@(80,0,16,32,48,64);MeshFrame=@(128,0,96,112)}
        foreach($type in $layouts.Keys){
            $layout=$layouts[$type];$stride="OpDecorate %_runtimearr_$type ArrayStride $($layout[0])"
            if(-not$text.Contains($stride)){throw "CPU/SPIR-V layout mismatch: $stride"}
            for($member=0;$member -lt $layout.Count-1;$member++){
                $offset="OpMemberDecorate %$type $member Offset $($layout[$member+1])"
                if(-not$text.Contains($offset)){throw "CPU/SPIR-V layout mismatch: $offset"}
            }
        }
    }
    if($project.Name -like 'Advanced04*'){
        $text=(& $disassembler (Join-Path $vk 'Mesh.spv'))-join"`n"
        if($LASTEXITCODE){throw 'SPIR-V disassembly failed: Basic Mesh.spv'}
        foreach($check in @('OpDecorate %_runtimearr_Vertex ArrayStride 32','OpMemberDecorate %Vertex 0 Offset 0','OpMemberDecorate %Vertex 1 Offset 16','OpMemberDecorate %type_Scene 0 Offset 0','OpMemberDecorate %type_Scene 1 Offset 64','OpMemberDecorate %type_Scene 2 Offset 80')){
            if(-not$text.Contains($check)){throw "Basic CPU/SPIR-V layout mismatch: $check"}
        }
    }
    Write-Output "$($project.Name) DXIL/SPIR-V/EXT mesh stages and CPU layouts passed"
}
