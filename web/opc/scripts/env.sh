#!/bin/bash
thisDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )";
webDir=$(dirname $(readlink -e $thisDir/..));
frameworkDir=$webDir/framework;
source $JDE_BASH/build/common.sh;
source $frameworkDir/scripts/common-proto.sh;
if ! source $JDE_BASH/build/common-error.sh; then exit 1; fi;