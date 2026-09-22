; Jde OpcHub - Windows installer.  Build with build-setup.ps1 beside this file (or makensis /D... directly); README.md has the
; installed layout, what each install mode does, and what uninstall leaves behind.
;
; Two install modes (MultiUser.nsh):  All users - Program Files, the selected products registered as Windows services,
; administrator rights;  Current user - %LOCALAPPDATA%\Programs, run from Start Menu shortcuts, no administrator rights.
; Three components:  the OPC Hub (required), the OPC UA Server, the Web UI (served by the hub).  The database is sqlite - one file
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
!ifndef VC_REDIST_VERSION
	!define VC_REDIST_VERSION "14.5x" ;build-setup.ps1 reads it off the bundled file (reviews/m4-closing.md #21)
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
Var StartNow   ;/Start - a silent install starts the products at the end, as the finish page's box does
Var RuntimeOld ;current-user mode: the VC++ runtime is still below the build's after the offer to install it (#14) - the finish page says so
Var UserClosed ;current-user mode: a running hub/OpcServer of this user's was closed to reinstall over it (#37) - the finish page says to start it again

;--------------------------------------------------------------------------------------------------------------------------
; Pages
;--------------------------------------------------------------------------------------------------------------------------
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRC_DIR}\LICENSE"
;MultiUser draws this in a 42-dialog-unit label - five lines at the page's width - and three paragraphs overran it, the
;"Current user" one drawn behind the radio buttons (reviews/install-issues.md #9).  Two sentences, one per mode.
!define MULTIUSER_INSTALLMODEPAGE_TEXT_TOP "All users: under Program Files, as Windows services - administrator rights are required.$\r$\nCurrent user: under your profile, from Start Menu shortcuts - no administrator rights."
!define MULTIUSER_INSTALLMODEPAGE_TEXT_ALLUSERS "All users - Windows services (administrator)"
!define MULTIUSER_INSTALLMODEPAGE_TEXT_CURRENTUSER "Current user - Start Menu shortcuts (no administrator)"
!define MULTIUSER_PAGE_CUSTOMFUNCTION_LEAVE ModePageLeave
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!define MUI_PAGE_CUSTOMFUNCTION_SHOW ComponentsShow ;a standard user is told the mode was chosen for them (#15)
!insertmacro MUI_PAGE_COMPONENTS
;the page names the program folder only, and a profile install writes outside the profile too - the data root is
;%ProgramData%\Jde-Cpp in both modes (the 09-15 reruns' location rows).  DirText expands $DataDir at run time.
!define MUI_DIRECTORYPAGE_TEXT_TOP "Setup installs ${PRODUCT}'s programs in the folder below.  Its data - the settings, the sqlite database, keys and certificates, logs - goes under $DataDir, in both install modes.  $_CLICK"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
;the "Start now" box (reviews/install-issues.md #5): the services in the all-users mode - this installer is the elevated console
;the finish text used to send a standard user to - or the products' console windows in the current-user mode.  A silent
;install has no finish page: /Start does the same (.onInstSuccess).
!define MUI_FINISHPAGE_RUN ""
!define MUI_FINISHPAGE_RUN_TEXT "Start ${PRODUCT} now"
!define MUI_FINISHPAGE_RUN_FUNCTION StartProducts
!define MUI_FINISHPAGE_TEXT "$FinishText"
;The text control is 40 dialog units - five lines of ~48 characters - once the "Start now" box takes its rows, and what
;overflows is clipped without a sign: the Web UI url fell off it (reviews/install-issues.md #10).  TEXT_LARGE makes it 60u,
;seven lines, the texts are written to that, and the url is the link MUI draws on its own row under the box.  The restart
;form (below) has no link, so its text carries the url in its first lines.
!define MUI_FINISHPAGE_TEXT_LARGE
!define MUI_FINISHPAGE_LINK "Web UI: http://localhost:1967/"
!define MUI_FINISHPAGE_LINK_LOCATION "http://localhost:1967/"
;With the reboot flag set (-VCRedist: the runtime's installer returned 3010) MUI shows this text and restart-now/later
;instead - no "Start now" box, the products may not load until the restart.  The reboot case is all-users only (the
;runtime is installed in that mode alone), so "the services start with Windows" is always true of it.
!define MUI_FINISHPAGE_TEXT_REBOOT "Windows must be restarted to finish installing the Visual C++ runtime ${PRODUCT} runs on; its services start with Windows after the restart.$\r$\n$\r$\nWeb UI: http://localhost:1967/ after the restart."
!define MUI_FINISHPAGE_TEXT_REBOOTNOW "Restart now"
!define MUI_FINISHPAGE_TEXT_REBOOTLATER "I will restart Windows later"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

;--------------------------------------------------------------------------------------------------------------------------
; Helpers
;--------------------------------------------------------------------------------------------------------------------------
; Can this install write the data root it is about to fill?  -> $0: 1 yes, 0 no - it belongs to another install.
; What is probed is a file the install will overwrite - the hub's config, else its .db - opened for writing and left as it
; was;  only a fresh tree gets a new file.  Whether the folder admits a new file said nothing:  %ProgramData% hands Users
; the right to add files to every folder beneath it, so that probe passed on every tree - another account's current-user
; install's, an all-users install's - and the Files then failed on each file already there (reviews/m4-closing.md #4).
; A current-user install never shares an all-users install's root, which is SYSTEM's and the Administrators' alone (#2)
; and which an elevated run would pass:  TakeDataDir's mark, .all-users, refuses it whoever runs Setup.
Function CheckDataDir
	StrCpy $0 0
	${If} $MultiUser.InstallMode == "CurrentUser"
	${AndIf} ${FileExists} "$DataDir\.all-users"
		Return
	${EndIf}
	StrCpy $1 "$ConfigDir\${HUB_SETTINGS}" ;the first file SEC_HUB overwrites
	${IfNot} ${FileExists} $1
		StrCpy $1 "$DataDir\OpcHub\OpcHub.db" ;an uninstall keeps it (README.md), the config goes
	${EndIf}
	${IfNot} ${FileExists} $1
		CreateDirectory "$DataDir"
		StrCpy $1 "$DataDir\.write-test"
	${EndIf}
	ClearErrors
	FileOpen $2 $1 a ;append:  an existing file is opened for writing, and nothing is written
	${IfNot} ${Errors}
		FileClose $2
		StrCpy $0 1
	${EndIf}
	Delete "$DataDir\.write-test"
FunctionEnd

; Stop a service and let its exe deregister itself; the exe's own -uninstall throws on a missing service, so the result
; is ignored - this is the reinstall path (CreateService fails on ERROR_SERVICE_EXISTS) and the uninstaller's.
; /y:  Jde.OpcServer depends on Jde.OpcHub (`sc config ... depend=`, -Services), and `net stop` of a service with a running
; dependent asks "continue? (Y/N)" - with no console to answer it stops nothing, and -uninstall below only deletes the
; registration (Process::Uninstall is DeleteService alone), so the hub would run on, its image locked, through the install.
; ${exe} is relative to the program folder.  The one under $INSTDIR is not always the registered one:  the directory page
; is pre-filled with the previous install's folder, and changeable - so a reinstall into another folder found no exe,
; skipped the deregistration, and -install then met the old service (reviews/m4-closing.md #7).  So the previous install's
; exe (MultiUser read its InstallLocation), else - a registration with no exe of ours left to remove it - sc delete.
!macro StopAndRemove svc exe settings
	nsExec::ExecToLog 'net stop "${svc}" /y'
	Pop $0
	${If} ${FileExists} "$INSTDIR\${exe}"
		nsExec::ExecToLog '"$INSTDIR\${exe}" -uninstall -settings=${settings} -include=args/install'
		Pop $0
	${ElseIf} $MultiUser.InstDir != ""
	${AndIf} ${FileExists} "$MultiUser.InstDir\${exe}"
		nsExec::ExecToLog '"$MultiUser.InstDir\${exe}" -uninstall -settings=${settings} -include=args/install'
		Pop $0
	${Else}
		nsExec::ExecToStack 'sc query "${svc}"' ;guarded:  on a first install there is nothing to delete, and nothing to print
		Pop $0
		Pop $1
		${If} $0 == 0
			nsExec::ExecToLog 'sc delete "${svc}"'
			Pop $0
		${EndIf}
	${EndIf}
!macroend

; Did -install register the service?  Its exit code first - the caller's $0:  the exe exits 0 only through its Trace
; "successfully installed." throw (process.cpp, Process::ExitException) and nonzero for every failure, "Service already
; exists." and "CreateService failed - 1072" (marked for deletion:  a Services console holding it) among them.  This used to
; read `sc query` alone, which an earlier install's registration passes - and the install "succeeded" on the previous
; release's exe (reviews/m4-closing.md #7).  `sc query` stays, as the second word.
!macro RequireService svc
	StrCpy $1 $0
	nsExec::ExecToStack 'sc query "${svc}"'
	Pop $0
	Pop $2
	${If} $1 != 0
	${OrIf} $0 != 0
		MessageBox MB_OK|MB_ICONSTOP "Registering the ${svc} service failed (-install returned $1) - see the details above.  If the Services console is open, close it and run Setup again." /SD IDOK
		Abort "Service registration failed"
	${EndIf}
!macroend

; Inbound firewall rules for the all-users install: a service never gets the "allow this app?" prompt an
; interactive program does, so without these the hub answers only its own machine (reviews/install-issues.md #11).
; "any" by ruling: Windows puts a new network in Public unless someone says otherwise - the clean-machine VM is - and a
; private,domain rule silently would not apply there, which is worse than no rule, since the details pane still reads
; "Allowing".  The price is that 1967 (plain http with a login on it, by ruling) answers on an untrusted network too.
; One word here narrows it again.
!define FIREWALL_PROFILES "any"
; A netsh failure is printed, never fatal: the products run either way, they are simply local-only.
!macro OpenFirewallPort name port exe
	nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${name}"' ;idempotent - a reinstall must not stack rules
	Pop $0
	DetailPrint "Allowing inbound TCP ${port} (${name}) on ${FIREWALL_PROFILES}"
	nsExec::ExecToLog 'netsh advfirewall firewall add rule name="${name}" dir=in action=allow protocol=TCP localport=${port} profile=${FIREWALL_PROFILES} program="${exe}"'
	Pop $0
	${If} $0 != 0
		DetailPrint "  netsh returned $0 - open TCP ${port} by hand to reach this machine from another"
	${EndIf}
!macroend

!macro CloseFirewallPort name
	nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${name}"'
	Pop $0
!macroend

; Is this user running ${exe}?  -> $0:  0 yes, 1 no, anything else *not known*.  tasklist piped through find rather than a
; plugin:  the exit code is the whole answer (find returns 0 for a match and 1 for none), and the uninstaller already
; reaches for taskkill for the same job.  Three answers, not two (reviews/m2-closing.md #14):  nsExec pushes the word
; `error` when it cannot launch the command and `timeout` when it gives up, and reading everything but 0 as "gone" - the one
; place this file turned "non-zero is failure" round - made a probe that never ran the same as a product that had closed.  The
; USERNAME filter is what keeps an all-users *service* - Local Service's, which this mode may not touch - out of the answer.
; One spelling for the probe and for every kill that acts on it (reviews/m2-closing.md #13):  the kills carried no filter, so
; what was *found* was this user's and what was *ended* was every process of that image on the machine - and with
; MULTIUSER_EXECUTIONLEVEL Highest an administrator's run is elevated in either mode, so that could reach a service's.
; Always through `cmd /c`:  %USERNAME% is cmd's to expand - handed straight to nsExec it is a literal, the filter matches
; nobody, and a kill so filtered ends nothing at all.
!define USER_FILTER '/FI "USERNAME eq %USERNAME%"'
!macro UserProcRunning exe
	nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq ${exe}" ${USER_FILTER} /NH | find /I "${exe}"'
	Pop $0
	Pop $1
!macroend

; A service's process still there?  -> $0 == 0 when yes.  UserProcRunning turned round:  session 0 is where services run and
; no console window does, and unlike the service account's name it reads the same on every language of Windows.
!macro ServiceProcRunning exe
	nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq ${exe}" /FI "SESSION eq 0" /NH | find /I "${exe}"'
	Pop $0
	Pop $1
!macroend

; `net stop` returns when the service *reports* stopped, which is a moment before its process has gone and let go of its
; image - and a File that meets a locked exe is an Abort/Retry/Ignore box, or under /S a file skipped in silence.  Twenty
; seconds, then the install stops rather than carry on over an exe it cannot replace (reviews/m2-closing.md #4) - and the
; uninstall, rather than delete around a mapped image and leave the program folder behind with no uninstaller (m4-closing #17).
; `done`/`again`:  what the files cannot be (replaced, removed) and what to run again (Setup, the uninstaller).
!macro WaitServiceGone exe svc done again
	StrCpy $2 0
	${Do}
		!insertmacro ServiceProcRunning "${exe}"
		${If} $0 == 1
			${ExitDo}
		${ElseIf} $0 != 0 ;the probe did not answer.  Not fatal here, unlike CloseUserProduct:  net stop has already reported the service stopped, and this wait only covers the moment between that and the process going.
			DetailPrint "  could not check that ${svc}'s process has gone (tasklist answered $0) - continuing"
			${ExitDo}
		${EndIf}
		IntOp $2 $2 + 1
		${If} $2 >= 40
			MessageBox MB_OK|MB_ICONSTOP "The ${svc} service did not stop, so its files cannot be ${done}.  Stop it (net stop ${svc}, or the Services console) and run ${again} again." /SD IDOK
			Abort "${svc} did not stop"
		${EndIf}
		Sleep 500
	${Loop}
!macroend

; Close a product this user is running, and wait for it to go.  Windows will not let an installer overwrite a running
; image, and - the case #37 found - a hub that keeps running through the install never applies the seeds a newly added
; component just wrote, because those land on a `-sync` start:  the pages look exactly as they did before the component
; was added.  This is the current-user counterpart of StopAndRemove, which does the same for the services.
; taskkill without /F first:  these run as console windows (-c), so they get a close and shut down as they would on
; Ctrl+C;  /F only if ten seconds pass.  -> $UserClosed 1 when it was closed - and only then.
; One loop, and the only way out of it into the File commands is the probe saying *gone* (reviews/m2-closing.md #14).  The
; forced kill used to be followed by a second's sleep and an exit with no look at all, so a kill that was refused - a copy
; this user started elevated - or an image not yet released read as a success:  $UserClosed was set, the finish page said
; the running copy had been closed, and the Files went ahead over a locked exe - an Abort/Retry/Ignore box, or under /S
; nothing, and new settings and seeds over the old binary.  Now the forced kill gets five seconds of the same probe, and
; after that Setup stops and says what to close.
!macro CloseUserProduct exe label
	!insertmacro UserProcRunning "${exe}"
	${If} $0 == 0
		DetailPrint "Closing ${label} - it is running from an earlier install"
		nsExec::ExecToLog 'cmd /c taskkill /IM "${exe}" ${USER_FILTER}'
		Pop $0
		StrCpy $2 0
		${Do}
			Sleep 500
			!insertmacro UserProcRunning "${exe}"
			${If} $0 == 1 ;gone - find's "no match", the one answer that says so.  `error`/`timeout` are not it:  keep asking.
				${ExitDo}
			${EndIf}
			IntOp $2 $2 + 1
			${If} $2 == 20
				DetailPrint "  ${label} did not close - ending it"
				nsExec::ExecToLog 'cmd /c taskkill /F /IM "${exe}" ${USER_FILTER}'
				Pop $0
			${ElseIf} $2 >= 30
				MessageBox MB_OK|MB_ICONSTOP "${label} is still running and could not be closed, so its files cannot be replaced.  Close its console window - or end ${exe} in Task Manager - and run Setup again." /SD IDOK
				Abort "${label} could not be closed"
			${EndIf}
		${Loop}
		StrCpy $UserClosed 1
	${EndIf}
!macroend

; #37:  in the current-user mode there is no service to stop, so a reinstall walks into a running product.  Its binaries
; cannot be replaced while it runs, and a hub that survives the install never applies the seeds a newly added component
; just wrote - `<schema>*.mutation` and `*.roles` are read by a `-sync` start, so the walk that added the OPC UA Server
; component over an existing install got the files on disk, the server started, and a hub still showing no Google
; provider, no OpcServer connection and no "OPC Server Instance" role.  Asked, not assumed:  these are console windows
; the user opened.  /SD IDOK so a silent install closes them without a prompt, as it must.
Function CloseRunningUserProducts
	!insertmacro UserProcRunning "Jde.Opc.Hub.exe"
	StrCpy $3 $0
	!insertmacro UserProcRunning "Jde.Opc.Server.exe"
	${If} $3 == 1
	${AndIf} $0 == 1
		DetailPrint "No ${PRODUCT} of yours is running - nothing to close" ;said either way, so an install log shows the check ran (#37)
	${ElseIf} $3 != 0
	${AndIf} $0 != 0
		;neither is known to be running, and a probe did not answer (#14) - tasklist or cmd refused to this user, by policy say.
		;Not a reason to refuse the install:  a locked exe still stops the Files below with its own box.  But not "nothing to close".
		DetailPrint "Could not check whether ${PRODUCT} is running (tasklist answered $3 / $0) - if it is, close it:  Setup cannot replace a running copy's files"
	${Else}
		MessageBox MB_OKCANCEL|MB_ICONINFORMATION "${PRODUCT} is already running from an earlier install.  Setup has to close it: its files cannot be replaced while it runs, and a component added now is only picked up when it next starts.$\r$\n$\r$\nThe finish page's 'Start now' box brings it back." /SD IDOK IDOK closeThem
		Abort "Close ${PRODUCT} and run Setup again"
		closeThem:
		;the server first:  it holds a session on the hub, and stopping it after would leave the hub logging a lost client
		!insertmacro CloseUserProduct "Jde.Opc.Server.exe" "Jde.OpcServer"
		!insertmacro CloseUserProduct "Jde.Opc.Hub.exe" "Jde.OpcHub"
	${EndIf}
FunctionEnd

;--------------------------------------------------------------------------------------------------------------------------
; Components
;--------------------------------------------------------------------------------------------------------------------------
;the component names without the service names - the components list is too narrow for "OPC UA Server (Jde.OpcServer)"
;and clipped it (reviews/install-issues.md #17); the descriptions name the services.
Section "OPC Hub" SEC_HUB
	SectionIn RO
	Call CheckDataDir
	${If} $0 == 0
		MessageBox MB_OK|MB_ICONSTOP "$DataDir belongs to another install - an all-users one, or another account's - and this one cannot write to it.  Choose All users (as an administrator), or remove that install first." /SD IDOK
		Abort "Data dir not writable"
	${EndIf}
	${If} $MultiUser.InstallMode == "CurrentUser" ;before the first File, in both modes - what is running holds the images the Files below replace
		Call CloseRunningUserProducts ;see the function (#37)
	${Else}
		Call TakeDataDir ;before anything is written under it, and before the services stop - a refusal leaves them running (reviews/m4-closing.md #2)
		Call StopRunningServices ;see the function (reviews/m2-closing.md #4)
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
	SetOutPath "$ConfigDir\apps\OpcHub\config\args\install-user"
	File "${SRC_DIR}\apps\OpcHub\config\args\install-user\args.libsonnet" ;the current-user mode's -include: args/install plus a loopback listen address (#16)
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

Section /o "OPC UA Server" SEC_OPCSERVER
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
	SetOutPath "$ConfigDir\apps\OpcServer\config\args\install-user"
	File "${SRC_DIR}\apps\OpcServer\config\args\install-user\args.libsonnet" ;as the hub's: the current-user mode's -include (#16)
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
	;(OpcServer) they reference.  Only with this component - a hub without it has no *seeded* login (#36: it is not without a
	;login path, since adding a server connection creates one; what it lacks is one that needs no setup).
	SetOutPath "$DataDir\OpcHub\sql"
	File /oname=access_google.mutation "${SRC_DIR}\libs\access\config\release-google.mutation"
	File /oname=access_opcServer.mutation "${SRC_DIR}\libs\access\config\release-opcServer.mutation"
	File /oname=gateway_opcServer.mutation "${SRC_DIR}\apps\OpcGateway\config\release-opcServer.mutation"
	;the machine role for this server (reviews/install-issues.md #25): an "OPC Server Instance" role with Administer on
	;opc.install nodeIds, so granting OpcServer.web is one tick on its Roles tab, not a hand-written mutation.  A *.roles file,
	;applied by the same post-configure pass as access.roles above; only with this component, since only then does the server exist.
	File /oname=access_opcServer.roles "${SRC_DIR}\libs\access\config\release-opcServer.roles"
SectionEnd

!ifndef SKIP_WEB
Section "Web UI" SEC_WEB
	SetOutPath "$INSTDIR\Web"
	File /r /x *.map "${WEB_DIST}\*.*" ;not the source maps - ~7 MB a browser never asks for unless devtools are open (reviews/install-issues.md, "Shipped weight")
	File "${SRC_DIR}\web\opc\scripts\web.config"
SectionEnd
!endif

;current-user mode only - ModeChanged hides it otherwise
Section /o "Start at logon" SEC_AUTOSTART
	${If} $MultiUser.InstallMode == "CurrentUser"
		WriteRegStr HKCU "${REG_RUN}" "Jde.OpcHub" '"$INSTDIR\OpcHub\Jde.Opc.Hub.exe" -c -settings=$ConfigDir\${HUB_SETTINGS} -include=args/install-user -sync'
		${If} ${SectionIsSelected} ${SEC_OPCSERVER}
			WriteRegStr HKCU "${REG_RUN}" "Jde.OpcServer" '"$INSTDIR\OpcServer\Jde.Opc.Server.exe" -c -settings=$ConfigDir\${SERVER_SETTINGS} -include=args/install-user -sync'
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
	${Else}
!if /FileExists "${VC_REDIST}"
		;One File, whichever mode runs it.  NSIS adds every File in a section to the size the components and location pages show,
		;whichever branch executes, so the second copy the current-user branch used to carry made the pages count the runtime
		;twice - 103.8 MB for an install that leaves neither copy behind (reviews/install-issues.md, the 09-15 rerun's 5:44 row).
		;Current user (#14): the runtime is machine-wide, so this mode cannot add it in silence - but vc_redist asks UAC for
		;itself, so a user with administrator credentials to hand can let it in from here, and only one with none at all is
		;left with the download.  The products ran a whole walk on 14.40, so the wording is a risk, not a verdict, and the
		;install goes on either way; while the runtime is still old the finish page repeats the warning.  Asked before the
		;extraction, so a No leaves nothing to clean up.
		${If} $MultiUser.InstallMode != "AllUsers"
			MessageBox MB_YESNO|MB_ICONQUESTION "The Microsoft Visual C++ v14 x64 Redistributable this build expects (14.50 or later) is not installed; ${PRODUCT} may fail to start without it.$\r$\n$\r$\nInstall it now?  It is machine-wide, so Windows will ask for an administrator.$\r$\n$\r$\n(No: it can be installed later from https://aka.ms/vs/18/release/vc_redist.x64.exe)" /SD IDNO IDNO runtimeDeclined
		${EndIf}
		SetOutPath "$TEMP"
		File "${VC_REDIST}"
		${If} $MultiUser.InstallMode == "AllUsers"
			DetailPrint "Installing the Visual C++ v14 x64 runtime (${VC_REDIST_VERSION})..."
			ExecWait '"$TEMP\vc_redist.x64.exe" /install /quiet /norestart' $0
			${If} $0 == 3010
				;the runtime's files were in use (an older msvcp140 loaded by some process): Windows swaps them in at the next
				;restart, and until then the exes may load the old ones and fail.  Used to be accepted in silence
				;(reviews/install-issues.md, Notes "VC++ runtime"); the reboot flag turns the finish page into its restart form
				;(MUI_FINISHPAGE_TEXT_REBOOT above) and a silent install exits 3010 (.onInstSuccess).
				DetailPrint "The Visual C++ runtime needs Windows restarted to finish - ${PRODUCT} starts after it"
				SetRebootFlag true
			${ElseIf} $0 != 0
				MessageBox MB_OK|MB_ICONEXCLAMATION "The Visual C++ runtime installer returned $0.  Install the Microsoft Visual C++ v14 x64 Redistributable, 14.50 or later, before starting ${PRODUCT}." /SD IDOK
			${EndIf}
		${Else}
			DetailPrint "Installing the Visual C++ v14 x64 runtime (${VC_REDIST_VERSION}) - Windows asks for an administrator..."
			ExecShellWait "runas" "$TEMP\vc_redist.x64.exe" "/install /passive /norestart" ;elevated by UAC; no exit code comes back through runas, so the registry says whether it landed
			ReadRegDWORD $2 HKLM "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" "Minor"
		${EndIf}
		Delete "$TEMP\vc_redist.x64.exe"
		runtimeDeclined:
!else
	!warning "VC_REDIST not found - the installer will not bundle the Visual C++ runtime"
		${If} $MultiUser.InstallMode == "AllUsers"
			MessageBox MB_OK|MB_ICONEXCLAMATION "The Microsoft Visual C++ v14 x64 Redistributable, 14.50 or later, is not installed.  Install it (vc_redist.x64.exe, https://aka.ms/vs/18/release/vc_redist.x64.exe) before starting ${PRODUCT}." /SD IDOK
		${Else}
			MessageBox MB_OK|MB_ICONEXCLAMATION "The Microsoft Visual C++ v14 x64 Redistributable this build expects (14.50 or later) is not installed; ${PRODUCT} may fail to start without it.  It is machine-wide - an administrator installs it from https://aka.ms/vs/18/release/vc_redist.x64.exe." /SD IDOK
		${EndIf}
!endif
		${If} $MultiUser.InstallMode != "AllUsers"
		${AndIf} $2 < 50
			StrCpy $RuntimeOld 1
			DetailPrint "Visual C++ runtime still below 14.50 - ${PRODUCT} may fail to start"
		${EndIf}
	${EndIf}
SectionEnd

Section -Services
	${If} $MultiUser.InstallMode == "AllUsers"
		;registration only:  an earlier install's services were stopped and deregistered before the first File
		;(StopRunningServices), so CreateService finds no ERROR_SERVICE_EXISTS and these are the new exes.
		DetailPrint "Registering the Jde.OpcHub service"
		nsExec::ExecToLog '"$INSTDIR\OpcHub\Jde.Opc.Hub.exe" -install -settings=$ConfigDir\${HUB_SETTINGS} -include=args/install -sync'
		Pop $0
		!insertmacro RequireService "Jde.OpcHub"
		nsExec::ExecToLog 'sc config Jde.OpcHub start= auto'
		Pop $0
		;the hub's port: the Web UI and the api, for a browser or a client on any other machine (#11)
		!insertmacro OpenFirewallPort "Jde OpcHub (TCP 1967)" "1967" "$INSTDIR\OpcHub\Jde.Opc.Hub.exe"
		${If} ${SectionIsSelected} ${SEC_OPCSERVER}
			DetailPrint "Registering the Jde.OpcServer service"
			nsExec::ExecToLog '"$INSTDIR\OpcServer\Jde.Opc.Server.exe" -install -settings=$ConfigDir\${SERVER_SETTINGS} -include=args/install -sync'
			Pop $0
			!insertmacro RequireService "Jde.OpcServer"
			nsExec::ExecToLog 'sc config Jde.OpcServer start= auto depend= Jde.OpcHub'
			Pop $0
			;the UA endpoint, for OPC clients elsewhere.  1970 (its http) stays closed - the hub reaches it over loopback.
			!insertmacro OpenFirewallPort "Jde OpcServer (TCP 4840)" "4840" "$INSTDIR\OpcServer\Jde.Opc.Server.exe"
		${EndIf}
		;six lines at the finish page's width (MUI_FINISHPAGE_TEXT_LARGE, seven) - the database's whereabouts are the README's.  The url
		;is the link row's alone (the 09-15 rerun saw it twice); the restart form, which has no link, keeps it in its text.
		StrCpy $FinishText "${PRODUCT} is installed as Windows services: they start when you finish (the box below), or later with net start Jde.OpcHub / Jde.OpcServer.$\r$\n$\r$\nThe Web UI is the link below, once Jde.OpcHub runs."
	${Else}
		;no services without administrator rights: shortcuts, the exes run in a console window (-c).  args/install-user, here and
		;wherever else this mode starts the products (the Run key, StartProducts): args/install plus a loopback listen address, so
		;the products answer this machine only and Windows raises no firewall prompt - one a standard user could only answer with
		;an administrator's credentials (reviews/install-issues.md #16).
		CreateDirectory "$SMPROGRAMS\${COMPANY}"
		SetOutPath "$INSTDIR\OpcHub" ;the shortcut's working dir
		CreateShortcut "$SMPROGRAMS\${COMPANY}\Jde OpcHub.lnk" "$INSTDIR\OpcHub\Jde.Opc.Hub.exe" "-c -settings=$ConfigDir\${HUB_SETTINGS} -include=args/install-user -sync"
		${If} ${SectionIsSelected} ${SEC_OPCSERVER}
			SetOutPath "$INSTDIR\OpcServer"
			CreateShortcut "$SMPROGRAMS\${COMPANY}\Jde OpcServer.lnk" "$INSTDIR\OpcServer\Jde.Opc.Server.exe" "-c -settings=$ConfigDir\${SERVER_SETTINGS} -include=args/install-user -sync"
		${EndIf}
		SetOutPath "$INSTDIR" ;before the shortcut: its working dir is the last SetOutPath, and this one used to inherit the OpcServer's (the 09-15 rerun noted it)
		CreateShortcut "$SMPROGRAMS\${COMPANY}\Uninstall ${PRODUCT}.lnk" "$INSTDIR\Uninstall.exe" "/CurrentUser"
		${If} $RuntimeOld == 1
			StrCpy $FinishText "${PRODUCT} is installed for your account.  The Visual C++ runtime is older than this build expects (14.50), so it may fail to start - https://aka.ms/vs/18/release/vc_redist.x64.exe installs it (administrator).$\r$\nThe Web UI is the link below."
		${ElseIf} $UserClosed == 1
			;#37:  the copy that was running is gone, and starting it again is the only way this install - its new files, and
			;any seeds a component added with it - takes effect.  Said here because this mode has no service to do it.
			StrCpy $FinishText "${PRODUCT} is installed for your account.  The copy that was running was closed to replace its files - start it again for this install to take effect: the box below, or the Start Menu folder '${COMPANY}'.$\r$\n$\r$\nThe Web UI is the link below, once it runs."
		${Else}
			StrCpy $FinishText "${PRODUCT} is installed for your account: it starts when you finish (the box below), or from the Start Menu folder '${COMPANY}' later.$\r$\n$\r$\nThe Web UI is the link below, once it runs."
		${EndIf}
	${EndIf}
SectionEnd

Section -Post
	;the licenses on disk, not just on the wizard's page: ours, and the notices of the third-party code inside the exes and
	;dlls (build/third-party-notices.sh) - MIT, BSD and Apache-2.0 ask for theirs to travel with the binaries
	SetOutPath "$INSTDIR"
	File /oname=LICENSE.txt "${SRC_DIR}\LICENSE"
	File "${SRC_DIR}\THIRD-PARTY-NOTICES.txt"
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
	;the description box holds ~200 characters and has no scrollbar - this one lost its last sentence (reviews/install-issues.md #9)
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_OPCSERVER} "Jde's OPC UA server (Jde.OpcServer, opc.tcp 4840): DI/IA nodesets and the pumps demo; seeded as the hub's default connection, with the Web UI's Google login.  Optional."
!ifndef SKIP_WEB
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_WEB} "The Angular site under <install dir>\Web, served by the hub at http://<host>:1967/.  IIS is not needed; web.config is included for putting the site behind it (see the README)."
!endif
	!insertmacro MUI_DESCRIPTION_TEXT ${SEC_AUTOSTART} "Current-user installs only: start the selected products at logon (HKCU Run)."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------------------------------------------------------------------------------------------------
; Functions
;--------------------------------------------------------------------------------------------------------------------------
;MultiUser calls this on every mode switch, .onInit's included
;A standard user never sees Choose Users: MultiUser skips that page for anyone without administrator privileges and picks
;the current-user mode in silence (reviews/install-issues.md #15), so the mode's explanation - written for exactly that user
;- never reaches them, and nothing says how an all-users install is reached.  Choose Components is the first page they do
;see; its top line says what was decided and how to decide otherwise.  An administrator who picked the mode saw the page.
Function ComponentsShow
	${If} $MultiUser.InstallMode == "CurrentUser"
	${AndIf} $MultiUser.Privileges != "Admin"
	${AndIf} $MultiUser.Privileges != "Power"
		SendMessage $mui.ComponentsPage.Text ${WM_SETTEXT} 0 "STR:Installing for your account - no administrator rights, so no Windows services: the products run in console windows.  For services, run Setup as administrator."
	${EndIf}
FunctionEnd

Function ModeChanged
	${If} $MultiUser.InstallMode == "AllUsers"
		SectionSetText ${SEC_AUTOSTART} "" ;hidden
		!insertmacro UnselectSection ${SEC_AUTOSTART}
	${Else}
		SectionSetText ${SEC_AUTOSTART} "Start at logon"
	${EndIf}
FunctionEnd

;reviews/m2-closing.md #4 - the all-users counterpart of CloseRunningUserProducts, and for the same reason.  These stops
;used to sit in -Services, which NSIS runs in declaration order:  after "OPC Hub" and "OPC UA Server" had already written
;the exes and dlls.  Windows holds a running image against writes, so a reinstall over live services failed the binary
;Files - an Ignore on the Abort/Retry/Ignore box, or under /S no box at all - while every unlocked file beside them, the
;settings mirror and the recreated sql\ seeds, was replaced:  new configs and new seeds, re-registered on the *previous*
;release's exe, with the new DisplayVersion in Add/Remove Programs and nothing to say so.  Here it is the old exe that
;deregisters itself, against the old settings it was installed with;  on a first install there is no exe and the macro
;skips it.  Below the sections because it reads SEC_OPCSERVER.
Function StopRunningServices
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
	;the server first, as the uninstaller does:  it depends on the hub and holds a session on it.  Only when this install
	;brings the component - one an earlier install registered and this one leaves unticked is stopped with the hub (/y)
	;and stays registered, its files untouched.
	${If} ${SectionIsSelected} ${SEC_OPCSERVER}
		!insertmacro StopAndRemove "Jde.OpcServer" "OpcServer\Jde.Opc.Server.exe" "$ConfigDir\${SERVER_SETTINGS}"
		!insertmacro WaitServiceGone "Jde.Opc.Server.exe" "Jde.OpcServer" "replaced" "Setup"
	${EndIf}
	!insertmacro StopAndRemove "Jde.OpcHub" "OpcHub\Jde.Opc.Hub.exe" "$ConfigDir\${HUB_SETTINGS}"
	!insertmacro WaitServiceGone "Jde.Opc.Hub.exe" "Jde.OpcHub" "replaced" "Setup"
FunctionEnd

;the install-mode page's leave: a current-user install into a data root it cannot write stays on the page
Function ModePageLeave
	${If} $MultiUser.InstallMode == "CurrentUser"
		Call CheckDataDir
		${If} $0 == 0
			MessageBox MB_OK|MB_ICONEXCLAMATION "$DataDir belongs to another install - an all-users one, or another account's - so a current-user install cannot use it.  Choose All users, or remove that install first." /SD IDOK
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
	;/Start starts the products at the end of a silent install - the finish page's box, which /S never shows
	ClearErrors
	${GetOptions} $0 "/Start" $1
	${IfNot} ${Errors}
		StrCpy $StartNow 1
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

;The finish page's "Start now" box, and /Start in a silent install.  All users: the services, in order - the hub first, the
;OpcServer (which depends on it) after; this installer runs elevated in that mode, which `net start` needs.  Current user: the
;same console windows the Start Menu shortcuts open, the hub given a moment to listen before the OpcServer logs in to it.
;A failed `net start` is reported here rather than swallowed - the finish page shows no log.
Function StartProducts
	${If} $MultiUser.InstallMode == "AllUsers"
		nsExec::ExecToStack 'net start Jde.OpcHub'
		Pop $0
		Pop $1
		${If} $0 != 0
			MessageBox MB_OK|MB_ICONEXCLAMATION "net start Jde.OpcHub returned $0:$\r$\n$1$\r$\nStart it from an elevated console." /SD IDOK
		${EndIf}
		${If} ${SectionIsSelected} ${SEC_OPCSERVER}
			nsExec::ExecToStack 'net start Jde.OpcServer'
			Pop $0
			Pop $1
			${If} $0 != 0
				MessageBox MB_OK|MB_ICONEXCLAMATION "net start Jde.OpcServer returned $0:$\r$\n$1$\r$\nStart it from an elevated console." /SD IDOK
			${EndIf}
		${EndIf}
	${Else}
		SetOutPath "$INSTDIR\OpcHub" ;the working dir, as the shortcut's
		Exec '"$INSTDIR\OpcHub\Jde.Opc.Hub.exe" -c -settings=$ConfigDir\${HUB_SETTINGS} -include=args/install-user -sync'
		${If} ${SectionIsSelected} ${SEC_OPCSERVER}
			;the OpcServer anchors the hub's certificate (Opc.Server.Install.jsonnet caFile), and a first start of the hub writes it
			;only after its schema sync and keys - later than the five seconds this used to wait (reviews/install-issues.md #12).
			;Wait for the file, up to a minute, then a moment for the listener.  The server retries its login on its own now,
			;so a miss here costs it a retry, not its life.
			StrCpy $2 0
			${DoUntil} ${FileExists} "$DataDir\OpcHub\ssl\certs\OpcHub.pem"
				Sleep 500
				IntOp $2 $2 + 1
				${If} $2 >= 120
					${ExitDo}
				${EndIf}
			${Loop}
			Sleep 2000
			SetOutPath "$INSTDIR\OpcServer"
			Exec '"$INSTDIR\OpcServer\Jde.Opc.Server.exe" -c -settings=$ConfigDir\${SERVER_SETTINGS} -include=args/install-user -sync'
		${EndIf}
		SetOutPath "$INSTDIR"
	${EndIf}
FunctionEnd

Function .onInstSuccess
	${If} ${RebootFlag}
		;the runtime's restart (-VCRedist): the products may not load until then, so /Start is not honoured - the services
		;come up with Windows - and a silent install reports it the way msiexec does, exit code 3010, for whatever ran it
		;to act on.  NSIS never restarts a silent install's machine by itself.
		${If} ${Silent}
			SetErrorLevel 3010
		${EndIf}
	${ElseIf} ${Silent}
	${AndIf} $StartNow == 1
		Call StartProducts
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
		!insertmacro StopAndRemove "Jde.OpcServer" "OpcServer\Jde.Opc.Server.exe" "$ConfigDir\${SERVER_SETTINGS}"
		!insertmacro WaitServiceGone "Jde.Opc.Server.exe" "Jde.OpcServer" "removed" "the uninstaller" ;the installer's wait (m4-closing #17):  its RMDir /r would skip a still-mapped image in silence
		!insertmacro StopAndRemove "Jde.OpcHub" "OpcHub\Jde.Opc.Hub.exe" "$ConfigDir\${HUB_SETTINGS}"
		!insertmacro WaitServiceGone "Jde.Opc.Hub.exe" "Jde.OpcHub" "removed" "the uninstaller"
		!insertmacro CloseFirewallPort "Jde OpcHub (TCP 1967)"
		!insertmacro CloseFirewallPort "Jde OpcServer (TCP 4840)"
	${Else}
		nsExec::ExecToLog 'cmd /c taskkill /F /IM Jde.Opc.Server.exe /IM Jde.Opc.Hub.exe ${USER_FILTER}' ;this user's alone, as the installer's close is - an all-users service of the same image is not this mode's to end (#13)
		Pop $0
		DeleteRegValue HKCU "${REG_RUN}" "Jde.OpcHub"
		DeleteRegValue HKCU "${REG_RUN}" "Jde.OpcServer"
		Delete "$SMPROGRAMS\${COMPANY}\*.lnk"
		RMDir "$SMPROGRAMS\${COMPANY}"
	${EndIf}
	RMDir /r "$INSTDIR\OpcHub"
	RMDir /r "$INSTDIR\OpcServer"
	RMDir /r "$INSTDIR\Web"
	Delete "$INSTDIR\LICENSE.txt"
	Delete "$INSTDIR\THIRD-PARTY-NOTICES.txt"
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
	Delete "$DataDir\.all-users"
	RMDir "$DataDir"
	${If} $MultiUser.InstallMode == "AllUsers"
	${AndIf} ${FileExists} "$DataDir\*.*" ;kept - the .db, ssl\, the logs - and still SYSTEM's and the Administrators' (#2):  still no current-user install's (#4)
		FileOpen $1 "$DataDir\.all-users" w
		FileClose $1
	${EndIf}
	DeleteRegKey SHCTX "${REG_UNINST}"
SectionEnd

;--------------------------------------------------------------------------------------------------------------------------
; The all-users data root - SEC_HUB's TakeDataDir (reviews/m4-closing.md #2)
;--------------------------------------------------------------------------------------------------------------------------
; An icacls step of TakeDataDir:  a failure stops the install - a data root this mode could not secure is not one to put a
; service's settings and keys in.
!macro Icacls args
	nsExec::ExecToLog 'icacls ${args} /Q'
	Pop $0
	${If} $0 != 0
		MessageBox MB_OK|MB_ICONSTOP "Setup could not secure $DataDir - icacls returned $0 (the details show the command).  Take it over as an administrator - takeown /F $\"$DataDir$\" /A /R - or move it aside, and run Setup again." /SD IDOK
		Abort "Could not secure $DataDir"
	${EndIf}
!macroend

; All users (reviews/m4-closing.md #2, #10):  the data root becomes the services', SYSTEM's and the Administrators' alone.  %ProgramData% lets
; any account create a folder in it and makes that account the folder's owner, and what is created beneath inherits Users'
; right to add files - so a standard account that made the root first (a current-user install of theirs, or a mkdir) held
; Full Control of the overlay that names the dll a service loads, and of its private key; and any account could
; drop a seed into OpcHub\sql or a certificate into a trustedCertDirs.  Nothing a standard account runs reads this tree in
; this mode - the services run as Local Service (#10), the Web UI reads the logs through the hub - so no Users entry:  that also
; keeps the keys, written in the clear under a service (README.md), and the .db from every other local account.  The root:
; owner Administrators, its DACL protected - nothing inherited from %ProgramData% - with SYSTEM and Administrators full
; control and Local Service read, which everything under it inherits, and Local Service's Modify on the product dirs it writes
; (the .db, ssl\, the logs); then every object under it owned by Administrators.  SIDs, not names, which Windows translates.
; Before that, whose is what is already there.  powershell, the one thing here that can read an owner, lists the first
; object that is not SYSTEM's, Local Service's, the Administrators' or this account's:  11 - another account's files, usually a current-user
; install of theirs (its .db, keys and settings) - asked about, since taking them over hands them to the services, and a Yes
; also resets every object's own ACL, which that account may have written;  12 - a link another account made, refused:
; the services' data would land wherever it points.  A probe that cannot run (a policy, say) is not a reason to refuse the
; install - the whole tree is reset instead, unasked.  Root first, then the tree:  once the root is protected nobody else
; can add to it, and the tree-wide owner pass catches anything added before.
Function TakeDataDir
	StrCpy $3 0 ;1 - reset every object's ACL beneath the root too, not only the root's
	nsExec::ExecToStack `powershell.exe -NoProfile -NonInteractive -Command "$$ProgressPreference='SilentlyContinue';$$ErrorActionPreference='Stop';try{$$k='S-1-5-18','S-1-5-19','S-1-5-32-544',[Security.Principal.WindowsIdentity]::GetCurrent().User.Value;$$d=Join-Path $$env:ProgramData '${COMPANY}';function t($$i){$$o=$$i.GetAccessControl('Owner').GetOwner([Security.Principal.SecurityIdentifier]);if($$k -notcontains $$o.Value){$$n=$$o.Value;try{$$n=$$o.Translate([Security.Principal.NTAccount]).Value}catch{};[Console]::Write($$n+' owns '+$$i.FullName);if($$i.Attributes -band 1024){exit 12};exit 11}};if(Test-Path -LiteralPath $$d){t (Get-Item -LiteralPath $$d -Force);Get-ChildItem -LiteralPath $$d -Recurse -Force|%{t $$_}};exit 0}catch{[Console]::Write($$_.Exception.Message);exit 13}"`
	Pop $0
	Pop $1
	${If} $0 == 11
		MessageBox MB_YESNO|MB_ICONEXCLAMATION "$1.$\r$\n$\r$\nAnother account has files in $DataDir - usually a current-user install of theirs: its database, keys and settings.  The services run as Local Service, so Setup makes the folder theirs and the administrators' alone, and taking it over hands them that account's database and keys.$\r$\n$\r$\nYes: take it over.  No: stop, to uninstall that account's copy and move $DataDir aside first." /SD IDNO IDYES takeOver
		Abort "$DataDir holds another account's files"
		takeOver:
		StrCpy $3 1
	${ElseIf} $0 == 12
		MessageBox MB_OK|MB_ICONSTOP "$1, and it is a link - the services' data would land wherever it points.  Remove it, or move $DataDir aside, and run Setup again." /SD IDOK
		Abort "$DataDir holds another account's link"
	${ElseIf} $0 != 0
		DetailPrint "Could not check who owns what under $DataDir (powershell answered $0: $1) - resetting all of it"
		StrCpy $3 1
	${EndIf}
	DetailPrint "Making $DataDir the services', SYSTEM's and the Administrators' alone"
	!insertmacro Icacls '"$DataDir" /setowner *S-1-5-32-544'
	${If} $3 == 1
		!insertmacro Icacls '"$DataDir" /setowner *S-1-5-32-544 /T'
		!insertmacro Icacls '"$DataDir" /reset /T'
	${EndIf}
	!insertmacro Icacls '"$DataDir" /inheritance:r /grant:r *S-1-5-18:(OI)(CI)F *S-1-5-32-544:(OI)(CI)F *S-1-5-19:(OI)(CI)RX'
	!insertmacro Icacls '"$DataDir" /setowner *S-1-5-32-544 /T'
	;what the services write:  Local Service (Process::Install, #10) - one identity for both, so the hub can still stop the
	;OpcServer (AppInstanceHook's Process::Kill), and each reads the other's certificates.  Created here, before SEC_HUB's Files.
	CreateDirectory "$DataDir\OpcHub"
	!insertmacro Icacls '"$DataDir\OpcHub" /grant *S-1-5-19:(OI)(CI)M'
	${If} ${SectionIsSelected} ${SEC_OPCSERVER}
	${OrIf} ${FileExists} "$DataDir\OpcServer\*.*"
		CreateDirectory "$DataDir\OpcServer"
		!insertmacro Icacls '"$DataDir\OpcServer" /grant *S-1-5-19:(OI)(CI)M'
	${EndIf}
	FileOpen $1 "$DataDir\.all-users" w ;the mark CheckDataDir refuses a current-user install on, elevated or not - no such install can use this root (#4)
	FileClose $1
FunctionEnd
