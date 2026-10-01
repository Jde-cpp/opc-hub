#!/bin/bash
wsDir=${1:-`pwd`};
clean=${2:-0};
echo jde-opc-proto.sh wsDir=$wsDir clean=$clean;
pushd `pwd` > /dev/null;
pushd `pwd` > /dev/null;
scriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )";
if ! source $scriptDir/env.sh; then exit 1; fi;
cd $scriptDir/..;

popd > /dev/null;
#after the popd so $wsDir wins over wherever the caller was standing; the cd below and create()'s pbjs lookup are
#both workspace-relative - see the note in jde-framework-proto.sh.
#`||` not `; if [ $? ]` - env.sh installs trap error ERR, which fires on a bare failing command and exits before
#any $? test can run.  Same reason for the nested call below.
cd $wsDir || { echo `pwd`; echo cd $wsDir failed - not a directory; exit 1; };
#see the note in jde-framework-proto.sh: generated proto output belongs to the jde-proto package, not to a library.
#Common.proto is not re-linked here - the framework pass below puts it in the same flat set, and ts-proto has no
#per-module registry, so one generated Common serves both libraries.
protoDir=$wsDir/proto;
#Linked before the framework pass, not after:  it generates every .proto in the directory, these included, so one
#whose link is stale - Opc.Common.proto moved from the gateway to libs/opc - fails that pass before this one can fix it.
moveToDir $protoDir/src;
declare -A opcFiles=( [Opc.FromServer]=1 [Opc.FromClient]=1 );
linkProtos $JDE_BASH/apps/OpcGateway/src/types/proto opcFiles;
declare -A opcLibFiles=( [Opc.Common]=1 );
linkProtos $JDE_BASH/libs/opc/src/proto opcLibFiles;

cd $wsDir;
$frameworkDir/scripts/jde-framework-proto.sh $wsDir $clean || { echo `pwd`; echo jde-framework-proto.sh $wsDir $clean failed; exit 1; };
cd $protoDir/src;
stale=$clean;
for name in "${!opcFiles[@]}" "${!opcLibFiles[@]}"; do
	if isStale $name; then stale=1; fi;
done;
if [ $stale != 0 ]; then
	generateProtos $wsDir;
	buildProtos $wsDir $protoDir;
fi;
echo 'jde-opc-proto done';
popd > /dev/null;