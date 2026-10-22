param([string]$BlenderPath='C:\Program Files\Blender Foundation\Blender 5.2\blender.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$work=Join-Path $root '.cache/motion-matching/assets'
New-Item -ItemType Directory -Path $work -Force | Out-Null
$validator=Join-Path $root '.cache/gltf-validator/gltf_validator.exe'
if(-not(Test-Path -LiteralPath $validator)){& (Join-Path $root 'Tools/TestSilverRobotAssets.ps1') -SkipReexport}
$sources=@('SourceArt/Blender/MotionMatching/SilverRobotMotionSet.blend','SourceArt/Blender/TraversalCourse/TraversalCourse.blend')
$before=@{};foreach($source in $sources){$before[$source]=(Get-FileHash -LiteralPath (Join-Path $root $source)).Hash}
$motion=Join-Path $root 'Assets/Models/SilverRobotMotionSet'
$course=Join-Path $root 'Assets/Models/TraversalCourse'
$issues=@{}
foreach($file in @((Join-Path $motion 'SilverRobot.gltf'),(Join-Path $motion 'SilverRobot.glb'),(Join-Path $course 'Course.gltf'))){
    $text=& $validator -o $file
    if($LASTEXITCODE -ne 0){throw "Khronos validation failed: $file"}
    $report=($text -join "`n")|ConvertFrom-Json
    if($report.issues.numErrors -ne 0 -or $report.issues.numWarnings -ne 0){throw "Khronos issues: $file"}
    $issues[[IO.Path]::GetFileName($file)]=$report.issues
}
& $BlenderPath --background (Join-Path $root $sources[0]) --python-exit-code 1 --python (Join-Path $root 'SourceArt/Blender/MotionMatching/export.py') -- (Join-Path $work 'MotionSet') --verify *> (Join-Path $work 'motion-reexport.log')
if($LASTEXITCODE -ne 0){throw 'Motion re-export/deformation comparison failed'}
& $BlenderPath --background (Join-Path $root $sources[1]) --python-exit-code 1 --python (Join-Path $root 'SourceArt/Blender/TraversalCourse/export.py') -- (Join-Path $work 'Course') *> (Join-Path $work 'course-reexport.log')
if($LASTEXITCODE -ne 0){throw 'Course re-export failed'}
foreach($pair in @(@($motion,(Join-Path $work 'MotionSet')),@($course,(Join-Path $work 'Course')))){
    foreach($file in Get-ChildItem -LiteralPath $pair[0] -File){
        if($file.Extension -eq '.mmdb'){continue}
        $other=Join-Path $pair[1] $file.Name
        if((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $other).Hash){throw "Saved-source re-export differs: $($file.Name)"}
    }
}
foreach($source in $sources){if((Get-FileHash -LiteralPath (Join-Path $root $source)).Hash -ne $before[$source]){throw 'Verification modified a saved source'}}
[ordered]@{result='PASS';khronos=$issues;savedSourceReexport='byte-identical';sourceHashes=$before;deformation=(Get-Content (Join-Path $root '.cache/motion-matching/deformation.json') -Raw|ConvertFrom-Json)}|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $root 'Docs/MotionAssetsVerification.json') -Encoding utf8
Write-Host 'Motion/course assets PASS: Khronos, deformation, byte-identical saved-source re-export.'
