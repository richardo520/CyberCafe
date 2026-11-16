param([string]$EngineRoot = 'F:/UnrealEngine')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project = Join-Path $projectRoot 'CyberCafe.uproject'
$reportRoot = Join-Path $projectRoot 'Saved/AssetOrganization'
$previousMode = $env:CYBERCAFE_ASSET_MIGRATION_MODE
$previousMapMode = $env:CYBERCAFE_MAP_MIGRATION_MODE
try {
    $env:CYBERCAFE_ASSET_MIGRATION_MODE = 'verify'
    & $editor $project -run=pythonscript "-script=$PSScriptRoot/Editor/organize_project_assets.py" -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosplash -nullrhi -nohmd "-abslog=$reportRoot/verify-final.log"
    if ($LASTEXITCODE -ne 0) { throw 'Asset target, alias or scene verification failed.' }
    & $editor $project -run=pythonscript "-script=$PSScriptRoot/Editor/audit_migrated_dependencies.py" -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosplash -nullrhi -nohmd "-abslog=$reportRoot/dependency-audit.log"
    if ($LASTEXITCODE -ne 0) { throw 'Original asset dependency comparison failed.' }
    $env:CYBERCAFE_MAP_MIGRATION_MODE = 'verify'
    & $editor $project -run=pythonscript "-script=$PSScriptRoot/Editor/organize_main_map.py" -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosplash -nullrhi -nohmd "-abslog=$projectRoot/Saved/MapOrganization/verify.log"
    if ($LASTEXITCODE -ne 0) { throw 'Main-map migration verification failed.' }
    & $editor $project -run=pythonscript "-script=$PSScriptRoot/Editor/audit_runtime_template_dependencies.py" -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosplash -nullrhi -nohmd "-abslog=$projectRoot/Saved/MapOrganization/runtime-audit.log"
    if ($LASTEXITCODE -ne 0) { throw 'Runtime template dependency audit failed.' }
    $templateAudit = Get-Content "$projectRoot/Saved/MapOrganization/template-dependencies.json" -Raw | ConvertFrom-Json
    if ($templateAudit.real_template_assets.Count -gt 0 -or $templateAudit.template_redirectors.Count -gt 0) { throw 'Main-map closure still references VRTemplate.' }
    Write-Output "Asset organization verified. Reports: $reportRoot"
}
finally {
    $env:CYBERCAFE_ASSET_MIGRATION_MODE = $previousMode
    $env:CYBERCAFE_MAP_MIGRATION_MODE = $previousMapMode
}
