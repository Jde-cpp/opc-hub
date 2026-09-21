#Configured via the root CMakeLists (jde_REPOS=ON), not this directory directly - e.g.:
#  cmake $JDE_DIR -B $JDE_BUILD_DIR/$JDE_COMPILER-repos/<repo-basename> --preset linux-relWithDebInfo-repos   (or linux-debug-repos, win-clang-debug-repos)
if( NOT DEFINED MIN_REQ_CMAKE_VERSION )
	message( FATAL_ERROR "MIN_REQ_CMAKE_VERSION not set - configure with a preset (see comment above)." )
endif()
cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ExternalProject )
include( ${CMAKE_CURRENT_SOURCE_DIR}/functions.cmake )
project( repos VERSION ${JDE_VERSION} LANGUAGES CXX )

set( stdArgs
	-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
	-DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
	-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
	-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
	-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
	-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
	-DCMAKE_CXX_FLAGS_DEBUG=${CMAKE_CXX_FLAGS_DEBUG}
)
if( isMultiConfig )
	set( stdArgs ${stdArgs} -DCMAKE_CONFIGURATION_TYPES=$ENV{buildType} )
endif()
if( WIN32 )
	# clang + lld-link on Windows can't link the cmake compiler test executable (mainCRTStartup).
	# Use STATIC_LIBRARY mode so cmake still identifies the compiler (features, ABI, MSVC runtime)
	# without needing to link a test executable.
	set( stdArgs ${stdArgs} -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_POLICY_DEFAULT_CMP0091=NEW )
	# CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY above means try_compile() only archives objects and never
	# links, so CheckLibraryExists()-based probes (e.g. FindThreads' search for -lpthreads/-lpthread, or
	# LibXml2's search for -ldld/shl_load) report false positives instead of the real linker failure. The
	# win-clang-debug-repos preset pre-seeds these so the probes are skipped and fall through to the correct
	# native Windows behavior; forward them to the sub-builds here.
	set( stdArgs ${stdArgs} -DCMAKE_HAVE_LIBC_PTHREAD=${CMAKE_HAVE_LIBC_PTHREAD} -DCMAKE_HAVE_PTHREADS_CREATE=${CMAKE_HAVE_PTHREADS_CREATE} -DCMAKE_HAVE_PTHREAD_CREATE=${CMAKE_HAVE_PTHREAD_CREATE} -DTHREADS_HAVE_PTHREAD_ARG=${THREADS_HAVE_PTHREAD_ARG} -DHAVE_SHLLOAD=${HAVE_SHLLOAD} )
	if( CMAKE_MSVC_RUNTIME_LIBRARY )
		set( stdArgs ${stdArgs} -DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY} )
	endif()
	if( CMAKE_GENERATOR_PLATFORM )
		set( stdArgs ${stdArgs} -DCMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM} )
	elseif( CMAKE_VS_PLATFORM_NAME )
		set( stdArgs ${stdArgs} -DCMAKE_GENERATOR_PLATFORM=${CMAKE_VS_PLATFORM_NAME} )
	endif()
	if( CMAKE_TOOLCHAIN_FILE )
		set( stdArgs ${stdArgs} -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET} -DVCPKG_MANIFEST_MODE=OFF )
	endif()
else()
	set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-rpath=$ORIGIN" )
	set( stdArgs ${stdArgs} -DCMAKE_POSITION_INDEPENDENT_CODE=ON )
endif()

message( CMAKE_CONFIGURATION_TYPES=${CMAKE_CONFIGURATION_TYPES} )
message( CMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} )

# Tags of dependencies that other superbuild projects compile against. Each consumer
# echoes the tag back as an inert -Djde_DEP_TAG_<dep> so its recorded configure command
# changes — forcing a reconfigure+rebuild — when the dependency is bumped. Stamps never
# invalidate across ExternalProjects on their own (StepDependencies only orders steps),
# so without the echo a bump leaves consumers built against the old headers.
# After bumping any GIT_TAG in this file, re-run ./third-party-notices.sh: the checked-in THIRD-PARTY-NOTICES.txt the
# installers ship names these versions.
set( fmtTag 12.2.0 )
set( abslTag 20260526.0 )
set( rymlTag v0.10.0 )
set( open62541Tag v1.5.6 )

