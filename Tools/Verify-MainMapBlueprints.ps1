param([string]$EngineRoot = 'F:/UnrealEngine')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project = Join-Path $projectRoot 'CyberCafe.uproject'
$outputRoot = Join-Path $projectRoot 'Saved/ArchitectureAudit'
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
& $editor $project -run=pythonscript "-script=$PSScriptRoot/Editor/verify_main_map_blueprints.py" -EnablePlugins=PythonScriptPlugin -unattended -nop4 -nosplash -nullrhi -nohmd "-abslog=$outputRoot/blueprint-verification.log"
if ($LASTEXITCODE -ne 0) { throw 'Could not prepare main-map Blueprint allow-list.' }
# UE5.6's commandlet prepends ProjectDir to this argument unconditionally.
& $editor $project -run=CompileAllBlueprints '-AllowListFile=Saved/ArchitectureAudit/main-map-blueprints.txt' -unattended -nop4 -nosplash -nullrhi -nohmd "-abslog=$outputRoot/blueprint-compile.log"
if ($LASTEXITCODE -ne 0) { throw 'Main-map Blueprint compilation failed; see Saved/ArchitectureAudit/blueprint-compile.log.' }
Write-Output "Main-map Blueprint compilation succeeded; log=$outputRoot/blueprint-compile.log"
