$Script:RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$Script:AppName = "Krevon Station"
$Script:PackageName = "com.krevon.station"
$Script:ExeName = "Krevon Station.exe"

function Get-RepoPath {
  param([Parameter(Mandatory = $true)][string]$Path)
  return [System.IO.Path]::GetFullPath((Join-Path $Script:RepoRoot $Path))
}

function Get-AppVersion {
  $versionPath = Get-RepoPath "VERSION"
  if (-not (Test-Path $versionPath)) {
    throw "VERSION file is missing."
  }
  return (Get-Content $versionPath -Raw).Trim()
}

function ConvertTo-FourPartVersion {
  param([Parameter(Mandatory = $true)][string]$Version)

  $clean = $Version.Trim()
  if ($clean.StartsWith("v", [System.StringComparison]::OrdinalIgnoreCase)) {
    $clean = $clean.Substring(1)
  }

  if ($clean -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') {
    throw "Version must look like 1.2.3 or 1.2.3.4."
  }

  $parts = $clean.Split(".")
  if ($parts.Count -eq 3) {
    return "$clean.0"
  }
  return $clean
}

function ConvertTo-ThreePartVersion {
  param([Parameter(Mandatory = $true)][string]$Version)

  $four = ConvertTo-FourPartVersion $Version
  $parts = $four.Split(".")
  return "$($parts[0]).$($parts[1]).$($parts[2])"
}

function Repair-ProcessPathEnvironment {
  $value = [Environment]::GetEnvironmentVariable("Path", "Process")
  if (-not $value) {
    $value = [Environment]::GetEnvironmentVariable("PATH", "Process")
  }
  if ($value) {
    [Environment]::SetEnvironmentVariable("Path", $value, "Process")
  }
}

function Get-MSBuildPath {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $vswhere) {
    $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\Current\Bin\MSBuild.exe" 2>$null | Select-Object -First 1
    if ($found -and (Test-Path $found)) {
      return $found
    }
  }

  $candidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
  )

  foreach ($candidate in $candidates) {
    if (Test-Path $candidate) {
      return $candidate
    }
  }

  throw "MSBuild was not found. Install Visual Studio with Desktop development with C++, or open this repo in Visual Studio and build there."
}

function Get-WindowsSdkTool {
  param([Parameter(Mandatory = $true)][string]$ToolName)

  $kitsRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
  if (-not (Test-Path $kitsRoot)) {
    throw "Windows SDK tools were not found under $kitsRoot."
  }

  $kit = Get-ChildItem $kitsRoot -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
    Sort-Object Name -Descending |
    Select-Object -First 1

  if (-not $kit) {
    throw "Windows SDK tools were not found under $kitsRoot."
  }

  $toolPath = Join-Path $kit.FullName "x64\$ToolName"
  if (-not (Test-Path $toolPath)) {
    throw "$ToolName was not found at $toolPath."
  }
  return $toolPath
}

function Invoke-KrevonBuild {
  param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
  )

  Repair-ProcessPathEnvironment
  $msbuild = Get-MSBuildPath
  $project = Get-RepoPath "KrevonStation.vcxproj"
  & $msbuild $project /p:Configuration=$Configuration /p:Platform=$Platform /m
  if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed."
  }
}

function Get-BuildOutputDir {
  param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
  )
  return Get-RepoPath "$Platform\$Configuration"
}

function Get-BuildExePath {
  param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
  )
  return Join-Path (Get-BuildOutputDir -Configuration $Configuration -Platform $Platform) $Script:ExeName
}

function Stop-KrevonProcess {
  Get-Process -Name "Krevon Station", "KrevonStation" -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
}

function Register-KrevonSparsePackage {
  param(
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [Parameter(Mandatory = $true)][string]$ExternalLocation,
    [switch]$Force
  )

  if (-not (Test-Path $PackagePath)) {
    throw "Package not found: $PackagePath"
  }
  if (-not (Test-Path $ExternalLocation)) {
    throw "External location not found: $ExternalLocation"
  }

  $existing = Get-AppxPackage -Name $Script:PackageName -ErrorAction SilentlyContinue
  if ($existing -and -not $Force) {
    Write-Host "Sparse package identity is already registered. Use .\dev.ps1 -ForceRegister after manifest/capability changes."
    return
  }

  if ($existing) {
    Get-AppxPackage -Name $Script:PackageName | Remove-AppxPackage -ErrorAction SilentlyContinue
  }

  Add-AppxPackage -Path $PackagePath -ExternalLocation $ExternalLocation
}

function Get-NSISPath {
  $makensis = Get-Command makensis.exe -ErrorAction SilentlyContinue
  if ($makensis) {
    return $makensis.Source
  }

  $candidate = "${env:ProgramFiles(x86)}\NSIS\makensis.exe"
  if (Test-Path $candidate) {
    return $candidate
  }

  throw "NSIS is not installed. Install NSIS, then rerun .\release.ps1."
}