ExternalProject_Add(fmt
	GIT_REPOSITORY https://github.com/fmtlib/fmt.git
	GIT_TAG ${fmtTag}
	PREFIX fmt
	INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/fmt
	CMAKE_ARGS ${stdArgs}
		-DBUILD_SHARED_LIBS=ON
		-DFMT_MASTER_PROJECT=OFF
		-DFMT_DOC=OFF
		-DFMT_INSTALL=ON
#-DCMAKE_POSITION_INDEPENDENT_CODE
)

ExternalProject_Add(gtest
	GIT_REPOSITORY https://github.com/google/googletest.git
	GIT_TAG v1.18.0
	PREFIX gtest
	INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/gtest
	LOG_CONFIGURE ON
  LOG_BUILD ON
	CMAKE_ARGS ${stdArgs}
		-DBUILD_GMOCK=OFF
)

ExternalProject_Add(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.17.0
	PREFIX spdlog
	INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/spdlog
	CMAKE_ARGS ${stdArgs}
		-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
		-Djde_DEP_TAG_fmt=${fmtTag}
		-DSPDLOG_BUILD_EXAMPLE:BOOL=OFF
		-DSPDLOG_BUILD_BENCH:BOOL=OFF
		-DSPDLOG_BUILD_TESTS:BOOL=OFF
		-DSPDLOG_FMT_EXTERNAL=${SPDLOG_FMT_EXTERNAL}
		-DSPDLOG_NO_THREAD_ID=ON
		-DSPDLOG_BUILD_SHARED=ON
		-DSPDLOG_NO_ATOMIC_LEVELS=ON
		-DSPDLOG_BUILD_PIC=ON
)

ExternalProject_Add(absl
	GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
	GIT_TAG ${abslTag}
	PREFIX absl
	INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/absl
	CMAKE_ARGS ${stdArgs}
		-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
		-DABSL_BUILD_TESTING=OFF
		-DABSL_USE_EXTERNAL_GOOGLETEST=ON
		#-DABSL_FIND_GOOGLETEST=ON
		-DABSL_PROPAGATE_CXX_STD=ON
)

# jsonnet is pinned to upstream v0.22.0 plus the source patches below (previously held
# as commits on a Jde-cpp fork). Shared by both platforms; the Windows-only RuntimeError
# ctor fix is enabled with -DPATCH_CTOR=ON.
set( _jsonnetPatch "${CMAKE_CURRENT_BINARY_DIR}/jsonnet-patch.cmake" )
set( _jsonnetPatchBody [=[
# Literal replace that fails the build when the needle is gone (upstream drift)
# and skips files already patched (re-runs). Writes only when changing content.
function(replace_or_die file needle replacement)
    file(READ "${file}" _c)
    string(FIND "${_c}" "${replacement}" _at)
    if(_at EQUAL -1)
        string(FIND "${_c}" "${needle}" _at)
        if(_at EQUAL -1)
            message(FATAL_ERROR "jsonnet patch no longer applies to ${file}: cannot find '${needle}'")
        endif()
        string(REPLACE "${needle}" "${replacement}" _c "${_c}")
        file(WRITE "${file}" "${_c}")
    endif()
endfunction()

# VERSION is not a find_package keyword; CMake treats VERSION/0.10.0 as required
# components, so ryml's check_required_components() sets ryml_FOUND to FALSE.
replace_or_die("${SOURCE_DIR}/CMakeLists.txt"
    "find_package(ryml REQUIRED VERSION 0.10.0)"
    "find_package(ryml 0.10.0 REQUIRED)")

# <ryml.hpp> only forward-declares c4::to_csubstr(std::string); the C4_ALWAYS_INLINE
# definition is in <ryml_std.hpp>, so without it vm.cpp.o has an undefined reference.
replace_or_die("${SOURCE_DIR}/core/vm.cpp"
    "#include <ryml.hpp>"
    "#include <ryml_std.hpp>\n#include <ryml.hpp>")

if(PATCH_CTOR) # RuntimeError's variadic ctor is ill-formed for win-clang; collapse to the std::string overload
    replace_or_die("${SOURCE_DIR}/core/vm.cpp"
        "Args&&... args): msg_(std::forward<Args>(args)...)"
        "std::string msg): msg_(std::move(msg))")
endif()
]=] )
file( WRITE "${_jsonnetPatch}" "${_jsonnetPatchBody}" )
# The patch stamp only records the script's *path* (patch-info.txt), so edits to the body
# would never re-apply; embedding the body's hash in the command makes the recorded
# command — and therefore the stamp — go stale whenever the script changes.
string( SHA1 _jsonnetPatchSha "${_jsonnetPatchBody}" )

