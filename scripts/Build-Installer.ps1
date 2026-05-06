param(
  [string]$Platform = "x64",
  [string]$CertificateFile = $env:KREVON_CERT_PFX,
  [string]$CertificatePassword = $env:KREVON_CERT_PASSWORD,
  [switch]$CreateDevCertificate
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\Common.ps1"

$makensis = Get-NSISPath

& "$PSScriptRoot\Build-SparsePackage.ps1" `
  -Configuration Release `
  -Platform $Platform `
  -CertificateFile $CertificateFile `
  -CertificatePassword $CertificatePassword `
  -CreateDevCertificate:$CreateDevCertificate

New-Item -ItemType Directory -Force "$PSScriptRoot\..\dist" | Out-Null

$nsisArgs = @("$PSScriptRoot\..\installer\KrevonStation.nsi")
$devCerPath = "$PSScriptRoot\..\artifacts\cert\KrevonStationDev.cer"
if ($CreateDevCertificate -and (Test-Path $devCerPath)) {
  $nsisArgs = @("/DDEV_CERT_PATH=$devCerPath") + $nsisArgs
}

& $makensis @nsisArgs
if ($LASTEXITCODE -ne 0) { throw "makensis failed." }

$version = (Get-Content "$PSScriptRoot\..\VERSION").Trim()
Write-Host "Installer written to dist\Krevon Station Setup $version.exe"
