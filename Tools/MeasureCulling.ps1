param(
    [ValidateSet('Direct3D12','Vulkan')][string[]]$Apis=@('Direct3D12','Vulkan'),
    [ValidateRange(1,5)][int]$Repetitions=5,
    [string]$OutputDirectory=(Join-Path (Split-Path $PSScriptRoot -Parent) '.cache/culling-performance')
)
$ErrorActionPreference='Stop'
$repositoryRoot=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$modes=@(
    @('31_FrustumCulling','off'),@('31_FrustumCulling','frustum'),
    @('32_OcclusionQueries','depth-only'),@('32_OcclusionQueries','query'),
    @('33_SoftwareOcclusion','software'),@('34_HiZOcclusion','depth-only'),@('34_HiZOcclusion','hiz')
)
foreach($api in $Apis){foreach($camera in @('Hero','Plaza')){foreach($mode in $modes){for($repeat=1;$repeat -le $Repetitions;$repeat++){
    $name=$api+'_'+$mode[0]
    $prefix=Join-Path $OutputDirectory ($name+'-'+$mode[1]+'-'+$camera+'-'+$repeat)
    $measurementArguments=@('--benchmark','--culling',$mode[1],'--camera',$camera,'--culling-scene','Standard','--timings',('"'+$prefix+'.json"'),'--log',('"'+$prefix+'.log"'))
    $process=Start-Process -FilePath (Join-Path $repositoryRoot "bin/x64/Release/$name/$name.exe") -ArgumentList $measurementArguments -PassThru -WindowStyle Hidden
    if(-not $process.WaitForExit(120000)){$process.Kill();throw "$name measurement timeout"}
    if($process.ExitCode -ne 0){throw (Get-Content -LiteralPath ($prefix+'.log') -Raw)}
    $data=Get-Content -LiteralPath ($prefix+'.json') -Raw | ConvertFrom-Json
    if($data.cullingSamples.Count -ne 1200){throw "$name did not measure 1200 frames"}
    Write-Output "$name / $($mode[1]) / $camera / $repeat : $($data.cullingMs.gpuTotal.median) ms GPU"
}}}}
