<#
.SYNOPSIS
The Windows side of files.txt - what the installers ship.

.DESCRIPTION
Dot-sourced (build-setup.ps1), it defines Get-SetupFiles:  files.txt's lines for the Windows installer - ships all, windows
or user - as objects.  Run with -Nsh, it writes them as OpcHubSetup.nsi's OpcHubFiles and OpcServerFiles macros, which the nsi
does through !system as it compiles, so a bare makensis run needs no wrapper.
#>
param( [string]$Nsh )
$ErrorActionPreference = 'Stop'

function Get-SetupFiles{
	$manifest = Join-Path $PSScriptRoot 'files.txt'
	foreach( $line in Get-Content $manifest -Encoding UTF8 ){
		$fields = @( ($line -replace '#.*', '').Trim() -split '\s+' | Where-Object { $_ } )
		if( -not $fields ){ continue }
		if( $fields.Count -notin 4, 5 ){ throw "${manifest}: '$line' - want section ships to from [as]" }
		$section, $ships, $to, $from, $as = $fields
		if( $section -notin 'hub', 'opcserver' ){ throw "${manifest}: section '$section' is neither hub nor opcserver" }
		if( $ships -notin 'all', 'windows', 'linux', 'user' ){ throw "${manifest}: ships '$ships' is none of all, windows, linux, user" }
		if( $ships -eq 'linux' ){ continue }
		$root, $path = if( $from -match '^(build|nodesets):(.+)' ){ $Matches[1], $Matches[2] } else { 'repo', $from }
		[pscustomobject]@{ Section=$section; To=$to.TrimEnd('!'); Owned=$to.EndsWith('!'); Root=$root; Path=$path -replace '/', '\'; As=$as }
	}
}

if( $Nsh ){
	$roots = @{ repo='${SRC_DIR}'; build='${BUILD_DIR}'; nodesets='${UA_NODE_SETS}' }
	$programDirs = @{ hub='OpcHub'; opcserver='OpcServer' }
	$files = @( Get-SetupFiles )
	$recreated = @{}
	$lines = foreach( $section in 'hub', 'opcserver' ){
		"!macro $($programDirs[$section])Files"
		$outPath = $null
		foreach( $f in $files | Where-Object Section -eq $section ){
			$dir = switch -regex( $f.To ){
				'^program$' { '$INSTDIR\' + $programDirs[$section] }
				'^config/(.+)' { '$ConfigDir\' + ($Matches[1] -replace '/', '\') }
				'^data/(.+)' { '$DataDir\' + ($Matches[1] -replace '/', '\') }
				default { throw "files.txt: to '$($f.To)' is none of program, config/<dir>, data/<dir>" }
			}
			if( $f.Owned -and -not $recreated[$dir] ){ "`tRMDir /r `"$dir`""; $recreated[$dir] = $true; $outPath = $null }
			if( $dir -ne $outPath ){ "`tSetOutPath `"$dir`""; $outPath = $dir }
			$source = $roots[$f.Root] + '\' + $f.Path
			if( $f.As ){ "`tFile /oname=$($f.As) `"$source`"" } else { "`tFile `"$source`"" }
		}
		'!macroend'
	}
	[IO.File]::WriteAllLines( $Nsh, [string[]]$lines )
}