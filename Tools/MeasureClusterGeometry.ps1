param(
    [ValidateRange(1,10)][int]$Repeats=5,
    [ValidateRange(0,10000)][int]$Warmup=120,
    [ValidateRange(1,10000)][int]$Frames=600,
    [string]$PythonPath='C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe'
)
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repo 'Docs/Measurements/ClusterGeometry';New-Item -ItemType Directory -Force -Path $work|Out-Null
$cases=@(@(20,'ClusteredGeometry','clusters','spatial'),@(20,'ClusteredGeometry','rebuild','spatial'),
    @(21,'ClusterTemplates','templates','spatial'),@(21,'ClusterTemplates','direct','spatial'),@(21,'ClusterTemplates','rebuild','spatial'),@(21,'ClusterTemplates','refit','spatial'),
    @(22,'ClusterTessellation','templates','spatial'),@(22,'ClusterTessellation','direct','spatial'),@(22,'ClusterTessellation','rebuild','spatial'),
    @(23,'PartitionedTLAS','ptlas','spatial'),@(23,'PartitionedTLAS','ptlas','global'),@(23,'PartitionedTLAS','rebuild','spatial'),@(23,'PartitionedTLAS','refit','spatial'))
foreach($case in $cases){
    $name="Direct3D12_Advanced$($case[0])_$($case[1])"
    for($run=1;$run -le $Repeats;$run++){
        $label="$($case[0])-$($case[2])-$($case[3])-$run";$json=Join-Path $work ($label+'.json');$log=Join-Path $work ($label+'.log')
        $arguments=@('--benchmark','--frames',"$($Warmup+$Frames)",'--fixed-dt','0.016666667','--animation-time','0.7','--cg-mode',$case[2],'--cg-partition',$case[3],'--timings',$json,'--log',$log)
        do {
            while(@(Get-Process -Name Direct3D12_*,Vulkan_* -ErrorAction SilentlyContinue).Count){Start-Sleep -Seconds 2}
            $p=Start-Process -FilePath (Join-Path $repo "bin/x64/Release/$name/$name.exe") -ArgumentList $arguments -WorkingDirectory $work -WindowStyle Hidden -PassThru
            $clean=$true;$started=Get-Date
            while(-not $p.WaitForExit(500)) {
                if(@(Get-Process -Name Direct3D12_*,Vulkan_* -ErrorAction SilentlyContinue|Where-Object{$_.Id -ne $p.Id}).Count){$clean=$false}
                if(((Get-Date)-$started).TotalSeconds -gt 600){$p.Kill();throw "Measurement timeout: $label"}
            }
            if(-not $clean){Write-Output "$label overlapped another sample; retrying"}
        } while(-not $clean)
        $text=Get-Content -LiteralPath $log -Raw
        if($p.ExitCode -or $text -notmatch 'Validation errors: 0' -or $text -match 'Debug Layer enabled|D3D12 warning:'){throw "Measurement failed: $label"}
        Write-Output "$label measured"
    }
}
& $PythonPath (Join-Path $repo 'Tools/SummarizeClusterGeometry.py') $work --warmup $Warmup --frames $Frames --repeats $Repeats
if($LASTEXITCODE){throw 'Measurement summary failed'}
