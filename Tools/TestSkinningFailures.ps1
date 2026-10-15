param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$work=Join-Path $root ".cache/skinning/failures/$Configuration"
New-Item -ItemType Directory -Path $work -Force | Out-Null
foreach($api in @('Direct3D12','Vulkan')){
    foreach($topic in @('22_VertexSkinning','23_ComputeSkinningCache','24_DualQuaternionSkinning','25_DualQuaternionComputeCache','Advanced11_DualQuaternionMeshSkinning','Advanced10_MeshShaderSkinning')){
        $name="$api`_$topic"
        $directory=Join-Path $root "bin/x64/$Configuration/$name"
        $exe=Join-Path $directory "$name.exe"
        $shaderName=if($topic -in @('23_ComputeSkinningCache','25_DualQuaternionComputeCache')){'SkinningCompute'}else{'ReferenceVertex'}
        $extension=if($api -eq 'Direct3D12'){'cso'}else{'spv'}
        $shader=Join-Path $directory "Shaders/$shaderName.$extension"
        $backup=$shader+'.failure-test-'+[guid]::NewGuid().ToString('N')
        $log=Join-Path $work "$name.log"
        Move-Item -LiteralPath $shader -Destination $backup
        try{
            $process=Start-Process -FilePath $exe -ArgumentList @('--frames','2','--no-ui','--log',('"'+$log+'"')) -WorkingDirectory $work -WindowStyle Hidden -PassThru
            if(-not $process.WaitForExit(30000)){$process.Kill();throw "Shader failure shutdown timeout: $name"}
            $text=Get-Content -LiteralPath $log -Raw
            if($process.ExitCode -ne 1 -or $text -notmatch 'ERROR:' -or $text -notmatch 'Validation errors: 0'){throw "Missing shader was not diagnosed cleanly: $name"}
            Write-Host "$name missing $shaderName PASS (error, orderly shutdown, validation 0)."
        }finally{Move-Item -LiteralPath $backup -Destination $shader}
    }
}
