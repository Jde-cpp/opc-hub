#pragma once
#include <jde/fwk/co/Await.h>
#include <absl/functional/any_invocable.h>

namespace boost::asio{ class io_context; }
#define Φ Γ α
namespace Jde{
	Φ Executor()ι->sp<boost::asio::io_context>;//never null:  once the executor has stopped, what is posted to it is destroyed unrun.
	Φ ThreadCount()ι->unsigned;//the executor's threads:  /workers/executor/threads, else the hardware's, at least one.
	Φ Post( absl::AnyInvocable<void()> f )ι->void;
	Ŧ Post( T&& value, typename TAwait<T>::Handle h )ι->void;
	Φ Post( VoidAwait::Handle&& h )ι->void;
	Φ Post( VoidAwait::Handle&& h, Exception&& e )ι->void;

	namespace Execution{
		Φ AddShutdown( IShutdown* pShutdown )ι->void;
		Φ OnThread()ι->bool;//whether this thread is one of the executor's.
		Φ Running()ι->bool;//whether work posted now can still run.  Creates nothing.
		α Start()ι->void;//Process::Startup's.
		α Stop( bool terminate, SRCE )ι->void;//Process::Shutdown's.
		α Release()ι->sp<boost::asio::io_context>;//Process::Shutdown's, for the io_context to die last.
	}
}
Ŧ Jde::Post( T&& value, typename TAwait<T>::Handle h )ι->void{
		Post( [ v = move(value), h ]() mutable {
			h.promise().Resume( move(v), h );
		} );
}
#undef Φ