#include <cstdio>
#include <cstdlib>
#include <jde/fwk/process/process.h>
#include "../Harness.h"

namespace Jde{
	α Process::ProductName()ι->sv{ return "Fuzz.Opc.OpenProtocol"; }
}
//With no settings Init makes no logger and closes the startup buffer, which would otherwise keep every line the codec
//logs for an input with bytes past its layout, until the run met libFuzzer's memory limit.
extern "C" int LLVMFuzzerInitialize( int*, char*** ){
	Jde::Logging::Init();
	return 0;
}
//libFuzzer's entry:  each input through what CorpusTests replays, a broken invariant a crash.
extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size ){
	const auto broke = Jde::Opc::OpenProtocol::Tests::Exercise( {(const char*)data, size} );
	if( broke.empty() )
		return 0;
	fprintf( stderr, "%s\n", broke.c_str() );
	abort();
}