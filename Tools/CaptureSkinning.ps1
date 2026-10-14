param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$work=Join-Path $root '.cache/skinning/doc-captures'
New-Item -ItemType Directory -Path $work -Force | Out-Null
foreach($api in @('Direct3D12','Vulkan')){
    $shots=@(
        @('22_VertexSkinning','weights','UAL_Walk',@('--post-view','weights')),
        @('22_VertexSkinning','apose','Bind_Apose',@()),
        @('23_ComputeSkinningCache','normal','UAL_Walk',@('--post-view','normal')),
        @('Advanced10_MeshShaderSkinning','meshlets','UAL_Walk',@('--meshlet-view','meshlets'))
    )
    foreach($shot in $shots){
        $topic=$shot[0];$label=$shot[1];$clip=$shot[2]
        $name="$api`_$topic"
        $image=Join-Path $root "Docs/$api/Images/$topic-$label.png"
        $log=Join-Path $work "$name-$label.log"
        $arguments=@('--frames','4','--pause-animation','--animation',$clip,'--animation-time','0.3','--no-ui','--screenshot',('"'+$image+'"'),'--log',('"'+$log+'"'))+$shot[3]
        $process=Start-Process -FilePath (Join-Path $root "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
        if(-not $process.WaitForExit(30000)){$process.Kill();throw "Capture timeout: $name-$label"}
        if($process.ExitCode -ne 0 -or (Get-Content -LiteralPath $log -Raw) -notmatch 'Validation errors: 0'){throw "Capture error: $name-$label"}
        Write-Host "$name-$label captured (1280 x 720)."
    }
}
