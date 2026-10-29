param([string]$OutputPath='')
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
if(-not $OutputPath){$OutputPath=Join-Path $repo 'Docs/ClusterGeometryEnvironment.json'}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -products '*' -utf8 -format json|ConvertFrom-Json)[0]
$toolVersion=(Get-Content -LiteralPath (Join-Path $vs.installationPath 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$compiler=Get-Item -LiteralPath (Join-Path $vs.installationPath "VC/Tools/MSVC/$toolVersion/bin/Hostx64/x64/cl.exe")
$result=[ordered]@{
    capturedUtc=[DateTime]::UtcNow.ToString('o');timeZone='Asia/Tokyo'
    os=(Get-CimInstance Win32_OperatingSystem|Select-Object Caption,Version,BuildNumber)
    cpu=@(Get-CimInstance Win32_Processor|Select-Object -ExpandProperty Name)
    gpu=@(Get-CimInstance Win32_VideoController|Select-Object Name,DriverVersion)
    visualStudio=$vs.installationVersion;toolset='v145';msvcTools=$toolVersion;compiler=$compiler.VersionInfo.FileVersion
    windowsSdk='10.0.26100.0';language='C++20 / HLSL2021';retailAgility='1.620.1';previewAgility='1.722.0-preview'
    vulkanSdk=$env:VULKAN_SDK
    developerMode=(Get-ItemProperty -LiteralPath 'HKLM:/SOFTWARE/Microsoft/Windows/CurrentVersion/AppModelUnlock').AllowDevelopmentWithoutDevLicense
    shaderModels=@{existing='6.6';clusterLibrary='6.10';previewTriangleAndCompute='6.6'}
    meshoptimizer=@{tag='v1.3';commit='9e1f07b159d3cb777f1c67ed31fc11fd117986f4'}
    microsoftReference='68a2b1e97f71642924e74c7757ca21756ad221b5'
    rtxmgReference=@{tag='v2.2.0';commit='1c0bc90b06c482213bf313670b238fbc683ed67b'}
    compilers=@{};binaries=@{};runtimePackages=@{}
}
foreach($version in @('1.620.1','1.722.0-preview')) {
    $runtime=@{}
    foreach($dll in @('D3D12Core.dll','D3D12SDKLayers.dll')) {
        $path=Join-Path $repo "packages/Microsoft.Direct3D.D3D12.$version/build/native/bin/x64/$dll"
        $file=Get-Item -LiteralPath $path
        $runtime[$dll]=@{version=$file.VersionInfo.FileVersion;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $result.runtimePackages[$version]=$runtime
}
foreach($version in @('1.9.2609.5','1.10.2605.37-preview')){
    $dxc=Join-Path $repo "packages/Microsoft.Direct3D.DXC.$version/build/native/bin/x64/dxc.exe"
    $result.compilers[$version]=@{version=(& $dxc --version 2>&1)-join "`n";sha256=(Get-FileHash -LiteralPath $dxc -Algorithm SHA256).Hash.ToLowerInvariant()}
}
foreach($configuration in @('Debug','Release')){
    Get-ChildItem -Path (Join-Path $repo "bin/x64/$configuration/Direct3D12_Advanced2[0-3]_*") -Directory|ForEach-Object{
        $folder=$_;$entry=@{exe=(Get-FileHash -LiteralPath (Join-Path $folder.FullName ($folder.Name+'.exe')) -Algorithm SHA256).Hash.ToLowerInvariant();shaders=@{}}
        foreach($dll in @('D3D12Core.dll','D3D12SDKLayers.dll')){
            $path=Join-Path $folder.FullName "D3D12/$dll"
            if(Test-Path -LiteralPath $path){$file=Get-Item -LiteralPath $path;$entry[$dll]=@{version=$file.VersionInfo.FileVersion;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}}
        }
        Get-ChildItem -LiteralPath (Join-Path $folder.FullName 'Shaders') -Filter *.cso|ForEach-Object{$entry.shaders[$_.Name]=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        $result.binaries["$configuration/$($folder.Name)"]=$entry
    }
}
$result|ConvertTo-Json -Depth 15|Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Output "Recorded $OutputPath"
