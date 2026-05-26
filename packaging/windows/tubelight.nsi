; SPDX-License-Identifier: MIT
; Copyright (c) 2026 GS-RUN
;
; Tubelight Windows installer (NSIS).
; Build with: makensis packaging/windows/tubelight.nsi
;
; Expects ../../build/windows-vcpkg/Release/{tubelight.exe,
; tubelight_backend.dll, tubelight_inject.exe, VkLayer_tubelight_overlay.dll,
; VkLayer_tubelight_overlay.json} to exist.

!define APP_NAME       "Tubelight"
!define APP_VERSION    "0.1.0-alpha"
!define APP_PUBLISHER  "GS-RUN"
!define APP_URL        "https://github.com/gs-run/tubelight"
!define APP_EXE        "tubelight.exe"
!define UNINST_KEY     "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

Name             "${APP_NAME} ${APP_VERSION}"
OutFile          "tubelight-${APP_VERSION}-win64-setup.exe"
InstallDir       "$PROGRAMFILES64\${APP_NAME}"
InstallDirRegKey HKLM "Software\${APP_NAME}" "InstallDir"
RequestExecutionLevel admin

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Tubelight" SEC_MAIN
    SetOutPath "$INSTDIR"
    File "..\..\build\windows-vcpkg\Release\tubelight.exe"
    File "..\..\build\windows-vcpkg\Release\tubelight_backend.dll"
    File "..\..\build\windows-vcpkg\Release\tubelight_inject.exe"

    SetOutPath "$INSTDIR\shaders"
    File /r "..\..\shaders\*.frag"
    File /r "..\..\shaders\*.md"

    SetOutPath "$INSTDIR\profiles\crts"
    File /r "..\..\profiles\crts\*.json"
    SetOutPath "$INSTDIR\profiles\signals"
    File /r "..\..\profiles\signals\*.json"

    SetOutPath "$INSTDIR\schemas"
    File "..\..\schemas\crt_profile.schema.json"
    File "..\..\schemas\signal_profile.schema.json"

    ; Vulkan layer (optional — only if it was built)
    SetOutPath "$INSTDIR\vulkan_layer"
    File /nonfatal "..\..\build\windows-vcpkg\Release\VkLayer_tubelight_overlay.dll"
    File /nonfatal "..\..\build\windows-vcpkg\Release\VkLayer_tubelight_overlay.json"

    SetOutPath "$INSTDIR"
    File "..\..\LICENSE"
    File "..\..\README.md"
    File "..\..\CHANGELOG.md"

    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Registry
    WriteRegStr HKLM "Software\${APP_NAME}"      "InstallDir"     "$INSTDIR"
    WriteRegStr HKLM "Software\${APP_NAME}"      "Version"        "${APP_VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}"             "DisplayName"    "${APP_NAME}"
    WriteRegStr HKLM "${UNINST_KEY}"             "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "${UNINST_KEY}"             "Publisher"      "${APP_PUBLISHER}"
    WriteRegStr HKLM "${UNINST_KEY}"             "URLInfoAbout"   "${APP_URL}"
    WriteRegStr HKLM "${UNINST_KEY}"             "UninstallString" '"$INSTDIR\uninstall.exe"'

    ; Start Menu
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut  "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortcut  "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"   "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\tubelight.exe"
    Delete "$INSTDIR\tubelight_backend.dll"
    Delete "$INSTDIR\tubelight_inject.exe"
    RMDir /r "$INSTDIR\shaders"
    RMDir /r "$INSTDIR\profiles"
    RMDir /r "$INSTDIR\schemas"
    RMDir /r "$INSTDIR\vulkan_layer"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\CHANGELOG.md"
    Delete "$INSTDIR\uninstall.exe"
    RMDir  "$INSTDIR"

    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"

    DeleteRegKey HKLM "Software\${APP_NAME}"
    DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
