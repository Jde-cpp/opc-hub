#!/bin/bash
libRootDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." &> /dev/null && pwd )"
source $JDE_BASH/build/common.sh;
#create-workspace.sh's per-library hook.  Nothing to do for jde-framework:  it used to install @material-ui/core and
#@material-ui/icons - React packages nothing here ever imported.