param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [switch]$CaptureDocs
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path $PSScriptRoot -Parent
$resultsDirectory = Join-Path $repositoryRoot ('.cache\lighting-tests\' + $Configuration)
New-Item -ItemType Directory -Path $resultsDirectory -Force | Out-Null
Add-Type -AssemblyName System.Drawing

function Capture([string]$Api, [string]$Topic, [string]$Mode, [string]$Model = '') {
    $name = "${Api}_${Topic}"
    $executable = Join-Path $repositoryRoot "bin\x64\$Configuration\$name\$name.exe"
    $suffix = if ($Model) { '-fixture' } else { '' }
    $image = Join-Path $resultsDirectory "$name-$Mode$suffix.png"
    if ($CaptureDocs -and -not $Model) { $image = Join-Path $repositoryRoot "Docs\$Api\Images\$Topic-$Mode.png" }
    $log = Join-Path $resultsDirectory "$name-$Mode$suffix.log"
    $arguments = @('--frames','8','--shading',$Mode,'--screenshot',('"' + $image + '"'),'--log',('"' + $log + '"'))
    if ($Model) { $arguments += @('--model',('"' + $Model + '"')) }
    $previousSyncSetting = $env:VK_LAYER_VALIDATE_SYNC
    try {
        if ($Api -eq 'Vulkan' -and $Configuration -eq 'Debug') { $env:VK_LAYER_VALIDATE_SYNC = '1' }
        $process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $resultsDirectory -PassThru -WindowStyle Hidden
    } finally { $env:VK_LAYER_VALIDATE_SYNC = $previousSyncSetting }
    if (-not $process.WaitForExit(15000)) { $process.Kill(); throw "$name $Mode timed out" }
    $text = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -ne 0 -or $text -notmatch 'Validation errors: 0') { throw "$name $Mode failed: $text" }
    if ($Configuration -eq 'Debug' -and $text -notmatch '(Debug Layer|Validation Layer) enabled') { throw 'Debug validation is disabled' }
    if (-not (Test-Path -LiteralPath $image)) { throw 'No GPU image captured' }
    return $image
}
function Normalize([double[]]$V) {
    $length = [Math]::Sqrt($V[0]*$V[0] + $V[1]*$V[1] + $V[2]*$V[2])
    return ,@(($V[0]/$length),($V[1]/$length),($V[2]/$length))
}
function Cross([double[]]$A,[double[]]$B) {
    return ,@(($A[1]*$B[2]-$A[2]*$B[1]),($A[2]*$B[0]-$A[0]*$B[2]),($A[0]*$B[1]-$A[1]*$B[0]))
}
function Decode([double]$V) {
    if ($V -le 0.04045) { return $V / 12.92 }
    return [Math]::Pow(($V + 0.055) / 1.055, 2.4)
}
function Encode([double]$V) {
    if ($V -le 0.0031308) { return 255 * 12.92 * $V }
    return 255 * (1.055 * [Math]::Pow($V, 1.0 / 2.4) - 0.055)
}
function CheckPixel([string]$Image,[double[]]$Linear,[string]$Case) {
    $bitmap = [System.Drawing.Bitmap]::new($Image)
    try { $color = $bitmap.GetPixel(400,300) } finally { $bitmap.Dispose() }
    $actual = @([int]$color.R,[int]$color.G,[int]$color.B)
    for ($c=0; $c -lt 3; ++$c) {
        $expected = Encode $Linear[$c]
        if ([Math]::Abs($actual[$c] - $expected) -gt 2) {
            throw "$Case channel $c expected $expected but got $($actual[$c]); $Image"
        }
    }
}

