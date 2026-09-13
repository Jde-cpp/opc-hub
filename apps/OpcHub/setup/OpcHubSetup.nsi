; Jde OpcHub - Windows installer.  Build with build-setup.ps1 beside this file (or makensis /D... directly); README.md has the
; installed layout, what each install mode does, and what uninstall leaves behind.
;
; Two install modes (MultiUser.nsh):  All users - Program Files, the selected products registered as Windows services,
; administrator rights;  Current user - %LOCALAPPDATA%\Programs, run from Start Menu shortcuts, no administrator rights.
; Three components:  the OPC Hub (required), the OPC UA Server, the Web UI files for IIS.  The database is sqlite - one file
; per product under %ProgramData%\Jde-Cpp\<Product>, created on the first start (-sync).
;
; The service registration is the exe's own (`-install`, libs/fwk/src/process/process.cpp): this script never writes an
; ImagePath, it passes the settings/include/sync arguments and the exe composes the quoted command line.

Unicode true
SetCompressor /SOLID lzma

;--------------------------------------------------------------------------------------------------------------------------
; Inputs - each overridable with makensis /DNAME=value (build-setup.ps1 sets them all).
;--------------------------------------------------------------------------------------------------------------------------
!ifndef BUILD_DIR
	!define BUILD_DIR "R:\clang++\opc-hub\release" ;the release build tree: bin\<Target>\<Target>.exe + dlls, bin\Jde.DB.Sqlite*.dll, bin\sqlite3.dll
!endif
!define SRC_DIR "${__FILEDIR__}\..\..\.." ;apps\OpcHub\setup -> the repo root
!ifndef WEB_DIST
	!define WEB_DIST "${SRC_DIR}\web\opc\my-workspace\dist\my-workspace\browser"
!endif
!ifndef UA_NODE_SETS
	!define UA_NODE_SETS "C:\Users\duffyj\source\repos\libs\UA-Nodeset" ;OPCFoundation/UA-Nodeset clone - DI/IA for the OpcServer
!endif
!ifndef VC_REDIST
	!define VC_REDIST "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Redist\MSVC\v145\vc_redist.x64.exe"
!endif
!ifdef SIGN_SCRIPT
	;build-setup.ps1 -Sign.  The uninstaller is generated at install time from a stub makensis builds here, so this hook is the
	;only place it can be signed; the payload and the installer are signed by build-setup.ps1 around makensis.  SIGN_HOST is the
	;PowerShell that ran build-setup.ps1 - the one the ArtifactSigning module is installed for; the certificate settings reach
	;sign.ps1 through the environment, the only channel this hook has (sign.ps1's header).
	!uninstfinalize '"${SIGN_HOST}" -NoProfile -ExecutionPolicy Bypass -File "${SIGN_SCRIPT}" "%1"'
!endif
!ifndef VERSION
	!define VERSION "dev"
!endif
!ifndef VI_VERSION
	!define VI_VERSION "0.0.0.0" ;VIProductVersion wants 4 numbers; build-setup.ps1 derives them from the git describe
!endif
!ifndef OUT_DIR
	!define OUT_DIR "${__FILEDIR__}"
!endif
!define BIN "${BUILD_DIR}\bin"
!define PRODUCT "Jde OpcHub"
!define COMPANY "Jde-Cpp" ;the install dir name and the data root's - matches Process::CompanyName().
!define REG_UNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\Jde-Cpp-OpcHub"
!define REG_RUN "Software\Microsoft\Windows\CurrentVersion\Run"
!define HUB_SETTINGS "apps\OpcHub\config\Opc.Hub.jsonnet" ;under the config mirror - see README.md
!define SERVER_SETTINGS "apps\OpcServer\config\Opc.Server.Install.jsonnet"

;--------------------------------------------------------------------------------------------------------------------------
; MultiUser - the defines must precede the include.  Highest: a standard user gets no UAC prompt and the current-user mode
; only; an administrator gets the prompt and both modes.  The registry key lets a reinstall/uninstall find the mode and dir.
;--------------------------------------------------------------------------------------------------------------------------
!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE ;/AllUsers | /CurrentUser
!define MULTIUSER_USE_PROGRAMFILES64
!define MULTIUSER_INSTALLMODE_INSTDIR "${COMPANY}" ;$PROGRAMFILES64\Jde-Cpp or $LOCALAPPDATA\Programs\Jde-Cpp
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "${REG_UNINST}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME "InstallLocation"
!define MULTIUSER_INSTALLMODE_FUNCTION ModeChanged
!include "MultiUser.nsh"
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "Sections.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

Name "${PRODUCT}"
OutFile "${OUT_DIR}\OpcHubSetup-${VERSION}.exe"
BrandingText "${PRODUCT} ${VERSION}"
VIProductVersion "${VI_VERSION}"
VIFileVersion "${VI_VERSION}"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "CompanyName" "${COMPANY}"
VIAddVersionKey "FileDescription" "${PRODUCT} setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "MIT license"

Var DataDir    ;%ProgramData%\Jde-Cpp - both modes; the apps hardcode it (Process::ProgramDataFolder, paths-common.libsonnet)
Var ConfigDir  ;$DataDir\config - the settings mirror
Var FinishText

;--------------------------------------------------------------------------------------------------------------------------
; Pages
;--------------------------------------------------------------------------------------------------------------------------
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRC_DIR}\LICENSE"
!define MULTIUSER_INSTALLMODEPAGE_TEXT_TOP "Choose how ${PRODUCT} runs.$\r$\n$\r$\nAll users installs under Program Files and registers the selected products as Windows services - administrator rights are required.$\r$\n$\r$\nCurrent user installs under your profile and runs them from Start Menu shortcuts - no administrator rights."
!define MULTIUSER_INSTALLMODEPAGE_TEXT_ALLUSERS "All users - Windows services (administrator)"
!define MULTIUSER_INSTALLMODEPAGE_TEXT_CURRENTUSER "Current user - Start Menu shortcuts (no administrator)"
!define MULTIUSER_PAGE_CUSTOMFUNCTION_LEAVE ModePageLeave
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TEXT "$FinishText"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

