param(
  [string]$Version,
  [switch]$Build,
  [switch]$Prerelease,
  [string]$Notes = "",
  [string]$Repo = "Krevon-Studios/Krevon-Station"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\Common.ps1"

if (-not $Version) {
  $Version = Get-AppVersion
}

$version3 = ConvertTo-ThreePartVersion $Version
$tag      = "v$version3"
$asset    = Get-RepoPath "dist\Krevon Station Setup $version3.exe"

if ($Build) {
  & "$PSScriptRoot\Build-Installer.ps1"
}

if (-not (Test-Path $asset)) {
  throw "Installer not found at $asset. Run .\release.ps1 first, or use .\publish.ps1 $version3 -Build."
}

$gh = Get-Command gh.exe -ErrorAction SilentlyContinue
if (-not $gh) {
  throw "GitHub CLI is not installed. Install gh, run 'gh auth login', then rerun publish."
}

# GitHub replaces spaces with dots in uploaded asset names.
$assetLeaf   = (Split-Path $asset -Leaf) -replace ' ', '.'
$assetSize   = (Get-Item $asset).Length
$sha512Bytes = [System.Security.Cryptography.SHA512]::Create().ComputeHash(
                 [System.IO.File]::ReadAllBytes($asset))
$sha512b64   = [Convert]::ToBase64String($sha512Bytes)

$latestYml = Get-RepoPath "dist\latest.yml"
Set-Content -Path $latestYml -Encoding utf8 -Value @"
version: $version3
files:
  - url: $assetLeaf
    sha512: $sha512b64
    size: $assetSize
path: $assetLeaf
sha512: $sha512b64
releaseDate: '$([DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ss.fffZ"))'
"@

$releaseExists = $false
try {
  $null = & $gh.Source release view $tag --repo $Repo 2>&1
  if ($LASTEXITCODE -eq 0) { $releaseExists = $true }
} catch { }

if ($releaseExists) {
  & $gh.Source release upload $tag $asset $latestYml --repo $Repo --clobber
  if ($LASTEXITCODE -ne 0) { throw "GitHub release upload failed." }
  if ($Notes) {
    & $gh.Source release edit $tag --repo $Repo --notes $Notes
  }
  Write-Host "Uploaded installer and latest.yml to existing release $tag."
} else {
  $createArgs = @(
    "release", "create", $tag, $asset, $latestYml,
    "--repo",  $Repo,
    "--title", $version3,
    "--notes", $Notes
  )
  if ($Prerelease) {
    $createArgs += "--prerelease"
  }
  & $gh.Source @createArgs
  if ($LASTEXITCODE -ne 0) { throw "GitHub release create failed." }
  Write-Host "Created GitHub release $tag and uploaded installer and latest.yml."
}
