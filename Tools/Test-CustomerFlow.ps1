param(
    [string]$EngineRoot = 'F:/UnrealEngine',
    [string]$Filter = 'CyberCafe.Customer.+CyberCafe.SceneMusic.+CyberCafe.Recipe.+CyberCafe.Architecture.'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if (!(Test-Path -LiteralPath $editor)) { throw "Missing editor: $editor" }
$runRoot = Join-Path $projectRoot ('Saved/ArchitectureAudit/CustomerFlow-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$previousDiagnostics = $env:DRINK_FIX_RUN_DIR
try {
    $env:DRINK_FIX_RUN_DIR = Join-Path $runRoot 'Measurements'
    $arguments = @(
        (Join-Path $projectRoot 'CyberCafe.uproject'),
        # Saved-map fixtures load and duplicate L_Cafe themselves. Keep editor startup
        # actors out of that source world, independent of the user's startup map.
        '/Engine/Maps/Entry',
        '-unattended', '-nop4', '-nosplash', '-nullrhi', '-nohmd',
        "-ExecCmds=Automation RunTests $Filter",
        '-TestExit=Automation Test Queue Empty',
        "-ReportExportPath=$runRoot/Tests", "-abslog=$runRoot/tests.log"
    )
    & $editor @arguments
    $editorExit = $LASTEXITCODE
    $reportFile = Join-Path $runRoot 'Tests/index.json'
    if (!(Test-Path -LiteralPath $reportFile)) { throw "No automation report; editor exit=$editorExit. Log: $runRoot/tests.log" }
    $report = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
    $passed = $report.succeeded + $report.succeededWithWarnings
    Write-Output "Passed=$passed Failed=$($report.failed) NotRun=$($report.notRun) Report=$reportFile"
    # UE's queue-empty exit code alone does not prove test success.
    if ($editorExit -ne 0 -or $report.failed -gt 0 -or $report.notRun -gt 0 -or $report.inProcess -gt 0 -or $passed -eq 0) {
        throw 'Customer flow regression failed; inspect the JSON report and log.'
    }
}
finally {
    $env:DRINK_FIX_RUN_DIR = $previousDiagnostics
}
