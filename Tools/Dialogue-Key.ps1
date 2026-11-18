function Get-CyberCafeDialogueConfig {
    $values = @{}
    $active = $false
    foreach ($line in Get-Content -LiteralPath (Join-Path (Split-Path $PSScriptRoot -Parent) 'Config/DefaultGame.ini')) {
        $text = $line.Trim()
        if ($text.StartsWith('[')) { $active = $text -eq '[/Script/CyberCafe.RegularDialogueSettings]'; continue }
        if ($active -and $text -match '^(Provider|Model|Endpoint)=(.*)$') { $values[$Matches[1]] = $Matches[2].Trim().Trim('"') }
    }
    if ($values.Provider -notin @('MiniMax', 'OpenAI')) { throw 'Unknown cloud dialogue provider in DefaultGame.ini.' }
    $values.KeyVariable = if ($values.Provider -eq 'MiniMax') { 'MINIMAX_API_KEY' } else { 'OPENAI_API_KEY' }
    return $values
}

function Get-CyberCafeDialogueKeyPath {
    param([ValidateSet('MiniMax', 'OpenAI')][string]$Provider)
    return Join-Path (Split-Path $PSScriptRoot -Parent) "Saved/LocalSecrets/$Provider.key"
}

function Save-CyberCafeDialogueKey {
    param([ValidateSet('MiniMax', 'OpenAI')][string]$Provider, [Security.SecureString]$Secret)
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'Local key storage requires Windows DPAPI.' }
    if (!$Secret -or $Secret.Length -eq 0) { throw 'Empty API key was not saved.' }
    $path = Get-CyberCafeDialogueKeyPath -Provider $Provider
    $folder = Split-Path $path -Parent
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    # Without -Key, Windows encrypts SecureString using DPAPI CurrentUser.
    # Write ciphertext only, then atomically replace the previous encrypted file.
    $encrypted = ConvertFrom-SecureString -SecureString $Secret -ErrorAction Stop
    $temporary = Join-Path $folder ([guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [IO.File]::WriteAllText($temporary, $encrypted, [Text.Encoding]::ASCII)
        if (Test-Path -LiteralPath $path) { [IO.File]::Replace($temporary, $path, [NullString]::Value) }
        else { [IO.File]::Move($temporary, $path) }
    }
    finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        $encrypted = $null
    }
}

function Get-CyberCafeDialogueKey {
    param([ValidateSet('MiniMax', 'OpenAI')][string]$Provider, [switch]$Prompt, [switch]$PromptIfMissing)
    $secure = $null
    if (!$Prompt) {
        $path = Get-CyberCafeDialogueKeyPath -Provider $Provider
        if (Test-Path -LiteralPath $path) {
            if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { throw 'Local key storage requires Windows DPAPI.' }
            try { $secure = ConvertTo-SecureString -String ([IO.File]::ReadAllText($path)) -ErrorAction Stop }
            catch { throw 'Saved API key cannot be decrypted by this Windows user. Run Tools/Set-DialogueApiKey.ps1 to replace it.' }
        }
    }
    if (!$secure -and !$Prompt) {
        $variable = if ($Provider -eq 'MiniMax') { 'MINIMAX_API_KEY' } else { 'OPENAI_API_KEY' }
        foreach ($scope in @('Process', 'User', 'Machine')) {
            $value = [Environment]::GetEnvironmentVariable($variable, $scope)
            if ($value) { return $value.Trim() }
        }
    }
    if (!$secure -and ($Prompt -or $PromptIfMissing)) {
        $secure = Read-Host "$Provider API Key (masked; saved encrypted for this Windows user)" -AsSecureString
        try { Save-CyberCafeDialogueKey -Provider $Provider -Secret $secure }
        catch { if ($secure) { $secure.Dispose() }; throw 'API key was not saved. Check local storage permissions and enter a nonempty key.' }
    }
    if (!$secure) { return '' }
    $buffer = [IntPtr]::Zero
    try {
        $buffer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
        return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($buffer).Trim()
    }
    finally {
        if ($buffer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($buffer) }
        $secure.Dispose()
    }
}
