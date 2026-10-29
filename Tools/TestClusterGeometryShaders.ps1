param([ValidateSet('Debug','Release')][string]$Configuration='Debug')
$ErrorActionPreference='Stop';$repo=Split-Path $PSScriptRoot -Parent
$dxc=Join-Path $repo 'packages/Microsoft.Direct3D.DXC.1.10.2605.37-preview/build/native/bin/x64/dxc.exe'
foreach($topic in @('Advanced20_ClusteredGeometry','Advanced21_ClusterTemplates','Advanced22_ClusterTessellation','Advanced23_PartitionedTLAS')) {
    $name='Direct3D12_'+$topic;$folder=Join-Path $repo "bin/x64/$Configuration/$name/Shaders"
    $triangle=(& $dxc -dumpbin (Join-Path $folder 'TriangleLibrary.cso') 2>&1)-join "`n"
    if($LASTEXITCODE -or $triangle -notmatch '!"lib", i32 6, i32 6' -or $triangle -match 'call .*dx.op.clusterID'){throw "$topic ordinary library is not a clean SM6.6 path"}
    if($topic -notmatch '23_') {
        $cluster=(& $dxc -dumpbin (Join-Path $folder 'ClusterLibrary.cso') 2>&1)-join "`n"
        if($LASTEXITCODE -or $cluster -notmatch '!"lib", i32 6, i32 10' -or $cluster -notmatch 'dx.op.clusterID'){throw "$topic clustered library profile/intrinsic missing"}
    }
    foreach($shader in $(if($topic -match '23_'){@('UpdateInstances')}else{@('GenerateGeometry')})) {
        $code=(& $dxc -dumpbin (Join-Path $folder ($shader+'.cso')) 2>&1)-join "`n"
        if($LASTEXITCODE -or $code -notmatch '!"cs", i32 6, i32 6'){throw "$topic compute profile changed"}
    }
    Write-Output "$Configuration $topic offline shader profiles PASS"
}
