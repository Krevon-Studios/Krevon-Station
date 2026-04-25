# notification-monitor.ps1
# Polls Windows.UI.Notifications.Management.UserNotificationListener every 1s.
# Emits newline-delimited JSON to stdout.
# Requires Windows 10 1803+ (UserNotificationListener API).

$ErrorActionPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Add-Type -AssemblyName System.Runtime.WindowsRuntime -ErrorAction SilentlyContinue

# Load WinRT types
try {
  $null = [Windows.UI.Notifications.Management.UserNotificationListener, Windows.UI.Notifications.Management, ContentType=WindowsRuntime]
  $null = [Windows.UI.Notifications.NotificationKinds, Windows.UI.Notifications, ContentType=WindowsRuntime]
  $null = [Windows.UI.Notifications.KnownNotificationBindings, Windows.UI.Notifications, ContentType=WindowsRuntime]
} catch {
  Write-Output '{"error":"winrt_unavailable"}'
  [Console]::Out.Flush()
  exit 1
}

# Generic AsTask helper for IAsyncOperation<T>
$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() |
  Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]

function Await-Async {
  param($asyncOp)
  $resultType = $asyncOp.GetType().GetMethod('GetResults').ReturnType
  $task = $asTaskGeneric.MakeGenericMethod($resultType).Invoke($null, @($asyncOp))
  $task.GetAwaiter().GetResult()
}

$listener = [Windows.UI.Notifications.Management.UserNotificationListener]::Current

# Request notification access (one-time prompt; subsequent calls return cached result)
try {
  $access = Await-Async $listener.RequestAccessAsync()
  if ($access.ToString() -ne 'Allowed') {
    Write-Output '{"error":"access_denied"}'
    [Console]::Out.Flush()
    exit 1
  }
} catch {
  Write-Output '{"error":"access_request_failed"}'
  [Console]::Out.Flush()
  exit 1
}

function EscapeJson([string]$s) {
  $s -replace '\\','\\' -replace '"','\"' -replace "`r`n",' ' -replace "`n",' ' -replace "`r",' '
}

function Get-NotifLine {
  param($n, [string]$type)
  try {
    $binding = $n.Notification.Visual.GetBinding(
      [Windows.UI.Notifications.KnownNotificationBindings]::ToastGeneric)
    if (-not $binding) { return $null }
    $els   = $binding.GetTextElements()
    $title = if ($els.Count -gt 0) { EscapeJson $els[0].Text } else { '' }
    $body  = if ($els.Count -gt 1) { EscapeJson $els[1].Text } else { '' }
    $appName = ''
    $appId   = ''
    try {
      $appName = EscapeJson $n.AppInfo.DisplayInfo.DisplayName
      $appId   = EscapeJson $n.AppInfo.AppUserModelId
    } catch { }
    return "{""type"":""$type"",""id"":$($n.Id),""appId"":""$appId"",""appName"":""$appName"",""title"":""$title"",""body"":""$body""}"
  } catch { return $null }
}

$prevIds = @{}

while ($true) {
  try {
    $notifs = Await-Async ($listener.GetNotificationsAsync(
      [Windows.UI.Notifications.NotificationKinds]::Toast))

    $currentIds = @{}
    foreach ($n in $notifs) {
      $currentIds[$n.Id] = $true
      if (-not $prevIds.ContainsKey($n.Id)) {
        $line = Get-NotifLine $n 'added'
        if ($line) {
          Write-Output $line
          [Console]::Out.Flush()
        }
      }
    }

    foreach ($id in @($prevIds.Keys)) {
      if (-not $currentIds.ContainsKey($id)) {
        Write-Output "{""type"":""removed"",""id"":$id}"
        [Console]::Out.Flush()
      }
    }

    $prevIds = $currentIds
  } catch { }

  Start-Sleep -Milliseconds 1000
}
