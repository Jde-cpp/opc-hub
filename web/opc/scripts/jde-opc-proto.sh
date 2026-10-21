#!/bin/bash
#create-workspace.sh's hook for jde-opc:  its protos and the framework's they import, generated once.  The framework's hook runs
#first, so a workspace build finds that set made and regenerates only when one of these is stale.
#usage:  jde-opc-proto.sh <workspace dir> [clean]
scriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )";
if ! source $scriptDir/env.sh; then exit 1; fi;
$frameworkDir/scripts/generate-protos.sh "${1:-`pwd`}" "${2:-0}" $frameworkDir/scripts/framework.protos $scriptDir/opc.protos || { echo generate-protos.sh failed; exit 1; };