$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$fixture = Join-Path $projectRoot ('Saved/KeyStorageTests/' + [guid]::NewGuid().ToString('N'))
$toolsFolder = Join-Path $fixture 'Tools'
New-Item -ItemType Directory -Force -Path $toolsFolder, (Join-Path $fixture 'Config') | Out-Null
foreach ($name in @('Dialogue-Key.ps1', 'Play-RegularCustomer.ps1', 'Set-DialogueApiKey.ps1')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "Tools/$name") -Destination $toolsFolder
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'Config/DefaultGame.ini') -Destination (Join-Path $fixture 'Config')
$previousKey = $env:MINIMAX_API_KEY
$previousOpenAI = $env:OPENAI_API_KEY
$previousDisabled = $env:CYBERCAFE_DISABLE_CLOUD_DIALOGUE
function Assert-KeyTest($condition, $label) { if (!$condition) { throw "Key storage test failed: $label" } }
try {
    # All writes and all dummy credentials stay in an isolated ignored fixture.
    . (Join-Path $toolsFolder 'Dialogue-Key.ps1')
    $env:MINIMAX_API_KEY = 'dummy-environment-key'
    $env:OPENAI_API_KEY = 'dummy-other-provider'
    Assert-KeyTest ((Get-CyberCafeDialogueKey -Provider MiniMax) -ceq 'dummy-environment-key') 'environment fallback'
    $secret = ConvertTo-SecureString 'dummy-storage-key-1' -AsPlainText -Force
    try { Save-CyberCafeDialogueKey -Provider MiniMax -Secret $secret } finally { $secret.Dispose() }
    $path = Get-CyberCafeDialogueKeyPath -Provider MiniMax
    Assert-KeyTest ($path.StartsWith($fixture)) 'isolated fixture path'
    $cipher = [IO.File]::ReadAllText($path)
    Assert-KeyTest (!$cipher.Contains('dummy-storage-key-1')) 'ciphertext does not contain plaintext'
    Assert-KeyTest ($cipher.StartsWith('01000000d08c9ddf')) 'Windows DPAPI envelope'
    Assert-KeyTest ((Get-CyberCafeDialogueKey -Provider MiniMax) -ceq 'dummy-storage-key-1') 'decrypt and prefer local storage'
    Assert-KeyTest ((Get-CyberCafeDialogueKey -Provider OpenAI) -ceq 'dummy-other-provider') 'provider separation'
    $secret = ConvertTo-SecureString 'dummy-storage-key-2' -AsPlainText -Force
    try { Save-CyberCafeDialogueKey -Provider MiniMax -Secret $secret } finally { $secret.Dispose() }
    Assert-KeyTest ((Get-CyberCafeDialogueKey -Provider MiniMax) -ceq 'dummy-storage-key-2') 'atomic key replacement'
    $secret = New-Object Security.SecureString
    $rejected = $false
    try { Save-CyberCafeDialogueKey -Provider MiniMax -Secret $secret } catch { $rejected = $true } finally { $secret.Dispose() }
    Assert-KeyTest ($rejected -and (Get-CyberCafeDialogueKey -Provider MiniMax) -ceq 'dummy-storage-key-2') 'empty replacement preserves previous key'
    [IO.File]::WriteAllText($path, 'corrupted-ciphertext')
    $rejected = $false
    try { $null = Get-CyberCafeDialogueKey -Provider MiniMax } catch { $rejected = $_.Exception.Message -like 'Saved API key cannot be decrypted*' }
    Assert-KeyTest $rejected 'corruption fails without silently selecting another credential'
    function Read-Host { param($Prompt, [switch]$AsSecureString); return ConvertTo-SecureString 'dummy-prompt-key' -AsPlainText -Force }
    Assert-KeyTest ((Get-CyberCafeDialogueKey -Provider MiniMax -Prompt) -ceq 'dummy-prompt-key') 'prompt replaces unreadable storage'
    $launches = [Collections.Generic.List[string]]::new()
    function Start-Process {
        param($FilePath, $ArgumentList)
        Assert-KeyTest (!(($ArgumentList -join ' ').Contains('dummy-'))) 'key absent from command line'
        $launches.Add([string]$env:MINIMAX_API_KEY)
    }
    & (Join-Path $toolsFolder 'Play-RegularCustomer.ps1')
    Assert-KeyTest ($launches[0] -ceq 'dummy-prompt-key') 'launcher injects decrypted key into editor environment'
    Assert-KeyTest ($env:MINIMAX_API_KEY -ceq 'dummy-environment-key') 'launcher restores caller environment'
    & (Join-Path $toolsFolder 'Play-RegularCustomer.ps1') -AuthoredOnly
    Assert-KeyTest ([string]::IsNullOrEmpty($launches[1])) 'authored mode does not pass key'
    & (Join-Path $toolsFolder 'Set-DialogueApiKey.ps1') -Remove | Out-Null
    Assert-KeyTest (!(Test-Path -LiteralPath $path)) 'remove saved key'
    $env:MINIMAX_API_KEY = $null
    # Explicit prompt bypasses ambient machine/user credentials, without reading them.
    $null = Get-CyberCafeDialogueKey -Provider MiniMax -Prompt
    Assert-KeyTest (Test-Path -LiteralPath $path) 'first-use prompt saves encrypted key'
    Write-Output 'PASS: DPAPI round trip, ciphertext, replacement, corruption, provider isolation, launcher environment and deletion. No real API requests.'
}
finally {
    $env:MINIMAX_API_KEY = $previousKey
    $env:OPENAI_API_KEY = $previousOpenAI
    $env:CYBERCAFE_DISABLE_CLOUD_DIALOGUE = $previousDisabled
    # Delete only files created by this test, never the real LocalSecrets directory.
    foreach ($name in @('MiniMax.key', 'OpenAI.key')) {
        $testFile = Join-Path $fixture "Saved/LocalSecrets/$name"
        if (Test-Path -LiteralPath $testFile) { Remove-Item -LiteralPath $testFile -Force }
    }
}
