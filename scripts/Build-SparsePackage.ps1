param(
  [string]$Configuration = "Release",
  [string]$Platform = "x64",
  [string]$PackagePath = ".\artifacts\KrevonStation.msix",
  [string]$CertificateFile = $env:KREVON_CERT_PFX,
  [string]$CertificatePassword = $env:KREVON_CERT_PASSWORD,
  [switch]$CreateDevCertificate,
  [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\Common.ps1"

if (-not [System.IO.Path]::IsPathRooted($PackagePath)) {
  $PackagePath = Get-RepoPath $PackagePath
}

if ($CreateDevCertificate -and -not $CertificateFile) {
  $cert = & "$PSScriptRoot\New-DevCertificate.ps1" -OutDir (Get-RepoPath "artifacts\cert")
  $CertificateFile = $cert.PfxPath
  $CertificatePassword = $cert.Password
}

if (-not $CertificateFile -or -not $CertificatePassword) {
  throw "Provide KREVON_CERT_PFX and KREVON_CERT_PASSWORD, or pass -CreateDevCertificate for local testing."
}

if (-not $NoBuild) {
  Invoke-KrevonBuild -Configuration $Configuration -Platform $Platform
}

$sparseDir = Join-Path $PSScriptRoot "..\artifacts\sparse"
New-Item -ItemType Directory -Force $sparseDir | Out-Null
Copy-Item "$PSScriptRoot\..\Package.appxmanifest" (Join-Path $sparseDir "AppxManifest.xml") -Force

New-Item -ItemType Directory -Force (Split-Path $PackagePath -Parent) | Out-Null

$makeappx = Get-WindowsSdkTool "makeappx.exe"
$signtool = Get-WindowsSdkTool "signtool.exe"

& $makeappx pack /o /d $sparseDir /nv /p $PackagePath
if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed." }

& $signtool sign /fd SHA256 /f $CertificateFile /p $CertificatePassword $PackagePath
if ($LASTEXITCODE -ne 0) { throw "SignTool failed." }

Write-Host "Sparse package written to $PackagePath"
