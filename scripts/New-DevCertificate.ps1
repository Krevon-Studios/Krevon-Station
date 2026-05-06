param(
  [string]$Subject = "CN=Krevon Studios",
  [string]$OutDir = ".\artifacts\cert",
  [string]$Password = "krevon-dev-pass"
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force $OutDir | Out-Null

$securePassword = ConvertTo-SecureString $Password -AsPlainText -Force
$pfxPath = Join-Path $OutDir "KrevonStationDev.pfx"
$cerPath = Join-Path $OutDir "KrevonStationDev.cer"

if (Test-Path $pfxPath) {
  if (Test-Path $cerPath) {
    Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\CurrentUser\TrustedPeople | Out-Null
    Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\CurrentUser\Root | Out-Null
    Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\CurrentUser\TrustedPublisher | Out-Null
  }

  [PSCustomObject]@{
    PfxPath = (Resolve-Path $pfxPath).Path
    CerPath = if (Test-Path $cerPath) { (Resolve-Path $cerPath).Path } else { $null }
    Password = $Password
  }
  return
}

$certParams = @{
  Type = "Custom"
  Subject = $Subject
  KeyUsage = "DigitalSignature"
  KeyAlgorithm = "RSA"
  KeyLength = 2048
  KeyExportPolicy = "Exportable"
  FriendlyName = "Krevon Station Dev"
  CertStoreLocation = "Cert:\CurrentUser\My"
  TextExtension = @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")
}

try {
  $cert = New-SelfSignedCertificate @certParams -Provider "Microsoft Enhanced RSA and AES Cryptographic Provider"
} catch {
  $cert = New-SelfSignedCertificate @certParams
}

Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $securePassword | Out-Null
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null
Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\CurrentUser\TrustedPeople | Out-Null
Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\CurrentUser\Root | Out-Null
Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\CurrentUser\TrustedPublisher | Out-Null

[PSCustomObject]@{
  PfxPath = (Resolve-Path $pfxPath).Path
  CerPath = (Resolve-Path $cerPath).Path
  Password = $Password
}
