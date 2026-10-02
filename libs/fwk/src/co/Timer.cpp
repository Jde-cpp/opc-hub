#include <jde/fwk/co/Timer.h>
#include <jde/fwk/process/execution.h>

#define let const auto

namespace Jde{
	DurationTimer::DurationTimer( steady_clock::duration duration, SL sl )ι:
		TimerAwait{sl},
		_ctx{ Executor() },
		_duration{duration},
		_timer{ *_ctx }
	{}

	DurationTimer::DurationTimer( steady_clock::duration duration, boost::asio::any_io_executor executor, SL sl )ι:
		TimerAwait{sl},
		_ctx{ Executor() },
		_duration{duration},
		_executor{ move(executor) },
		_timer{ *_ctx }
	{}

	DurationTimer::~DurationTimer(){
		ASSERT( !_h && "Timer destroyed before awaited completion." );
	}

	α DurationTimer::Start()ι->void{
		ul _{_mutex};
		_timer.expires_after( _duration );
		auto handler = [h=_h](const boost::system::error_code& ec){
			if( !ec )
				h.promise().Resume( {}, h );
			else
				h.promise().Resume( std::unexpected{ec}, h );
	  };
		if( _executor )
			_timer.async_wait( boost::asio::bind_executor(*_executor, move(handler)) );
		else
			_timer.async_wait( move(handler) );
		if( _cancelled )
			_timer.cancel();
	}
	α DurationTimer::Suspend()ι->void{
		Start();
		Execution::Run();
	}
}