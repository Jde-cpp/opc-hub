#!/bin/bash
source ${BASH_SOURCE%/*}/buildFunctions.sh; #shared helpers: buildRelativePath (src->lib/exe mapping), absoluteFile. Function bodies are subshells - see note there.

#vcxproj/ninja target name for a source file - msbuild and single-file compiles need it; buildProject's ninja branch uses <dir>/all instead.
#Echoes empty when unmapped - callers must check, or ninja would silently build everything.
function projectName() (
	absoluteFile=$1;
	filename=$(basename "$absoluteFile");
	project="";
	if [[ $filename == "main.cpp" ]]; then
		if [[ $absoluteFile == *"AppServer"* ]]; then project="Jde.App.Server";
		elif [[ $absoluteFile == *"OpcGateway"* ]]; then project="Jde.Opc.Gateway";
		elif [[ $absoluteFile == *"OpcServer"* ]]; then project="Jde.Opc.Server";
		fi;
	elif [[ $absoluteFile == *"fwk/src"* ]]; then project="Jde";
	elif [[ $absoluteFile == *"fwk/tests"* ]]; then project="Jde.Fwk.Tests";
	elif [[ $absoluteFile == *"access/tests"* ]]; then project="Jde.Access.Tests";
	elif [[ $absoluteFile == *"config/sql/sqlite"* ]]; then #native-proc modules
		if [[ $absoluteFile == *"AppServer"* || $absoluteFile == *"access"* ]]; then project="Jde.DB.Sqlite.AppServer"; #access procs are registered in AppServer's module
		elif [[ $absoluteFile == *"OpcGateway"* ]]; then project="Jde.DB.Sqlite.OpcGateway";
		fi;
	elif [[ $absoluteFile == *"access/src"* ]]; then project="Jde.Access";
	elif [[ $absoluteFile == *"db/drivers/sqlite/tests"* ]]; then project="Jde.DB.Sqlite.Tests"; #each tests arm precedes its driver arm - the driver pattern also matches files under it.
	elif [[ $absoluteFile == *"db/drivers/sqlite"* ]]; then project="Jde.DB.Sqlite";
	elif [[ $absoluteFile == *"db/drivers/mysql/tests"* ]]; then project="Jde.DB.MySql.Tests";
	elif [[ $absoluteFile == *"db/drivers/mysql"* ]]; then project="Jde.DB.MySql";
	elif [[ $absoluteFile == *"db/drivers/odbc/tests"* ]]; then project="Jde.DB.Odbc.Tests";
	elif [[ $absoluteFile == *"db/drivers/odbc"* ]]; then project="Jde.DB.Odbc";
	elif [[ $absoluteFile == *"db/tests"* ]]; then project="Jde.DB.Tests";
	elif [[ $absoluteFile == *"db/src"* ]]; then project="Jde.DB";
	elif [[ $absoluteFile == *"libs/ql"* ]]; then project="Jde.QL";
	elif [[ $absoluteFile == *"web/client"* ]]; then project="Jde.Web.Client";
	elif [[ $absoluteFile == *"web/server"* ]]; then project="Jde.Web.Server";
	elif [[ $absoluteFile == *"web/tests"* ]]; then project="Jde.Web.Tests";
	elif [[ $absoluteFile == *"app/shared"* ]]; then project="Jde.App.Shared";
	elif [[ $absoluteFile == *"app/client"* ]]; then project="Jde.App.Client";
	elif [[ $absoluteFile == *"opc/shared/src"* ]]; then project="Jde.Opc";
	elif [[ $absoluteFile == *"opc/server/src"* ]]; then project="Jde.Opc.UAServer";
	elif [[ $absoluteFile == *"AppServer/src"* ]]; then project="Jde.App.ServerLib";
	elif [[ $absoluteFile == *"OpcGateway/src"* ]]; then project="Jde.Opc.GatewayLib";
	elif [[ $absoluteFile == *"OpcGateway/tests"* ]]; then project="Jde.Opc.Tests";
	elif [[ $absoluteFile == *"OpcServer/src"* ]]; then project="Jde.Opc.ServerLib";
	elif [[ $absoluteFile == *"OpcServer/tests"* ]]; then project="Jde.Opc.Server.Tests";
	fi;
	echo $project;
)
#CMAKE_HOME_DIRECTORY is the -S path CMake cached at configure time - authoritative source root for buildRoot,
#same role it plays in buildFunctions.sh's makefileDirForFile. Echoes the repo source dir on success, the
#failure reason on non-zero return.
function repoSourceDirFor() (
	source $JDE_BASH/build/common.sh; #toBashDir
	buildRoot=$1; # /mnt/ram/win/clang++/opc-hub/debug
	cache=$buildRoot/CMakeCache.txt;
	if [ ! -f "$cache" ]; then
		echo "no CMakeCache.txt in $buildRoot - run reconfig first.";
		return 1;
	fi;
	homeDir=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache"); #CMake caches this as a Windows-style C:/... path
	if [[ -z $homeDir ]]; then
		echo "no CMAKE_HOME_DIRECTORY in $cache - reconfigure the build.";
		return 1;
	fi;
	toBashDir "$homeDir" bashHomeDir;
	realpath "$bashHomeDir";
)
#Ninja is monolithic (one build.ninja per buildRoot, no per-directory Makefiles), so instead of walking up
#directories like makefileDirForFile does, look up the .obj target compile_commands.json already recorded for
#this source file - CMake's Ninja generator writes each entry's "output" as the ninja target path, so no
#hardcoded project->target table, and no dependency on ninja query's human-readable stdout format, is needed.
#Echoes "<repoSourceDir> <ninjaTarget> <relativeFile>" on success, the failure reason on non-zero return.
function ninjaTargetForFile() (
	buildRoot=$1; # /mnt/ram/win/clang++/opc-hub/debug
	file=$2; #/c/Users/duffyj/source/repos/jde/opc-hub/libs/fwk/src/io/json.cpp
	repoSourceDir=$(repoSourceDirFor "$buildRoot") || { echo "$repoSourceDir"; return 1; };
	relativeFile=${file#"$repoSourceDir/"}; #libs/fwk/src/io/json.cpp
	if [[ $relativeFile == "$file" ]]; then
		echo "$file is not under $repoSourceDir.";
		return 1;
	fi;
	ccj="$buildRoot/compile_commands.json";
	if [ ! -f "$ccj" ]; then
		echo "no compile_commands.json in $buildRoot - reconfigure the build.";
		return 1;
	fi;
	#compile_commands.json's "file" entries are Windows-style C:/... paths - convert to match.
	winFile="${file#/}"; winFile="${winFile^}"; winFile="${winFile:0:1}:${winFile:1}"; #/c/Users/... -> C:/Users/...
	target=$(jq -r --arg f "$winFile" '[.[] | select(.file == $f)] | if length==0 then empty else (.[0] as $e | $e.output | ltrimstr($e.directory + "/")) end' "$ccj");
	if [[ -z $target ]]; then
		echo "no compile command for $relativeFile in $ccj - not a source of any target there.";
		return 1;
	fi;
	echo "$repoSourceDir $target $relativeFile";
)
function buildProject() (
	source $JDE_BASH/build/common.sh; #toBashDir
	toBashDir $1 buildRoot; # /mnt/ram/win/clang++/opc-hub/debug
	toBashDir $2 file; #/c/Users/duffyj/source/repos/jde/opc-hub/libs/web/server/Server.cpp
	file=$(realpath "$file");
	repoSourceDir=$(repoSourceDirFor "$buildRoot") || { echo "buildProject: $repoSourceDir"; return 1; };
	buildRelativePath=`buildRelativePath $repoSourceDir $file`; #libs/web/server/auth - may be nested below the CMakeLists.txt dir
	echo "buildProject buildRoot=$buildRoot, repoSourceDir=$repoSourceDir, buildRelativePath=$buildRelativePath";
	if [ -f "$buildRoot/build.ninja" ]; then
		#CMake's Ninja generator makes a phony <dir>/all only per CMakeLists.txt directory (add_subdirectory), same
		#granularity as the Makefiles generator's per-directory Makefile - so walk up like makefileDirForFile does
		#until the phony target resolves, since a source file's own folder (e.g. .../server/auth) may not have one.
		dir=$buildRelativePath;
		while [[ -n $dir ]] && ! ninja -C "$buildRoot" -t query "$dir/all" >/dev/null 2>&1; do
			if [[ $dir == */* ]]; then dir=${dir%/*}; else dir=""; fi;
		done;
		if [[ -z $dir ]]; then echo "buildProject: no ninja 'all' target found for $buildRelativePath under $buildRoot"; return 1; fi;
		cd $buildRoot || return 1;
		echo ninja -C $buildRoot $dir/all;
		ninja -C $buildRoot $dir/all 2>&1;
	else
		#legacy fallback for a Visual Studio/vcxproj-generated build - none of the documented -jde presets use it (win-clang* is Ninja-only).
		project=`projectName $file`;
		if [[ -z $project ]]; then echo "buildProject: no project mapping for $file"; return 1; fi;
		cd $buildRoot/$buildRelativePath || return 1;
		echo $buildRoot/$buildRelativePath msbuild.exe $project.vcxproj -p:Configuration=Debug;
		msbuild.exe $project.vcxproj -p:Configuration=Debug -v:m;
	fi
)
function compile() (
	source $JDE_BASH/build/common.sh; #toBashDir
	toBashDir $1 buildRoot; # /mnt/ram/win/clang++/opc-hub/debug
	toBashDir $2 file; #/c/Users/duffyj/source/repos/jde/opc-hub/libs/fwk/src/io/json.cpp
	file=$(realpath "$file");
	if [ -f "$buildRoot/build.ninja" ]; then
		resolved=$(ninjaTargetForFile "$buildRoot" "$file") || { echo "compile: $resolved"; return 1; };
		read repoSourceDir target relativeFile <<< "$resolved"; #relativeFile=libs/fwk/src/io/json.cpp
		cd $buildRoot || return 1;
		echo "ninja -C $buildRoot $target";
		ninja -C $buildRoot $target 2>&1;
	else
		#legacy fallback for a Visual Studio/vcxproj-generated build - none of the documented -jde presets use it (win-clang* is Ninja-only).
		repoSourceDir=$(repoSourceDirFor "$buildRoot") || { echo "compile: $repoSourceDir"; return 1; };
		project=`projectName $file`;
		if [[ -z $project ]]; then echo "compile: no project mapping for $file"; return 1; fi;
		buildRelativePath=`buildRelativePath $repoSourceDir $file`;
		relativeFile=${file#"$repoSourceDir/"};
		cd $buildRoot/$buildRelativePath || return 1;
		echo $buildRoot/$buildRelativePath msbuild.exe $project.vcxproj -p:Configuration=Debug -t:ClCompile -p:ClCompile=$relativeFile;
		msbuild.exe $project.vcxproj -p:Configuration=Debug -t:ClCompile -p:ClCompile=$relativeFile //v:m; #single-file ClCompile via property is IDE-flavored; outside VS it may compile the whole project.
	fi
)
function clean() (
	repoBuildDir=$1; #scope to this checkout's build dir - a find from $1 would sweep every checkout's PCHs.
	cd $repoBuildDir || return 1;
	cmake --build . --target clean;
	#repos holds the superbuild's git checkouts - these patterns would take their .pack/.idx and source .xml/.json.
	#-exec rm, not -delete: -delete implies -depth, which disables -prune.
	local pattern;
	for pattern in 'cmake_pch.hxx.pch' '*.obj' '*.pack' '*.lib' '*.xml' '*.srcjar' '*.yml' '*.exe' '*.pcm' '*.idx' '*.yaml' '*.pdb' '*.pdf' '*.json'; do
		find . -path ./repos -prune -o -name "$pattern" -print -exec rm -f {} +;
	done
)