;--------------------------------------------------------------------------------------------------------------------------
; Helpers
;--------------------------------------------------------------------------------------------------------------------------
; The data root must be writable:  a standard user can create %ProgramData%\Jde-Cpp and owns it, but not one an
; administrator's all-users install created.  -> $0: 1 writable, 0 not.
Function CheckDataDir
	ClearErrors
	CreateDirectory "$DataDir"
	FileOpen $1 "$DataDir\.write-test" w
	${If} ${Errors}
		StrCpy $0 0
	${Else}
		FileClose $1
		Delete "$DataDir\.write-test"
		StrCpy $0 1
	${EndIf}
FunctionEnd

; Stop a service and let its exe deregister itself; the exe's own -uninstall throws on a missing service, so the result
; is ignored - this is the reinstall path (CreateService fails on ERROR_SERVICE_EXISTS) and the uninstaller's.
!macro StopAndRemove svc exe settings
	nsExec::ExecToLog 'net stop "${svc}"'
	Pop $0
	${If} ${FileExists} "${exe}"
		nsExec::ExecToLog '"${exe}" -uninstall -settings=${settings} -include=args/install'
		Pop $0
	${EndIf}
!macroend

; The service exists after -install?  The exe exits through a Trace exception on success, so `sc query` is the signal.
!macro RequireService svc
	nsExec::ExecToStack 'sc query "${svc}"'
	Pop $0
	Pop $1
	${If} $0 != 0
		MessageBox MB_OK|MB_ICONSTOP "Registering the ${svc} service failed - see the details above." /SD IDOK
		Abort "Service registration failed"
	${EndIf}
!macroend

