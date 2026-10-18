#!/bin/bash
wsDir=${1:-`pwd`};
clean=${2:-0};
baseDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." &> /dev/null && pwd )"
webDir=$(dirname $(readlink -e $baseDir));
source $JDE_BASH/build/common.sh;
frameworkDir=$baseDir;
source $frameworkDir/scripts/common-proto.sh;
echo jde-framework-proto.sh wsDir=$wsDir clean=$clean;

#npm installs into the current directory, but generateProtos' tool lookup reads $wsDir - the two must be the same
#place or the install never satisfies the guard.  Everything below is workspace-relative as well, so cd once here.
cd $wsDir || { echo `pwd`; echo cd $wsDir failed - not a directory; exit 1; };
if [ ! -d node_modules ]; then echo $wsDir is not a workspace dir - no node_modules.; exit 1; fi;

#ts-proto is the generator, buf drives it, and @bufbuild/protobuf is the wire runtime the generated code imports.
for package in ts-proto @bufbuild/buf; do
	npm list --depth=0 $package &> /dev/null || { echo installing $package; npm install --save-dev $package; };
done;
npm list --depth=0 @bufbuild/protobuf &> /dev/null || { echo installing @bufbuild/protobuf; npm install --save @bufbuild/protobuf; };
npm list --depth=0 long &> /dev/null || { echo installing long; npm install --save long; };

#The generated code is not library source and must not live inside a library: ng-packagr cannot carry a
#relatively-imported declaration file into a library's typings, which is what blocked `ng build jde-framework`.
#It lands in the jde-proto package instead - `proto` is the workspace symlink to web/proto - and the libraries import
#it by name, so a library build leaves the import alone.  One flat set: ts-proto has no shared registry, so unlike
#pbjs there is no reason to generate the same .proto twice under different roots.
protoDir=`pwd`/proto;
cd $protoDir;
moveToDir src;

declare -A webFiles=( [Web.FromServer]=1 );
declare -A appFiles=( [Log]=1 [App.FromClient]=1 [App.FromServer]=1 [App]=1 [Common]=1 );
#Log.proto imports the record layer's framing for FileEntry.  The web reads none of it, but buf generates a file whole.
declare -A recordFiles=( [Jde.IO.Records]=1 );
linkProtos $JDE_BASH/libs/web/client/proto webFiles;
linkProtos $JDE_BASH/libs/app/shared/proto appFiles;
linkProtos $JDE_BASH/libs/fwk/src/io/records/proto recordFiles;

stale=$clean;
for name in "${!webFiles[@]}" "${!appFiles[@]}"; do
	if isStale $name; then stale=1; fi;
done;
if [ $stale != 0 ]; then
	echo 'Creating framework proto files';
	generateProtos $wsDir;
	buildProtos $wsDir $protoDir;
	echo 'Created framework proto files';
else
	echo framework proto files up to date;
fi;

echo jde-framework-proto.sh complete.