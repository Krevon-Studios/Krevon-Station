param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$Version
)

$ErrorActionPreference = "Stop"
& "$PSScriptRoot\scripts\Set-Version.ps1" -Version $Version