;--------------------------------------------------------------------------------------------------------------------------
; Components
;--------------------------------------------------------------------------------------------------------------------------
Section "OPC Hub (Jde.OpcHub)" SEC_HUB
	SectionIn RO
	Call CheckDataDir
	${If} $0 == 0
		MessageBox MB_OK|MB_ICONSTOP "$DataDir is not writable by you.  It was created by an all-users install - choose All users, or ask an administrator." /SD IDOK
		Abort "Data dir not writable"
	${EndIf}
	;binaries - the exe's dir carries its dlls; the sqlite driver and the proc MODULEs come from the bin root
	SetOutPath "$INSTDIR\OpcHub"
	File "${BIN}\Jde.Opc.Hub\Jde.Opc.Hub.exe"
	File "${BIN}\Jde.Opc.Hub\*.dll"
	File "${BIN}\Jde.DB.Sqlite.dll"
	File "${BIN}\sqlite3.dll"
	File "${BIN}\Jde.DB.Sqlite.AppServer.dll"
	File "${BIN}\Jde.DB.Sqlite.OpcGateway.dll"
	;settings mirror - the hub config imports the AppServer's and the gateway's by repo-relative path, and the gateway's
	;introspection files by Settings::Directory()-relative path, so the repo layout is kept
	SetOutPath "$ConfigDir\apps\OpcHub\config"
	File "${SRC_DIR}\apps\OpcHub\config\Opc.Hub.jsonnet"
	SetOutPath "$ConfigDir\apps\OpcHub\config\args\install"
	File "${SRC_DIR}\apps\OpcHub\config\args\install\args.libsonnet"
	SetOutPath "$ConfigDir\apps\AppServer\config"
	File "${SRC_DIR}\apps\AppServer\config\App.Server.jsonnet"
	SetOutPath "$ConfigDir\apps\OpcGateway\config"
	File "${SRC_DIR}\apps\OpcGateway\config\Opc.Gateway.jsonnet"
	SetOutPath "$ConfigDir\apps\OpcGateway\config\introspection"
	File "${SRC_DIR}\apps\OpcGateway\config\introspection\*.jsonnet"
	SetOutPath "$ConfigDir\libs\db\config"
	File "${SRC_DIR}\libs\db\config\paths-common.libsonnet"
	;the product's data dir - meta/sql flat, where args/install points (common-meta from libs/db: the copies beside the
	;app metas in the repo are configure-time links)
	SetOutPath "$DataDir\OpcHub"
	File "${SRC_DIR}\libs\access\config\access-meta.jsonnet"
	File "${SRC_DIR}\libs\access\config\access-ql.jsonnet"
	File "${SRC_DIR}\apps\AppServer\config\app-meta.jsonnet"
	File "${SRC_DIR}\apps\OpcGateway\config\opcGateway-meta.jsonnet"
	File "${SRC_DIR}\libs\db\config\common-meta.libsonnet"
	;sql\ is installer-owned - recreated, so a seed an older version shipped cannot linger.  Not the gateway's
	;access-opcGateway.mutation: its createRole( permissionRights:[…] ) shape is not one the seed applies - the roles go in as
	;access.roles below, the pass the hub runs once the access server is up (appStartup.cpp; the .mutation pass runs before it).
	RMDir /r "$DataDir\OpcHub\sql"
	SetOutPath "$DataDir\OpcHub\sql"
	File /oname=access.mutation "${SRC_DIR}\libs\access\config\release.mutation" ;<schema>*.mutation - the release seed, not the dev one
	File /oname=access.roles "${SRC_DIR}\libs\access\config\release.roles" ;<schema>*.roles - the roles (Viewer … Owner), applied after the access server is configured
	File "${SRC_DIR}\apps\AppServer\config\app.mutation"
	File "${SRC_DIR}\libs\access\config\sql\sqlite\*.sql" ;<schema>_*.sql - the sqlite views; the procs are compiled into the MODULEs
	File "${SRC_DIR}\apps\AppServer\config\sql\sqlite\*.sql"
	File "${SRC_DIR}\apps\OpcGateway\config\sql\sqlite\*.sql"
SectionEnd

