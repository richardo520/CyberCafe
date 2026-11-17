param([string]$EngineRoot = 'F:/UnrealEngine')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
if (!(Test-Path -LiteralPath $editor)) { throw "Missing editor: $editor" }
# Interactive editor window so the player can choose VR Preview after the map loads.
Start-Process -FilePath $editor -ArgumentList @(
    ('"' + (Join-Path $projectRoot 'CyberCafe.uproject') + '"'),
    '/Game/CyberCafe/Maps/L_RegularCustomer'
)
