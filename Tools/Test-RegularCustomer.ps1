param([string]$EngineRoot = 'F:/UnrealEngine')
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot/Test-CustomerFlow.ps1" -EngineRoot $EngineRoot -Filter 'CyberCafe.RegularCustomer.'