Section /o "OPC UA Server (Jde.OpcServer)" SEC_OPCSERVER
	SetOutPath "$INSTDIR\OpcServer"
	File "${BIN}\Jde.Opc.Server\Jde.Opc.Server.exe"
	File "${BIN}\Jde.Opc.Server\*.dll"
	File "${BIN}\Jde.DB.Sqlite.dll"
	File "${BIN}\sqlite3.dll"
	SetOutPath "$ConfigDir\apps\OpcServer\config"
	File "${SRC_DIR}\apps\OpcServer\config\Opc.Server.jsonnet"
	File "${SRC_DIR}\apps\OpcServer\config\Opc.Server.Install.jsonnet"
	SetOutPath "$ConfigDir\apps\OpcServer\config\args\install"
	File "${SRC_DIR}\apps\OpcServer\config\args\install\args.libsonnet"
	SetOutPath "$ConfigDir\apps\OpcServer\config\pubsub"
	File "${SRC_DIR}\apps\OpcServer\config\pubsub\pumps.libsonnet"
	SetOutPath "$DataDir\OpcServer"
	File "${SRC_DIR}\libs\access\config\access-meta.jsonnet"
	File "${SRC_DIR}\libs\access\config\access-ql.jsonnet"
	File "${SRC_DIR}\libs\db\config\common-meta.libsonnet"
	File "${SRC_DIR}\apps\OpcServer\config\opcServer-meta.jsonnet"
	RMDir /r "$DataDir\OpcServer\nodesets" ;installer-owned, recreated
	SetOutPath "$DataDir\OpcServer\nodesets"
	File "${UA_NODE_SETS}\DI\Opc.Ua.Di.NodeSet2.xml"
	File "${UA_NODE_SETS}\IA\Opc.Ua.IA.NodeSet2.xml"
	File "${UA_NODE_SETS}\IA\Opc.Ua.IA.NodeSet2.examples.xml"
	File "${SRC_DIR}\apps\OpcServer\config\nodesets\pumps.NodeSet2.xml"
	;the hub's seeds for this component (reviews/install-issues.md #1): the Web UI's Google provider - the fresh install's login,
	;by ruling no username is seeded - and this server as the default connection with its provider row.  <schema>*.mutation,
	;applied by the hub's -sync inside the schema sync; the underscore names sort after access.mutation, whose provider type 7
	;(OpcServer) they reference.  Only with this component - a hub without it has no login path, by decision.
	SetOutPath "$DataDir\OpcHub\sql"
	File /oname=access_google.mutation "${SRC_DIR}\libs\access\config\release-google.mutation"
	File /oname=access_opcServer.mutation "${SRC_DIR}\libs\access\config\release-opcServer.mutation"
	File /oname=gateway_opcServer.mutation "${SRC_DIR}\apps\OpcGateway\config\release-opcServer.mutation"
SectionEnd

!ifndef SKIP_WEB
Section "Web UI files (for IIS)" SEC_WEB
	SetOutPath "$INSTDIR\Web"
	File /r "${WEB_DIST}\*.*"
	File "${SRC_DIR}\web\opc\scripts\web.config"
SectionEnd
!endif

;current-user mode only - ModeChanged hides it otherwise
Section /o "Start at logon" SEC_AUTOSTART
	${If} $MultiUser.InstallMode == "CurrentUser"
		WriteRegStr HKCU "${REG_RUN}" "Jde.OpcHub" '"$INSTDIR\OpcHub\Jde.Opc.Hub.exe" -c -settings=$ConfigDir\${HUB_SETTINGS} -include=args/install -sync'
		${If} ${SectionIsSelected} ${SEC_OPCSERVER}
			WriteRegStr HKCU "${REG_RUN}" "Jde.OpcServer" '"$INSTDIR\OpcServer\Jde.Opc.Server.exe" -c -settings=$ConfigDir\${SERVER_SETTINGS} -include=args/install -sync'
		${EndIf}
	${EndIf}
SectionEnd

