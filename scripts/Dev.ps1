param(
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",
  [string]$Platform = "x64",
  [switch]$NoLaunch,
  [switch]$NoBuild,
  [switch]$ForceRegister
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\Common.ps1"

$packagePath = Get-RepoPath "artifacts\KrevonStation-dev.msix"
$outputDir = Get-BuildOutputDir -Configuration $Configuration -Platform $Platform
$exePath = Get-BuildExePath -Configuration $Configuration -Platform $Platform

Stop-KrevonProcess

& "$PSScriptRoot\Build-SparsePackage.ps1" `
  -Configuration $Configuration `
  -Platform $Platform `
  -PackagePath $packagePath `
  -CreateDevCertificate `
  -NoBuild:$NoBuild

if (-not (Test-Path $exePath)) {
  throw "Build output was not found: $exePath"
}

try {
  Register-KrevonSparsePackage -PackagePath $packagePath -ExternalLocation $outputDir -Force:$ForceRegister
} catch {
  $message = $_.Exception.Message
  if ($message -notmatch '0x800B0109|root certificate|must be trusted') {
    throw
  }

  Write-Host "Windows does not trust the dev signing certificate yet. A one-time admin prompt will import it for MSIX registration."
  & "$PSScriptRoot\Trust-DevCertificate.ps1"
  Register-KrevonSparsePackage -PackagePath $packagePath -ExternalLocation $outputDir -Force:$ForceRegister
}

Write-Host "Dev package identity is ready for $outputDir"

if (-not $NoLaunch) {
  Start-Process -FilePath $exePath
  Write-Host "Started $exePath"
}
