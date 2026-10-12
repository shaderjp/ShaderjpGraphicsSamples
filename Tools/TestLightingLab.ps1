param([ValidateSet('Debug','Release')][string]$Configuration='Debug',[switch]$CaptureDocs,[switch]$SkipCpu)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot -Parent
$work=Join-Path $repoRoot ".cache/lightinglab-tests/$Configuration"
New-Item -ItemType Directory -Path $work -Force|Out-Null
Add-Type -AssemblyName System.Drawing
if(-not ('LightingLabPixelTests' -as [type])){
Add-Type -TypeDefinition @'
using System;
public static class LightingLabPixelTests {
  public static int[] Compare(byte[] ax, byte[] by, int left, int[][] exclude) {
        int count=0,max=0;
        for(int row=0;row<720;row++) for(int col=left;col<1280;col++) {
          bool skip=false;
          foreach(var r in exclude) if(col>=r[0]&&col<=r[2]&&row>=r[1]&&row<=r[3]) {skip=true;break;}
          if(skip) continue;
          int p=(row*1280+col)*4,d=0;
          for(int c=0;c<3;c++) d=Math.Max(d,Math.Abs(ax[p+c]-by[p+c]));
          if(d>2)count++; max=Math.Max(max,d);
        }
        return new int[]{count,max};
  }
}
'@
}
if(-not $SkipCpu){
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if($env:VSCMD_ARG_TGT_ARCH -ne 'x64'){
        Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
        Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'|Out-Null
    }
    $runtime=if($Configuration -eq 'Debug'){'/MDd'}else{'/MD'}
    $sources=@('Tests/LightingLabTests.cpp','Common/Application.cpp','Common/Gltf.cpp','Common/Dds.cpp','Common/LightingScene.cpp')|ForEach-Object{Join-Path $repoRoot $_}
    & cl.exe /nologo /std:c++20 /EHsc /W4 /utf-8 $runtime /DUNICODE /D_UNICODE ("/I"+(Join-Path $repoRoot 'Common')) ("/I"+(Join-Path $repoRoot 'ThirdParty/nlohmann-json/single_include')) ("/I"+(Join-Path $repoRoot 'ThirdParty/DirectXTex/DirectXTex')) ("/Fo"+$work+'\') ("/Fe"+(Join-Path $work 'LightingLabTests.exe')) @sources /link (Join-Path $repoRoot "bin/x64/$Configuration/DirectXTexCpu/DirectXTexCpu.lib") user32.lib shell32.lib ole32.lib windowscodecs.lib
    if($LASTEXITCODE){throw 'LightingLab CPU test compile failed'}
    & (Join-Path $work 'LightingLabTests.exe') $repoRoot
    if($LASTEXITCODE){throw 'LightingLab CPU tests failed'}
}
function Capture([string]$Api,[string]$Topic,[string]$Mode,[string]$Environment='studio',[string]$Offset='0',[switch]$UiTest){
    $name="${Api}_${Topic}";$executable=Join-Path $repoRoot "bin/x64/$Configuration/$name/$name.exe"
    $suffix="$Mode-$Environment-$Offset";$log=Join-Path $work "$name-$suffix.log";$image=Join-Path $work "$name-$suffix.png"
    if($CaptureDocs -and -not $UiTest){$image=Join-Path $repoRoot "Docs/$Api/Images/$Topic-$suffix.png"}
    $arguments=@('--frames',$(if($UiTest){'40'}else{'8'}),'--shading',$Mode,'--environment',$Environment,'--occluder-x',$Offset,'--log',('"'+$log+'"'),'--screenshot',('"'+$image+'"'))
    if($UiTest){$arguments+='--test-ui'}else{$arguments+='--no-ui'}
    $old=$env:VK_LAYER_VALIDATE_SYNC
    try{if($Api -eq 'Vulkan' -and $Configuration -eq 'Debug'){$env:VK_LAYER_VALIDATE_SYNC='1'}
        $p=Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $work -PassThru -WindowStyle Hidden
    }finally{$env:VK_LAYER_VALIDATE_SYNC=$old}
    if(-not $p.WaitForExit(30000)){$p.Kill();throw "$name timeout"}
    $text=Get-Content -LiteralPath $log -Raw
    if($p.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0'){throw "$name failed: $text"}
    if($Configuration -eq 'Debug' -and $text -notmatch '(Debug Layer|Validation Layer) enabled'){throw 'Validation layer disabled'}
    return $image
}
function ReadPixels([string]$Path){
    $bitmap=[Drawing.Bitmap]::new($Path)
    try{
        if($bitmap.Width -ne 1280 -or $bitmap.Height -ne 720){throw 'Capture size mismatch'}
        $data=$bitmap.LockBits([Drawing.Rectangle]::new(0,0,1280,720),[Drawing.Imaging.ImageLockMode]::ReadOnly,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try{
            $pixels=[byte[]]::new(1280*720*4)
            for($row=0;$row -lt 720;$row++){[Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0,$row*$data.Stride),$pixels,$row*1280*4,1280*4)}
            return ,$pixels
        }finally{$bitmap.UnlockBits($data)}
    }finally{$bitmap.Dispose()}
}
function Difference([string]$A,[string]$B,[int]$Left=0,[int[][]]$Exclude=@()){
    return [LightingLabPixelTests]::Compare((ReadPixels $A),(ReadPixels $B),$Left,$Exclude)
}
$summary=@()
foreach($api in @('Direct3D12','Vulkan')){
    $orientation=Capture $api '10_Cubemap' 'mirror' 'orientation'
    $studio=Capture $api '10_Cubemap' 'mirror'
    $room=Capture $api '10_Cubemap' 'mirror' 'room'
    if((Difference $studio $room)[0] -lt 1000){throw 'Environment change did not reach rendered pixels'}
    $direct=Capture $api '11_DiffuseIBL' 'direct';$diffuse=Capture $api '11_DiffuseIBL' 'diffuse';$combined=Capture $api '11_DiffuseIBL' 'combined'
    if((Difference $direct $combined)[0] -lt 1000){throw 'Diffuse IBL missing'}
    $diffuse12=Capture $api '12_SpecularIBL' 'diffuse';$specular=Capture $api '12_SpecularIBL' 'specular';$full=Capture $api '12_SpecularIBL' 'combined';$room12=Capture $api '12_SpecularIBL' 'combined' 'room'
    if((Difference $diffuse12 $full)[0] -lt 1000){throw 'Specular IBL missing'}
    $noShadow=Capture $api '13_ShadowMap' 'no-shadow';$hard=Capture $api '13_ShadowMap' 'hard';$pcf=Capture $api '13_ShadowMap' 'pcf';$depth=Capture $api '13_ShadowMap' 'shadow-depth';$moved=Capture $api '13_ShadowMap' 'pcf' 'studio' '1'
    $shadowDiff=Difference $noShadow $pcf;$moveDiff=Difference $pcf $moved
    if($shadowDiff[0] -lt 100 -or $moveDiff[0] -lt 100){throw 'Shadow or movable occluder did not change rendering'}
    $bounds=(Get-Content -LiteralPath (Join-Path $repoRoot '.cache/lightinglab-cpu/occluder-bounds.json') -Raw)|ConvertFrom-Json
    $shadowMovement=Difference $pcf $moved 0 ([int[][]]$bounds)
    if($shadowMovement[0] -lt 50){throw 'Only the occluder geometry moved; projected shadow did not change'}
    foreach($topic in @('10_Cubemap','11_DiffuseIBL','12_SpecularIBL','13_ShadowMap')){
        $uiMode=if($topic -eq '10_Cubemap'){'mirror'}else{'combined'}
        $ui=Capture $api $topic $uiMode 'studio' '0' -UiTest
        $base=$ui.Replace('.png','-baseline.png');$edited=$ui.Replace('.png','-edited.png');$reset=$ui.Replace('.png','-reset.png')
        if((Difference $base $edited 350)[0] -lt 1000){throw 'Environment UI change missing from GPU scene'}
        if((Difference $base $reset 350)[1] -ne 0){throw 'Reset did not restore baseline scene pixels'}
    }
    $summary+=@{api=$api;shadowChangedPixels=$shadowDiff[0];occluderChangedPixels=$moveDiff[0];shadowMovementPixels=$shadowMovement[0];uiReset='exact baseline'}
    Write-Output "$api LightingLab environment, IBL, shadow, occluder and UI GPU checks PASS"
}
$summary|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $work 'summary.json') -Encoding utf8
