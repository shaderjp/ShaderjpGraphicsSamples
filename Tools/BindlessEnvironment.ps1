# Records the binaries used to build the offline shaders, independently of run timings.
function Get-BindlessEnvironment([string]$RepositoryRoot,[string]$RuntimeRoot='') {
    if(-not $RuntimeRoot){$RuntimeRoot=Join-Path $RepositoryRoot 'bin/x64/Release'}
    $dxcD3d=Join-Path $RepositoryRoot 'packages/microsoft.direct3d.dxc.1.9.2609.5/build/native/bin/x64/dxc.exe'
    $dxcVulkan=Join-Path $env:VULKAN_SDK 'Bin/dxc.exe'
    $tools=@{}
    foreach($entry in @(@('d3dDxc',$dxcD3d),@('vulkanDxc',$dxcVulkan))){
        $file=Get-Item -LiteralPath $entry[1]
        $version=(& $file.FullName --version 2>&1)-join "`n"
        if($LASTEXITCODE){throw "Cannot identify compiler: $($file.FullName)"}
        $tools[$entry[0]]=@{fileVersion=$file.VersionInfo.FileVersion;output=$version;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $binaries=@{}
    foreach($api in @('Direct3D12','Vulkan')){
        Get-ChildItem -Path (Join-Path $RuntimeRoot "$api`_Advanced1[2-5]_*") -Directory|ForEach-Object{
            $sample=$_
            $binaries[$sample.Name]=@{executableSha256=(Get-FileHash -LiteralPath (Join-Path $sample.FullName ($sample.Name+'.exe'))).Hash.ToLowerInvariant();shaders=@{}}
            Get-ChildItem -LiteralPath (Join-Path $sample.FullName 'Shaders') -File|ForEach-Object{$binaries[$sample.Name].shaders[$_.Name]=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
        }
    }
    $os=Get-CimInstance Win32_OperatingSystem
    return @{
        capturedUtc=[DateTime]::UtcNow.ToString('o');os=$os.Caption;osVersion=$os.Version
        cpu=@(Get-CimInstance Win32_Processor|Select-Object -ExpandProperty Name)
        video=@(Get-CimInstance Win32_VideoController|Select-Object Name,DriverVersion)
        vulkanSdk=(Split-Path $env:VULKAN_SDK -Leaf);windowsSdk='10.0.26100.0';agilitySdk='1.619.6'
        hlsl='2021';shaderModel='6.6';vulkanTarget='1.3';compilers=$tools;binaries=$binaries;runtimeRoot=$RuntimeRoot
    }
}
