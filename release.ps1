param(
  [switch]$LocalTest
)

$ErrorActionPreference = "Stop"
& "$PSScriptRoot\scripts\Build-Installer.ps1" -CreateDevCertificate:$LocalTest
