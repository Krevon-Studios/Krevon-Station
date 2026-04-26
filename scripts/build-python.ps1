$ErrorActionPreference = "Stop"

$pythonDir = Join-Path $PWD "resources\python"
$zipPath = Join-Path $PWD "resources\python.zip"

Write-Host "Creating python directory..."
if (Test-Path $pythonDir) { Remove-Item -Recurse -Force $pythonDir }
New-Item -ItemType Directory -Force -Path $pythonDir | Out-Null

Write-Host "Downloading Python embeddable..."
curl.exe -L -o $zipPath "https://www.python.org/ftp/python/3.14.0/python-3.14.0-embed-amd64.zip"

Write-Host "Extracting Python..."
Expand-Archive -Path $zipPath -DestinationPath $pythonDir -Force
Remove-Item $zipPath

Write-Host "Installing pip dependencies to target folder..."
# Use the system Python to download and install packages into our embeddable Lib\site-packages
$sitePackages = Join-Path $pythonDir "Lib\site-packages"
New-Item -ItemType Directory -Force -Path $sitePackages | Out-Null
py -m pip install pycaw psutil winrt-Windows.Foundation winrt-Windows.Foundation.Collections winrt-Windows.UI.Notifications winrt-Windows.UI.Notifications.Management --target $sitePackages

Write-Host "Configuring Python path..."
$pthFile = Join-Path $pythonDir "python314._pth"
$pthContent = Get-Content $pthFile
# Add Lib\site-packages to the paths so Python can find pycaw/psutil
$pthContent += "Lib\site-packages"
$pthContent | Set-Content $pthFile

Write-Host "Portable Python environment created successfully in $pythonDir!"
