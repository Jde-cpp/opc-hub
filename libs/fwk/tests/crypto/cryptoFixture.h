#pragma once
#include "../testsDir.h"

namespace Jde::Crypto::Tests{
	//Both crypto fixtures used to mint into fixed absolute paths - /tmp/public.pem, or ProgramData/Jde-Cpp/<product>
	//on windows - so every build and both test workflows wrote the same files, and with cryptoTests.clear:false a
	//leftover from one run fed the next (#26).  TestsDir's per-workflow root, plus a per-suite leaf so the two fixtures
	//cannot collide with each other either.  Deliberately not process-scoped: clear:false means to reuse the key pair
	//across runs, and a pid in the path would silently defeat that.
	Ξ FixtureDir( sv suite )->fs::path{
		const auto dir = Jde::Tests::TestsDir( fs::path{"crypto"}/suite );
		if( !fs::exists(dir) )
			fs::create_directories( dir );
		return dir;
	}
}