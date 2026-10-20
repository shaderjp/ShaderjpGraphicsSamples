param([ValidateSet('Direct3D12','Vulkan')][string[]]$Api=@('Direct3D12','Vulkan'), [switch]$EncodeOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$work=Join-Path $root '.cache/character/video'
$destination=Join-Path $root 'Docs/Assets/Videos'
New-Item -ItemType Directory -Path $work,$destination -Force | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module (Join-Path $vsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$encoder=Join-Path $work 'EncodeCharacterVideo.exe'
$sources=@('Tools/EncodeCharacterVideo.cpp','Common/Image.cpp','Common/Application.cpp') | ForEach-Object { Join-Path $root $_ }
& cl.exe /nologo /std:c++20 /EHsc /W4 /O2 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX ("/I"+(Join-Path $root 'Common')) ("/Fo"+$work+'\') ("/Fe"+$encoder) @sources /link mfplat.lib mfreadwrite.lib mfuuid.lib windowscodecs.lib user32.lib shell32.lib ole32.lib
if($LASTEXITCODE -ne 0) { throw 'Windows SDK video encoder build failed' }
foreach($graphicsApi in $Api) {
    $name="${graphicsApi}_28_CharacterController"
    $frames=Join-Path $work "$graphicsApi-frames"
    New-Item -ItemType Directory -Path $frames -Force | Out-Null
    $previous=$env:SHADERJP_CHARACTER_FRAMES
    try {
        if(-not $EncodeOnly) {
            $env:SHADERJP_CHARACTER_FRAMES=$frames
            $exe=Join-Path $root "bin/x64/Release/$name/$name.exe"
            $log=Join-Path $work "$name.log"
            $p=Start-Process -FilePath $exe -ArgumentList @('--test-character','--frames','301','--no-ui','--log',('"'+$log+'"')) -WorkingDirectory $work -PassThru -WindowStyle Hidden
            if(-not $p.WaitForExit(120000)) { $p.Kill(); throw "$name frame capture timed out" }
            if($p.ExitCode -ne 0 -or (Get-Content -LiteralPath $log -Raw) -notmatch 'Validation errors: 0') { throw "$name frame capture failed" }
        }
    } finally { $env:SHADERJP_CHARACTER_FRAMES=$previous }
    $video=Join-Path $destination "$name.mp4"
    $encodeLog=Join-Path $work "$name-encode.log"
    & $encoder $frames $video *> $encodeLog
    if($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $video)) { throw "Video encode failed: $encodeLog" }
    Write-Host "$name captured: 300 frames, 1280x720, 60 fps, 5 seconds"
}
