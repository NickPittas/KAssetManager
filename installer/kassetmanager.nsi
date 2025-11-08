; KAsset Manager - NSIS Installer Script
; Requires NSIS 3.0 or later
; Download from: https://nsis.sourceforge.io/

;--------------------------------
; Includes

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"

;--------------------------------
; General

; Name and file
Name "KAsset Manager"
OutFile "..\dist\KAssetManager-Setup-1.1.0.exe"

; Default installation folder (user-writable location)
InstallDir "$LOCALAPPDATA\KAsset Manager"

; Get installation folder from registry if available
InstallDirRegKey HKCU "Software\KAssetManager" "InstallDir"

; Request user-level privileges (no admin required, allows drag-and-drop)
RequestExecutionLevel user

; Compression
SetCompressor /SOLID lzma

; Version Information
VIProductVersion "1.1.0.0"
VIAddVersionKey "ProductName" "KAsset Manager"
VIAddVersionKey "CompanyName" "Your Company Name"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2024"
VIAddVersionKey "FileDescription" "KAsset Manager Installer"
VIAddVersionKey "FileVersion" "1.1.0.0"
VIAddVersionKey "ProductVersion" "1.1.0.0"

;--------------------------------
; Interface Settings

!define MUI_ABORTWARNING
!define MUI_ICON "..\icon.ico"
!define MUI_UNICON "..\icon.ico"

; Welcome page
!define MUI_WELCOMEPAGE_TITLE "Welcome to KAsset Manager Setup"
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of KAsset Manager.$\r$\n$\r$\nKAsset Manager is a professional asset management software for organizing, tagging, and previewing digital media files.$\r$\n$\r$\nClick Next to continue."

; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\kassetmanagerqt.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch KAsset Manager"
!define MUI_FINISHPAGE_LINK "Visit the project website"
!define MUI_FINISHPAGE_LINK_LOCATION "https://github.com/yourusername/KAssetManager"

;--------------------------------
; Pages

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages

!insertmacro MUI_LANGUAGE "English"

;--------------------------------
; Installer Sections

Section "KAsset Manager" SecMain

  SetOutPath "$INSTDIR"
  
  ; Copy all files from portable distribution
  File /r "..\dist\portable\*.*"

  ; Store installation folder (HKCU for user-level install)
  WriteRegStr HKCU "Software\KAssetManager" "InstallDir" "$INSTDIR"

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  ; Create Start Menu shortcuts
  CreateDirectory "$SMPROGRAMS\KAsset Manager"
  CreateShortcut "$SMPROGRAMS\KAsset Manager\KAsset Manager.lnk" "$INSTDIR\bin\kassetmanagerqt.exe" "" "$INSTDIR\bin\kassetmanagerqt.exe" 0
  CreateShortcut "$SMPROGRAMS\KAsset Manager\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0

  ; Create Desktop shortcut (optional)
  CreateShortcut "$DESKTOP\KAsset Manager.lnk" "$INSTDIR\bin\kassetmanagerqt.exe" "" "$INSTDIR\bin\kassetmanagerqt.exe" 0

  ; Add to Add/Remove Programs (HKCU for user-level install)
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KAssetManager" "DisplayName" "KAsset Manager"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KAssetManager" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KAssetManager" "DisplayIcon" "$INSTDIR\bin\kassetmanagerqt.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KAssetManager" "Publisher" "Your Company Name"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KAssetManager" "DisplayVersion" "1.1.0"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KAssetManager" "URLInfoAbout" "https://github.com/yourusername/KAssetManager"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KAssetManager" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KAssetManager" "NoRepair" 1

  ; Calculate and store installation size
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KAssetManager" "EstimatedSize" "$0"

SectionEnd

;--------------------------------
; Uninstaller Section

Section "Uninstall"

  ; Remove files and directories (but preserve data folder initially)
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\plugins"
  RMDir /r "$INSTDIR\translations"
  Delete "$INSTDIR\Uninstall.exe"
  
  ; Remove Start Menu shortcuts
  Delete "$SMPROGRAMS\KAsset Manager\KAsset Manager.lnk"
  Delete "$SMPROGRAMS\KAsset Manager\Uninstall.lnk"
  RMDir "$SMPROGRAMS\KAsset Manager"
  
  ; Remove Desktop shortcut
  Delete "$DESKTOP\KAsset Manager.lnk"
  
  ; Remove registry keys (HKCU for user-level install)
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\KAssetManager"
  DeleteRegKey HKCU "Software\KAssetManager"
  
  ; Ask user if they want to delete user data (only if not silent)
  IfSilent SkipDataPrompt
  
  MessageBox MB_YESNO|MB_ICONQUESTION "Do you want to delete your database and thumbnails?$\r$\n$\r$\nThis will permanently delete all your asset organization data.$\r$\n$\r$\nClick Yes to delete everything, or No to keep your data." IDYES DeleteUserData IDNO KeepUserData
  
  DeleteUserData:
    ; Remove user data directory
    RMDir /r "$INSTDIR\data"
    MessageBox MB_OK "All data has been removed."
    Goto Done
  
  KeepUserData:
    MessageBox MB_OK "Your data has been preserved in:$\r$\n$INSTDIR\data$\r$\n$\r$\nYou can manually delete this folder later if needed."
    Goto Done
  
  SkipDataPrompt:
    ; Silent uninstall during upgrade: preserve data by default
    Goto Done
  
  Done:
  
  ; Remove installation directory if empty
  RMDir "$INSTDIR"

SectionEnd

;--------------------------------
; Installer Functions

Function .onInit
  ; Check if running on 64-bit Windows
  ${If} ${RunningX64}
    ; Good, we're on 64-bit
  ${Else}
    MessageBox MB_OK|MB_ICONSTOP "KAsset Manager requires 64-bit Windows. Installation cannot continue."
    Abort
  ${EndIf}

  ; Check if already installed (HKCU for user-level install)
  ReadRegStr $0 HKCU "Software\KAssetManager" "InstallDir"
  ${If} $0 != ""
    MessageBox MB_YESNO|MB_ICONQUESTION "KAsset Manager is already installed in:$\r$\n$0$\r$\n$\r$\nDo you want to uninstall the previous version first?" IDYES Uninstall IDNO Continue

    Uninstall:
      ExecWait '"$0\Uninstall.exe" /S _?=$0'
      Delete "$0\Uninstall.exe"
      RMDir "$0"

    Continue:
  ${EndIf}
FunctionEnd

Function .onInstSuccess
  MessageBox MB_OK "KAsset Manager has been successfully installed!$\r$\n$\r$\nYou can now launch the application from the Start Menu or Desktop shortcut."
FunctionEnd

;--------------------------------
; Uninstaller Functions

Function un.onInit
  MessageBox MB_YESNO|MB_ICONQUESTION "Are you sure you want to uninstall KAsset Manager?" IDYES +2
  Abort
FunctionEnd

Function un.onUninstSuccess
  MessageBox MB_OK "KAsset Manager has been successfully uninstalled."
FunctionEnd

;--------------------------------
; Section Descriptions

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Installs KAsset Manager application files, Qt libraries, and dependencies."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

