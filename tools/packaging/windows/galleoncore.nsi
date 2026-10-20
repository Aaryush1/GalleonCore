; SPDX-FileCopyrightText: 2026 Paul Woisard
; SPDX-License-Identifier: GPL-3.0-or-later
Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!ifndef VERSION
!error "VERSION must be supplied"
!endif
!ifndef STAGING_DIR
!error "STAGING_DIR must be supplied"
!endif
!ifndef OUTPUT_FILE
!error "OUTPUT_FILE must be supplied"
!endif
!ifndef UNINSTALL_MANIFEST
!error "UNINSTALL_MANIFEST must be supplied"
!endif

!ifndef PAYLOAD_MANIFEST
!error "PAYLOAD_MANIFEST must be supplied"
!endif

Name "GalleonCore ${VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\GalleonCore"
InstallDirRegKey HKCU "Software\GalleonCore\Installer" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetFont "Segoe UI" 9
BrandingText "GalleonCore - Galleon Bosun"
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "GalleonCore"
VIAddVersionKey /LANG=1033 "FileDescription" "GalleonCore Setup"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "2026 Paul Woisard"

!define MUI_ICON "${__FILEDIR__}\assets\galleon-bosun.ico"
!define MUI_UNICON "${__FILEDIR__}\assets\galleon-bosun.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "${__FILEDIR__}\assets\header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${__FILEDIR__}\assets\welcome.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${__FILEDIR__}\assets\welcome.bmp"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\galleon-bosun.exe"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${__FILEDIR__}\..\..\..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"
LangString CoreLabel ${LANG_ENGLISH} "GalleonCore (required)"
LangString CoreLabel ${LANG_FRENCH} "GalleonCore (obligatoire)"
LangString DesktopLabel ${LANG_ENGLISH} "Desktop shortcut"
LangString DesktopLabel ${LANG_FRENCH} "Raccourci sur le bureau"
LangString StartupLabel ${LANG_ENGLISH} "Start galleond when signing in"
LangString StartupLabel ${LANG_FRENCH} "Démarrer galleond à l'ouverture de session"
LangString StopError ${LANG_ENGLISH} "GalleonCore could not stop cleanly. Close Bosun and galleond, then retry."
LangString StopError ${LANG_FRENCH} "GalleonCore n'a pas pu s'arrêter proprement. Fermez Bosun et galleond, puis réessayez."


Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "GalleonCore requires Windows x64."
        Abort
    ${EndIf}
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_ICONSTOP "GalleonCore requires Windows 10 or later."
        Abort
    ${EndIf}
    SetShellVarContext current
FunctionEnd
Function un.onInit
    SetShellVarContext current
FunctionEnd

!macro StopApplications PREFIX MODE
Function ${PREFIX}StopApplications
    InitPluginsDir
    File /oname=$PLUGINSDIR\stop-galleon.ps1 "${__FILEDIR__}\stop-galleon.ps1"
    ClearErrors
    ExecWait '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\stop-galleon.ps1" -InstallDir "$INSTDIR" -Mode ${MODE}' $0
    IfErrors shutdown_failed
    ${If} $0 == 0
        Return
    ${EndIf}
shutdown_failed:
    IfSilent +2
    MessageBox MB_ICONSTOP "$(StopError)"
    SetErrorLevel 1
    Abort
FunctionEnd
!macroend
!insertmacro StopApplications "" "Prepare"
!insertmacro StopApplications "un." "Stop"

Section "$(CoreLabel)" Core
    SectionIn RO
    Call StopApplications
    SetOutPath "$INSTDIR"
    File /r "${STAGING_DIR}\*"
    File /oname=install-payload.txt "${PAYLOAD_MANIFEST}"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\GalleonCore"
    CreateShortCut "$SMPROGRAMS\GalleonCore\Galleon Bosun.lnk" "$INSTDIR\bin\galleon-bosun.exe"
    CreateShortCut "$SMPROGRAMS\GalleonCore\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "Software\GalleonCore\Installer" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GalleonCore" "DisplayName" "GalleonCore"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GalleonCore" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GalleonCore" "Publisher" "Paul Woisard"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GalleonCore" "DisplayIcon" "$INSTDIR\bin\galleon-bosun.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GalleonCore" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GalleonCore" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GalleonCore" "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GalleonCore" "NoRepair" 1
SectionEnd
Section /o "$(DesktopLabel)" Desktop
    CreateShortCut "$DESKTOP\Galleon Bosun.lnk" "$INSTDIR\bin\galleon-bosun.exe"
SectionEnd
Section /o "$(StartupLabel)" Startup
    CreateShortCut "$SMSTARTUP\GalleonCore.lnk" "$INSTDIR\bin\galleond.exe"
SectionEnd
Section "Uninstall"
    Call un.StopApplications
    !include "${UNINSTALL_MANIFEST}"
    Delete "$SMPROGRAMS\GalleonCore\Galleon Bosun.lnk"
    Delete "$SMPROGRAMS\GalleonCore\Uninstall.lnk"
    RMDir "$SMPROGRAMS\GalleonCore"
    Delete "$DESKTOP\Galleon Bosun.lnk"
    Delete "$SMSTARTUP\GalleonCore.lnk"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GalleonCore"
    DeleteRegKey HKCU "Software\GalleonCore\Installer"
    Delete "$INSTDIR\install-payload.txt"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
