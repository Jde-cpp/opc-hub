#!/bin/bash
library=${1};  #jde-opc
dir=${2}; #web/opc
overwrite=${3:-0}; #true
echo starting Library $library;
source $JDE_BASH/build/common.sh;
controlDir=$dir/control;
echo controlDir=$controlDir;
echo pwd=`pwd`;
if [ $overwrite != 0 ]; then
	rm -rf projects/$library;
fi;
if [ ! -d projects/$library ]; then
	configPath=.compilerOptions.paths.\"$library\"
	#a library entry left by a workspace whose projects/$library was deleted:  ng g library refuses a name angular.json has.
	#jqEdit drops the /* */ header the cli writes in tsconfig.json, which jq cannot parse.
	if jq -e ".projects.\"$library\"" angular.json > /dev/null; then
		echo removing from [angular][tsconfig].json;
		jqEdit angular.json "del(.projects.\"$library\")";
		jqEdit tsconfig.json "del($configPath)";
	fi;

	ng g library $library --defaults || { echo `pwd`; echo ng g library $library --defaults; exit 1; };
	jqEdit tsconfig.json "$configPath = [\"./projects/$library/src/public-api\", \"./dist/$library/$library\", \"./dist/$library\"]";
fi;
cd projects/$library;
cd src;
#public-api.ts is symlinked by create-workspace.sh on every run, not hard-linked here on first create only.
cd lib;
if [ -f $library.component.ts ]; then rm $library.component.*; fi;
if [ -f $library.service.ts ]; then rm $library.service.*; fi;