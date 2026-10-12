param([string]$RepositoryRoot = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
$RepositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$allFilterGuids = @{}
$projects = @(Get-ChildItem -LiteralPath (Join-Path $RepositoryRoot 'Samples') -Recurse -Filter *.vcxproj)
if ($projects.Count -eq 0) { throw 'No sample projects found' }
foreach ($project in $projects) {
    [xml]$projectXml = Get-Content -LiteralPath $project.FullName -Raw
    $filtersPath = $project.FullName + '.filters'
    if (-not (Test-Path -LiteralPath $filtersPath)) { throw "Missing filters: $filtersPath" }
    [xml]$filterXml = Get-Content -LiteralPath $filtersPath -Raw
    $projectNs = [System.Xml.XmlNamespaceManager]::new($projectXml.NameTable)
    $projectNs.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
    $filterNs = [System.Xml.XmlNamespaceManager]::new($filterXml.NameTable)
    $filterNs.AddNamespace('m', 'http://schemas.microsoft.com/developer/msbuild/2003')
    $declared = @{}
    foreach ($filter in $filterXml.SelectNodes('/m:Project/m:ItemGroup/m:Filter', $filterNs)) {
        $name = $filter.GetAttribute('Include')
        if ($declared.ContainsKey($name)) { throw "Duplicate filter $name in $filtersPath" }
        $guidText = $filter.SelectSingleNode('m:UniqueIdentifier', $filterNs).InnerText
        $guid = [guid]$guidText
        if ($allFilterGuids.ContainsKey($guid.ToString())) { throw "Duplicate filter GUID $guid" }
        $allFilterGuids[$guid.ToString()] = $true
        $declared[$name] = $true
    }
    foreach ($name in $declared.Keys) {
        if ($name.Contains('\')) {
            $parent = $name.Substring(0, $name.LastIndexOf('\'))
            if (-not $declared.ContainsKey($parent)) { throw "Undeclared parent filter $parent" }
        }
    }
    $expected = @{}
    foreach ($item in $projectXml.SelectNodes('/m:Project/m:ItemGroup/*[@Include]', $projectNs)) {
        if ($item.LocalName -eq 'ProjectConfiguration') { continue }
        $include = $item.GetAttribute('Include')
        $fullPath = [IO.Path]::GetFullPath((Join-Path $project.DirectoryName $include))
        if (-not (Test-Path -LiteralPath $fullPath)) { throw "Missing project file $fullPath" }
        $key = $item.LocalName + '|' + $fullPath
        if ($expected.ContainsKey($key)) { throw "Duplicate project item $include" }
        $expected[$key] = $false
    }
    foreach ($item in $filterXml.SelectNodes('/m:Project/m:ItemGroup/*[@Include]', $filterNs)) {
        if ($item.LocalName -eq 'Filter') { continue }
        $include = $item.GetAttribute('Include')
        $key = $item.LocalName + '|' + [IO.Path]::GetFullPath((Join-Path $project.DirectoryName $include))
        if (-not $expected.ContainsKey($key)) { throw "Extra filter mapping: $include" }
        if ($expected[$key]) { throw "Duplicate filter mapping: $include" }
        $assignments = @($item.SelectNodes('m:Filter', $filterNs))
        if ($assignments.Count -ne 1 -or -not $declared.ContainsKey($assignments[0].InnerText)) { throw "Invalid filter assignment: $include" }
        $expected[$key] = $true
    }
    foreach ($key in $expected.Keys) { if (-not $expected[$key]) { throw "Missing filter mapping: $key" } }
    Write-Output ("Validated {0}: {1} items" -f $project.BaseName, $expected.Count)
}

# Solution folders are part of the visible IDE organization, separately from file filters.
$solutionPath=Join-Path $RepositoryRoot 'ShaderjpGraphicsSamples.sln'
$solution=Get-Content -LiteralPath $solutionPath -Raw
$solutionProjects=@{}
$solutionFolders=@{}
foreach($match in [regex]::Matches($solution,'(?m)^Project\("(?<type>\{[^}]+\})"\) = "(?<name>[^"]+)", "(?<path>[^"]+)", "(?<id>\{[^}]+\})"')) {
    $id=$match.Groups['id'].Value
    if($solutionProjects.ContainsKey($id)){throw "Duplicate solution project GUID: $id"}
    $solutionProjects[$id]=$match.Groups['path'].Value
    if($match.Groups['type'].Value -eq '{2150E333-8FDC-42A3-9474-1A3956D46DE8}') {
        $solutionFolders[$match.Groups['name'].Value]=$id
    }
}
$nested=[regex]::Match($solution,'(?s)GlobalSection\(NestedProjects\) = preSolution\s*(.*?)EndGlobalSection')
if(-not $nested.Success){throw 'Missing solution folder assignments'}
$assignments=@{}
foreach($match in [regex]::Matches($nested.Groups[1].Value,'(?m)^\s*(\{[^}]+\}) = (\{[^}]+\})\s*$')) {
    $child=$match.Groups[1].Value;$parent=$match.Groups[2].Value
    if($assignments.ContainsKey($child)){throw "Duplicate solution folder assignment: $child"}
    if(-not $solutionProjects.ContainsKey($child) -or $parent -notin $solutionFolders.Values){throw "Invalid solution folder assignment: $child -> $parent"}
    $assignments[$child]=$parent
}
$apiProjects=0
foreach($id in $solutionProjects.Keys) {
    $path=$solutionProjects[$id]
    if($path -match '^Samples\\(Direct3D12|Vulkan)\\.*\.vcxproj$') {
        $api=$Matches[1]
        if($assignments[$id] -ne $solutionFolders[$api]){throw "$path must belong to the $api solution folder"}
        [xml]$project=Get-Content -LiteralPath (Join-Path $RepositoryRoot $path) -Raw
        $actualGuid=$project.Project.PropertyGroup.ProjectGuid | Where-Object {$_}
        if($actualGuid -ne $id){throw "Solution and project GUID mismatch: $path"}
        $apiProjects++
    }
}
Write-Output "Validated solution folders: $apiProjects API sample projects"
