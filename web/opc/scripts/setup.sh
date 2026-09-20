#!/bin/bash
#--release: hash the build's output names (see the ng build line); anything else on the command line is ignored.
release=0; for arg in "$@"; do [ "$arg" = "--release" ] && release=1; done
scriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd $scriptDir;
source env.sh;
baseWebDir=$JDE_BASH/web;
cd ..;
cmd="../framework/scripts/create-workspace.sh my-workspace $baseWebDir/spa $baseWebDir/framework $baseWebDir/access $baseWebDir/opc";
echo $cmd
$cmd; if [ $? -ne 0 ]; then echo `pwd`; echo $cmd; exit 1; fi;
cd my-workspace/src;
sitePath=`realpath $scriptDir/../site`;
rm main.ts;
addHard main.ts $sitePath;
addHard styles.scss $sitePath;
addHard index.html $sitePath;
#the icons go in public/, the one folder the build copies to the output root - a favicon.ico beside index.html in src/
#is never served, and `ng new`'s own public/favicon.ico (the Angular logo) was, until addHard replaced it here.
cd ../public;
addHard favicon.ico $sitePath;
addHard favicon.svg $sitePath;
cd ../src/app;
#`ng new` scaffolds a hello-world root component (app.ts/app.html/app.scss/app.spec.ts) that this site replaces.
#addHard rm's its target first, so the three linked names overwrite themselves; the orphan spec has no link to
#overwrite it and would otherwise keep running against the deleted scaffold under `ng test`.
rm -f app.spec.ts;
addHard app.routes.ts $sitePath/app;
addHard app.html $sitePath/app;
addHard app.scss $sitePath/app;
addHard app.ts $sitePath/app;
rm -f app.config.ts;
addHard app.config.ts $sitePath/app;
addHard google-relogin.spec.ts $sitePath/app;
addHard profile-store.spec.ts $sitePath/app;
addHard profile-service.spec.ts $sitePath/app;
#app-project specs for jde-spa services (library specs themselves live beside the code since the buildTarget fix in create-workspace.sh).
addHard search-service.spec.ts $sitePath/app;
addHard route-search-provider.spec.ts $sitePath/app;
addHard node-search-provider.spec.ts $sitePath/app;
addHard environment-keys.spec.ts $sitePath/app;
moveToDir services;
addHard environment-service.ts $sitePath/app/services;
cd ../..;
#the site's help markdown (assets/help/*.md).  A symlink, not addHardDir:  .md is exactly what gets hand-edited, and an editor
#save replaces the file and silently breaks a hard link.  Served under assets/site by the angular.json entry below.
mklinkDir assets $sitePath;
moveToDir environments;
addHard environment.ts $sitePath/environments;
addHard environment.development.ts $sitePath/environments;
cd ../..;
#the application itself is never published, but stamping it keeps the manifest reporting the same version the
#libraries and the C++ services carry, so anything reading it (npm ls, a future about-box) agrees with them.
jdeVersion jdeVer;
jqEdit package.json ".version = \"$jdeVer\"";
#the version as a build-time constant for the about page:  the builder replaces the identifier JDE_VERSION with the string
#literal under serve, build and test alike (the unit-test builder inherits the application options through buildTarget).
#As the presets spell it (2026.09.01) - the string the installers name themselves by - not npm's 2026.9.1, which only the
#package versions above need (reviews/install-issues.md, "Version string").
jdeVersionRaw jdeVerRaw;
jqEdit angular.json ".projects.\"my-workspace\".architect.build.options.define = {\"JDE_VERSION\": (\"$jdeVerRaw\" | tojson)}";
#create-workspace.sh writes angular.json, but only when the workspace is absent, so the swap is re-applied here on
#every run.  Without it `ng serve` and `--configuration development` build against the production environment.ts.
jqEdit angular.json '.projects."my-workspace".architect.build.configurations.development.fileReplacements = [{"replace":"src/environments/environment.ts","with":"src/environments/environment.development.ts"}]';
#src/assets (linked above) ships as assets/site - the same shape create-workspace.sh writes for each library's assets dir.
jqEdit angular.json '.projects."my-workspace".architect.build.options.assets |= ((. // []) | map(select((type=="object" and .input=="src/assets") | not)) + [{"glob":"**/*","input":"src/assets","output":"assets/site","followSymlinks":true}])';
echo ------------------- Starting Build -------------------;
#Output hashing: none for a dev build - the dist keeps main.js/styles.css, stable names for whatever points at them and
#no churn - and `all` for a release (--release: the workflows' tag runs), main-<8 chars>.js, which the hub serves as
#immutable (libs/web/server/StaticSite.cpp's cache policy) while index.html stays no-cache, so a new build is picked up
#on the next load and its assets are never revalidated.  Source maps either way: the installers skip *.map
#(apps/OpcHub/setup - reviews/install-issues.md, "Shipped weight"), so they cost nothing shipped and a local dist still debugs.
if [ $release = 1 ]; then hashing=all; else hashing=none; fi
ng build --output-hashing=$hashing --source-map=true;