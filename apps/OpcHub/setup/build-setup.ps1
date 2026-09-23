<#
.SYNOPSIS
Builds OpcHubSetup-<version>.exe from the release build tree - see README.md beside this script.

.DESCRIPTION
Resolves the inputs the NSIS script takes as /D defines (build tree, Angular dist, UA-Nodeset clone, VC++ redistributable,
version), checks they exist, and runs makensis.  -Sign signs what ships - the exes and dlls before makensis packs them, the
uninstaller from inside it (!uninstfinalize) and the installer after - through sign.ps1 (README.md, "Signing").

.EXAMPLE
.\build-setup.ps1                       # defaults: $env:JDE_RBUILD_DIR\clang++\<repo dir>\release, the repo's web dist, CMakePresets.common.json's JDE_VERSION
.\build-setup.ps1 -SkipWeb -Version 1.0 # no Web UI component
.\build-setup.ps1 -Sign -PfxPath C:\certs\test.pfx  # signed with a .pfx; -Sign alone signs with Azure Artifact Signing ($env:JDE_SIGN_*)
#>
[CmdletBinding()]
param(
	[string]$BuildDir,                   # the release build tree: bin\Jde.Opc.Hub\, bin\Jde.Opc.Server\, bin\Jde.DB.Sqlite*.dll
	[string]$WebDist,                    # ng build output - web\opc\my-workspace\dist\my-workspace\browser
	[switch]$SkipWeb,                    # omit the Web UI component
	[string]$UaNodeSets = $env:UA_NODE_SETS, # OPCFoundation/UA-Nodeset clone (DI/IA for the OpcServer)
	[string]$VcRedist = 'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Redist\MSVC\v145\vc_redist.x64.exe',
	[string]$Version,                    # default: `git describe --tags` - the tag on a tag, <tag>-N-gsha past one, else CMakePresets.common.json's JDE_VERSION; CI passes the release tag
	[string]$OutDir,                     # default: <BuildDir>\setup - outside the repo
	[string]$MakeNsis = 'C:\Program Files (x86)\NSIS\makensis.exe',
	[switch]$Sign,                       # sign.ps1: Azure Artifact Signing ($env:JDE_SIGN_ENDPOINT/ACCOUNT/PROFILE) or a .pfx ($env:JDE_SIGN_PFX)
	[string]$PfxPath                     # shorthand for $env:JDE_SIGN_PFX
)
$ErrorActionPreference = 'Stop'
$setupDir = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $setupDir '..\..\..')).Path

