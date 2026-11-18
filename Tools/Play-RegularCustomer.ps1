param([string]$EngineRoot = 'F:/UnrealEngine', [switch]$PromptForApiKey, [switch]$AuthoredOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
if (!(Test-Path -LiteralPath $editor)) { throw "Missing editor: $editor" }
. "$PSScriptRoot/Dialogue-Key.ps1"
$config = Get-CyberCafeDialogueConfig
$previousKey = [Environment]::GetEnvironmentVariable($config.KeyVariable, 'Process')
$previousDisabled = $env:CYBERCAFE_DISABLE_CLOUD_DIALOGUE
try {
    $key = if ($AuthoredOnly) { '' } else { Get-CyberCafeDialogueKey -Provider $config.Provider -Prompt:$PromptForApiKey -PromptIfMissing }
    [Environment]::SetEnvironmentVariable($config.KeyVariable, $key, 'Process')
    if ($AuthoredOnly) { $env:CYBERCAFE_DISABLE_CLOUD_DIALOGUE = '1' }
    else { $env:CYBERCAFE_DISABLE_CLOUD_DIALOGUE = $null }
    if (!$AuthoredOnly -and !$key) { Write-Output 'No API key found. The encounter will use authored dialogue.' }
    # The interactive editor inherits the key through its environment, never command-line arguments.
    Start-Process -FilePath $editor -ArgumentList @(
        ('"' + (Join-Path $projectRoot 'CyberCafe.uproject') + '"'),
        '/Game/CyberCafe/Maps/L_RegularCustomer'
    )
}
finally {
    [Environment]::SetEnvironmentVariable($config.KeyVariable, $previousKey, 'Process')
    $env:CYBERCAFE_DISABLE_CLOUD_DIALOGUE = $previousDisabled
    $key = $null
}
