; Installateur Windows d'Amal Studio (NSIS 3, Unicode)
; Amal Studio est basé sur OBS Studio et distribué sous licence GNU GPL v2 ou ultérieure.
;
; Compilation (faite automatiquement par GitHub Actions) :
;   makensis /DAPP_VERSION=0.1.0 /DSOURCE_DIR=<dossier install> /DOUTFILE=<setup.exe> amal-studio.nsi

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel admin
ManifestDPIAware true

!ifndef APP_VERSION
  !define APP_VERSION "0.3.1"
!endif
!ifndef SOURCE_DIR
  !error "SOURCE_DIR doit pointer vers le dossier install produit par la compilation"
!endif
!ifndef OUTFILE
  !define OUTFILE "Amal-Studio-${APP_VERSION}-Installer-x64.exe"
!endif

!define APP_NAME "Amal Studio"
!define APP_EXE "bin\64bit\obs64.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\AmalStudio"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\Amal Studio"
InstallDirRegKey HKLM "Software\Amal Studio" "InstallDir"
BrandingText "Amal Studio ${APP_VERSION} - basé sur OBS Studio"

!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!define MUI_ICON "..\..\..\frontend\cmake\windows\obs-studio.ico"
!define MUI_UNICON "..\..\..\frontend\cmake\windows\obs-studio.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Lancer Amal Studio"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\..\..\COPYING"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "French"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "Amal Studio nécessite Windows 10 ou 11 en version 64 bits."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Function un.onInit
  SetRegView 64
FunctionEnd

Section "Amal Studio" SecMain
  SectionIn RO

  ; Fermer une instance éventuellement ouverte avant de remplacer les fichiers
  nsExec::Exec 'taskkill /IM obs64.exe /F'

  SetOutPath "$INSTDIR"
  File /r "${SOURCE_DIR}\*.*"

  ; Bibliothèques d'exécution Microsoft Visual C++ (nécessaires au logiciel)
  !ifdef VCREDIST
    SetOutPath "$PLUGINSDIR"
    File "/oname=vc_redist.x64.exe" "${VCREDIST}"
    DetailPrint "Installation des bibliothèques Microsoft Visual C++..."
    ExecWait '"$PLUGINSDIR\vc_redist.x64.exe" /install /quiet /norestart'
  !endif

  ; Raccourcis : le dossier de travail doit être celui de l'exécutable
  SetOutPath "$INSTDIR\bin\64bit"
  CreateDirectory "$SMPROGRAMS\Amal Studio"
  CreateShortcut "$SMPROGRAMS\Amal Studio\Amal Studio.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
  CreateShortcut "$SMPROGRAMS\Amal Studio\Désinstaller Amal Studio.lnk" "$INSTDIR\uninstall.exe"
  CreateShortcut "$DESKTOP\Amal Studio.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0

  ; Carte graphique hautes performances (NVIDIA/AMD dédiée) pour Amal Studio,
  ; équivalent du réglage Windows Paramètres > Écran > Graphiques.
  WriteRegStr HKCU "Software\Microsoft\DirectX\UserGpuPreferences" "$INSTDIR\${APP_EXE}" "GpuPreference=2;"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Amal Studio" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "Amal Studio"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "Amal Studio"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/amal-studio/amal-studio"
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  nsExec::Exec 'taskkill /IM obs64.exe /F'
  Delete "$DESKTOP\Amal Studio.lnk"
  RMDir /r "$SMPROGRAMS\Amal Studio"
  ; Seuls les fichiers du programme sont supprimés.
  ; Les projets et réglages de l'utilisateur (%APPDATA%\amal-studio) sont conservés.
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\data"
  RMDir /r "$INSTDIR\obs-plugins"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegValue HKCU "Software\Microsoft\DirectX\UserGpuPreferences" "$INSTDIR\${APP_EXE}"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  DeleteRegKey HKLM "Software\Amal Studio"
SectionEnd
