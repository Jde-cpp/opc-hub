#pragma once
#include <boost/asio/any_io_executor.hpp>
#include "AnyAwait.h"

namespace Jde{
	namespace Internal{ struct TimerState; }
	//Cancels a DurationTimer from any thread:  before its wait starts, during it, or after (a no-op).  It shares the timer's state, not
	//the awaitable, so it may outlive both.
	struct Γ TimerCancel final{
		TimerCancel()ι=default;
		α Cancel()Ι->void;
		explicit operator bool()Ι{ return _state!=nullptr; }
		α operator==( const TimerCancel& )Ι->bool=default;
	private:
		explicit TimerCancel( sp<Internal::TimerState> state )ι:_state{ move(state) }{}
		sp<Internal::TimerState> _state;
		friend struct DurationTimer;
	};

	//Any coroutine can co_await it (CLAUDE.md 'Coroutines').  Fails with operation_aborted when cancelled.
	struct Γ DurationTimer final : AnyAwait<std::expected<void,boost::system::error_code>>{
		using Result = std::expected<void,boost::system::error_code>;
		DurationTimer( steady_clock::duration duration, SRCE )ι;
		DurationTimer( steady_clock::duration duration, boost::asio::any_io_executor executor, SRCE )ι;//resumes on `executor`, e.g. a strand.
		α await_ready()ι->bool override;
		//For a timer awaited again, as a loop's sleep is:  the next co_await waits `duration`, and an earlier Cancel no longer applies.
		//Not while a wait is pending.
		α Rearm( steady_clock::duration duration )ι->DurationTimer&;
		α Canceller()Ι->TimerCancel{ return TimerCancel{ _state }; }
		α Cancel()Ι->void{ Canceller().Cancel(); }
	private:
		α Suspend()ι->void override;
		steady_clock::duration _duration;
		sp<Internal::TimerState> _state;
	};
}