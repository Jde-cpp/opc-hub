#!/bin/bash
#Boost and the sqlite amalgamation for the Linux clang/libc++ build - the two deps the superbuild (build/CMakeLists.txt,
#the -repos presets) does not cover.  Installs under $REPO_DIR/install/clang++/<Debug|RelWithDebInfo>/{boost,sqlite},
#where the -jde presets' CMAKE_PREFIX_PATH finds them (the sqlite driver appends /sqlite itself).  Re-runnable.
#Sources, unpacked beside $REPO_DIR/install:
#  https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.gz  -> $REPO_DIR/boost_1_92_0
#  https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip          -> $REPO_DIR/sqlite-amalgamation-3530400
set -euo pipefail;
if [[ -z ${REPO_DIR:-} ]]; then echo "REPO_DIR is not set."; exit 1; fi;
boostDir=$REPO_DIR/boost_1_92_0;
sqliteDir=$REPO_DIR/sqlite-amalgamation-3530400;
for dir in $boostDir $sqliteDir; do [ -d "$dir" ] || { echo "$dir not found - download it per the header."; exit 1; }; done;

#Boost - json + charconv (container comes in as json's dependency).  toolset=clang uses whatever /usr/bin/clang++
#resolves to (update-alternatives -> clang++-23), so nothing here pins a compiler version.  Debug carries ASan to match
#the linux-clang-debug preset's -fsanitize=address -static-libasan.
cd $boostDir;
./bootstrap.sh --with-toolset=clang;
./b2 -j$(nproc) toolset=clang variant=debug address-sanitizer=on cxxflags="-stdlib=libc++" linkflags="-stdlib=libc++ -static-libasan" --prefix=$REPO_DIR/install/clang++/Debug/boost --with-json --with-charconv install;
./b2 -j$(nproc) toolset=clang variant=release debug-symbols=on cxxflags="-stdlib=libc++" linkflags="-stdlib=libc++" --prefix=$REPO_DIR/install/clang++/RelWithDebInfo/boost --with-json --with-charconv install;

#sqlite - a static lib from the amalgamation.  No ASan on purpose.
sqliteOpts="-DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_COLUMN_METADATA -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0";
cd $sqliteDir;
for cfg in Debug:"-g -O0" RelWithDebInfo:"-g -O2"; do
	dst=$REPO_DIR/install/clang++/${cfg%%:*}/sqlite; mkdir -p $dst/lib $dst/include;
	clang -c ${cfg#*:} -fPIC $sqliteOpts sqlite3.c -o sqlite3.o;
	ar rcs $dst/lib/libsqlite3.a sqlite3.o;
	cp sqlite3.h sqlite3ext.h $dst/include/;
done;
