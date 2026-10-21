#!/bin/bash
#create-workspace.sh's hook for jde-framework:  its protos (framework.protos).  usage:  jde-framework-proto.sh <workspace dir> [clean]
scriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )";
$scriptDir/generate-protos.sh "${1:-`pwd`}" "${2:-0}" $scriptDir/framework.protos || { echo generate-protos.sh failed; exit 1; };