;--------------------------------------------------------------------------------------------------------------------------
; Hidden steps
;--------------------------------------------------------------------------------------------------------------------------
;the exes import the v14 runtime (msvcp140, vcruntime140_1, msvcp140_atomic_wait) of the toolset that built them - VS 2026's
;14.51, on CI (win2025-build.yml) and the dev box alike.  Microsoft's rule is a redistributable at least as new as the
;toolset, so the gate is 14.50 and VC_REDIST defaults to the v145 one (Add/Remove Programs names it "Microsoft Visual C++
;v14 Redistributable (x64) - 14.5x"; https://aka.ms/vs/18/release/vc_redist.x64.exe).  The 14.44 redist happened to export
;every symbol the exes import (checked 09-12), but only by luck.
Section -VCRedist
	ReadRegDWORD $0 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Installed"
	ReadRegDWORD $1 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Major"
	ReadRegDWORD $2 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Minor"
	${If} $0 == 1
	${AndIf} $1 >= 14
	${AndIf} $2 >= 50
		DetailPrint "Visual C++ runtime $1.$2 present"
	${ElseIf} $MultiUser.InstallMode == "AllUsers"
!if /FileExists "${VC_REDIST}"
		DetailPrint "Installing the Visual C++ v14 x64 runtime (14.51)..."
		SetOutPath "$TEMP"
		File "${VC_REDIST}"
		ExecWait '"$TEMP\vc_redist.x64.exe" /install /quiet /norestart' $0
		Delete "$TEMP\vc_redist.x64.exe"
		${If} $0 != 0
		${AndIf} $0 != 3010
			MessageBox MB_OK|MB_ICONEXCLAMATION "The Visual C++ runtime installer returned $0.  Install the Microsoft Visual C++ v14 x64 Redistributable, 14.50 or later, before starting ${PRODUCT}." /SD IDOK
		${EndIf}
!else
	!warning "VC_REDIST not found - the installer will not bundle the Visual C++ runtime"
		MessageBox MB_OK|MB_ICONEXCLAMATION "The Microsoft Visual C++ v14 x64 Redistributable, 14.50 or later, is not installed.  Install it (vc_redist.x64.exe, https://aka.ms/vs/18/release/vc_redist.x64.exe) before starting ${PRODUCT}." /SD IDOK
!endif
	${Else}
		MessageBox MB_OK|MB_ICONEXCLAMATION "The Microsoft Visual C++ v14 x64 Redistributable, 14.50 or later, is not installed, and a current-user install cannot add it.  Install it (vc_redist.x64.exe, https://aka.ms/vs/18/release/vc_redist.x64.exe) before starting ${PRODUCT}." /SD IDOK
	${EndIf}
SectionEnd

Section -Services
	${If} $MultiUser.InstallMode == "AllUsers"
		;a split AppServer/OpcGateway pair shares port 1967 with the hub - never beside it
		nsExec::ExecToStack 'sc query Jde.AppServer'
		Pop $0
		Pop $1
		${If} $0 == 0
			nsExec::ExecToLog 'net stop Jde.AppServer'
			Pop $0
			nsExec::ExecToLog 'net stop Jde.OpcGateway'
			Pop $0
			MessageBox MB_OK|MB_ICONEXCLAMATION "The split Jde.AppServer / Jde.OpcGateway services are registered on this machine and share port 1967 with the hub.  They have been stopped; deregister them (each exe's -uninstall) before starting Jde.OpcHub." /SD IDOK
		${EndIf}
		!insertmacro StopAndRemove "Jde.OpcHub" "$INSTDIR\OpcHub\Jde.Opc.Hub.exe" "$ConfigDir\${HUB_SETTINGS}"
		DetailPrint "Registering the Jde.OpcHub service"
		nsExec::ExecToLog '"$INSTDIR\OpcHub\Jde.Opc.Hub.exe" -install -settings=$ConfigDir\${HUB_SETTINGS} -include=args/install -sync'
		Pop $0
		!insertmacro RequireService "Jde.OpcHub"
		nsExec::ExecToLog 'sc config Jde.OpcHub start= auto'
		Pop $0
		${If} ${SectionIsSelected} ${SEC_OPCSERVER}
			!insertmacro StopAndRemove "Jde.OpcServer" "$INSTDIR\OpcServer\Jde.Opc.Server.exe" "$ConfigDir\${SERVER_SETTINGS}"
			DetailPrint "Registering the Jde.OpcServer service"
			nsExec::ExecToLog '"$INSTDIR\OpcServer\Jde.Opc.Server.exe" -install -settings=$ConfigDir\${SERVER_SETTINGS} -include=args/install -sync'
			Pop $0
			!insertmacro RequireService "Jde.OpcServer"
			nsExec::ExecToLog 'sc config Jde.OpcServer start= auto depend= Jde.OpcHub'
			Pop $0
		${EndIf}
		StrCpy $FinishText "${PRODUCT} is installed as Windows services.$\r$\n$\r$\nStart them with:$\r$\n    net start Jde.OpcHub$\r$\n    net start Jde.OpcServer$\r$\n$\r$\nThe sqlite database is created on the first start under $DataDir."
	${Else}
		;no services without administrator rights: shortcuts, the exes run in a console window (-c)
		CreateDirectory "$SMPROGRAMS\${COMPANY}"
		SetOutPath "$INSTDIR\OpcHub" ;the shortcut's working dir
		CreateShortcut "$SMPROGRAMS\${COMPANY}\Jde OpcHub.lnk" "$INSTDIR\OpcHub\Jde.Opc.Hub.exe" "-c -settings=$ConfigDir\${HUB_SETTINGS} -include=args/install -sync"
		${If} ${SectionIsSelected} ${SEC_OPCSERVER}
			SetOutPath "$INSTDIR\OpcServer"
			CreateShortcut "$SMPROGRAMS\${COMPANY}\Jde OpcServer.lnk" "$INSTDIR\OpcServer\Jde.Opc.Server.exe" "-c -settings=$ConfigDir\${SERVER_SETTINGS} -include=args/install -sync"
		${EndIf}
		CreateShortcut "$SMPROGRAMS\${COMPANY}\Uninstall ${PRODUCT}.lnk" "$INSTDIR\Uninstall.exe" "/CurrentUser"
		SetOutPath "$INSTDIR"
		StrCpy $FinishText "${PRODUCT} is installed for your account.$\r$\n$\r$\nStart it from the Start Menu folder '${COMPANY}' - each product runs in its own console window.$\r$\n$\r$\nThe sqlite database is created on the first start under $DataDir."
	${EndIf}
SectionEnd

Section -Post
	WriteUninstaller "$INSTDIR\Uninstall.exe"
	${If} $MultiUser.InstallMode == "AllUsers"
		StrCpy $1 "/AllUsers"
		StrCpy $2 "${PRODUCT}"
	${Else}
		StrCpy $1 "/CurrentUser"
		StrCpy $2 "${PRODUCT} (current user)"
	${EndIf}
	;SHCTX: HKLM for all users, HKCU for the current user - MultiUser set the context with the mode
	WriteRegStr SHCTX "${REG_UNINST}" "DisplayName" "$2"
	WriteRegStr SHCTX "${REG_UNINST}" "DisplayVersion" "${VERSION}"
	WriteRegStr SHCTX "${REG_UNINST}" "Publisher" "${COMPANY}"
	WriteRegStr SHCTX "${REG_UNINST}" "InstallLocation" "$INSTDIR"
	WriteRegStr SHCTX "${REG_UNINST}" "DisplayIcon" "$INSTDIR\OpcHub\Jde.Opc.Hub.exe"
	WriteRegStr SHCTX "${REG_UNINST}" "UninstallString" '"$INSTDIR\Uninstall.exe" $1'
	WriteRegStr SHCTX "${REG_UNINST}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" $1 /S'
	WriteRegDWORD SHCTX "${REG_UNINST}" "NoModify" 1
	WriteRegDWORD SHCTX "${REG_UNINST}" "NoRepair" 1
	${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
	IntFmt $0 "0x%08X" $0
	WriteRegDWORD SHCTX "${REG_UNINST}" "EstimatedSize" "$0"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_HUB} "The AppServer and the OPC gateway in one process (service Jde.OpcHub, port 1967): the REST/websocket API the Web UI talks to.  Required."
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_OPCSERVER} "Jde's own OPC UA server (service Jde.OpcServer, opc.tcp 4840, http 1970) with the DI/IA nodesets and the pumps demo address space; seeded as the hub's default connection, with the Web UI's Google login.  Optional - the hub can connect to any OPC UA server."
!ifndef SKIP_WEB
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_WEB} "The Angular site, copied to <install dir>\Web for an IIS site to serve (IIS itself is configured by hand - see the README)."
!endif
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_AUTOSTART} "Current-user installs only: start the selected products at logon (HKCU Run)."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------------------------------------------------------------------------------------------------
; Functions
;--------------------------------------------------------------------------------------------------------------------------
;MultiUser calls this on every mode switch, .onInit's included
Function ModeChanged
	${If} $MultiUser.InstallMode == "AllUsers"
		SectionSetText ${SEC_AUTOSTART} "" ;hidden
		!insertmacro UnselectSection ${SEC_AUTOSTART}
	${Else}
		SectionSetText ${SEC_AUTOSTART} "Start at logon"
	${EndIf}
