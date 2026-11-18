param(
    [switch]$PromptForApiKey,
    [string]$Model = 'gpt-6.1-sol',
    [string]$Endpoint = 'https://api.openai.com/v1/responses'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/OpenAI-Key.ps1"
$key = Get-CyberCafeOpenAIKey -Prompt:$PromptForApiKey
if (!$key) { throw 'No API key found. Configure OPENAI_API_KEY or use -PromptForApiKey.' }
if (!$Endpoint.StartsWith('https://') -or !$Model) { throw 'Use an HTTPS Responses endpoint and a model ID.' }
$schema = @{type='object'; properties=@{line=@{type='string'; enum=@('连接正常。')}}; required=@('line'); additionalProperties=$false}
$body = @{
    model=$Model; store=$false; max_output_tokens=1024; reasoning=@{effort='low'}
    input='Return a JSON object whose line is exactly: 连接正常。'
    text=@{format=@{type='json_schema'; name='npc_connection_probe'; strict=$true; schema=$schema}}
} | ConvertTo-Json -Depth 8 -Compress
$watch = [Diagnostics.Stopwatch]::StartNew()
$report = [ordered]@{model=$Model; completed=$false; schema_valid=$false; elapsed_seconds=0; http_status=0; purpose='connectivity_only'}
try {
    # One real, billable request. No player saves, chat history, key or provider error body is logged.
    $result = Invoke-RestMethod -Method Post -Uri $Endpoint -Headers @{Authorization=('Bearer ' + $key)} -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($body)) -TimeoutSec 30
    $report.http_status = 200
    $report.completed = $result.status -eq 'completed'
    $parts = @($result.output | Where-Object {$_.type -eq 'message'} | ForEach-Object {$_.content} | Where-Object {$_.type -eq 'output_text'})
    if ($parts.Count -eq 1) { $parsed = $parts[0].text | ConvertFrom-Json; $report.schema_valid = $parsed.line -eq '连接正常。' }
}
catch {
    if ($_.Exception.Response) { $report.http_status = [int]$_.Exception.Response.StatusCode }
}
finally {
    $watch.Stop(); $report.elapsed_seconds = [math]::Round($watch.Elapsed.TotalSeconds, 3)
    $key = $null
    $folder = Join-Path (Split-Path $PSScriptRoot -Parent) 'Saved/RegularCustomer/CloudDialogue'
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $report | ConvertTo-Json | Set-Content (Join-Path $folder 'live-connectivity.json') -Encoding utf8
}
if (!$report.completed -or !$report.schema_valid) { throw "OpenAI probe failed or returned an incomplete result. HTTP=$($report.http_status). See Saved/RegularCustomer/CloudDialogue/live-connectivity.json." }
Write-Output "OpenAI model=$Model completed successfully in $($report.elapsed_seconds)s. This verifies API connectivity, not the in-headset dialogue experience."