IF( WIN32 )
	ExternalProject_Add(zlib
		GIT_REPOSITORY https://github.com/madler/zlib
		GIT_TAG "v1.3.2"
		PREFIX ZLIB
		INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/zlib
		CMAKE_ARGS ${stdArgs}
			-DZLIB_BUILD_SHARED=ON
			-DZLIB_BUILD_TESTING=OFF
	)
		ExternalProject_Add(
		LibXml2
		GIT_REPOSITORY https://github.com/GNOME/libxml2.git
		GIT_TAG v2.15.3
		PREFIX libxml2
		INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/libxml2
		CMAKE_ARGS ${stdArgs}
			-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
			-DLIBXML2_WITH_ZLIB=OFF
			-DLIBXML2_WITH_PYTHON=OFF
			-DLIBXML2_WITH_TESTS=OFF
			-DLIBXML2_WITH_ICONV=OFF
			-DLIBXML2_WITH_LZMA=OFF
			-DLIBXML2_WITH_THREADS=ON
			-DLIBXML2_BUILD_SHARED_LIBS=OFF
	)

	set( LIBXML2_INCLUDE_DIR ${CMAKE_INSTALL_PREFIX}/libxml2/include/libxml2 CACHE PATH "the directory containing LibXml2 headers" )
	set( LIBXML2_LIBRARY ${CMAKE_INSTALL_PREFIX}/libxml2/lib/libxml2.lib CACHE PATH "the directory containing LibXml2 libraries" )
	set( nodesetLoaderArgs
		-DLIBXML2_LIBRARY=${LIBXML2_LIBRARY}
		-DLIBXML2_INCLUDE_DIR=${LIBXML2_INCLUDE_DIR} )

	# c4core's _c4_get_platform_tag has no case for WIN32 + Clang (non-MSVC, non-MINGW).
	# Patch c4Project.cmake to fall back to a generic tag rather than calling c4_err("not implemented").
	set( _rymlPatch "${CMAKE_CURRENT_BINARY_DIR}/ryml-win-clang.cmake" )
	file( WRITE "${_rymlPatch}" [=[
file(READ "${SOURCE_DIR}/ext/c4core/cmake/c4Project.cmake" _c)
string(REPLACE
    "c4_err(\"not implemented\")"
    "set(\${var} \"win-clang\" PARENT_SCOPE)"
    _c "${_c}")
file(WRITE "${SOURCE_DIR}/ext/c4core/cmake/c4Project.cmake" "${_c}")

# alloca.h is POSIX-only; on Windows with non-MSVC clang use malloc.h instead
file(READ "${SOURCE_DIR}/src/c4/yml/common.hpp" _c)
string(REPLACE
    "#include <alloca.h>"
    "#ifdef _WIN32\n#  include <malloc.h>\n#  ifndef alloca\n#    define alloca _alloca\n#  endif\n#else\n#  include <alloca.h>\n#endif"
    _c "${_c}")
file(WRITE "${SOURCE_DIR}/src/c4/yml/common.hpp" "${_c}")
]=] )
	ExternalProject_Add(ryml
		GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
		GIT_TAG "${rymlTag}"
		PREFIX ryml
		INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/ryml
		PATCH_COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=<SOURCE_DIR>" -P "${_rymlPatch}"
		CMAKE_ARGS ${stdArgs}
			-DRYML_BUILD_TESTS=OFF
			-DRYML_BUILD_EXAMPLES=OFF
			-DRYML_BUILD_BENCHMARKS=OFF
			-DRYML_BUILD_TOOLS=OFF
		CMAKE_CACHE_ARGS
			"-DCMAKE_CXX_COMPILE_FEATURES:STRING=cxx_std_11;cxx_std_14;cxx_std_17;cxx_std_20;cxx_std_23;cxx_std_26"
		GIT_SUBMODULES_RECURSE TRUE
	)
	set( _jsonnetPatchArgs -DPATCH_CTOR=ON )
	set( jsonnetPlatformArgs
		"-DCMAKE_CXX_FLAGS_DEBUG=-g"
		-DBUILD_SHARED_LIBS=OFF
		-DBUILD_STATIC_LIBS=ON
		-DBUILD_SHARED_BINARIES=OFF
	)
