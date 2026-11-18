#!/bin/bash
if [ ! -d node_modules ]; then echo must run from angular dir; exit 1; fi;
#the site's files are linked into src/ by setup.sh, after create-workspace.sh has run this:  on every run, so a broken link is repaired.
#jde-opc's history trend is typed against highcharts, a devDependency for the typings and hist-trend.spec alone:  the runtime is
#an add-on the site serves from addons/highcharts/esm/, never bundled (#302 - web/CLAUDE.md, "Add-ons load by URL").  The
#workspace has to install it - create-workspace.sh's install block only runs when the workspace is first created, so an
#existing one never sees it.  At the exact version the demo's highcharts.sh lays, which build-demo.sh holds it to:  a range let a
#fresh workspace type the site against a later release than the box serves (reviews-issue/487-demo-ops.md #11).
if [ "$(jq -r '.devDependencies.highcharts // empty' package.json)" != "13.1.1" ]; then
	npm --silent install --save-dev --save-exact highcharts@13.1.1 || { echo `pwd`; echo npm install highcharts failed; exit 1; };
fi;
#the flow editor's canvas (#455):  @foblex/flow, MIT, bundled, at the version the spike walked (#446, docs/flows/plan.md 0.4), which
#the editor's records and sheet are written against.  A dependency, not a devDependency:  the page imports it, once its tab is shown.
if [ "$(jq -r '.dependencies."@foblex/flow" // empty' package.json)" != "19.4.0" ]; then
	npm --silent install --save-exact @foblex/flow@19.4.0 || { echo `pwd`; echo npm install @foblex/flow failed; exit 1; };
fi;
#the Function node's block editor (#458):  Blockly, Apache 2.0, bundled, at the version its blocks, its toolbox and its sheet are
#written against, in a chunk of its own that loads once a body's blocks are asked for (web/CLAUDE.md, "The block editor").
if [ "$(jq -r '.dependencies.blockly // empty' package.json)" != "13.4.0" ]; then
	npm --silent install --save-exact blockly@13.4.0 || { echo `pwd`; echo npm install blockly failed; exit 1; };
fi;
#Blockly's media - its cursors, and the sprites of the trash can and the zoom controls - from the package to
#assets/jde-opc/blockly/, where the editor points it:  left to itself Blockly fetches them from a site of Google's, which a plant's
#network may not reach.  Its sounds are switched off, and stay behind.  Re-applied every run, as create-workspace.sh's entries are.
source $JDE_BASH/build/common.sh;
jqEdit angular.json ".projects.\"$(basename `pwd`)\".architect.build.options.assets |= ((. // []) | map(select((type==\"object\" and .input==\"node_modules/blockly/media\") | not)) + [{\"glob\":\"**/*\",\"input\":\"node_modules/blockly/media\",\"output\":\"assets/jde-opc/blockly\",\"ignore\":[\"*.mp3\",\"*.ogg\",\"*.wav\"]}])";