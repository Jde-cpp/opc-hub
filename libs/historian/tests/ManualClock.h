#pragma once
#include <map>
#include <jde/historian/Clock.h>

namespace Jde::Opc::Hist::Tests{
	//Time moves only when a test moves it.  Advance runs every timer that falls due on the way, in due order and ties in
	//the order scheduled, with Now() at each timer's own due time - so a timer a callback schedules inside the span also
	//runs, and midnight, delay and the heartbeat take no wall-clock time.  One timeline serves both kinds of timer:  an
	//interval is due at Now()+after.
	struct ManualClock final : IClock{
		ManualClock( TimePoint now )ι:_now{now}{}
		α Now()Ι->TimePoint override{ ul _{_mutex}; return _now; }
		α Schedule( TimePoint due, absl::AnyInvocable<void()> f )ι->TimerId override{
			ul _{ _mutex };
			const auto id = ++_nextId;
			_timers.emplace( std::pair{due, id}, std::move(f) );
			return id;
		}
		α Schedule( Duration after, absl::AnyInvocable<void()> f )ι->TimerId override{ return Schedule( Now()+after, std::move(f) ); }
		α Cancel( TimerId id )ι->bool override{
			ul _{ _mutex };
			auto p = find_if( _timers, [id]( const auto& t ){ return t.first.second==id; } );
			if( p==_timers.end() )
				return false;
			_timers.erase( p );
			return true;
		}
		//Returns how many timers ran.  A callback's exception propagates, with the timers after it still pending.
		α AdvanceTo( TimePoint to )ε->uint{
			uint count{};
			for( ;; ){
				absl::AnyInvocable<void()> f;
				{
					ul _{ _mutex };
					if( _timers.empty() || _timers.begin()->first.first>to )
						break;
					auto node = _timers.extract( _timers.begin() );
					_now = std::max( _now, node.key().first );
					f = std::move( node.mapped() );
				}
				f();
				++count;
			}
			ul _{ _mutex };
			_now = std::max( _now, to );
			return count;
		}
		α Advance( Duration d )ε->uint{ return AdvanceTo( Now()+d ); }
		α Pending()Ι->uint{ ul _{_mutex}; return _timers.size(); }
	private:
		mutable absl::Mutex _mutex;
		TimePoint _now ABSL_GUARDED_BY(_mutex);
		TimerId _nextId ABSL_GUARDED_BY(_mutex){};
		std::map<std::pair<TimePoint,TimerId>,absl::AnyInvocable<void()>> _timers ABSL_GUARDED_BY(_mutex);
	};
}