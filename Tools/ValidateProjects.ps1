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
