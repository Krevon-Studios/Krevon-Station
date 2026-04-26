; build/installer.nsh
; Custom NSIS script included by electron-builder.
; Adds a "Run at Windows startup" registry key so Krevon Station
; launches automatically when the user logs in.

!macro customInstall
  ; Write startup registry entry (HKCU — no admin required)
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" \
    "KrevonStation" "$INSTDIR\Krevon Station.exe"
!macroend

!macro customUninstall
  ; Remove startup registry entry on uninstall
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" \
    "KrevonStation"
!macroend
