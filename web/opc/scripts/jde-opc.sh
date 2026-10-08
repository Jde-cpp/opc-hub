#!/bin/bash
if [ ! -d node_modules ]; then echo must run from angular dir; exit 1; fi;
angularDir=`pwd`;
scriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )";
cd $scriptDir/..;
baseDir=`pwd`;
source $JDE_BASH/build/common.sh;

cd $angularDir/src;
addHard styles.scss $baseDir/site;
addHard index.html $baseDir/site;
addHard favicon.ico $baseDir/site;

cd $angularDir;
#jde-opc imports highcharts (the history trend).  A peer of the library, so the workspace has to install
#it - and create-workspace.sh's install block only runs when the workspace is first created, so an existing one never sees
#it.  Dynamic import only, as jde-spa's marked:  the libraries are not lazy, and a static import would put Highcharts Stock
#in the initial bundle for every page.
if ! jq -e '.dependencies.highcharts' package.json > /dev/null; then
	npm --silent install highcharts@^13.1.1 || { echo `pwd`; echo npm install highcharts failed; exit 1; };
fi;