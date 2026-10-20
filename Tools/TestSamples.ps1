param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [switch]$CaptureDocs,
    [ValidateSet('01_Initialization','02_Triangle','03_Texture','04_Box','05_Model','06_Gltf','07_Lighting','08_PBR','09_ImGui','10_Cubemap','11_DiffuseIBL','12_SpecularIBL','13_ShadowMap','14_RenderTexture','15_ComputeBloom','16_GpuParticles','17_DeferredPixel','18_DeferredCompute','Advanced01_RaytracingBasic','Advanced02_RaytracingShadows','Advanced03_RaytracingReflections','Advanced04_MeshShaderBasic','Advanced05_GpuDrivenRendering','Advanced06_MeshletCulling','Advanced07_MeshShaderShadows','Advanced08_MeshShaderDeferred','Advanced09_MeshShaderTiledDeferred','19_TiledDeferred','20_ClusteredShading','21_VisibilityBuffer','22_VertexSkinning','23_ComputeSkinningCache','24_DualQuaternionSkinning','25_DualQuaternionComputeCache','Advanced11_DualQuaternionMeshSkinning','Advanced10_MeshShaderSkinning','Advanced12_BindlessMaterials','Advanced13_BindlessMeshShader','Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid','26_InlineRaytracingShadows','27_InlineRaytracingReflections','28_CharacterController','Advanced16_BindlessInlineHybrid','Advanced17_MeshShaderTiledInlineShadows')]
    [string[]]$Topics = @('01_Initialization','02_Triangle','03_Texture','04_Box','05_Model','06_Gltf','07_Lighting','08_PBR','09_ImGui','10_Cubemap','11_DiffuseIBL','12_SpecularIBL','13_ShadowMap','14_RenderTexture','15_ComputeBloom','16_GpuParticles','17_DeferredPixel','18_DeferredCompute','Advanced01_RaytracingBasic','Advanced02_RaytracingShadows','Advanced03_RaytracingReflections','Advanced04_MeshShaderBasic','Advanced05_GpuDrivenRendering','Advanced06_MeshletCulling','Advanced07_MeshShaderShadows','Advanced08_MeshShaderDeferred','Advanced09_MeshShaderTiledDeferred','19_TiledDeferred','20_ClusteredShading','21_VisibilityBuffer','22_VertexSkinning','23_ComputeSkinningCache','24_DualQuaternionSkinning','25_DualQuaternionComputeCache','Advanced11_DualQuaternionMeshSkinning','Advanced10_MeshShaderSkinning','Advanced12_BindlessMaterials','Advanced13_BindlessMeshShader','Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid','26_InlineRaytracingShadows','27_InlineRaytracingReflections','28_CharacterController','Advanced16_BindlessInlineHybrid','Advanced17_MeshShaderTiledInlineShadows')
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$resultsDirectory = Join-Path $repositoryRoot ('.cache\validation\' + $Configuration)
New-Item -ItemType Directory -Path $resultsDirectory -Force | Out-Null
Add-Type -AssemblyName System.Drawing
$failures = @()
foreach ($api in @('Direct3D12','Vulkan')) {
    foreach ($topic in $Topics) {
        $name = $api + '_' + $topic
        $executable = Join-Path $repositoryRoot ("bin\x64\$Configuration\$name\$name.exe")
        $log = Join-Path $resultsDirectory ($name + '.log')
        $image = Join-Path $resultsDirectory ($name + '.png')
        if ($CaptureDocs) { $image = Join-Path $repositoryRoot ("Docs\$api\Images\$topic.png") }
        $arguments = @('--frames','48','--test-window','--log',('"' + $log + '"'),'--screenshot',('"' + $image + '"'))
        $previousSyncSetting = $env:VK_LAYER_VALIDATE_SYNC
        try {
            if ($api -eq 'Vulkan' -and $Configuration -eq 'Debug') { $env:VK_LAYER_VALIDATE_SYNC = '1' }
            $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $resultsDirectory -PassThru -WindowStyle Hidden
        } finally { $env:VK_LAYER_VALIDATE_SYNC = $previousSyncSetting }
        if (-not $process.WaitForExit($(if($topic -match "^Advanced1[2-7]_"){300000}else{30000}))) { $process.Kill(); $failures += "$name timed out"; continue }
        if ($process.ExitCode -ne 0) { $failures += "$name exited with $($process.ExitCode): $(Get-Content -LiteralPath $log -Raw)"; continue }
        if (-not (Test-Path -LiteralPath $image)) { $failures += "$name did not capture its frame"; continue }
        $bitmap = [System.Drawing.Bitmap]::new($image)
        try { $validSize = $bitmap.Width -eq 1280 -and $bitmap.Height -eq 720 } finally { $bitmap.Dispose() }
        if (-not $validSize) { $failures += "$name did not capture a 1280 x 720 frame"; continue }
        $text = Get-Content -LiteralPath $log -Raw
        if ($Configuration -eq 'Debug' -and $text -notmatch '(Debug Layer|Validation Layer) enabled') { $failures += "$name did not enable its validation layer"; continue }
        if ($text -notmatch 'Validation errors: 0' -or $text -notmatch 'Initialized \(1280 x 720\)' -or $text -notmatch 'Minimize/restore test' -or $text -notmatch 'Resized: 640 x 480' -or $text -notmatch 'Resized: 1280 x 720') { $failures += "$name did not complete all lifecycle checks"; continue }
        Write-Output "$name passed (1280 x 720 render/capture, resize, minimize/restore, shutdown)"
    }
}
if ($failures.Count) { throw ($failures -join "`n") }
