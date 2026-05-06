!define APP_NAME "Krevon Station"
!define APP_ID "com.krevon.station"
!define EXE_NAME "Krevon Station.exe"
!define COMPANY_NAME "Krevon Studios"
!define VERSION "1.1.0"

!define MUI_ICON "..\assets\logo_solid.ico"
!define MUI_UNICON "..\assets\logo_solid.ico"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

Icon "..\assets\logo_solid.ico"
UninstallIcon "..\assets\logo_solid.ico"

Name "${APP_NAME}"
OutFile "..\dist\Krevon Station Setup ${VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\Krevon Station"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails nevershow
ShowUninstDetails nevershow

VIProductVersion "1.1.0.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "CompanyName" "${COMPANY_NAME}"
VIAddVersionKey "FileDescription" "${APP_NAME} Installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "© 2026 Krevon Studios"

!define MUI_WELCOMEPAGE_TITLE "Welcome to Krevon Station ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "Krevon Station replaces the Windows taskbar with a GPU-accelerated top bar.$\r$\n$\r$\nClick Install to continue."
!define MUI_FINISHPAGE_TITLE "Installation Complete"
!define MUI_FINISHPAGE_TEXT "Krevon Station ${VERSION} has been installed.$\r$\n$\r$\nThe app will start automatically."
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetShellVarContext current

  ExecWait 'taskkill /IM "${EXE_NAME}" /F'
  ExecWait 'taskkill /IM "KrevonStation.exe" /F'

  SetOutPath "$INSTDIR"
  RMDir /r "$INSTDIR\assets"

  File "..\x64\Release\${EXE_NAME}"
  File "..\artifacts\KrevonStation.msix"
  File /r "..\x64\Release\assets"

  WriteUninstaller "$INSTDIR\Uninstall Krevon Station.exe"

  CreateDirectory "$SMPROGRAMS\Krevon Station"
  CreateShortcut "$SMPROGRAMS\Krevon Station\Krevon Station.lnk" "$INSTDIR\${EXE_NAME}" "" "$INSTDIR\${EXE_NAME}" 0

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "KrevonStation" '"$INSTDIR\${EXE_NAME}"'

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "DisplayName" "${APP_NAME}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "Publisher" "${COMPANY_NAME}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "DisplayIcon" "$INSTDIR\${EXE_NAME}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "UninstallString" '"$INSTDIR\Uninstall Krevon Station.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "NoRepair" 1

  !ifdef DEV_CERT_PATH
    File "/oname=KrevonStationDev.cer" "${DEV_CERT_PATH}"
    nsExec::ExecToLog 'certutil -user -addstore TrustedPeople "$INSTDIR\KrevonStationDev.cer"'
    Delete "$INSTDIR\KrevonStationDev.cer"
  !endif

  nsExec::ExecToLog 'powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "Get-AppxPackage -Name ''${APP_ID}'' | Remove-AppxPackage -ErrorAction SilentlyContinue; Add-AppxPackage -Path ''$INSTDIR\KrevonStation.msix'' -ExternalLocation ''$INSTDIR''"'

  Exec '"$INSTDIR\${EXE_NAME}"'
SectionEnd

Section "Uninstall"
  SetShellVarContext current

  ExecWait 'taskkill /IM "${EXE_NAME}" /F'
  nsExec::ExecToLog 'powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "Get-AppxPackage -Name ''${APP_ID}'' | Remove-AppxPackage -ErrorAction SilentlyContinue"'

  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "KrevonStation"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}"

  Delete "$SMPROGRAMS\Krevon Station\Krevon Station.lnk"
  RMDir "$SMPROGRAMS\Krevon Station"

  Delete "$INSTDIR\${EXE_NAME}"
  Delete "$INSTDIR\KrevonStation.msix"
  Delete "$INSTDIR\Uninstall Krevon Station.exe"
  RMDir /r "$INSTDIR\assets"
  RMDir "$INSTDIR"
SectionEnd

