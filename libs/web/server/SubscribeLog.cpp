#include <jde/web/server/SubscribeLog.h>
#include <absl/synchronization/mutex.h>
#include <jde/ql/types/Subscription.h>
#include <jde/web/server/IWebsocketSession.h>

#define let const auto

namespace Jde::Web::Server{
	Ω toMinLevel( const QL::Filter& filter, ELogLevel dflt )ι->ELogLevel{
		let kv = filter.ColumnFilters.find( "level" );
		if( kv==filter.ColumnFilters.end() || kv->second.size()!=1 )
			return dflt;
		let& filterValue = kv->second.front();
		//#57: a number as well as a name.  `level:{gte:3}` used to fall to the else and leave the floor at Trace, so the client
		//was sent every entry and SubscribeLog::Add pushed Trace into the logger's tag threshold for the whole process.
		optional<ELogLevel> level;
		if( filterValue.Value.is_string() )
			level = ToLogLevel( filterValue.Value.get_string() ); // "error" if not-parsable
		else if( auto number = filterValue.Value.is_number() ? Json::FindNumber<uint8>(filterValue.Value, {}) : nullopt; number )
			level = (ELogLevel)*number;
		if( !level )
			return dflt;
		using enum DB::EOperator;
		//#57: only an operator that bounds the level from below may raise the floor.  `lte`/`lt`/`ne` admit entries beneath the
		//value, and Write's test is `m.Level>=MinLevel` - taking the value as a floor there would drop what was asked for.
		switch( filterValue.Operator ){
		case Greater: return (ELogLevel)( underlying(*level)+1 );
		case GreaterOrEqual: case Equal: return *level;
		default: return dflt;
		}
	}

	struct LogSubscription final : QL::Subscription{
		LogSubscription( LogSubscription&& sub )ι=default;
		LogSubscription( QL::Subscription&& sub )ε:
			QL::Subscription{ move(sub) }{
			let& filter = Fields.Filter();
			MinLevel = toMinLevel( filter, MinLevel );
			if( let kv = filter.ColumnFilters.find("tags"); kv != filter.ColumnFilters.end()  && kv->second.size()==1 )
				Tags = ToLogTags( kv->second.front().Value );
		}
		α operator=( LogSubscription&& sub )ι->LogSubscription& = default;
		ELogLevel MinLevel{ ELogLevel::Trace };
		ELogTags Tags{ ELogTags::All };
	};
	struct SessionSubscription{
		SessionSubscription( sp<IWebsocketSession> session, QL::Subscription&& sub )ι:Session{ move(session) },Sub{ move(sub) }{}
		sp<IWebsocketSession> Session;
		LogSubscription Sub;
	};

	absl::Mutex _mutex;
	vector<sp<SessionSubscription>> _subs ABSL_GUARDED_BY(_mutex);

	//O1: what Add overwrote when it lowered a tag-set's floor, so Unsubscribe can put it back.  Prior is the effective level
	//before the first lowering - what a surviving subscription's floor is compared against; PriorOverride is the explicit
	//override if there was one, so the restore reinstates it rather than leaving behind one the configuration never had.
	struct LoweredTag{ ELogLevel Prior; optional<ELogLevel> PriorOverride; };
	flat_map<ELogTags,LoweredTag> _loweredTags ABSL_GUARDED_BY(_mutex);

