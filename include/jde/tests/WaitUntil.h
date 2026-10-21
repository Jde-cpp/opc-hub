#pragma once
#include <thread>
#include <jde/fwk/usings.h>

namespace Jde{
	//`pred` polled every `interval` until it holds;  false once `timeout` passes, though it is always asked once.
	Ξ WaitUntil( function<bool()> pred, Duration timeout=std::chrono::seconds{10}, Duration interval=std::chrono::milliseconds{10} )ι->bool{
		for( const auto deadline = std::chrono::steady_clock::now()+timeout; !pred(); std::this_thread::sleep_for(interval) ){
			if( std::chrono::steady_clock::now()>deadline )
				return false;
		}
		return true;
	}
}