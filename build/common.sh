windows() { [[ -n "$WINDIR" ]]; }

if windows; then
  compiler=msvc;
else
  compiler=g++14;
fi;

function addHard {
	local file=$1;#TwsSocketClient64.vcxproj
	local fetchLocation=$2;
	if [ -f $file ]; then rm $file; fi;
	if windows; then
		toWinDir "$fetchLocation" _source;
		toWinDir "`pwd`" _destination;
		cmd <<< "mklink /H \"$_destination\\$file\" \"$_source\\$file\" " > /dev/null; #"
		if [ $? -ne 0 ]; then
			echo `pwd`;
			echo cmd <<< "mklink \"$_destination\\$file\" \"$_source\\$file\" "; #"
			exit 1;
		fi;
	else
		ln $fetchLocation/$file .;
	fi;
};

#The version the C++ build carries - CMakePresets.common.json's JDE_VERSION, the same string `project( … VERSION ${JDE_VERSION} )`
#uses and the Windows exes' version resource (build/version.rc.h.in) - into the variable named by $1, normalised to what npm accepts.
#The presets file to read defaults to $JDE_BASH's, which is the checkout whose build/common.sh was sourced; pass $2
#to read another one.
#JDE_VERSION is a yyyy.MM.dd date and that is not semver: npm's semver rejects leading zeros in a numeric
#identifier, so semver.valid("2026.09.01") is null and satisfies("2026.09.01","2026.09.01") answers false - a
#library packed at that version would never match a sibling's peer range.  Drop the zeros: 2026.9.1.
#The version as the presets spell it - the yyyy.MM.dd date, zeros and all: what the installers name themselves and
#Add/Remove Programs shows (build-setup.ps1, build-deb.sh), and what the Web UI's about page displays (setup.sh's
#JDE_VERSION define).  Only npm's package versions take the semver form, jdeVersion below.
function jdeVersionRaw {
	local -n _jdeVersionRaw=$1;
	local presets=${2:-$JDE_BASH/CMakePresets.common.json};
	#assign on its own line - `local raw=`cmd`` returns local's status, not the command's, so the || never fires.
	local presetsVersion; #not `raw`: a caller passing a variable of that name would be shadowed by it (nameref)
	presetsVersion=`jq -er '.configurePresets[] | select(.name=="common") | .cacheVariables.JDE_VERSION' "$presets" 2> /dev/null` || { echo `pwd`; echo could not read JDE_VERSION from $presets; exit 1; };
	_jdeVersionRaw=$presetsVersion;
}
function jdeVersion {
	local -n _jdeVersion=$1;
	local presets=${2:-$JDE_BASH/CMakePresets.common.json};
	local rawVersion;
	jdeVersionRaw rawVersion "$presets";
	#a lone 0 is left alone: `0+([0-9])` needs a digit after the zeros, so 1.0.0 does not become 1..
	_jdeVersion=`echo "$rawVersion" | sed -E 's/(^|\.)0+([0-9])/\1\2/g'`;
}

#`ng new` emits tsconfig.json with leading /* */ comment lines, which jq cannot parse.  Strip whole-line
#comments before piping to jq - matching create-library.sh - so a // inside a string value is left alone.
#Only replace the original once jq has succeeded: `cmd > tmp; rm orig; mv tmp orig` zeroed the file on any failure.
#Lives here rather than in create-workspace.sh because the per-site setup.sh scripts patch angular.json too - their
#edits have to re-apply on every run, while create-workspace.sh only runs when the workspace is absent.
function jqEdit() {
	local file=$1; local filter=$2;
	if [ ! -f "$file" ]; then echo `pwd`; echo "jqEdit: $file not found"; exit 1; fi;
	sed -e '/^[[:space:]]*\/\*/d' -e '/^[[:space:]]*\*/d' -e '/^[[:space:]]*\/\//d' "$file" | jq "$filter" > "$file.tmp";
	#jq exits 0 on empty input, so check for output too - otherwise an already-zeroed file rewrites as still-zeroed.
	if [ ${PIPESTATUS[1]} -ne 0 ] || [ ! -s "$file.tmp" ]; then echo `pwd`; echo jq "$filter" $file; rm -f "$file.tmp"; exit 1; fi;
	mv "$file.tmp" "$file";
}

