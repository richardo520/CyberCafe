param([string]$ProjectRoot = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$reportRoot = Join-Path $ProjectRoot 'Saved/ArchitectureAudit'
New-Item -ItemType Directory -Force -Path $reportRoot | Out-Null
$contentRoot = Join-Path $ProjectRoot 'Content'
$assets = @(Get-ChildItem -LiteralPath $contentRoot -Recurse -File | Where-Object { $_.Extension -in '.uasset', '.umap' } | ForEach-Object {
    $relative = $_.FullName.Substring($contentRoot.Length + 1).Replace('\', '/')
    [pscustomobject]@{ Package = '/Game/' + $relative.Substring(0, $relative.LastIndexOf('.')); Folder = $relative.Substring(0, $relative.LastIndexOf('/')); Name = $_.BaseName; Extension = $_.Extension; Bytes = $_.Length }
})
$assets | Sort-Object Package | Export-Csv -LiteralPath (Join-Path $reportRoot 'assets.csv') -NoTypeInformation -Encoding utf8
$assets | Group-Object Folder | ForEach-Object {
    [pscustomobject]@{ Folder = $_.Name; Count = $_.Count; Bytes = ($_.Group | Measure-Object Bytes -Sum).Sum }
} | Sort-Object Folder | Export-Csv -LiteralPath (Join-Path $reportRoot 'folders.csv') -NoTypeInformation -Encoding utf8
$assets | Group-Object Name | Where-Object Count -gt 1 | ForEach-Object {
    [pscustomobject]@{ Name = $_.Name; Count = $_.Count; Packages = ($_.Group.Package -join ';') }
} | Sort-Object Name | Export-Csv -LiteralPath (Join-Path $reportRoot 'duplicate-names.csv') -NoTypeInformation -Encoding utf8
$sourceRoot = Join-Path $ProjectRoot 'Source/CyberCafe'
$references = @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -File | Where-Object { $_.Extension -in '.cpp', '.h' } | Select-String -Pattern '/Game/[A-Za-z0-9_/.]+' -AllMatches | ForEach-Object {
    $hit = $_
    foreach ($match in $hit.Matches) {
        [pscustomobject]@{ File = $hit.Path.Substring($ProjectRoot.Length + 1); Line = $hit.LineNumber; ObjectPath = $match.Value }
    }
})
$references | Export-Csv -LiteralPath (Join-Path $reportRoot 'source-asset-references.csv') -NoTypeInformation -Encoding utf8
Write-Output "Assets: $($assets.Count); source references (including comments): $($references.Count); reports: $reportRoot"
Write-Output 'Filesystem inventory only. Duplicate names are not proof of duplicate content; use the UE Asset Registry for dependencies.'
