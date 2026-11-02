#include <jde/fwk/co/Timer.h>
#include <boost/asio/steady_timer.hpp>
#include <jde/fwk/process/execution.h>

namespace Jde{
	struct Internal::TimerState final{
		TimerState( sp<boost::asio::io_context> ctx )ι:Ctx{ move(ctx) }, Timer{ *Ctx }{}
		TimerState( sp<boost::asio::io_context> ctx, boost::asio::any_io_executor executor )ι:Ctx{ move(ctx) }, Timer{ move(executor) }{}
		const sp<boost::asio::io_context> Ctx;//outlives Timer, whose destructor deregisters from the io_context's timer service.
		absl::Mutex Mutex;//asio timers aren't safe for a concurrent cancel and async_wait.
		bool Cancelled ABSL_GUARDED_BY(Mutex){};
		boost::asio::steady_timer Timer ABSL_GUARDED_BY(Mutex);
	};

	α TimerCancel::Cancel()Ι->void{
		ul _{ _state->Mutex };
		_state->Cancelled = true;
		_state->Timer.cancel();
	}

	DurationTimer::DurationTimer( steady_clock::duration duration, SL sl )ι:
		AnyAwait{ sl },
		_duration{ duration },
		_state{ ms<Internal::TimerState>(Executor()) }
	{}

	DurationTimer::DurationTimer( steady_clock::duration duration, boost::asio::any_io_executor executor, SL sl )ι:
		AnyAwait{ sl },
		_duration{ duration },
		_state{ ms<Internal::TimerState>(Executor(), move(executor)) }
	{}

	α DurationTimer::Rearm( steady_clock::duration duration )ι->DurationTimer&{
		ASSERT( !_h );
		_duration = duration;
		_result.reset();
		ul _{ _state->Mutex };
		_state->Cancelled = false;
		return *this;
	}

	α DurationTimer::await_ready()ι->bool{
		if( _duration>=steady_clock::duration::zero() )
			return false;
		_result.emplace();
		return true;
	}

	α DurationTimer::Suspend()ι->void{
		Execution::Run();
		auto state = _state;//the handler may resume the awaiter, which destroys this, before the lock below is released.
		ul _{ state->Mutex };
		state->Timer.expires_after( _duration );
		state->Timer.async_wait(
			[this]( const boost::system::error_code& ec ){
				Resume( ec ? Result{std::unexpected{ec}} : Result{} );
			}
		);
		//A Cancel before the wait started had nothing to cancel.  Re-issued, not completed inline:  the awaiter may hold a lock its
		//continuation takes.
		if( state->Cancelled )
			state->Timer.cancel();
	}
}