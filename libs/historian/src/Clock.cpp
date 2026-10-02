#include <jde/historian/Clock.h>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/system_timer.hpp>
#include <jde/fwk/process/execution.h>

#define let const auto

namespace Jde::Opc::Hist{
	using namespace std::chrono;
	constexpr ELogTags _tags{ ELogTags::Scheduler };
	namespace{
		using Timer = std::variant<sp<boost::asio::system_timer>,sp<boost::asio::steady_timer>>;
		Ω cancel( const Timer& timer )ι->void{ std::visit( []( auto& t ){ t->cancel(); }, timer ); }
		//On an executor thread, where an escaping exception would end the process.
		Ω run( absl::AnyInvocable<void()>& f )ι->void{
			try{
				f();
			}
			catch( Exception& e ){
				e.SetLevel( ELogLevel::Critical );
			}
			catch( const runtime_error& e ){
				CRITICAL( "Timer callback threw: {}", e.what() );
			}
		}

		struct System final : IClock, std::enable_shared_from_this<System>{
			~System(){
				ul _{ _mutex };
				for( auto&& [_,timer] : _timers )
					cancel( timer );
			}
			α Now()Ι->TimePoint override{ return Clock::now(); }
			α Schedule( TimePoint due, absl::AnyInvocable<void()> f )ι->TimerId override{ return Arm<boost::asio::system_timer>( due, std::move(f) ); }
			α Schedule( Duration after, absl::AnyInvocable<void()> f )ι->TimerId override{ return Arm<boost::asio::steady_timer>( after, std::move(f) ); }
			α Cancel( TimerId id )ι->bool override{
				optional<Timer> timer;
				{
					ul _{ _mutex };
					if( auto p = _timers.find(id); p!=_timers.end() ){
						timer = std::move( p->second );
						_timers.erase( p );
					}
				}
				if( timer )
					cancel( *timer );
				return (bool)timer;
			}
		private:
			template<class T, class At> α Arm( At at, absl::AnyInvocable<void()> f )ι->TimerId{
				auto ctx = Executor();
				if( !ctx ){
					WARN( "Schedule after executor teardown - dropping timer." );
					return {};
				}
				auto timer = ms<T>( *ctx, at );
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
							run( f );
					});
				}
				Execution::Run();
				return id;
			}
			absl::Mutex _mutex;
			TimerId _nextId ABSL_GUARDED_BY(_mutex){};
			flat_map<TimerId,Timer> _timers ABSL_GUARDED_BY(_mutex);
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