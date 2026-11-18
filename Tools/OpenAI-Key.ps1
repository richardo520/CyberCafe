function Get-CyberCafeOpenAIKey {
    param([switch]$Prompt)
    if ($Prompt) {
        $secure = Read-Host 'OpenAI API Key (masked, session only)' -AsSecureString
        $buffer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
        try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($buffer).Trim() }
        finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($buffer); $secure.Dispose() }
    }
    foreach ($scope in @('Process', 'User', 'Machine')) {
        $value = [Environment]::GetEnvironmentVariable('OPENAI_API_KEY', $scope)
        if ($value) { return $value.Trim() }
    }
    return ''
}
