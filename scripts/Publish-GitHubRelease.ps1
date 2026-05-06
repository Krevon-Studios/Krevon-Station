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
$tag = "v$version3"
$asset = Get-RepoPath "dist\Krevon Station Setup $version3.exe"

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

$releaseExists = $false
try {
  $null = & $gh.Source release view $tag --repo $Repo 2>&1
  if ($LASTEXITCODE -eq 0) { $releaseExists = $true }
} catch { }

if ($releaseExists) {
  & $gh.Source release upload $tag $asset --repo $Repo --clobber
  if ($LASTEXITCODE -ne 0) { throw "GitHub release upload failed." }
  if ($Notes) {
    & $gh.Source release edit $tag --repo $Repo --notes $Notes
  }
  Write-Host "Uploaded installer to existing release $tag."
} else {
  $args = @(
    "release", "create", $tag, $asset,
    "--repo", $Repo,
    "--title", "$version3",
    "--notes", $Notes
  )
  if ($Prerelease) {
    $args += "--prerelease"
  }
  & $gh.Source @args
  if ($LASTEXITCODE -ne 0) { throw "GitHub release create failed." }
  Write-Host "Created GitHub release $tag and uploaded installer."
}