	//The level each lowered tag-set should be put back to, given what is left in _subs.  nullopt = ClearLevel, i.e. there was no
	//override before.  Applying the answer is deliberately left until after they release it, because
	//SetLevel/ClearLevel call Logging::UpdateCumulative over the logger list and Logging::Log holds that while calling Write,
	//which takes _mutex - the opposite order.
	ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) Ω takeRestores()ι->vector<std::pair<ELogTags,optional<ELogLevel>>>{
		vector<std::pair<ELogTags,optional<ELogLevel>>> y;
		for( auto it = _loweredTags.begin(); it!=_loweredTags.end(); ){
			optional<ELogLevel> needed;//the deepest floor the surviving subscriptions on this tag-set still ask for.
			for( let& s : _subs ){
				if( s->Sub.Tags==it->first && (!needed || s->Sub.MinLevel<*needed) )
					needed = s->Sub.MinLevel;
			}
			if( needed && *needed<it->second.Prior ){//someone still wants it below where it started - raise it to just what they need, no further.
				y.emplace_back( it->first, needed );
				++it;
			}
			else{//nothing left lowering it: reinstate exactly what was there, override or none.
				y.emplace_back( it->first, it->second.PriorOverride );
				it = _loweredTags.erase( it );
			}
		}
		return y;
	}
	Ω applyRestores( const vector<std::pair<ELogTags,optional<ELogLevel>>>& restores )ι->void{
		auto log = Logging::FindLogger<SubscribeLog>();//Unsubscribe is static; the levels live on the logger instance.
		if( !log )
			return;
		for( let& [tags,level] : restores ){
			if( level )
				log->SetLevel( tags, *level );
			else
				log->ClearLevel( tags );
		}
	}

	α SubscribeLog::Shutdown( bool, SL )ι->void{
		ul _{ _mutex };
		_subs.clear();
		//Deliberately no restoreLevels here: SetLevel/ClearLevel call Logging::UpdateCumulative over the logger list, which is
		//itself being torn down around this call.  The process is ending, so the floor no longer matters.
		_loweredTags.clear();
	}

	//O1: a `logs` subscription defaults to Trace (LogSubscription::MinLevel) and Add pushes that into the logger's tag threshold
	//for the whole process.  Unsubscribe only erased the entry, so one client connecting and leaving held the process at Trace
	//until it died.  Put the floor back to whatever the surviving subscriptions still need - or to exactly what was there before.
	α SubscribeLog::Unsubscribe( SocketId socketId )ι->void{
		vector<std::pair<ELogTags,optional<ELogLevel>>> restores;
		{
			ul _{ _mutex };
			std::erase_if( _subs, [=](auto&& s){ return s->Session->Id() == socketId; } );
			restores = takeRestores();
		}
		applyRestores( restores );//outside _mutex - see takeRestores.
	}

	α SubscribeLog::Add( sp<Web::Server::IWebsocketSession> session, vector<QL::Subscription>&& subs )ι->void{
		ul _{ _mutex };
		for( auto& request : subs ){
			auto s = ms<SessionSubscription>( session, move(request) );
			if( let prior = MinLevel(s->Sub.Tags); s->Sub.MinLevel<prior ){
				if( !_loweredTags.contains(s->Sub.Tags) ){//O1: remember what to put back, once, before the first lowering overwrites it.
					optional<ELogLevel> priorOverride;
					ConfiguredTags().cvisit( s->Sub.Tags, [&priorOverride](auto& kv){ priorOverride = kv.second; } );
					_loweredTags.emplace( s->Sub.Tags, LoweredTag{prior, priorOverride} );
				}
				SetLevel( s->Sub.Tags, s->Sub.MinLevel );
			}
			_subs.push_back( move(s) );
		}
	}

	thread_local bool _writing{};
	struct WriteGuard final : noncopyable{
		WriteGuard()ι{ _writing = true; }
		~WriteGuard(){ _writing = false; }
	};
	α SubscribeLog::Write( const Logging::Entry& m )ι->void{
		Write( m, _appPK, _connectionPK );
	}
	α SubscribeLog::Write( const Logging::Entry& m, uint32 appPK, App::ConnectionPK connectionPK )ι->void{
		if( _writing )
			return;//re-entered from a session's own WriteSubscription - the subscriber does not want its delivery echoed back to it, and dispatching it would not terminate.
		WriteGuard guard;
		vector<sp<SessionSubscription>> matches;
		{
			rl _{ _mutex };
			for( let& s : _subs ){
				if( m.Level<s->Sub.MinLevel || empty(m.Tags & s->Sub.Tags) )
					continue;
				let& filter = s->Sub.Fields.Filter();
				let valid = filter.Test( "time", m.Time )
					&& filter.Test( "text", m.Text )
					&& filter.Test( "line", m.Line )
					&& filter.Test( "templateId", m.Id() )
					&& filter.Test( "message", m.Message() )
					&& filter.Test( "appPK", appPK )
					&& filter.Test( "appConnectionPK", connectionPK )
					&& ( !filter.ColumnFilters.contains("args") || std::ranges::all_of(m.Arguments, [&]( let& arg ){ return filter.Test("args", arg); }) );
				if( valid )
					matches.push_back( s );
			}
		}
		for( let& s : matches )//outside the lock - WriteSubscription logs, and that comes back through Logging::Log into this function.
			s->Session->WriteSubscription( appPK, connectionPK, m, s->Sub );
	}
}