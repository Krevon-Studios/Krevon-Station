param(
  [string]$CertificatePath = ".\artifacts\cert\KrevonStationDev.cer"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\Common.ps1"

$resolvedCertificatePath = if ([System.IO.Path]::IsPathRooted($CertificatePath)) {
  $CertificatePath
} else {
  Get-RepoPath $CertificatePath
}

if (-not (Test-Path $resolvedCertificatePath)) {
  throw "Dev certificate not found at $resolvedCertificatePath. Run .\dev.ps1 once to create it."
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
  $args = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", "`"$PSCommandPath`"",
    "-CertificatePath", "`"$resolvedCertificatePath`""
  )
  $process = Start-Process powershell.exe -Verb RunAs -Wait -PassThru -ArgumentList $args
  if ($process.ExitCode -ne 0) {
    throw "Admin certificate trust step was cancelled or failed."
  }
  return
}

Import-Certificate -FilePath $resolvedCertificatePath -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
Import-Certificate -FilePath $resolvedCertificatePath -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
Import-Certificate -FilePath $resolvedCertificatePath -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null

Import-Certificate -FilePath $resolvedCertificatePath -CertStoreLocation Cert:\CurrentUser\Root | Out-Null
Import-Certificate -FilePath $resolvedCertificatePath -CertStoreLocation Cert:\CurrentUser\TrustedPeople | Out-Null
Import-Certificate -FilePath $resolvedCertificatePath -CertStoreLocation Cert:\CurrentUser\TrustedPublisher | Out-Null

Write-Host "Krevon Station dev certificate is trusted for MSIX registration."
