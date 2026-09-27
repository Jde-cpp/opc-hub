#pragma once
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/process/process.h>

namespace Jde{
  struct WinHandle final{
    WinHandle( std::nullptr_t=nullptr )ι : _value(nullptr) {}
    explicit WinHandle( HANDLE value )ι : _value(value) {}
		//TODO: forward exception?
    WinHandle( HANDLE value, function<IO::IOException()> e )ε:
      _value( value ){
      if( _value==INVALID_HANDLE_VALUE )
				throw e();
    }

    explicit operator bool()Ι{ return _value != nullptr && _value != INVALID_HANDLE_VALUE; }
    operator HANDLE()Ι{ return _value; }
		struct Deleter final {
			using pointer=WinHandle;
			void operator()( WinHandle handle )Ι{
				if( handle && !::CloseHandle(handle) )
					WARNT( ELogTags::App, "CloseHandle returned {}", ::GetLastError() );
			}
		};
	private:
    HANDLE _value;
  };
  using HandlePtr=std::unique_ptr<WinHandle, WinHandle::Deleter>;
  //Unnamed, manual-reset, initially unsignaled - null if CreateEvent failed.
  Ξ ManualResetEvent()ι->HandlePtr{ return HandlePtr{ WinHandle{::CreateEvent(nullptr, TRUE, FALSE, nullptr)} }; }
}