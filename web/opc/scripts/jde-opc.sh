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
#jde-opc's history trend is typed against highcharts, a devDependency for the typings and hist-trend.spec alone:  the runtime is
#an add-on the site serves from addons/highcharts/esm/, never bundled (#302 - web/CLAUDE.md, "Add-ons load by URL").  The
#workspace has to install it - create-workspace.sh's install block only runs when the workspace is first created, so an
#existing one never sees it.
if ! jq -e '.devDependencies.highcharts' package.json > /dev/null; then
	npm --silent install --save-dev highcharts@^13.1.1 || { echo `pwd`; echo npm install highcharts failed; exit 1; };
fi;