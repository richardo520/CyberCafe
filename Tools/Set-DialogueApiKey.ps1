param([ValidateSet('MiniMax', 'OpenAI')][string]$Provider, [switch]$Remove)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Dialogue-Key.ps1"
if (!$Provider) { $Provider = (Get-CyberCafeDialogueConfig).Provider }
$path = Get-CyberCafeDialogueKeyPath -Provider $Provider
if ($Remove) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    Write-Output "$Provider saved key removed. Existing editor processes and environment variables are unchanged."
}
else {
    $secure = Read-Host "$Provider API Key (masked; saved encrypted for this Windows user)" -AsSecureString
    try { Save-CyberCafeDialogueKey -Provider $Provider -Secret $secure }
    finally { if ($secure) { $secure.Dispose() } }
    Write-Output "$Provider key saved with Windows user-bound encryption. Restart the editor using Tools/Play-RegularCustomer.ps1."
}