FunctionEnd

;the install-mode page's leave: a current-user install into a data root it cannot write stays on the page
Function ModePageLeave
	${If} $MultiUser.InstallMode == "CurrentUser"
		Call CheckDataDir
		${If} $0 == 0
			MessageBox MB_OK|MB_ICONEXCLAMATION "$DataDir exists but is not writable by you - it was created by an all-users install.  Choose All users, or ask an administrator." /SD IDOK
			Abort
		${EndIf}
	${EndIf}
FunctionEnd

Function .onInit
	${IfNot} ${RunningX64}
		MessageBox MB_OK|MB_ICONSTOP "${PRODUCT} requires 64-bit Windows." /SD IDOK
		Abort
	${EndIf}
	SetRegView 64
	ReadEnvStr $DataDir "ProgramData" ;not $APPDATA - MultiUser's current-user context turns that into the roaming profile
	StrCpy $DataDir "$DataDir\${COMPANY}"
	StrCpy $ConfigDir "$DataDir\config"
	!insertmacro MULTIUSER_INIT
	;/OpcServer selects the OPC UA Server component - how a silent install (/S), which has no components page, gets it
	${GetParameters} $0
	ClearErrors
	${GetOptions} $0 "/OpcServer" $1
	${IfNot} ${Errors}
		!insertmacro SelectSection ${SEC_OPCSERVER}
	${EndIf}
	;an earlier install (either hive): offer its uninstaller first
	ReadRegStr $0 HKLM "${REG_UNINST}" "UninstallString"
	ReadRegStr $1 HKLM "${REG_UNINST}" "InstallLocation"
	${If} $0 == ""
		ReadRegStr $0 HKCU "${REG_UNINST}" "UninstallString"
		ReadRegStr $1 HKCU "${REG_UNINST}" "InstallLocation"
	${EndIf}
	${If} $0 != ""
		MessageBox MB_YESNO|MB_ICONQUESTION "${PRODUCT} is already installed in $1.$\r$\n$\r$\nUninstall it first?  (No installs over it; the data under $DataDir is kept either way.)" /SD IDNO IDNO +2
		ExecWait '$0 /S _?=$1'
	${EndIf}
