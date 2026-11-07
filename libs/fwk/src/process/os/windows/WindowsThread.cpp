#include <processthreadsapi.h>
#include <jde/fwk/process/thread.h>
#include <jde/fwk/usings.h>
#include <thread>

#define let const auto
constexpr Jde::ELogTags _tags{ Jde::ELogTags::Threads };

typedef HRESULT (WINAPI *TGetThreadDescription)( HANDLE, PWSTR* );
TGetThreadDescription pGetThreadDescription = nullptr;

typedef HRESULT (WINAPI *TSetThreadDescription)( HANDLE, PCWSTR );
TSetThreadDescription pSetThreadDescription = nullptr;

Ω initialize()ι->void{
	using namespace Jde;
	HMODULE hKernelBase = GetModuleHandleA("KernelBase.dll");
	if( !hKernelBase ){
		CRITICALT( _tags, "FATAL: failed to get kernel32.dll module handle, error:  {}", ::GetLastError() );
		return;
	}
	pSetThreadDescription = reinterpret_cast<TSetThreadDescription>( ::GetProcAddress(hKernelBase, "SetThreadDescription") );
	if( !pSetThreadDescription )
		CRITICAL( "FATAL: failed to get SetThreadDescription() address, error:  {:x}", ::GetLastError() );
	pGetThreadDescription = reinterpret_cast<TGetThreadDescription>( ::GetProcAddress(hKernelBase, "GetThreadDescription") );
	if (!pGetThreadDescription)
		CRITICAL( "FATAL: failed to get GetThreadDescription() address, error:  {:x}", ::GetLastError() );
}

Ω winSetThreadDscrptn( HANDLE h, Jde::sv ansiDescription )ι->void{
	using namespace Jde;
	let count = ::MultiByteToWideChar( CP_UTF8, 0, string{ansiDescription}.c_str() , -1, nullptr, 0 );
	auto wc = std::make_unique_for_overwrite<wchar_t[]>( count );
	::MultiByteToWideChar( CP_UTF8, 0, ansiDescription.data(), -1, wc.get(), count );
	if( !pSetThreadDescription )
		initialize();
	if( pSetThreadDescription && FAILED((*pSetThreadDescription)(h, wc.get())) )
		ERR( "({:x})Could not set name for thread: '{}', h: {:x}", ::GetLastError(), ansiDescription, (uint)h );
}

namespace Jde{
	α Thread::SetName( Thread::ProcessThreadId id, sv description )ι->void{
		winSetThreadDscrptn( id, description );
	}
	α Thread::SetName( std::thread& thread, sv description )ι->void{
		winSetThreadDscrptn( static_cast<HANDLE>(thread.native_handle()), description );
	}
	α Thread::SetName( sv description )ι->void{
		winSetThreadDscrptn( ::GetCurrentThread(), description );
	}

	α Thread::Name()ι->string{
		string y;
		if( !pGetThreadDescription )
			initialize();
		if( !pGetThreadDescription )
			return y;
		PWSTR pszThreadDescription;
		if( let hr = (*pGetThreadDescription)( ::GetCurrentThread(), &pszThreadDescription ); SUCCEEDED(hr) ){
			let size = wcslen( pszThreadDescription );
			auto pDescription = std::make_unique<char[]>( size+1 );
			uint converted;
			wcstombs_s( &converted, pDescription.get(), size+1, pszThreadDescription, size );
			::LocalFree( pszThreadDescription );
			y = pDescription.get();
		}
		return y;
	}

	α Thread::Id()ι->Thread::ProcessThreadId{
		// Return a real handle (unlike the GetCurrentThread() pseudo-handle) so an id collected on
		// one thread can later be passed to SetName from another thread, matching pthread_self() on Linux.
		return ::OpenThread( THREAD_SET_LIMITED_INFORMATION|THREAD_QUERY_LIMITED_INFORMATION, FALSE, ::GetCurrentThreadId() );
	}
}
