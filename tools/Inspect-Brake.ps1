param([ValidateRange(0, 60)][int]$Seconds = 0)
$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $PSScriptRoot 'WindowsBrake.cs')
[WindowsBrake]::Inspect($Seconds).GetAwaiter().GetResult() | ConvertTo-Json -Depth 8
