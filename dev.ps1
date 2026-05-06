param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",
  [switch]$NoLaunch,
  [switch]$NoBuild,
  [switch]$ForceRegister
)

$ErrorActionPreference = "Stop"
& "$PSScriptRoot\scripts\Dev.ps1" `
  -Configuration $Configuration `
  -NoLaunch:$NoLaunch `
  -NoBuild:$NoBuild `
  -ForceRegister:$ForceRegister