FunctionEnd

;--------------------------------------------------------------------------------------------------------------------------
; Uninstall
;--------------------------------------------------------------------------------------------------------------------------
Function un.onInit
	SetRegView 64
	ReadEnvStr $DataDir "ProgramData"
	StrCpy $DataDir "$DataDir\${COMPANY}"
	StrCpy $ConfigDir "$DataDir\config"
	!insertmacro MULTIUSER_UNINIT
FunctionEnd

Section "Uninstall"
	${If} $MultiUser.InstallMode == "AllUsers"
		!insertmacro StopAndRemove "Jde.OpcServer" "$INSTDIR\OpcServer\Jde.Opc.Server.exe" "$ConfigDir\${SERVER_SETTINGS}"
		!insertmacro StopAndRemove "Jde.OpcHub" "$INSTDIR\OpcHub\Jde.Opc.Hub.exe" "$ConfigDir\${HUB_SETTINGS}"
	${Else}
		nsExec::ExecToLog 'taskkill /F /IM Jde.Opc.Server.exe /IM Jde.Opc.Hub.exe'
		Pop $0
		DeleteRegValue HKCU "${REG_RUN}" "Jde.OpcHub"
		DeleteRegValue HKCU "${REG_RUN}" "Jde.OpcServer"
		Delete "$SMPROGRAMS\${COMPANY}\*.lnk"
		RMDir "$SMPROGRAMS\${COMPANY}"
	${EndIf}
	RMDir /r "$INSTDIR\OpcHub"
	RMDir /r "$INSTDIR\OpcServer"
	RMDir /r "$INSTDIR\Web"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"
	RMDir /r "$ConfigDir"
	;the products' data dirs: what the installer put there goes; what the products created (the .db, ssl\, logs) stays
	Delete "$DataDir\OpcHub\*.jsonnet"
	Delete "$DataDir\OpcHub\common-meta.libsonnet"
	RMDir /r "$DataDir\OpcHub\sql"
	RMDir "$DataDir\OpcHub"
	Delete "$DataDir\OpcServer\*.jsonnet"
	Delete "$DataDir\OpcServer\common-meta.libsonnet"
	RMDir /r "$DataDir\OpcServer\nodesets"
	RMDir "$DataDir\OpcServer"
	RMDir "$DataDir"
	DeleteRegKey SHCTX "${REG_UNINST}"
SectionEnd
