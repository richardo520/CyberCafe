param([switch]$PromptForApiKey)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Dialogue-Key.ps1"
$config = Get-CyberCafeDialogueConfig
if (!$config.Endpoint.StartsWith('https://') -or !$config.Model) { throw 'Use an HTTPS Responses endpoint and a model ID.' }
$key = Get-CyberCafeDialogueKey -Provider $config.Provider -Prompt:$PromptForApiKey -PromptIfMissing
if (!$key) { throw "Configure $($config.KeyVariable) or use -PromptForApiKey." }
$body = @{
    model=$config.Model; store=$false; max_output_tokens=1024; reasoning=@{effort='low'}
    input='Return a JSON object with exactly one field: {"line":"连接正常。"}. No explanation, markdown or other text.'
}
if ($config.Provider -eq 'OpenAI') {
    $schema = @{type='object'; properties=@{line=@{type='string'; enum=@('连接正常。')}}; required=@('line'); additionalProperties=$false}
    $body.text = @{format=@{type='json_schema'; name='npc_connection_probe'; strict=$true; schema=$schema}}
}
$json = $body | ConvertTo-Json -Depth 8 -Compress
$watch = [Diagnostics.Stopwatch]::StartNew()
$report = [ordered]@{provider=$config.Provider; model=$config.Model; completed=$false; schema_valid=$false; elapsed_seconds=0; http_status=0; purpose='connectivity_only'}
try {
    # One real request, consuming subscription quota or paid API credit. No player data.
    $result = Invoke-RestMethod -Method Post -Uri $config.Endpoint -Headers @{Authorization=('Bearer ' + $key)} -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($json)) -TimeoutSec 30
    $report.http_status = 200
    $report.completed = $result.status -eq 'completed'
    $parts = @($result.output | Where-Object {$_.type -eq 'message' -and $_.role -eq 'assistant'} | ForEach-Object {$_.content} | Where-Object {$_.type -eq 'output_text'})
    if ($parts.Count -eq 1) {
        $parsed = $parts[0].text | ConvertFrom-Json
        $report.schema_valid = @($parsed.PSObject.Properties).Count -eq 1 -and $parsed.line -ceq '连接正常。'
    }
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
if (!$report.completed -or !$report.schema_valid) { throw "Cloud probe failed or returned an incomplete result. HTTP=$($report.http_status). See Saved/RegularCustomer/CloudDialogue/live-connectivity.json." }
Write-Output "$($config.Provider) model=$($config.Model) completed in $($report.elapsed_seconds)s. Connectivity only; in-headset dialogue still needs review."
