cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../../build/functions.cmake )

#A libFuzzer executable over the decoder, behind JDE_FUZZ and run by hand, for an hour before each Open Protocol milestone
#closes (docs/open-protocol/plan.md *Phase 1*).  No runner builds it:  the suite replays the corpus (../CorpusTests.cpp).
#	cmake -B $B -S <source> -Wno-dev --preset linux-clang-debug-jde -DJDE_FUZZ=ON
#	cmake --build $B -j8 --target Jde.Opc.OpenProtocol.Fuzz
#	cd <scratch> && $B/libs/opc/openProtocol/tests/fuzz/Jde.Opc.OpenProtocol.Fuzz -max_len=600 -max_total_time=3600 finds <source>/libs/opc/openProtocol/tests/corpus
#	$B/libs/opc/openProtocol/tests/fuzz/Jde.Opc.OpenProtocol.Fuzz -merge=1 <source>/libs/opc/openProtocol/tests/corpus finds
#The first run writes what it finds to finds/ and a crash to ./crash-<sha1>;  the merge adds to the corpus the finds that
#reach code the corpus did not.  A crash's file goes into the corpus by hand, with its fix.
#It compiles the codec's sources itself, so they carry libFuzzer's coverage instrumentation, which the library's do not.
set( targetName Jde.Opc.OpenProtocol.Fuzz )
project( ${targetName} VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
message( ${targetName} )

if( NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" )
	message( FATAL_ERROR "JDE_FUZZ generates libFuzzer targets, which take clang on Linux." )
endif()

add_executable( ${targetName} )
compileOptions( ${targetName} )
file( GLOB codec CONFIGURE_DEPENDS ../../src/*.cpp )
target_sources( ${targetName} PRIVATE Fuzz.cpp ${codec} )
target_compile_options( ${targetName} PRIVATE -fsanitize=fuzzer,address )
target_link_options( ${targetName} PRIVATE -fsanitize=fuzzer,address )
target_link_libraries( ${targetName} Jde )
#The packaged libFuzzer runtime is built on libstdc++ and the presets link libc++;  the two meet only at the C entry.  By
#its file name, since under -stdlib=libc++ the driver reads -lstdc++ as -lc++.
target_link_libraries( ${targetName} -l:libstdc++.so.6 )

target_precompile_headers( ${targetName} PRIVATE ../../src/pc.h )