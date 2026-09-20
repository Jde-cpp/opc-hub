#!/bin/bash
#update-angular.sh installs/updates the global @angular/cli - run it first to create the workspace on current Angular.
#sudo npm install -g npm@latest
#sudo apt  install jq
#chmod 777 ../WebFramework/jde-framework-proto.sh
#run from my-workspace/..
#../WebFramework/create-workspace.sh my-workspace MaterialSite WebFramework TwsWebsite WebBlockly
workspace=${1:-my-workspace};
librariesText=( "$@" );
declare -a libraries=();
for i in "${!librariesText[@]}"; do if (( $i > 0 )); then t=${librariesText[$i]}; libraries+=($t); libraryLog="$libraryLog $t";  fi; done;
echo create-workspace.sh workspace=$workspace libraries=\"${libraryLog:1}\";

scriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )";
echo $scriptDir;
source $JDE_BASH/build/common.sh;
source $scriptDir/common-ng.sh;
baseDir=`pwd`;
echo $baseDir;
#REPO_WEB=`readlink -f $scriptDir/../..`;
findExecutable npm;
#bootstrap only - an already-installed cli is left at whatever version it is on, since bumping it here would change
#the Angular a workspace comes out on as a side effect of creating one.  update-angular.sh is that entry point.
if [ ! -x "$(which "ng" 2> /dev/null)" ]; then
	npmInstallGlobal @angular/cli || { echo `pwd`; echo npm install -g @angular/cli failed; exit 1; };
fi;
##################
#jqEdit lives in build/common.sh (sourced above) - the per-site setup.sh scripts need it too.
##################
if [ ! -d $workspace ]; then
	echo -------------------- create workspace start --------------------;
	#the workspace must come out on the same Angular as the global cli that scaffolds it.  `ng new` already writes
	#its own version, but the installs below default to @latest and would drag a newer major into a workspace built
	#by an older cli.  ngGlobalVersion (common-ng.sh) reads it off the resolved cli's own package.json; keeping that
	#version current is update-angular.sh's job, not this script's.
	ngGlobalVersion ngVersion;
	if [ -z "$ngVersion" ]; then echo `pwd`; echo could not read the global @angular/cli version near `which ng`; exit 1; fi;
	ngMajor=${ngVersion%%.*};
	echo global @angular/cli $ngVersion - creating the workspace to match;
	createApplication=true;
	cmd="ng new $workspace --create-application=$createApplication --ai-config=claude-code --defaults --routing=false --style=scss"
	$cmd; if [ $? -ne 0 ]; then echo $cmd; exit 1; fi;
	echo -------------------- create workspace complete --------------------;
	cd $workspace;
	#claude code reads .mcp.json from the workspace root, so give the workspace its own angular cli mcp server
	#(list_projects, get_best_practices, search_documentation, ai_tutor).  pinned for the same reason as the installs
	#below - an unpinned npx fetches @latest and would answer for a cli the workspace was not built with.
	cat > .mcp.json <<-EOF
	{
	  "mcpServers": {
	    "angular-cli": { "command": "npx", "args": ["-y", "@angular/cli@$ngVersion", "mcp"] }
	  }
	}
	EOF
	if [ $? -ne 0 ]; then echo `pwd`; echo could not write .mcp.json; exit 1; fi;
	jqEdit angular.json ".projects.\"$workspace\".architect.build.configurations.production.budgets[0].maximumError = \"2mb\"";
	jqEdit angular.json ".projects.\"$workspace\".architect.build.configurations.production.budgets[0].maximumWarning = \"1mb\"";
	#was a sed on '"strict": true,' - Angular 22's --defaults template emits the individual noImplicit* flags and no
	#"strict" key at all, so it matched nothing and exited 0.  These are the tsc defaults today (strict unset), but
	#state them so the libraries keep building if a future template turns strict on.
	jqEdit tsconfig.json '.compilerOptions += {"strictPropertyInitialization":false, "strictNullChecks":false, "noImplicitAny":false, "noImplicitThis":false, "allowSyntheticDefaultImports":true}';
	jqEdit angular.json ".cli.analytics = false";
	#library sources are symlinked in from web/<lib>/control below - without preserveSymlinks tsc and esbuild resolve them to their real paths outside the workspace, which breaks the compilation and the sass node_modules lookup.
	jqEdit angular.json ".projects.\"$workspace\".architect.build.options.preserveSymlinks = true";
	jqEdit tsconfig.json ".compilerOptions.preserveSymlinks = true";
	#ng analytics disable;
	echo -------------------- npm install start --------------------;
	#Material only.  What a library needs beyond it, its own script installs (jde-spa.sh: marked; jde-framework-proto.sh:
	#@bufbuild/protobuf, long, ts-proto).  Not installed, on purpose: material-design-icons - the icon fonts come from
	#Google Fonts (site/index.html); @types/long - long 5 ships its own types; chalk, jsdoc, uglify-js - pbjs's cli
	#dependencies, gone with protobufjs.
	#@angular/animations is not installed: material 22 has no dependency on it, nothing here uses the animations
	#metadata api, and the package is deprecated in v22 in favour of animate.enter/animate.leave.
	#material/cdk have their own patch cadence (no @angular/material 22.0.8 exists when the cli is 22.0.8), so pin
	#the major rather than $ngVersion.
	ng add @angular/material@$ngMajor --defaults --skip-confirmation; #use skip-confirmation when interactive
	echo -------------------- material installed --------------------;
	#echo `pwd`;
	#exit 1;
	echo -------------------- npm install complete --------------------;
	echo `pwd`;
	#no protobufjs bootstrap appended to main.ts any more: ts-proto's generated code carries its own wire runtime
	#(@bufbuild/protobuf) and takes Long as a normal import, so there is no global registry to configure.
