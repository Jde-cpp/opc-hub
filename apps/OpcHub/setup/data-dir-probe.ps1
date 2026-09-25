# OpcHubSetup.nsi's two data-root probes, CheckDataDirOwner (-Mode CurrentUser) and TakeDataDir (-Mode AllUsers), extracted to
# $PLUGINSDIR and run with -File.  Inline -Command, TakeDataDir's outgrew NSIS's 1,024-character string and was cut:  it never
# parsed, and every all-users install took the root unasked (reviews/install-issues.md #55).
# Exit:  0 the root is fresh or this install's (CurrentUser writes this account's SID);  11 another account's - the output says
# whose;  12 (AllUsers) a link another account made;  13 the probe itself failed.  Setup reads the output with nsExec /OEM (#50).
param(
	[Parameter(Mandatory)][ValidateSet('CurrentUser','AllUsers')][string]$Mode,
	[Parameter(Mandatory)][string]$Company
)
$ProgressPreference = 'SilentlyContinue'
$ErrorActionPreference = 'Stop'
try{
	$u = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
	# Owners that are no other account's:  the Administrators - what an elevated Setup or first start creates, whoever ran it -
	# and this account.  An all-users root's also SYSTEM's and Local Service's, which only its services create;  in a current-user
	# root those mean an all-users install's services.
	$k = if( $Mode -eq 'AllUsers' ){ 'S-1-5-18','S-1-5-19','S-1-5-32-544',$u }else{ 'S-1-5-32-544',$u }
	$d = Join-Path $env:ProgramData $Company
	function Get-AccountName( $sid ){
		try{ (New-Object Security.Principal.SecurityIdentifier $sid).Translate([Security.Principal.NTAccount]).Value }catch{ $sid }
	}
	# The mark:  the SID of the account whose current-user install this is.  An owner cannot tell two administrators apart (#43).
	$m = Join-Path $d '.current-user'
	if( Test-Path -LiteralPath $m ){
		$o = (Get-Content -LiteralPath $m -Raw).Trim()
		if( $o -and $o -ne $u ){ [Console]::Write( (Get-AccountName $o)+' installed '+$d ); exit 11 }
	}
	function Test-Owner( $item ){
		$o = $item.GetAccessControl('Owner').GetOwner([Security.Principal.SecurityIdentifier]).Value
		if( $k -notcontains $o ){
			[Console]::Write( (Get-AccountName $o)+' owns '+$item.FullName )
			if( $Mode -eq 'AllUsers' -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) ){ exit 12 }
			exit 11
		}
	}
	if( Test-Path -LiteralPath $d ){
		Test-Owner (Get-Item -LiteralPath $d -Force)
		Get-ChildItem -LiteralPath $d -Recurse -Force | ForEach-Object { Test-Owner $_ }
	}
	if( $Mode -eq 'CurrentUser' ){ [Console]::Write( $u ) }
	exit 0
}
catch{
	[Console]::Write( $_.Exception.Message )
	exit 13
}
