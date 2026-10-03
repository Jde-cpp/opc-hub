#pragma once
#include <cstdio>
#include <cstdlib>
#if defined(__x86_64__) || defined(_M_X64)
	#ifdef _MSC_VER
		#include <intrin.h>
		#define JDE_CPUID( leaf, regs ) __cpuidex( reinterpret_cast<int*>(regs), static_cast<int>(leaf), 0 )
	#else
		#include <cpuid.h>
		#define JDE_CPUID( leaf, regs ) __cpuid_count( leaf, 0, regs[0], regs[1], regs[2], regs[3] )
	#endif
#endif

namespace Jde::Process{
	//Every preset compiles with cpuFlags (-march=x86-64-v3 -mpclmul -maes, CMakePresets.common.json):  on a CPU without them the
	//first such instruction is a SIGILL with nothing logged.  target + noinline:  compiled with cpuFlags' -march itself, or inlined
	//into a caller that is, this could fault before it reports - so it calls no inline function either, only macros and libc.
	//No sanitizer instrumentation: on linux it runs before asan is initialized.
	[[gnu::target("arch=x86-64"), gnu::noinline, gnu::no_sanitize_address]] Ξ CheckCpu()ι->bool{
		bool ok{ true };
#if defined(__x86_64__) || defined(_M_X64)
		enum{ Ecx1, Ebx7, Ecx81, Os };
		unsigned r[4], regs[4]{ 0, 0, 0, 1 };
		JDE_CPUID( 0, r ); const unsigned maxLeaf = r[0];
		JDE_CPUID( 1, r ); regs[Ecx1] = r[2];
		if( maxLeaf>=7 ){ JDE_CPUID( 7, r ); regs[Ebx7] = r[1]; }
		JDE_CPUID( 0x80000001, r ); regs[Ecx81] = r[2];
		if( regs[Ecx1] & (1u << 27) ){ //OSXSAVE:  xgetbv exists, and says whether the OS saves the ymm registers.
			unsigned xcr0;
	#if defined(__GNUC__) || defined(__clang__)
			__asm__( "xgetbv" : "=a"(xcr0) : "c"(0) : "edx" ); //_xgetbv needs the xsave target feature this function is compiled without.
	#else
			xcr0 = static_cast<unsigned>( _xgetbv(0) );
	#endif
			regs[Os] = (xcr0 & 6)==6;
		}
		struct Feature{ unsigned Reg, Bit; const char* Name; };
		static constexpr Feature features[]{
			{Ecx1, 0, "SSE3"}, {Ecx1, 9, "SSSE3"}, {Ecx1, 13, "CMPXCHG16B"}, {Ecx1, 19, "SSE4.1"}, {Ecx1, 20, "SSE4.2"}, {Ecx1, 23, "POPCNT"}, {Ecx81, 0, "LAHF/SAHF"},//x86-64-v2
			{Ecx1, 12, "FMA"}, {Ecx1, 22, "MOVBE"}, {Ecx1, 27, "OSXSAVE"}, {Ecx1, 28, "AVX"}, {Ecx1, 29, "F16C"}, {Ebx7, 3, "BMI1"}, {Ebx7, 5, "AVX2"}, {Ebx7, 8, "BMI2"}, {Ecx81, 5, "LZCNT"},//x86-64-v3
			{Os, 0, "AVX enabled by the operating system"},
			{Ecx1, 1, "PCLMULQDQ"}, {Ecx1, 25, "AES"}
		};
		for( const auto& feature : features ){
			if( regs[feature.Reg] & (1u << feature.Bit) )
				continue;
			std::fputs( ok ? "This build requires CPU features this machine lacks: " : ", ", stderr ); //fputs: glibc's fortified fprintf is an inline function.
			std::fputs( feature.Name, stderr );
			ok = false;
		}
		if( !ok )
			std::fputs( ".  In a VM, choose a CPU model that passes them through (e.g. host).\n", stderr );
#endif
		return ok;
	}
}
#undef JDE_CPUID
#if defined(__linux__) && defined(__x86_64__)
//main is too late on linux:  the shared libraries' initializers run first, and libabseil_dll's faults without SSE4.1.  The
//executable's preinit_array runs before any of them - so include this from an executable's main.cpp only, never a library.
[[gnu::target("arch=x86-64"), gnu::no_sanitize_address]] inline void JdeCheckCpuPreinit( int, char**, char** )noexcept{
	if( !Jde::Process::CheckCpu() )
		std::_Exit( EXIT_FAILURE );
}
[[gnu::used, gnu::section(".preinit_array")]] static void (*_jdeCheckCpuPreinit)( int, char**, char** ){ JdeCheckCpuPreinit };
#endif
