#pragma once

namespace Jde{
	//Owns a HANDLE that CloseHandle closes.  Win32 fails with null or with INVALID_HANDLE_VALUE, depending on the call:  either is held as null, the one empty state.
	struct HandlePtr final{
		HandlePtr()ι=default;
		explicit HandlePtr( HANDLE h )ι:_h{ h==INVALID_HANDLE_VALUE ? nullptr : h }{}
		HandlePtr( HandlePtr&& x )ι:_h{ x.release() }{}
		α operator=( HandlePtr&& x )ι->HandlePtr&{ reset( x.release() ); return *this; }
		~HandlePtr(){ reset(); }

		explicit operator bool()Ι{ return _h!=nullptr; }
		α get()Ι->HANDLE{ return _h; }
		α release()ι->HANDLE{ return std::exchange( _h, nullptr ); }
		α reset( HANDLE h=nullptr )ι->void{
			if( auto old = std::exchange(_h, h==INVALID_HANDLE_VALUE ? nullptr : h); old && !::CloseHandle(old) )
				WARNT( ELogTags::App, "CloseHandle returned {}", ::GetLastError() );
		}
	private:
		HANDLE _h{};
	};
	//Unnamed, manual-reset, initially unsignaled - null if CreateEvent failed.
	Ξ ManualResetEvent()ι->HandlePtr{ return HandlePtr{ ::CreateEvent(nullptr, TRUE, FALSE, nullptr) }; }
}