else()
#	message( LIBXML2_LIBRARY=${LIBXML2_LIBDIR} )
#	message( LIBXML2_INCLUDE_DIR=${LIBXML2_INCLUDE_DIR} )
	set( nodesetLoaderArgs
		-DLIBXML2_INCLUDE_DIR=${LIBXML2_INCLUDE_DIR}
		-DLIBXML2_LIBRARY=${LIBXML2_LIBDIR}/libxml2.so
	)
	ExternalProject_Add(ryml
		GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
		GIT_TAG "${rymlTag}"
		PREFIX ryml
		INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/ryml
		CMAKE_ARGS ${stdArgs}
			-DRYML_BUILD_TESTS=OFF
			-DRYML_BUILD_EXAMPLES=OFF
			-DRYML_BUILD_BENCHMARKS=OFF
			-DRYML_BUILD_TOOLS=OFF
		GIT_SUBMODULES_RECURSE TRUE
	)
	set( _jsonnetPatchArgs "" )
	set( jsonnetPlatformArgs "" )
endif()

# ryml's installed export omits its c4core dependency: rymlTargets.cmake gives ryml::ryml no
# INTERFACE_LINK_LIBRARIES, while libryml needs c4core symbols (c4::handle_error, …), so every
# consumer of ryml::ryml would hit undefined references. Append the archive to the installed
# rymlConfig.cmake after install.
set( _rymlExportPatch "${CMAKE_CURRENT_BINARY_DIR}/ryml-export-c4core.cmake" )
file( WRITE "${_rymlExportPatch}" [=[
file(READ "${CONFIG}" _c)
string(FIND "${_c}" "c4core" _at)
if(_at EQUAL -1)
    string(APPEND _c "
# Appended by the Jde superbuild: link the c4core archive ryml requires but does not export.
set_property(TARGET ryml::ryml APPEND PROPERTY INTERFACE_LINK_LIBRARIES \"\${RYML_LIB_DIR}${LIB_NAME}\")
")
    file(WRITE "${CONFIG}" "${_c}")
endif()
]=] )
# c4Project.cmake installs the package config to <prefix>/cmake on Windows but
# <prefix>/lib/cmake/ryml on Unix, so the path the step patches is OS-dependent.
if( WIN32 )
	set( _c4coreLib c4core.lib )
	set( _rymlConfig ${CMAKE_INSTALL_PREFIX}/ryml/cmake/rymlConfig.cmake )
else()
	set( _c4coreLib libc4core.a )
	set( _rymlConfig ${CMAKE_INSTALL_PREFIX}/ryml/lib/cmake/ryml/rymlConfig.cmake )
endif()
ExternalProject_Add_Step( ryml fix-export
	COMMAND ${CMAKE_COMMAND} "-DCONFIG=${_rymlConfig}" -DLIB_NAME=${_c4coreLib} -P "${_rymlExportPatch}"
	DEPENDEES install
	DEPENDS "${_rymlExportPatch}"
	COMMENT "ryml: appending the c4core dependency to the installed export"
)

ExternalProject_Add(jsonnet
	GIT_REPOSITORY https://github.com/google/jsonnet.git
	GIT_TAG v0.22.0
	UPDATE_DISCONNECTED ON
	PREFIX jsonnet
	INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/jsonnet
	PATCH_COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=<SOURCE_DIR>" -DPATCH_SHA1=${_jsonnetPatchSha} ${_jsonnetPatchArgs} -P "${_jsonnetPatch}"
	CMAKE_ARGS ${stdArgs}
		-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
		-Djde_DEP_TAG_ryml=${rymlTag}
		-DBUILD_JSONNET=OFF #cli is unused; skip building it
		-DBUILD_JSONNETFMT=OFF
		# Keep OFF: upstream core/unicode_test.cpp has a C++23 cast-conformance error under this
		# clang/libc++ toolchain (the abandoned Jde-cpp fork carried the fix as commit 2138da80).
		# To re-enable tests, port that fix into the shared jsonnet-patch.cmake as a replace_or_die.
		-DBUILD_TESTS=OFF
		-DUSE_SYSTEM_GTEST=ON
		-DUSE_SYSTEM_RAPIDYAML=ON
		${jsonnetPlatformArgs}
)
ExternalProject_Add_StepDependencies( jsonnet configure ryml ) #USE_SYSTEM_RAPIDYAML=ON needs ryml installed first
ExternalProject_Add_StepDependencies( jsonnet configure gtest )

ExternalProject_Add(protobuf
	GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
	GIT_TAG v35.1
	PREFIX protobuf
	INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/protobuf
	CMAKE_ARGS ${stdArgs}
		-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
		-Djde_DEP_TAG_absl=${abslTag}
		-Dprotobuf_USE_EXTERNAL_GTEST=ON
		-Dprotobuf_BUILD_TESTS=OFF
		-Dprotobuf_ABSL_PROVIDER=package
		-Dprotobuf_LOCAL_DEPENDENCIES_ONLY=ON
		-Dprotobuf_MSVC_STATIC_RUNTIME=OFF
)

# open62541-nodeset-loader only does install(EXPORT NodesetLoader ...), producing a bare
# NodesetLoader.cmake targets file with no NodesetLoaderConfig.cmake wrapper. find_package()
# in config mode requires that exact filename, so it can never locate the package as installed.
# Patch their CMakeLists.txt (same patch-at-checkout mechanism used below for the Windows
# -Werror fix) to also install a generated wrapper alongside the targets file.
set( _nodesetLoaderCfgFile "${CMAKE_CURRENT_BINARY_DIR}/NodesetLoaderConfig.cmake" )
file( WRITE "${_nodesetLoaderCfgFile}" "include(\"\${CMAKE_CURRENT_LIST_DIR}/NodesetLoader.cmake\")\n" )

if( WIN32 )
	# OpenSSL provided by vcpkg; toolchain file (forwarded via stdArgs) handles discovery for open62541.
	set( opensslArgs "" )
else()
	set( opensslArgs
		-DOPENSSL_USE_STATIC_LIBS=ON
		-DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}
	)
endif()
# nodeset-loader hardcodes -std=c99 -pedantic -Werror; clang trips it (on Linux, glibc's strchr
# is now a _Generic macro -> -Wc11-extensions) so strip -Werror. Their CMakeLists also installs
# include/NodesetLoader/ReferenceService.h, which no longer exists in the repo - drop that line.
# Finally append the missing NodesetLoaderConfig.cmake install rule (see comment above).
set( _nodesetPatch "${CMAKE_CURRENT_BINARY_DIR}/nodeset-loader-patch.cmake" )
file( CONFIGURE OUTPUT "${_nodesetPatch}" @ONLY CONTENT
[[file(READ "${SOURCE_DIR}/CMakeLists.txt" _c)
string(REPLACE " -Werror" "" _c "${_c}")
string(REGEX REPLACE "[^\n]*ReferenceService\\.h[^\n]*" "" _c "${_c}")
string(APPEND _c "\ninstall(FILES \"@_nodesetLoaderCfgFile@\" DESTINATION \${CMAKE_INSTALL_LIBDIR}/cmake/NodesetLoader)\n")
file(WRITE "${SOURCE_DIR}/CMakeLists.txt" "${_c}")
]]
)
set( _nodesetPatchCmd PATCH_COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=<SOURCE_DIR>" -P "${_nodesetPatch}" )

ExternalProject_Add(open62541
	GIT_REPOSITORY https://github.com/open62541/open62541.git
	GIT_TAG ${open62541Tag}
	PREFIX open62541
	INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/open62541
		CMAKE_ARGS ${stdArgs}
		# open62541's UA_ENABLE_DEBUG_SANITIZER block appends SANITIZER_FLAGS to CMAKE_C_FLAGS
		# in debug clang builds. Two problems we fix here, both via CMAKE_C_FLAGS_DEBUG since it
		# lands AFTER SANITIZER_FLAGS on the command line and therefore wins:
		#  1. "-fsanitize=undefined" trips on its casted copy/clear/order jump tables
		#     (e.g. String_copy via copyJumpTable) -> opt back out with -fno-sanitize=function.
		#  2. "-gline-tables-only" emits line tables but no variable/type DWARF, so debuggers
		#     report "no variable information is available" -> trailing -g restores full info.
		"-DCMAKE_C_FLAGS_DEBUG=-fno-sanitize=function -fno-sanitize=pointer-overflow -fno-sanitize=alignment -g"
		-DUA_ENABLE_ENCRYPTION=ON
		-DUA_ENABLE_ENCRYPTION_OPENSSL=ON
		${opensslArgs}
		-DBUILD_NODESET_LOADER=ON
		-DUA_ENABLE_XML_ENCODING=ON
		-DUA_NAMESPACE_ZERO=FULL
		-DUA_BUILD_EXAMPLES=OFF
		${nodesetLoaderArgs}
)

ExternalProject_Add(open62541-nodeset-loader
	GIT_REPOSITORY https://github.com/open62541/open62541-nodeset-loader.git
	GIT_TAG de474df8c114eef0c4efeb988df5901f9adc5005 #upstream master 2026-07 (v0.5.0 is older; this matches the locally-built clone)
	UPDATE_DISCONNECTED ON
	PREFIX nodeset-loader
	INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/NodesetLoader
	${_nodesetPatchCmd}
		CMAKE_ARGS ${stdArgs}
		-Djde_DEP_TAG_open62541=${open62541Tag}
		-DENABLE_BACKEND_OPEN62541=OFF
		-DENABLE_EXAMPLES=OFF
		-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
#linux
#		-DLIBXML2_INCLUDE_DIR=${LIBXML2_INCLUDE_DIR}
#		-DPC_LIBXML_LIBDIR=${LIBXML2_LIBDIR}
#win
#    -DLIBXML2_LIBRARY=${LIBXML2_LIBRARY}
#		-DLIBXML2_LIBRARIES=${LIBXML2_LIBRARIES}
#		-DLIBXML2_INCLUDE_DIR=${LIBXML2_INCLUDE_DIR}
		${nodesetLoaderArgs}
)

ExternalProject_Add_StepDependencies( spdlog configure fmt )
ExternalProject_Add_StepDependencies( absl configure gtest )
ExternalProject_Add_StepDependencies( protobuf configure absl )
ExternalProject_Add_StepDependencies( protobuf configure gtest )
ExternalProject_Add_StepDependencies( open62541-nodeset-loader configure open62541 )
if( WIN32 )
	ExternalProject_Add_StepDependencies( protobuf configure zlib )
	ExternalProject_Add_StepDependencies( open62541 configure LibXml2 )
	ExternalProject_Add_StepDependencies( open62541-nodeset-loader configure LibXml2 )
endif()