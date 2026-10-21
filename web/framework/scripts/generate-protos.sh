#!/bin/bash
#Generates the jde-proto package - web/proto, the workspace's `proto` link - from the .proto files the lists name, in one pass.
#usage:  generate-protos.sh <workspace dir> <clean 0|1> <list>...
#A list is a file of `<dir under $JDE_BASH> <name>...` lines (framework.protos, opc.protos), the names the .proto files there.
#Every list's files are linked into one generation dir first, then the set is generated and compiled once - when `clean`, or
#when a name's .ts is missing or older than a .proto it was generated from.  One flat set:  ts-proto has no per-module registry,
#and buf generates every .proto in the dir, whichever list linked it.
wsDir=${1:-`pwd`};
clean=${2:-0};
shift 2;
scriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )";
source $JDE_BASH/build/common.sh;
source $scriptDir/common-proto.sh;
echo generate-protos.sh wsDir=$wsDir clean=$clean lists="$*";

#npm installs into the current directory, but generateProtos' tool lookup reads $wsDir - the two must be the same
#place or the install never satisfies the guard.  Everything below is workspace-relative as well, so cd once here.
cd $wsDir || { echo `pwd`; echo cd $wsDir failed - not a directory; exit 1; };
if [ ! -d node_modules ]; then echo $wsDir is not a workspace dir - no node_modules.; exit 1; fi;

#ts-proto is the generator, buf drives it, and @bufbuild/protobuf is the wire runtime the generated code imports.
for package in ts-proto @bufbuild/buf; do
	npm list --depth=0 $package &> /dev/null || { echo installing $package; npm install --save-dev $package || { echo npm install $package failed; exit 1; }; };
done;
for package in @bufbuild/protobuf long; do
	npm list --depth=0 $package &> /dev/null || { echo installing $package; npm install --save $package || { echo npm install $package failed; exit 1; }; };
done;

#The generated code is not library source and must not live inside a library: ng-packagr cannot carry a
#relatively-imported declaration file into a library's typings, which is what blocked `ng build jde-framework`.
#It lands in the jde-proto package instead and the libraries import it by name, so a library build leaves the import alone.
protoDir=`pwd`/proto;
cd $protoDir || { echo `pwd`; echo cd $protoDir failed - no proto link; exit 1; };
moveToDir src;
#all linked before anything is generated:  buf compiles every .proto here, so one whose link is stale - Opc.Common.proto moved
#from the gateway to libs/opc - fails the pass however up to date the names asked about are.
names=();
for list in "$@"; do
	[ -f "$list" ] || { echo $list not found; exit 1; };
	while read -r dir files || [ -n "$dir" ]; do	#the last line has no newline
		if [ -z "$dir" ] || [ "${dir:0:1}" == "#" ]; then continue; fi;
		linkProtos $JDE_BASH/$dir $files;
		names+=( $files );
	done < "$list";
done;

stale=$clean;
for name in "${names[@]}"; do
	if isStale $name; then stale=1; fi;
done;
if [ $stale != 0 ]; then
	generateProtos $wsDir;
	#buf leaves a file whose output is unchanged as it was, older than the .proto that moved under it - stale again next run
	touch ${names[@]/%/.ts} || { echo `pwd`; echo touch failed; exit 1; };
	buildProtos $wsDir $protoDir;
	echo generated ${names[*]};
else
	echo proto files up to date;
fi;