#pragma once
#ifndef CONTEXT_THREAD_H
#define CONTEXT_THREAD_H
#include <jde/fwk/co/Await.h>
#include <absl/functional/any_invocable.h>

namespace boost::asio{ class io_context; class cancellation_signal; }
#define Φ Γ α
namespace Jde{
	Φ Executor()ι->sp<boost::asio::io_context>;
	Φ ExecutorIoc()ι->sp<boost::asio::io_context>;//current io_context without creating one; lets shutdown keep it alive and destroy it last.
	Φ Post( absl::AnyInvocable<void()> f )ι->void;
	Ŧ Post( T&& value, typename TAwait<T>::Handle h )ι->void;
	Φ PostIO( absl::AnyInvocable<void()> f )ι->void;
	Φ Post( VoidAwait::Handle&& h )ι->void;
	Φ Post( VoidAwait::Handle&& h, Exception&& e )ι->void;

	namespace Execution{
		Φ AddShutdown( IShutdown* pShutdown )ι->void;
		Φ AddCancelSignal( sp<boost::asio::cancellation_signal> s )ι->void;
		Φ RemoveCancelSignal( const sp<boost::asio::cancellation_signal>& s )ι->void;
		Φ CancelSignalCount()ι->uint;//how many are retained.  This list only ever grew before web-review3 #12; a count makes the regression visible.
		Φ Run()->void;
	}
}
	Ŧ Jde::Post( T&& value, typename TAwait<T>::Handle h )ι->void{
			Post( [ v = move(value), h ]() mutable {
				h.promise().Resume( move(v), h );
			} );
	}

#endif
#undef Φ