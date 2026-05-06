param(
  [Parameter(Mandatory = $true)]
  [string]$Version
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\Common.ps1"

$threePart = ConvertTo-ThreePartVersion $Version
$fourPart = ConvertTo-FourPartVersion $Version
$rcCommas = $fourPart.Replace(".", ",")

Set-Content -Path (Get-RepoPath "VERSION") -Value $threePart -Encoding utf8

$manifestPath = Get-RepoPath "Package.appxmanifest"
[xml]$manifest = Get-Content $manifestPath
$manifest.Package.Identity.Version = $fourPart
$settings = New-Object System.Xml.XmlWriterSettings
$settings.Indent = $true
$settings.Encoding = New-Object System.Text.UTF8Encoding($false)
$writer = [System.Xml.XmlWriter]::Create($manifestPath, $settings)
$manifest.Save($writer)
$writer.Close()

$rcPath = Get-RepoPath "app.rc"
$rc = Get-Content $rcPath -Raw
$rc = $rc -replace 'FILEVERSION\s+\d+,\d+,\d+,\d+', "FILEVERSION $rcCommas"
$rc = $rc -replace 'PRODUCTVERSION\s+\d+,\d+,\d+,\d+', "PRODUCTVERSION $rcCommas"
$rc = $rc -replace 'VALUE "FileVersion", "\d+\.\d+\.\d+(\.\d+)?"', "VALUE `"FileVersion`", `"$threePart`""
$rc = $rc -replace 'VALUE "ProductVersion", "\d+\.\d+\.\d+(\.\d+)?"', "VALUE `"ProductVersion`", `"$threePart`""
Set-Content -Path $rcPath -Value $rc -Encoding utf8

$nsiPath = Get-RepoPath "installer\KrevonStation.nsi"
$nsi = Get-Content $nsiPath -Raw
$nsi = $nsi -replace '!define VERSION "\d+\.\d+\.\d+(\.\d+)?"', "!define VERSION `"$threePart`""
$nsi = $nsi -replace 'VIProductVersion "\d+\.\d+\.\d+\.\d+"', "VIProductVersion `"$fourPart`""
Set-Content -Path $nsiPath -Value $nsi -Encoding utf8

Write-Host "Updated Krevon Station version to $threePart ($fourPart package/version resource)."