else
	echo workspace already exists
	cd $workspace;
fi;
pushd `pwd` > /dev/null;
##################
function execute() {
	file=$1;
	if [ -f  $file ];then
		t=$(stat -c "%a %n" $file); if [[ ${t:0:1} != "7" ]] ; then chmod 7${t:1:2} $file; fi;
		$file $2; if [ $? -ne 0 ]; then echo `pwd`; echo $file $2; exit 1; fi;
	fi;
}
##################
startDir=`pwd`;
#jde-proto holds the generated proto surface for every library (see jde-framework-proto.sh).  It is a plain package,
#not an Angular library - its own tsconfig compiles it - so it is linked in and mapped by path rather than added to
#angular.json.  Keeping it out of the libraries is what lets `ng build <lib>` package them at all: ng-packagr cannot
#carry a relatively-imported declaration file into a library's typings.
protoDir=$(dirname ${libraries[0]})/proto;
if [ -d $protoDir ]; then
	mklinkDir proto $(dirname $protoDir);
	#a /* subpath mapping, so jde-proto/App.FromServer resolves to proto/App.FromServer.d.ts.
	jqEdit tsconfig.json '.compilerOptions.paths["jde-proto/*"] = ["./proto/*"]';
fi;
#the names of every library in this workspace, needed before the loop body can write one library's view of its siblings.
declare -a libraryNames=();
for libraryDir in "${libraries[@]}"; do
	libraryNames+=( $( basename $(jq -er .name $libraryDir/control/package.json) ) ) || { echo `pwd`; echo no .name in $libraryDir/control/package.json; exit 1; };
