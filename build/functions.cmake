cmake_path( SET jdeRoot NORMALIZE ${CMAKE_CURRENT_LIST_DIR}/.. )
#Build-tree root for generated headers (protoc output).  Each protobuf_generate caller emits into
#${jdeGeneratedIncludeDir}/jde/<lib>/proto and exports this root PUBLIC, so consumers keep the <jde/app/proto/X.pb.h>
#spelling and every build tree resolves it from its own output.  Nothing generated is written into, or linked from, the
#source tree: the former linkGeneratedHeader symlinks under include/ pointed at whichever tree built last, so deleting
#(or not mounting) that tree broke every other one - and a dangling link looks up to date to ninja on windows, so the
#build could not repair it.
cmake_path( SET jdeGeneratedIncludeDir NORMALIZE ${CMAKE_BINARY_DIR}/include )
#Note: file(GLOB) calls repo-wide deliberately omit CONFIGURE_DEPENDS - adding a new source file requires a manual reconfigure.
#Two exceptions.  sqliteProcModule (below): its targets are MODULEs, where a source missing from a stale glob still links -
#undefined symbols are legal - and only fails at dlopen.  And gtest executables (libs/access/tests, libs/app/tests): a new
#self-contained *Tests.cpp is referenced by no other translation unit, so a stale glob drops it with no link error, no
#warning and a green ctest run - the suite's own invariant is the opposite of "a missed source is a link error"
#(access-review3 #30, app-review3 T9).  The other test targets have the same exposure and have not been converted.
#Everywhere else a missed source is a link error, which is the check.

if( CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR )
	message( FATAL_ERROR "In-source builds are not allowed. Configure from an out-of-source build directory, e.g.:\n  cd $JDE_BUILD_DIR/$JDE_COMPILER/<repo-name> && cmake ${CMAKE_SOURCE_DIR} --preset <preset>" )
endif()

#Nothing here uses modules (no `export module`, no `import`), but the C++26 standard level turns scanning on by
#default, costing a clang scan pass per TU plus a dyndep regen per target.  It also makes every object edge in a
#target depend on that target's single CXX.dd, so touching one source provisionally dirties all of them (a one-file
#change reads as a whole-target rebuild in ninja's progress count).  Must be set before any target is created -
#it seeds the CXX_SCAN_FOR_MODULES property at add_library/add_executable time.  Turn back on to adopt `import std`.
set( CMAKE_CXX_SCAN_FOR_MODULES OFF )

