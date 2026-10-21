#!/bin/bash

#Collects $1 and, transitively, every .proto it imports into the global $_protoDeps.  Imports are matched by basename
#against the cwd - the generation dir - because that is where linkProtos symlinks everything the compiler has to
#resolve; the google/protobuf/* ones buf ships itself are not linked in, so a name that is not there is not a dependency.
function protoDeps {
	local proto=$1;
	if [ -n "${_protoDeps[$proto]}" ] || [ ! -f $proto ]; then return 0; fi;
	_protoDeps[$proto]=1;
	local import;
	for import in `sed -n 's/^[[:space:]]*import[[:space:]]*"\([^"]*\)".*/\1/p' $proto`; do
		protoDeps `basename $import`;
	done;
}

#true when $1.ts still has to be generated - it is missing, or a .proto it was generated from has changed since.
#An import's timestamp counts as much as the target's own: Opc.FromServer.ts goes stale when Opc.Common.proto moves
#under it, not just when its own does.
#Only call this from an if/&&/|| condition - env.sh traps ERR, and the `return 1` here is an answer, not a failure.
function isStale {
	local name=$1;
	if [ ! -f $name.ts ]; then return 0; fi;
	unset _protoDeps; declare -gA _protoDeps;
	protoDeps $name.proto;
	local proto;
	for proto in "${!_protoDeps[@]}"; do
		if [ $proto -nt $name.ts ]; then return 0; fi;
	done;
	return 1;
}

#Symlink $2... (names, no .proto) out of $1 into the generation dir (the cwd).  Everything lands in one directory so
#cross-directory imports resolve - App.FromServer.proto imports Web.FromServer.proto out of libs/web/client/proto, for instance.
function linkProtos {
	local dir=$1;
	shift;
	local name;
	for name in "$@"; do
		if [ ! -f $name.proto ]; then
			mklink $name.proto $dir;
		fi;
	done;
}

#Generate TypeScript for every .proto in the cwd.  ts-proto emits real .ts - flat per-message interfaces plus a codec
#const - rather than protobufjs's static-module namespaces, so there is no shared `roots` registry and no need to keep
#one generated set apart from another.
#buf, not protoc: it resolves the google/protobuf well-known types itself and installs from npm like everything else
#here, where protoc would be a system dependency the web build does not otherwise have.
function generateProtos {
	local wsFolder=$1;
	if [ -z "$wsFolder" ]; then echo `pwd`; echo generateProtos: no workspace dir passed; exit 1; fi;
	local buf=$wsFolder/node_modules/.bin/buf;
	local plugin=$wsFolder/node_modules/.bin/protoc-gen-ts_proto;
	#do not use npx - these dirs are symlinked out of the workspace, so npm resolves its local prefix outside it and fetches unrelated registry packages.
	if [ ! -x $buf ] || [ ! -x $plugin ]; then echo $PS4 buf/ts-proto not found in $wsFolder/node_modules; exit 1; fi;
	#buf is a native binary.  On windows it cannot exec the bash shim behind that /c/... path, and CreateProcess will not run the
	#.cmd shim beside it either, so hand it node plus ts-proto's own entry point as a command list (a v2 template accepts one)
	#on windows-native paths - cygpath -m, since the list goes through JSON and toWinDir's backslashes would need escaping.
	local pluginSpec="\"$plugin\"";
	if windows; then
		pluginSpec="[\"$(cygpath -m "$(which node)")\",\"$(cygpath -m "$wsFolder/node_modules/ts-proto/protoc-gen-ts_proto")\"]";
	fi;
	#forceLong=long keeps 64-bit fields as Long, which ProtoUtils.toNumber and every uint64 tag field already expect;
	#the default (string) would silently change every one of those comparisons.
	local template="{\"version\":\"v2\",\"plugins\":[{\"local\":$pluginSpec,\"out\":\".\",\"opt\":[\"esModuleInterop=true\",\"forceLong=long\",\"outputJsonMethods=false\",\"useOptionals=messages\"]}]}";
	echo generateProtos pwd=`pwd`;
	$buf generate --template "$template" . || { echo `pwd`; echo $buf generate failed; exit 1; };
}

#Compile the generated .ts down to the .js/.d.ts pair the jde-proto package ships.  It is published as a plain package
#rather than compiled inside a library on purpose: ng-packagr cannot carry a relatively-imported declaration file into
#a library's typings, which is what used to make `ng build <lib>` impossible.  Keeping the .ts under src/ (and out of
#the package root) also stops a library build resolving jde-proto to source and pulling it outside its own rootDir.
function buildProtos {
	local wsFolder=$1;
	local protoDir=$2;
	local tsc=$wsFolder/node_modules/.bin/tsc;
	if [ ! -x $tsc ]; then echo $PS4 tsc not found in $wsFolder/node_modules; exit 1; fi;
	echo buildProtos $protoDir;
	$tsc -p $protoDir/tsconfig.json || { echo `pwd`; echo $tsc -p $protoDir/tsconfig.json failed; exit 1; };
}