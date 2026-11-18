param([string]$EngineRoot = 'F:/UnrealEngine', [string]$Filter = 'CyberCafe.CloudDialogue.')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$python = Join-Path $EngineRoot 'Engine/Binaries/ThirdParty/Python3/Win64/python.exe'
$run = Join-Path $projectRoot ('Saved/RegularCustomer/CloudDialogue/Mock-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
$endpointFile = Join-Path $run 'endpoint.txt'
$previousEndpoint = $env:CYBERCAFE_MOCK_OPENAI_ENDPOINT
$previousNoProxy = $env:NO_PROXY
$server = $null
try {
    $server = Start-Process -FilePath $python -WindowStyle Hidden -PassThru -ArgumentList @(
        ('"' + (Join-Path $PSScriptRoot 'Tests/mock_openai_responses.py') + '"'),
        '--endpoint-file', ('"' + $endpointFile + '"'), '--report-file', ('"' + (Join-Path $run 'contract.json') + '"')
    ) -RedirectStandardOutput (Join-Path $run 'server.log') -RedirectStandardError (Join-Path $run 'server-error.log')
    for ($attempt = 0; $attempt -lt 40 -and !(Test-Path -LiteralPath $endpointFile); $attempt++) { Start-Sleep -Milliseconds 250 }
    if (!(Test-Path -LiteralPath $endpointFile)) { throw 'Local contract server did not start.' }
    $env:CYBERCAFE_MOCK_OPENAI_ENDPOINT = (Get-Content -LiteralPath $endpointFile -Raw).Trim()
    $env:NO_PROXY = ($previousNoProxy + ',127.0.0.1,localhost').Trim(',')
    & "$PSScriptRoot/Test-CustomerFlow.ps1" -EngineRoot $EngineRoot -Filter $Filter
}
finally {
    $env:CYBERCAFE_MOCK_OPENAI_ENDPOINT = $previousEndpoint
    $env:NO_PROXY = $previousNoProxy
    if ($server -and !$server.HasExited) { $server.Kill() }
}