if( CMAKE_HOST_WIN32 )
	set( CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" )
	set( CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" )
	set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" )
	set( CMAKE_PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin" )
else()
	#$ORIGIN first in every exe's and .so's RUNPATH.  The build tree still resolves through the absolute entries cmake
	#appends after it (the build dirs, the $REPO_DIR deps); a staged tree - apps/OpcHub/setup/linux/build-deb.sh puts the
	#exe, libJde*.so, the sqlite modules and the bundled third-party .so's in one dir - resolves beside the exe, with no
	#patchelf pass needed.  BUILD_RPATH, not INSTALL_RPATH: the presets point CMAKE_INSTALL_PREFIX at the deps tree, so
	#nothing here is ever `cmake --install`ed.
	set( CMAKE_BUILD_RPATH "$ORIGIN" )
endif()

function(boost)
	if( WIN32 )
		set( Boost_NO_WARN_NEW_VERSIONS ON )
		set( _boostSrc $ENV{REPO_DIR}/boostorg/boost_1_91_0 )
		include_directories( ${_boostSrc} )
		add_compile_definitions( BOOST_ALL_NO_LIB=1 )
		#Boost.JSON is not built as a library: Jde.dll exports it.  io/json.cpp compiles <boost/json/src.hpp> in and the Jde
		#target defines BOOST_JSON_SOURCE, making BOOST_JSON_DECL dllexport there; BOOST_JSON_DYN_LINK here makes it dllimport
		#for everyone else, which all link Jde.lib anyway.  (A static boost_json.lib used to give every dll/exe its own copy.)
		if( NOT TARGET boost_json )
			add_library( boost_json INTERFACE )
			add_library( Boost::json ALIAS boost_json )
			target_include_directories( boost_json SYSTEM INTERFACE ${_boostSrc} )
			target_compile_definitions( boost_json INTERFACE BOOST_JSON_DYN_LINK )
		endif()
		#charconv is a compiled library (the mysql driver's Boost.MySQL needs it).  There is no BoostConfig.cmake in this
		#source tree, so find_package( Boost COMPONENTS charconv ) cannot work here - compile its sources into a static lib.
		if( NOT TARGET boost_charconv )
			add_library( boost_charconv STATIC ${_boostSrc}/libs/charconv/src/from_chars.cpp ${_boostSrc}/libs/charconv/src/to_chars.cpp )
			add_library( Boost::charconv ALIAS boost_charconv )
			target_include_directories( boost_charconv SYSTEM PUBLIC ${_boostSrc} )
		endif()
	else()
		cmake_policy(SET CMP0167 NEW)
		find_package( Boost REQUIRED COMPONENTS json )
		include_directories( ${Boost_INCLUDE_DIRS} )
	endif()
endfunction()

# protobuf_generate(TARGET...)'s .pb.cc outputs don't exist yet at configure time (they're
# produced by a build-time custom command), so file(GLOB ${outDir}/*.pb.cc) would find nothing
# on a fresh checkout. Derive the expected output paths from the known .proto source list
# instead - set_source_files_properties doesn't require the file to exist yet.
function( suppressProtoWarnings protos outDir )
	if( NOT MSVC )
		set( _protoSources )
		foreach( _proto ${protos} )
			get_filename_component( _name ${_proto} NAME_WLE )
			list( APPEND _protoSources ${outDir}/${_name}.pb.cc )
		endforeach()
		set_source_files_properties( ${_protoSources} PROPERTIES COMPILE_OPTIONS "-Wno-nullability-extension;-Wno-invalid-offsetof" )
	endif()
endfunction()

#Configure-time symlink for static config files - idempotent; replaces the old POST_BUILD create_symlink steps that reran every build.
function( linkConfigFile src dst )
	file( REMOVE ${dst} )
	file( CREATE_LINK ${src} ${dst} SYMBOLIC )
endfunction()

function(dumpVariables)
	get_cmake_property(_variableNames VARIABLES)
	list (SORT _variableNames)
	foreach (_variableName ${_variableNames})
#        if ((NOT DEFINED ${ARGV0}) OR _variableName MATCHES ${ARGV0})
					message(STATUS "${_variableName}=${${_variableName}}")
#        endif()
	endforeach()
endfunction()

if( WIN32 )
	function( copyLibDlls )
		set( buildLibDir ${CMAKE_BINARY_DIR}/libs )
		add_custom_command( TARGET ${targetName} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_INSTALL_PREFIX}/fmt/bin/fmt$<IF:$<CONFIG:Debug>,d,>.dll" $<TARGET_FILE_DIR:${targetName}>  COMMENT "fmtd.dll" )
		add_custom_command( TARGET ${targetName} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_INSTALL_PREFIX}/zlib/bin/z$<IF:$<CONFIG:Debug>,d,>.dll" $<TARGET_FILE_DIR:${targetName}> COMMENT "copy z.dll" )
	endfunction()
	#Stages the shared-library targets named in ARGN (+their pdbs) next to ${targetName}, for the targets whose
	#RUNTIME_OUTPUT_DIRECTORY is not <buildDir>/bin.  Defaults to `Jde Jde.DB` - the pair nearly every consumer needs -
	#so pass an explicit list to narrow it: the staging edge is a build-order dependency, so naming a dll the exe never
	#loads builds it for nothing (`--target Jde.Fwk.Tests` used to build Jde.DB that way).
	#Deliberately NOT a POST_BUILD step on ${targetName}: ninja lists bin/Jde.dll only as an order-only input (`||`) of the
	#consumer's link, and lld-link leaves the import lib byte-identical when the exported symbols don't change, so RESTAT
	#prunes the consumer's relink - a POST_BUILD command hanging off that link then silently never runs and leaves a stale
	#dll beside the exe (edit a function body, debug the old code).  An OUTPUT rule that DEPENDS on the dlls themselves is a
	#first-class edge that reruns whenever they are rewritten, relink or not.
	#The stamp exists because the destination cannot be the OUTPUT: for the targets that do live in bin, that path is
	#already the dll's own producing rule and ninja rejects the duplicate (the copy is then a no-op onto itself).
	#The pdbs are copied but kept out of DEPENDS - ninja knows no rule producing them, so listing them fails a clean tree.
	function( copyCommonDlls )
		copyLibDlls()
		set( dlls ${ARGN} )
		if( NOT dlls )
			set( dlls Jde Jde.DB )
		endif()
		foreach( dll ${dlls} )
			list( APPEND dllFiles $<TARGET_FILE:${dll}> )
			list( APPEND pdbFiles $<TARGET_PDB_FILE:${dll}> )
		endforeach()
		string( REPLACE ";" "/" dllNames "${dlls}" ) #COMMENT is one string: keep it readable instead of `Jde;Jde.DB`.
		set( stamp ${CMAKE_CURRENT_BINARY_DIR}/${targetName}.dlls.stamp )
		add_custom_command( OUTPUT ${stamp}
			COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${targetName}> #may not exist yet: this runs before ${targetName} links.
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dllFiles} ${pdbFiles} $<TARGET_FILE_DIR:${targetName}>
			COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
			DEPENDS ${dlls} ${dllFiles}
			COMMENT "copy ${dllNames} dlls -> ${targetName}"
		)
		add_custom_target( ${targetName}.dlls DEPENDS ${stamp} )
		add_dependencies( ${targetName} ${targetName}.dlls ) #staging depends on ${dlls}, not on ${targetName}, so this is not a cycle - and `--target ${targetName}` stages too.
	endfunction()
endif()

function(compileOptions)
	if( NOT MSVC )
		message( VERBOSE "compileOptions: ${ARGV0} -Wall -Wextra -pedantic -Werror ${EXCLUDED_WARNINGS}" )
		target_compile_options( ${ARGV0} PRIVATE -Wall -Wextra -pedantic -Werror ${EXCLUDED_WARNINGS} )
		if( NOT "$ENV{OPTIMIZATION_LEVEL}" STREQUAL "" )
			target_compile_options( ${ARGV0} PRIVATE -$ENV{OPTIMIZATION_LEVEL} )
		endif()
		set_property( TARGET ${ARGV0} PROPERTY POSITION_INDEPENDENT_CODE ON )
	endif()
	#Every exe runs with UTF-8 as its ANSI code page (Windows 10 1903+), so the narrow Win32/CRT calls - _get_pgmptr,
	#_dupenv_s, fs::path::string(), the A functions - hand back and take UTF-8, the encoding the rest of the code and the
	#logs assume.  Under cp1252 a profile path wrote 'ë' as byte EB into every log, and a letter outside the code page
	#became '?' (reviews/install-issues.md #45).  A .manifest source is merged into the linker's own (/MANIFESTINPUT).
	get_target_property( type ${ARGV0} TYPE )
	if( WIN32 AND type STREQUAL "EXECUTABLE" )
		target_sources( ${ARGV0} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/utf8.manifest )
	endif()
endfunction()

#Registers targetName with ctest: runs from ${CMAKE_BINARY_DIR}/Testing with the env vars the jsonnet
#configs expand via $(REPO_SOURCE_DIR)/$(REPO_BUILD_DIR); extra COMMAND args can follow the settings file.
#`-include=args/sqlite -arg path=:memory:` is the default so every ctest run is self-contained (in-memory sqlite, no
#db server): the db-backed suites need it as a pair, and fwk/web/sqlite-driver import no args dir and don't read
#`path`, so it is inert for them.  Extra args in ARGN follow it.
#app-review3 T9: seconds before ctest kills a test.  Nothing in the suites has a deadline of its own - BlockAwait waits
#forever - so a lost LockKey or a timer that never fires wedges the run, and CI invokes plain `ctest` (linux-ci.yml),
#where the only backstop is ctest's own 1500s default: 25 minutes of a hung job per suite.  300s is over twenty times
#the slowest suite on record (Jde.Opc.Tests, ~14s in Testing/Temporary/CTestCostData.txt) and matches the `--timeout`
#the run-services skill passes.  Raise it for a slow machine with -DJDE_TEST_TIMEOUT=<seconds>.  Note the property wins
#over `ctest --timeout`, so that flag can no longer lower the bound for these targets - the cache variable is the knob.
set( JDE_TEST_TIMEOUT 300 CACHE STRING "Seconds before ctest kills one of this repo's test targets" )

function( addJdeTest targetName settingsFile )
	add_test(
		NAME ${targetName}
		COMMAND $<TARGET_FILE:${targetName}> -ctest -settings=${settingsFile} -include=args/sqlite -arg path=:memory: ${ARGN}
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/Testing
	)
	set_tests_properties( ${targetName} PROPERTIES
		ENVIRONMENT "REPO_SOURCE_DIR=${CMAKE_SOURCE_DIR};REPO_BUILD_DIR=${CMAKE_BINARY_DIR}/.."
		TIMEOUT ${JDE_TEST_TIMEOUT}
	)
endfunction()

#Build-order dependency on the sqlite driver plus the native-proc MODULEs targetName dlopen's at runtime from the
#paths in its jsonnet.  They are never linked, so without this nothing makes the build produce them and the run dies
#in DB::DataSource with "Dynamic Library ... not found".  Jde.DB.Sqlite is implied - a proc MODULE is only reachable
#through it.  Deliberately unguarded: add_dependencies resolves at generate time, so naming a target defined by a
#later add_subdirectory is fine, whereas an `if( TARGET )` guard would evaluate now and silently drop it.
function( sqliteProcDependencies targetName )
	add_dependencies( ${targetName} Jde.DB.Sqlite ${ARGN} )
endfunction()

#Native-proc MODULE for the sqlite driver - dlopen'd for sqlite_api.h's RegisterProcs( IProcs& ), never linked.
#Globs *.cpp/*.h from the calling directory plus any extra source dirs passed after the target name.
function( sqliteProcModule targetName )
	find_package( Threads REQUIRED )

	add_library( ${targetName} MODULE )
	compileOptions( ${targetName} )
	set_property( TARGET ${targetName} PROPERTY POSITION_INDEPENDENT_CODE ON )
	#RegisterProcs is exported from source via JDE_SQLITE_PROC in <jde/db/sqlite_api.h> - no /EXPORT: link flag needed,
	#and nothing ever reads the <target>_EXPORTS symbol CMake defines by default.  That default was the only thing
	#differing between the proc modules' command lines; pinning one shared DEFINE_SYMBOL makes them identical, which
	#is what lets them share a single PCH below.
	set_target_properties( ${targetName} PROPERTIES DEFINE_SYMBOL JDE_SQLITE_PROC_EXPORTS )

	#CONFIGURE_DEPENDS: the module is a MODULE, so a new proc twin that isn't in the glob still links (undefined
	#symbols are legal) and only fails at dlopen - re-glob on build instead of making that a reconfigure-or-else.
	foreach( dir ${CMAKE_CURRENT_SOURCE_DIR} ${ARGN} )
		file( GLOB sources CONFIGURE_DEPENDS ${dir}/*.cpp )
		file( GLOB headers CONFIGURE_DEPENDS ${dir}/*.h )
		target_sources( ${targetName} PRIVATE ${sources} ${headers} )
	endforeach()

	target_link_libraries( ${targetName} PRIVATE Threads::Threads ) #no sqlite3 link: the driver is reached only through IProcs (sqlite_api.h), which forward-declares sqlite3.
	target_link_libraries( ${targetName} PRIVATE fmt::fmt Jde.DB ) #PUBLIC-links Jde on WIN32, propagated transitively.

	#Every proc module precompiles the same four headers with now-identical flags, so only the first one
	#configured builds the PCH (~61MB) and the rest reuse it.  The owner is tracked in a global property
	#rather than hard-coded, so this holds both in the superbuild (three modules) and in a standalone app
	#configure (one module, which then simply builds its own).
	get_property( sqliteProcPchOwner GLOBAL PROPERTY jdeSqliteProcPchOwner )
	if( sqliteProcPchOwner )
		target_precompile_headers( ${targetName} REUSE_FROM ${sqliteProcPchOwner} )
	else()
		set_property( GLOBAL PROPERTY jdeSqliteProcPchOwner ${targetName} )
		target_precompile_headers( ${targetName}
		  PRIVATE
			<jde/fwk.h>
			<jde/fwk/str.h>
			<jde/fwk/io/json.h>
			<jde/fwk/chrono.h>
		)
	endif()
endfunction()
#A Windows exe's version resource from JDE_VERSION:  writes version.rc.h (build/version.rc.h.in) beside the target's build
#and puts that dir on its include path, so the checked-in .rc stays as it is and #includes the values.  The numbers are
#JDE_VERSION's without their leading zeros - 2026.09.02's "09" is an octal literal to rc - and the copyright year is its
#first part, not the build's clock, so a rebuild of a tag stamps the same bytes (reviews/m4-closing.md #20).
function( jdeVersionResource targetName )
	if( NOT JDE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" )
		message( FATAL_ERROR "JDE_VERSION '${JDE_VERSION}' is not yyyy.MM.dd - the version resource needs three numbers." )
	endif()
	math( EXPR jdeRcMajor "${CMAKE_MATCH_1}" )
	math( EXPR jdeRcMinor "${CMAKE_MATCH_2}" )
	math( EXPR jdeRcPatch "${CMAKE_MATCH_3}" )
	configure_file( ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/version.rc.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc.h @ONLY )
	target_include_directories( ${targetName} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} )
endfunction()
