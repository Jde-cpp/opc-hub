#include <jde/historian/Clock.h>
#include <boost/asio/system_timer.hpp>
#include <jde/fwk/process/execution.h>

#define let const auto

namespace Jde::Opc::Hist{
	using namespace std::chrono;
	namespace{
		struct System final : IClock, std::enable_shared_from_this<System>{
			~System(){
				ul _{ _mutex };
				for( auto&& [_,timer] : _timers )
					timer->cancel();
			}
			α Now()Ι->TimePoint override{ return Clock::now(); }
			α Schedule( TimePoint due, absl::AnyInvocable<void()> f )ι->TimerId override{
				auto timer = ms<boost::asio::system_timer>( *Executor(), due );
				TimerId id;
				{
					//async_wait under the lock, so a Cancel never reaches the timer while it is being armed.
					ul _{ _mutex };
					id = ++_nextId;
					_timers.emplace( id, timer );
					timer->async_wait( [self=weak_from_this(), id, timer, f=std::move(f)]( const boost::system::error_code& ec )mutable{
						auto clock = self.lock();
						if( !clock )
							return;
						bool mine;
						{
							ul _{ clock->_mutex };
							mine = clock->_timers.erase( id );
						}
						if( !ec && mine )
							f();
					});
				}
				Execution::Run();
				return id;
			}
			α Cancel( TimerId id )ι->bool override{
				sp<boost::asio::system_timer> timer;
				{
					ul _{ _mutex };
					if( auto p = _timers.find(id); p!=_timers.end() ){
						timer = std::move( p->second );
						_timers.erase( p );
					}
				}
				if( timer )
					timer->cancel();
				return (bool)timer;
			}
		private:
			absl::Mutex _mutex;
			TimerId _nextId ABSL_GUARDED_BY(_mutex){};
			flat_map<TimerId,sp<boost::asio::system_timer>> _timers ABSL_GUARDED_BY(_mutex);
		};
	}
	α SystemClock()ι->sp<IClock>{ return ms<System>(); }

	α DayOf( TimePoint t, const time_zone& tz )ι->year_month_day{
		return Chrono::LocalYMD( t, tz );
	}
	α DayStart( year_month_day day, const time_zone& tz )ι->TimePoint{
		return tz.to_sys( local_days{day}, choose::earliest );
	}
	α NextDayStart( TimePoint t, const time_zone& tz )ι->TimePoint{
		return DayStart( year_month_day{local_days{DayOf(t, tz)}+days{1}}, tz );
	}
}