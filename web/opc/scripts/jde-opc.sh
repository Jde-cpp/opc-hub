#!/bin/bash
if [ ! -d node_modules ]; then echo must run from angular dir; exit 1; fi;
#the site's files are linked into src/ by setup.sh, after create-workspace.sh has run this:  on every run, so a broken link is repaired.
#jde-opc's history trend is typed against highcharts, a devDependency for the typings and hist-trend.spec alone:  the runtime is
#an add-on the site serves from addons/highcharts/esm/, never bundled (#302 - web/CLAUDE.md, "Add-ons load by URL").  The
#workspace has to install it - create-workspace.sh's install block only runs when the workspace is first created, so an
#existing one never sees it.
if ! jq -e '.devDependencies.highcharts' package.json > /dev/null; then
	npm --silent install --save-dev highcharts@^13.1.1 || { echo `pwd`; echo npm install highcharts failed; exit 1; };
fi;
#the flow editor's canvas (#455):  @foblex/flow, MIT, bundled, at the version the spike walked (#446, docs/flows/plan.md 0.4), which
#the editor's records and sheet are written against.  A dependency, not a devDependency:  the page imports it, once its tab is shown.
if [ "$(jq -r '.dependencies."@foblex/flow" // empty' package.json)" != "19.4.0" ]; then
	npm --silent install --save-exact @foblex/flow@19.4.0 || { echo `pwd`; echo npm install @foblex/flow failed; exit 1; };
fi;