function addHardDir {
	local dir=$1;
	local sourceDir=$2/$1;
	moveToDir $dir;
	for filename in $sourceDir/*; do
		if [ -f $filename ]; then addHard $(basename "$filename") $sourceDir;
		elif [ -d $filename ]; then addHardDir $(basename "$filename") $sourceDir; fi;
	done;
	cd ..;
}

function findExecutable {
	exe=$1;
	defaultPath=$2;
	exitFailure=${3:-1};
	local path_to_exe=$(which "$exe" 2> /dev/null);
	if [ ! -x "$path_to_exe" ]; then
		if  [[ -x "${defaultPath//\\}/$exe" ]]; then
     	PATH=${defaultPath//\\}:$PATH;
		else
			if [ $exitFailure -eq 1 ]; then
				echo `pwd`;
				echo common.sh:?? can not find "${defaultPath//\\}/$exe";
				exit 1;
			fi;
		fi;
	fi;
}

function mklink {
	local file=$1;
	local fetchLocation=$2;
	#-L as well as -f:  a link whose target moved (the checkout renamed from Public to opc-hub) fails -f, was never removed,
	#and cmd's mklink then refused it with "already exists" - silently, since cmd's exit status does not carry it - so a
	#stale workspace could not be repaired by re-running setup.  mklinkDir tests -L for the same reason.
	if [ -L $file ] || [ -f $file ]; then rm -f $file; fi;
	if windows; then
		toWinDir "$fetchLocation" _source;
		if [ ! -f "$_source/$file" ]; then echo $PS4 $_source/$file not found; exit 1; fi;
		toWinDir "`pwd`" _destination;
		cmd <<< "mklink \"$_destination\\$file\" \"$_source\\$file\" " > /dev/null;  #"
		if [ $? -ne 0 ]; then
			echo `pwd`;
			echo cmd <<< "mklink \"$_destination\\$file\" \"$_source\\$file\" "; #"
			exit 1;
		fi;
	else
 	if [ -L $file ]; then rm $file; fi;
		ln -s $fetchLocation/$file .;
	fi;
}

#Directory counterpart of mklink.  It cannot share mklink's body: cmd mklink needs /D for a directory, and the
#existence tests differ.  /D is a real symlink, matching the file mklink above and the workspace's preserveSymlinks;
#swap to /J (junction) if creating links without Developer Mode/elevation turns out to matter.
#Only an existing *symlink* is replaced - a real directory is left for the caller, so this never deletes a source
#tree through a link.
function mklinkDir {
	local dir=$1;
	local fetchLocation=$2;
	if [ -L $dir ]; then rm $dir; fi;
	if windows; then
		#test the bash-side path - the converted one is only good as an argument to cmd, not to test.
		if [ ! -d "$fetchLocation/$dir" ]; then echo $PS4 $fetchLocation/$dir not found; exit 1; fi;
		toWinDir "$fetchLocation" _source;
		toWinDir "`pwd`" _destination;
		cmd <<< "mklink /D \"$_destination\\$dir\" \"$_source\\$dir\" " > /dev/null;  #"
		if [ $? -ne 0 ]; then
			echo `pwd`;
			echo mklink /D "$_destination\\$dir" "$_source\\$dir";
			exit 1;
		fi;
	else
		ln -s $fetchLocation/$dir .;
	fi;
}

function moveToDir {
	local dir=$1;
	mkdir -p $dir; cd $dir;
}

function toBashDir {
	windowsDir=$1;
 	local -n _bashDir=$2
 	_bashDir=${windowsDir/:/}; _bashDir=${_bashDir//\\//};
 	if [[ $_bashDir == C/* || $_bashDir == C ]]; then
 		_bashDir="c${_bashDir:1}"
 	fi
	if [[ ${_bashDir:0:1} != "/" ]]; then _bashDir=/$_bashDir; fi;
}

function toWinDir {
	bashDir=$1;
	local -n _winDir=$2
	_winDir=${bashDir////\\};
	if [[ $_winDir == \\c\\* ]]; then _winDir=c:${_winDir:2};
	elif [[ $_winDir == \"\\c\\* ]]; then _winDir=\"c:${_winDir:3}; fi;
}
