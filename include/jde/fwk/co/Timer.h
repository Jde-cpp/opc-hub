#pragma once
#include <boost/asio.hpp>
#include <absl/synchronization/mutex.h>
#include <chrono>
#include "Await.h"
#include "jde/fwk/usings.h"

namespace Jde{
	using TimerAwait = TAwait<std::expected<void, boost::system::error_code>>;
	struct Γ DurationTimer final : TimerAwait{
		DurationTimer( steady_clock::duration duration, SRCE )ι;
		DurationTimer( steady_clock::duration duration, boost::asio::any_io_executor executor, SRCE )ι;//resumes the awaiter on `executor` (e.g. a strand) instead of an arbitrary pool thread.
		~DurationTimer();
		α await_ready()ι->bool override{ return _duration<steady_clock::duration::zero(); }
		α await_resume()ι->std::expected<void, boost::system::error_code> override{//ready path attaches no promise - complete successfully instead of the base's 'promise is null' throw.
			return _h ? TimerAwait::await_resume() : std::expected<void, boost::system::error_code>{};
		}
		α Suspend()ι->void override;
		α Cancel()ι->uint{ ul _{_mutex}; _cancelled = true; return _timer.cancel(); }//asio timers aren't safe for concurrent cancel vs Start's async_wait.
	private:
		α Start()ι->void;
		sp<boost::asio::io_context> _ctx;
		steady_clock::duration _duration;
		optional<boost::asio::any_io_executor> _executor;//when set, completion handlers are bound to it.
		absl::Mutex _mutex;
		//a Cancel that arrives before Start cancels nothing - `steady_timer::cancel()` with no pending wait has
		//nothing to cancel and returns 0 - and the wait then runs its full duration.  Remembered here so Start can
		//re-issue it.
		bool _cancelled ABSL_GUARDED_BY(_mutex){};
		boost::asio::steady_timer _timer ABSL_GUARDED_BY(_mutex);
	};
}