#pragma once
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>

namespace Jde::Tests{
	//Where a test scratches:  FileTests' dir, /testing/file's parent - cwd-relative, so per build dir and per workflow
	//($buildDir/Testing for ctest, $buildDir/runtime for a direct run) - then leaf.  A fixed name under the system temp
	//dir was shared by every build, workflow and concurrent run (fwk-review3 #26).  perProcess adds the pid, the only
	//thing that separates two runs from one cwd;  a leaf meant to be reused across runs leaves it off.  Nothing is created.
	Ξ TestsDir( const fs::path& leaf, bool perProcess=false )->fs::path{
		const auto configured = Settings::FindPath( "/testing/file" );//set by every test config; the fallback is for a settings-less run.
		auto dir = ( configured ? configured->parent_path() : fs::temp_directory_path() )/leaf;
		if( perProcess )
			dir /= std::to_string( Process::ProcessId() );
		return dir;
	}
}