# A constant-texture plane isolates resource bindings, sRGB decoding, material factors,
# tangent handedness and normalScale from the curved asset's varying UVs and normals.
$fixtureDirectory = Join-Path $resultsDirectory 'fixture'
New-Item -ItemType Directory -Path $fixtureDirectory -Force | Out-Null
$normal = Normalize @(-0.82,0.32,0.56)
$tangent = Normalize (Cross @(0,1,0) $normal)
$bitangent = Cross $normal $tangent
$stream = [System.IO.MemoryStream]::new()
$writer = [System.IO.BinaryWriter]::new($stream)
try {
    foreach ($signs in @(@(-1,-1),@(1,-1),@(1,1),@(-1,1))) {
        for ($c=0; $c -lt 3; ++$c) {
            $coordinate = 0.5 * ($signs[0]*$tangent[$c] + $signs[1]*$bitangent[$c])
            if ($c -eq 2) { $coordinate = -$coordinate }
            $writer.Write([single]$coordinate)
        }
    }
    for ($i=0; $i -lt 4; ++$i) {
        $writer.Write([single]$normal[0]); $writer.Write([single]$normal[1]); $writer.Write([single](-$normal[2]))
    }
    foreach ($uv in @(@(0,0),@(1,0),@(1,1),@(0,1))) { foreach ($v in $uv) { $writer.Write([single]$v) } }
    for ($i=0; $i -lt 4; ++$i) {
        $writer.Write([single]$tangent[0]); $writer.Write([single]$tangent[1]); $writer.Write([single](-$tangent[2])); $writer.Write([single]-1)
    }
    foreach ($index in @(0,1,2,0,2,3)) { $writer.Write([uint16]$index) }
    [System.IO.File]::WriteAllBytes((Join-Path $fixtureDirectory 'plane.bin'),$stream.ToArray())
} finally { $writer.Dispose(); $stream.Dispose() }
$colors = @(@(64,128,192),@(160,96,240),@(128,64,192),@(10,20,30))
for ($i=0; $i -lt 4; ++$i) {
    $bitmap = [System.Drawing.Bitmap]::new(1,1)
    try {
        $bitmap.SetPixel(0,0,[System.Drawing.Color]::FromArgb(255,$colors[$i][0],$colors[$i][1],$colors[$i][2]))
        $bitmap.Save((Join-Path $fixtureDirectory "$i.png"),[System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }
}
$data = @{
    asset = @{ version = '2.0' }; scene = 0
    scenes = @(@{ nodes = @(0) }); nodes = @(@{ mesh = 0 })
    meshes = @(@{ primitives = @(@{ attributes = @{ POSITION=0; NORMAL=1; TEXCOORD_0=2; TANGENT=3 }; indices=4; material=0 }) })
    materials = @(@{
        pbrMetallicRoughness = @{ baseColorTexture=@{ index=0 }; baseColorFactor=@(0.5,0.75,0.25,1); metallicRoughnessTexture=@{index=2}; metallicFactor=0.5; roughnessFactor=0.8 }
        normalTexture=@{index=1; scale=0.7}; occlusionTexture=@{index=2; strength=0.6}; emissiveTexture=@{index=3}; emissiveFactor=@(0.2,0.4,0.6)
    })
    textures=@(@{source=0},@{source=1},@{source=2},@{source=3})
    images=@(@{uri='0.png'},@{uri='1.png'},@{uri='2.png'},@{uri='3.png'})
    buffers=@(@{uri='plane.bin';byteLength=204})
    bufferViews=@(@{buffer=0;byteOffset=0;byteLength=48},@{buffer=0;byteOffset=48;byteLength=48},@{buffer=0;byteOffset=96;byteLength=32},@{buffer=0;byteOffset=128;byteLength=64},@{buffer=0;byteOffset=192;byteLength=12})
    accessors=@(@{bufferView=0;componentType=5126;type='VEC3';count=4},@{bufferView=1;componentType=5126;type='VEC3';count=4},@{bufferView=2;componentType=5126;type='VEC2';count=4},@{bufferView=3;componentType=5126;type='VEC4';count=4},@{bufferView=4;componentType=5123;type='SCALAR';count=6})
}
$fixture = Join-Path $fixtureDirectory 'plane.gltf'
$data | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $fixture -Encoding utf8
$base = @(((Decode (64/255.0))*0.5),((Decode (128/255.0))*0.75),((Decode (192/255.0))*0.25))
$emission = @(((Decode (10/255.0))*0.2),((Decode (20/255.0))*0.4),((Decode (30/255.0))*0.6))
$metal = 192/255.0*0.5; $rough = 64/255.0*0.8; $ao = 1 + 0.6*(128/255.0-1)
$normalTS = Normalize @(((160/255.0*2-1)*0.7),((96/255.0*2-1)*0.7),(240/255.0*2-1))
$worldNormal = Normalize @(($tangent[0]*$normalTS[0]+$bitangent[0]*$normalTS[1]+$normal[0]*$normalTS[2]),($tangent[1]*$normalTS[0]+$bitangent[1]*$normalTS[1]+$normal[1]*$normalTS[2]),($tangent[2]*$normalTS[0]+$bitangent[2]*$normalTS[1]+$normal[2]*$normalTS[2]))
$normalColor = @(($worldNormal[0]*0.5+0.5),($worldNormal[1]*0.5+0.5),($worldNormal[2]*0.5+0.5))
foreach ($api in @('Direct3D12','Vulkan')) {
    foreach ($mode in @('lambert','blinn-phong','albedo')) { $null = Capture $api '07_Lighting' $mode }
    foreach ($mode in @('pbr','albedo','normal','metallic','roughness','ao','emissive','pbr-flat')) { $null = Capture $api '08_PBR' $mode }
    foreach ($case in @(@('albedo',$base),@('metallic',@($metal,$metal,$metal)),@('roughness',@($rough,$rough,$rough)),@('ao',@($ao,$ao,$ao)),@('emissive',$emission),@('normal',$normalColor))) {
        $image = Capture $api '08_PBR' $case[0] $fixture
        CheckPixel $image $case[1] "$api $($case[0]) binding/format/factor"
    }
    $image = Capture $api '07_Lighting' 'albedo' $fixture
    CheckPixel $image $base "$api Sample 07 Albedo"
    $light = Normalize @(-0.5,0.8,0.55)
    $cosine = $normal[0]*$light[0]+$normal[1]*$light[1]+$normal[2]*$light[2]
    $linear = @(0.0,0.0,0.0)
    for ($c=0; $c -lt 3; ++$c) {
        $lit = $base[$c]*(0.035 + @(3.0,2.85,2.7)[$c]*$cosine/[Math]::PI)
        $linear[$c] = $lit/(1+$lit)
    }
    $image = Capture $api '07_Lighting' 'lambert' $fixture
    CheckPixel $image $linear "$api Lambert linear lighting/tone mapping"
    Write-Output "$api all shading modes and constant-texture GPU reference pixels passed"
}
