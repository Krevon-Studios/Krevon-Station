param(
  [string]$Version,
  [switch]$Build,
  [switch]$Prerelease,
  [string]$Notes = "",
  [string]$Repo = "Krevon-Studios/Krevon-Station"
)

$ErrorActionPreference = "Stop"

$argsForScript = @{
  Repo = $Repo
  Build = $Build
  Prerelease = $Prerelease
  Notes = $Notes
}
if ($Version) {
  $argsForScript.Version = $Version
}

& "$PSScriptRoot\scripts\Publish-GitHubRelease.ps1" @argsForScript