done;
#One version for the whole repo: the libraries carry the same JDE_VERSION the C++ targets are built with
#(CMakePresets.common.json, read by jdeVersion in build/common.sh).  Stamped onto the generated
#projects/<lib>/package.json below rather than into the tracked control/package.json, which stays the source of
#truth for .name and .peerDependencies only.
jdeVersion jdeVer;
echo stamping the libraries at version $jdeVer;
#the same names as a jq array literal, to spot the sibling jde-* peers below.  jde-proto is deliberately not in it -
#it is not a workspace library, ships from the tracked web/proto/package.json, and keeps that file's version.
libraryNamesJson=$(printf '%s\n' "${libraryNames[@]}" | jq -Rsc 'split("\n") | map(select(length > 0))');
echo -------------------- Start Libraries --------------------;
for libraryDir in "${libraries[@]}"; do
	echo $libraryDir - processing;
	#findExecutable jq
	#-e so jq exits non-zero when .name is absent - plain `jq -r` prints the string "null" and exits 0, and the
	#assignment on the next line would clobber the status anyway, so $? was only ever testing basename.
	dest=$(jq -er .name $libraryDir/control/package.json) || { echo `pwd`; echo no .name in $libraryDir/control/package.json; exit 1; };
	#dest=`perl -MJSON -e '$/ = undef; my $data = <>; for my $hash (new JSON->incr_parse($data)) { print $hash->{name}; }' < $libraryDir/control/package.json`;
	library=$( basename $dest );
	if [ ! -d projects/$library ]; then
		$scriptDir/create-library.sh $library $libraryDir; if [ $? -ne 0 ]; then echo `pwd`; echo $scriptDir/WebFramework/create-library.sh $library $libraryDir; exit 1; fi;
	fi;
	#A library build compiles that library's sources ONLY.  The workspace tsconfig maps every jde-* package source-first
	#(create-library.sh writes ["./projects/<lib>/src/public-api", "./dist/..."]), which is what lets `ng build/serve
	#my-workspace` compile the libs straight from the symlinked projects/ tree - but under `ng build <lib>` it drags the
	#sibling's .ts into this library's compilation, outside its rootDir, and ng-packagr fails with a wall of TS6059.
	#So point the siblings at their built output here, in the library's own tsconfig.lib.json (tsconfig.lib.prod.json
	#extends it, and paths resolve relative to the file that declares them).  `paths` replaces wholesale rather than
	#merging per key, so every sibling has to be listed - self excluded, since no library imports its own package name.
	#Libraries must then be built in dependency order, each into dist/: jde-spa, jde-framework, jde-access, jde-opc.
	#jde-proto is not an Angular library and has no dist: pbjs/pbts already emit the shipping .js/.d.ts pair, so it is
	#consumed straight from the workspace `proto` symlink in every build.
	libraryPaths="\"jde-proto/*\":[\"../../proto/*\"],";
	for sibling in "${libraryNames[@]}"; do
		if [ "$sibling" != "$library" ]; then libraryPaths="$libraryPaths\"$sibling\":[\"../../dist/$sibling\"],"; fi;
	done;
	jqEdit projects/$library/tsconfig.lib.json ".compilerOptions.paths = {${libraryPaths%,}}";
	#`ng test <lib>` needs the APPLICATION's build options, preserveSymlinks above all:  the library sources are symlinked in,
	#and without it esbuild resolves each spec to its real path outside the workspace while the TS program holds the
	#projects/<lib>/ one - every library spec then failed the build with "not found in TypeScript compilation" and the target
	#ran no tests at all.  The unit-test builder has no preserveSymlinks option of its own, buildTarget is how it inherits one,
	#and the default (the library's own ng-packagr build target) carries none.  The application's test target needs nothing:
	#it already defaults to <workspace>:build.
	jqEdit angular.json ".projects.\"$library\".architect.test.options.buildTarget = \"$workspace:build\"";
	#the generated projects/$library/package.json is what ng-packagr publishes, and `ng g library` only writes it when the
	#project is first created (with whatever @angular/* range that cli scaffolds).  The tracked control/package.json is
	#the source of truth for what the library needs at runtime - @angular/* + rxjs ranges and the jde-* siblings it
	#imports - so copy its peerDependencies over wholesale on every run.  Was a grep for `from 'jde-proto/` that added
	#that one peer and left the sibling jde-* libraries and the material/cdk/forms/router imports undeclared.
	peers=$(jq -ec .peerDependencies $libraryDir/control/package.json) || { echo `pwd`; echo no .peerDependencies in $libraryDir/control/package.json; exit 1; };
	#the version goes on in the same pass, and every sibling jde-* peer is re-pinned to it: control/package.json pins
	#them at the scaffolded 0.0.1, which no longer resolves once the packages carry a real version.
	jqEdit projects/$library/package.json ".version = \"$jdeVer\" | .peerDependencies = ($peers | with_entries(.key as \$k | .value = (if ($libraryNamesJson | index(\$k)) then \"$jdeVer\" else .value end)))";
	#unchecked, a failed cd left cwd at the workspace root and the rm -rf below ran there instead.
	cd $baseDir/$workspace/projects/$library/src; if [ $? -ne 0 ]; then echo `pwd`; echo cd $baseDir/$workspace/projects/$library/src; exit 1; fi;
	#public-api.ts is the library's export surface.  It used to be hard-linked by create-library.sh, which only runs
	#when projects/$library is absent - so once a replace-and-rename write (editor save, sed -i, git checkout) broke
	#the link, the workspace copy froze and re-running setup could not repair it.  Symlink it alongside lib/, on every
	#run.  mklink (build/common.sh) drops any existing file or stale symlink first and carries the windows branch.
	if [ ! -f $libraryDir/control/src/public-api.ts ]; then echo `pwd`; echo $libraryDir/control/src/public-api.ts not found; exit 1; fi;
	mklink public-api.ts $libraryDir/control/src; if [ $? -ne 0 ]; then echo `pwd`; echo mklink public-api.ts $libraryDir/control/src; exit 1; fi;
	#mklinkDir, not a raw ln -s: build/common.sh carries the windows() branch (cmd mklink /D), which a bare ln -s
	#silently loses - on Git Bash it writes an unusable ~120-byte stub file instead of a link.
	if [ -d $libraryDir/control/src/lib ]; then
		#addHardDir lib $libraryDir/control/src;
		#only a real directory needs the recursive delete.  Unlinking a symlink must never go through rm -rf, where a
		#stray trailing slash (`rm -rf lib/`) follows the link and empties the source tree instead of removing it.
		if [ -L lib ]; then rm lib; elif [ -d lib ]; then echo removing workspace lib directory; rm -rf ./lib; fi;
		echo mklinkDir lib $libraryDir/control/src;
		mklinkDir lib $libraryDir/control/src;
	fi;
	if [ -d $libraryDir/control/src/assets ]; then
		#addHardDir assets $libraryDir/control/src;
		mklinkDir assets $libraryDir/control/src;
	fi;
	if [ -d $libraryDir/control/src/styles ]; then
		#addHardDir styles $libraryDir/control/src; fi;
		mklinkDir styles $libraryDir/control/src;
	fi;
	cd $baseDir/$workspace;
	#assets/<lib>/... is runtime content the library ships (the help markdown).  The dir was symlinked at projects/<lib>/src/assets
	#above; the builder only checks that the input sits inside the workspace and never realpaths it, and its glob reads through
	#a symlinked root.  followSymlinks is for a link INSIDE assets - the root needs nothing.  Re-applied every run, like the
	#test buildTarget above:  drop any entry for this input, then append (the type guard: entries may be plain strings).
	if [ -d $libraryDir/control/src/assets ]; then
		jqEdit angular.json ".projects.\"$workspace\".architect.build.options.assets |= ((. // []) | map(select((type==\"object\" and .input==\"projects/$library/src/assets\") | not)) + [{\"glob\":\"**/*\",\"input\":\"projects/$library/src/assets\",\"output\":\"assets/$library\",\"followSymlinks\":true}])";
	fi;
	execute $libraryDir/scripts/$library.sh;
	echo execute $libraryDir/scripts/$library-proto.sh $baseDir/$workspace;
	execute $libraryDir/scripts/$library-proto.sh $baseDir/$workspace;
	#if release-mode then
	#ng build $library; if [ $? -ne 0 ]; then echo `pwd`; echo ng build $library; exit 1; fi;
	#cd dist/$library; npm pack;
	cd $startDir
done;
echo -------------------- End Libraries --------------------;
echo create-workspace.sh success