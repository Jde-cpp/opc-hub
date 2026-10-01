#pragma once
#include <cstdio>
#include <cstdlib>
#if defined(__x86_64__) || defined(_M_X64)
	#ifdef _MSC_VER
		#include <intrin.h>
	#else
		#include <cpuid.h>
	#endif
#endif

namespace Jde::Process{
	//Every preset compiles with cpuFlags (-msse4.2 -mpclmul, CMakePresets.common.json):  on a CPU without them the first such
	//instruction is a SIGILL with nothing logged.  No sanitizer instrumentation: on linux it runs before asan is initialized.
	[[gnu::no_sanitize_address]] Ξ CheckCpu()ι->bool{
#if defined(__x86_64__) || defined(_M_X64)
	#ifdef _MSC_VER
		int regs[4]; __cpuid( regs, 1 );
		const auto ecx = static_cast<unsigned>( regs[2] );
	#else
		unsigned eax, ebx, ecx, edx;
		__cpuid( 1, eax, ebx, ecx, edx );
	#endif
		const bool sse42 = ecx & (1u << 20), pclmul = ecx & (1u << 1);
		if( !sse42 || !pclmul ){
			std::fprintf( stderr, "This CPU lacks %s, which this build requires.  In a VM, choose a CPU model that passes them through (e.g. host).\n",
				!sse42 && !pclmul ? "SSE4.2 and PCLMULQDQ" : !sse42 ? "SSE4.2" : "PCLMULQDQ" );
			return false;
		}
#endif
		return true;
	}
}
#if defined(__linux__) && defined(__x86_64__)
//main is too late on linux:  the shared libraries' initializers run first, and libabseil_dll's faults without SSE4.1.  The
//executable's preinit_array runs before any of them - so include this from an executable's main.cpp only, never a library.
[[gnu::no_sanitize_address]] inline void JdeCheckCpuPreinit( int, char**, char** )noexcept{
	if( !Jde::Process::CheckCpu() )
		std::_Exit( EXIT_FAILURE );
}
[[gnu::used, gnu::section(".preinit_array")]] static void (*_jdeCheckCpuPreinit)( int, char**, char** ){ JdeCheckCpuPreinit };
#endif
