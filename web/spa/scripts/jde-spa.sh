#!/bin/bash
mainDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." &> /dev/null && pwd )"
source $JDE_BASH/build/common.sh
pushd `pwd` > /dev/null;

workspace=$(basename $PWD);
stylesPath=".projects.\"$workspace\".architect.build.options.styles";
#a bundle per theme file, uninjected - the theme picker links the one it wants - then the site's sheet
stylesContent="[";
for theme in $mainDir/control/src/styles/custom-themes/*.scss; do
	name=$(basename $theme .scss);
	stylesContent=$stylesContent"{\"inject\": false,\"input\": \"projects/jde-spa/src/styles/custom-themes/$name.scss\",\"bundleName\": \"$name\" },";
done;
stylesContent=$stylesContent\"src/styles.scss\"];
jqEdit angular.json "$stylesPath = $stylesContent";
#jde-spa imports marked (help pages).  A peer of the library, so the workspace has to install it - and create-workspace.sh's
#install block only runs when the workspace is first created, so an existing one never sees it.  Dynamic import only:  a
#static one would hoist it into the initial bundle (see web/CLAUDE.md).
if ! jq -e '.dependencies.marked' package.json > /dev/null; then
	npm --silent install marked@^18.0.0 || { echo `pwd`; echo npm install marked failed; exit 1; };
fi;

popd > /dev/null;