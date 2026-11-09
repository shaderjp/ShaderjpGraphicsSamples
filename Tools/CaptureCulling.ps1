param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repositoryRoot=Split-Path $PSScriptRoot -Parent
$topics=@('31_FrustumCulling','32_OcclusionQueries','33_SoftwareOcclusion','34_HiZOcclusion')
& (Join-Path $PSScriptRoot 'TestSamples.ps1') -Configuration $Configuration -Topics $topics -CaptureDocs
$directory=Join-Path $repositoryRoot '.cache/culling-captures'
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$variants=@(
    @{Topic=$topics[0];Suffix='bounds';Arguments=@('--culling-view','bounds','--culling-freeze','--test-camera')},
    @{Topic=$topics[0];Suffix='plaza';Arguments=@('--camera','Plaza','--no-ui')},
    @{Topic=$topics[1];Suffix='bounds';Arguments=@('--culling-view','bounds')},
    @{Topic=$topics[2];Suffix='depth';Arguments=@('--culling-view','depth')},
    @{Topic=$topics[3];Suffix='hiz';Arguments=@('--culling-view','hiz','--culling-mip','5')},
    @{Topic=$topics[3];Suffix='bounds';Arguments=@('--culling-view','bounds')}
)
foreach($api in @('Direct3D12','Vulkan')){foreach($variant in $variants){
    $name=$api+'_'+$variant.Topic
    $image=Join-Path $repositoryRoot ('Docs/'+$api+'/Images/'+$variant.Topic+'-'+$variant.Suffix+'.png')
    $temporaryImage=Join-Path $directory ($name+'-'+$variant.Suffix+'.png')
    $log=Join-Path $directory ($name+'-'+$variant.Suffix+'.log')
    # The camera test captures the displaced observer at frame 22 before its Reset.
    $frames=if($variant.Suffix -eq 'bounds' -and $variant.Topic -eq $topics[0]){'22'}else{'4'}
    $arguments=@('--frames',$frames,'--screenshot',('"'+$temporaryImage+'"'),'--log',('"'+$log+'"'))+$variant.Arguments
    $oldSync=$env:VK_LAYER_VALIDATE_SYNC
    try{
        if($Configuration -eq 'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $process=Start-Process -FilePath (Join-Path $repositoryRoot "bin/x64/$Configuration/$name/$name.exe") -ArgumentList $arguments -PassThru -WindowStyle Hidden -WorkingDirectory $directory
        if(-not $process.WaitForExit(90000)){$process.Kill();throw "$name capture timeout"}
        $text=Get-Content -LiteralPath $log -Raw
        if($process.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0'){throw $text}
    }finally{$env:VK_LAYER_VALIDATE_SYNC=$oldSync}
    Copy-Item -LiteralPath $temporaryImage -Destination $image -Force
    Copy-Item -LiteralPath ([IO.Path]::ChangeExtension($temporaryImage,'.culling.json')) -Destination ([IO.Path]::ChangeExtension($image,'.culling.json')) -Force
    Write-Output "$name / $($variant.Suffix) captured"
}}