if( -not $BuildDir ){
	# the win-clang-release-jde preset's binaryDir is $env:JDE_BUILD_DIR\clang++-jde\release; the release tree actually
	# built here is $env:JDE_RBUILD_DIR\clang++\<repo dir>\release - so no preset lookup, an explicit default.
	# The <repo dir> segment is the checkout's own name, the same basename rule buildFunctions.sh and the jde extension use.
	$rbuild = if( $env:JDE_RBUILD_DIR ){ $env:JDE_RBUILD_DIR } else { 'R:\' }
	$BuildDir = Join-Path $rbuild ('clang++\{0}\release' -f (Split-Path $repo -Leaf))
}
if( -not $WebDist ){ $WebDist = Join-Path $repo 'web\opc\my-workspace\dist\my-workspace\browser' }
if( -not $UaNodeSets ){ $UaNodeSets = 'C:\Users\duffyj\source\repos\libs\UA-Nodeset' }
if( -not $OutDir ){ $OutDir = Join-Path $BuildDir 'setup' }
# Native separators for everything handed to makensis: its compile-time `!if /FileExists` does not take a forward-slash
# path (the CI run passed C:/jde/vc_redist.x64.exe and the runtime silently went unbundled), and GetFullPath also
# resolves a relative dir against the caller's cwd rather than the script's.
$BuildDir = [IO.Path]::GetFullPath( $BuildDir )
$WebDist = [IO.Path]::GetFullPath( $WebDist )
$UaNodeSets = [IO.Path]::GetFullPath( $UaNodeSets )
$OutDir = [IO.Path]::GetFullPath( $OutDir )
if( $VcRedist ){ $VcRedist = [IO.Path]::GetFullPath( $VcRedist ) }

foreach( $f in 'bin\Jde.Opc.Hub\Jde.Opc.Hub.exe', 'bin\Jde.Opc.Server\Jde.Opc.Server.exe', 'bin\Jde.DB.Sqlite.dll', 'bin\sqlite3.dll', 'bin\Jde.DB.Sqlite.AppServer.dll', 'bin\Jde.DB.Sqlite.OpcGateway.dll' ){
	if( -not (Test-Path (Join-Path $BuildDir $f)) ){ throw "missing $f under $BuildDir - build Jde.Opc.Hub, Jde.Opc.Server, Jde.DB.Sqlite, Jde.DB.Sqlite.AppServer and Jde.DB.Sqlite.OpcGateway in the release tree first, or pass -BuildDir" }
}
if( -not $SkipWeb -and -not (Test-Path (Join-Path $WebDist 'index.html')) ){ throw "no index.html under $WebDist - run web/opc/scripts/setup.sh (ng build), pass -WebDist, or -SkipWeb" }
foreach( $f in 'DI\Opc.Ua.Di.NodeSet2.xml', 'IA\Opc.Ua.IA.NodeSet2.xml', 'IA\Opc.Ua.IA.NodeSet2.examples.xml' ){
	if( -not (Test-Path (Join-Path $UaNodeSets $f)) ){ throw "missing $f under $UaNodeSets - clone https://github.com/OPCFoundation/UA-Nodeset or pass -UaNodeSets" }
}
if( -not (Test-Path $MakeNsis) ){ throw "makensis not found at $MakeNsis - install NSIS 3.x or pass -MakeNsis" }
if( -not (Test-Path $VcRedist) ){ Write-Warning "vc_redist.x64.exe not found at $VcRedist - the installer will not bundle the Visual C++ runtime"; $VcRedist = '' }
$signScript = Join-Path $setupDir 'sign.ps1'
if( $Sign ){
	if( $PfxPath ){ $env:JDE_SIGN_PFX = [IO.Path]::GetFullPath( $PfxPath ) }
	if( -not $env:JDE_SIGN_ENDPOINT -and -not $env:JDE_SIGN_PFX ){ throw '-Sign needs a certificate: JDE_SIGN_ENDPOINT, JDE_SIGN_ACCOUNT and JDE_SIGN_PROFILE (Azure Artifact Signing), or -PfxPath / JDE_SIGN_PFX - see README.md, Signing' }
}

# The product version is CMakePresets.common.json's JDE_VERSION - a yyyy.MM.dd date, zeros and all: the string the C++ targets
# are built with (the exes' version resource too - build/version.rc.h.in) and the Web UI's about page displays, so Add/Remove Programs agrees with them.  A -Version names it outright
# (the release workflow passes the tag) and is expected to carry that same string; anything else is warned about, not refused.
$presets = Get-Content (Join-Path $repo 'CMakePresets.common.json') -Raw | ConvertFrom-Json
$jdeVersion = ($presets.configurePresets | Where-Object { $_.name -eq 'common' }).cacheVariables.JDE_VERSION
if( -not $jdeVersion ){ throw 'JDE_VERSION not found in CMakePresets.common.json' }
if( -not $Version ){
	# Without one it is `git describe`, not JDE_VERSION alone: Add/Remove Programs and the exe name carry the version and
	# not the contents, so a build past the tag calling itself the tag cannot be told from that release
	# (reviews/install-issues.md #27).  The tag itself on a tag, <tag>-N-gsha past one - the shape VI_VERSION below already
	# reads.  `20*` is the release workflow's own tag filter.  No tag reachable (a shallow clone, an export, no git on
	# PATH) leaves JDE_VERSION, as before.
	try{ $Version = (& git -C $repo describe --tags --match '20*' 2>$null | Select-Object -First 1) }catch{ $Version = $null }
	if( -not $Version ){ $Version = $jdeVersion }
}
# the release the installer claims, so the tag part is what is compared: <tag>-N-gsha carries JDE_VERSION, and is no disagreement
if( $Version.Split('-')[0] -ne $jdeVersion ){ Write-Warning "version $Version does not carry CMakePresets.common.json's JDE_VERSION $jdeVersion - the installer's version and the product's will disagree" }
# VIProductVersion needs four 16-bit numbers: yyyy.M.d.N from a `yyyy.MM.dd[-N-gsha]` describe, else 0.0.0.0
$vi = '0.0.0.0'
if( $Version -match '^(\d{4})\.(\d{1,2})\.(\d{1,2})(?:-(\d+)-g[0-9a-f]+)?$' ){
	$n = if( $Matches[4] ){ [int]$Matches[4] } else { 0 }
	$vi = "$([int]$Matches[1]).$([int]$Matches[2]).$([int]$Matches[3]).$n"
}
New-Item -ItemType Directory -Force $OutDir | Out-Null

if( $Sign ){
	# the payload, in place, before makensis packs it - what the nsi's File lines take from bin\
	$payload = @( Get-ChildItem -Path (Join-Path $BuildDir 'bin\Jde.Opc.Hub\*'), (Join-Path $BuildDir 'bin\Jde.Opc.Server\*') -Include *.exe, *.dll -File | ForEach-Object FullName )
	$payload += 'Jde.DB.Sqlite.dll', 'sqlite3.dll', 'Jde.DB.Sqlite.AppServer.dll', 'Jde.DB.Sqlite.OpcGateway.dll' | ForEach-Object { Join-Path $BuildDir "bin\$_" }
	& $signScript @payload
}

$defs = @( "/DBUILD_DIR=$BuildDir", "/DWEB_DIST=$WebDist", "/DUA_NODE_SETS=$UaNodeSets", "/DVERSION=$Version", "/DVI_VERSION=$vi", "/DOUT_DIR=$OutDir" )
if( $VcRedist ){
	$defs += "/DVC_REDIST=$VcRedist"
	#the runtime's version off the file - the installer's "Installing the Visual C++ … runtime" line said 14.51 whatever it bundled (reviews/m4-closing.md #21)
	$redistVersion = (Get-Item $VcRedist).VersionInfo.ProductVersion
	if( $redistVersion ){ $defs += "/DVC_REDIST_VERSION=$redistVersion" }
}
if( $SkipWeb ){ $defs += '/DSKIP_WEB' }
# the uninstaller's signing hook: sign.ps1, run by the PowerShell this script runs under (the one the ArtifactSigning module is installed for)
if( $Sign ){ $defs += "/DSIGN_SCRIPT=$signScript", "/DSIGN_HOST=$((Get-Process -Id $PID).Path)" }
Write-Host "makensis $($defs -join ' ')"
& $MakeNsis /V2 @defs (Join-Path $setupDir 'OpcHubSetup.nsi')
if( $LASTEXITCODE -ne 0 ){ throw "makensis failed ($LASTEXITCODE)" }
$out = Join-Path $OutDir "OpcHubSetup-$Version.exe"
if( -not (Test-Path $out) ){ throw "makensis succeeded but $out is missing" }

if( $Sign ){ & $signScript $out }
Write-Host "built $out ($([math]::Round((Get-Item $out).Length/1MB,1)) MB)"
