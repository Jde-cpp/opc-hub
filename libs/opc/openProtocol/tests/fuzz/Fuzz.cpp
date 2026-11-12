#include "../Harness.h"
#include <cstdio>
#include <cstdlib>

//libFuzzer's entry:  each input through what CorpusTests replays, a broken invariant a crash.
extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size ){
	const auto broke = Jde::Opc::OpenProtocol::Tests::Exercise( {(const char*)data, size} );
	if( broke.empty() )
		return 0;
	fprintf( stderr, "%s\n", broke.c_str() );
	abort();
}