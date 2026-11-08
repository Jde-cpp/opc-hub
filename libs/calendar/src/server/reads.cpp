#include "reads.h"
#include <jde/fwk/str.h>
#include "../refuse.h"

#define let const auto

namespace Jde::Calendar{
	constexpr ELogTags _tags{ ELogTags::App };

	α Server::SystemRead( string query, sp<QL::IQL> ql, SL sl )ε->QL::QLAwait<jvalue>{
		return QL::QLAwait<jvalue>{ move(query), jobject{}, Jde::UserPK{Jde::UserPK::System}, move(ql), true, sl };
	}

	α Server::ReadChildren( uint32 calendarId, bool liveOnly, sp<QL::IQL> ql, Rows& rows, SL sl )ι->AnyVoidTask{
		let filter = liveOnly ? sv{", deleted:null"} : sv{};
		let deleted = liveOnly ? sv{} : sv{" deleted"};
		auto childrenRead = SystemRead( Ƒ("{{ calendarPatterns( calendarId:{0}{1} ){{ id calendarId effective cycleDays cycleStart{2} }} calendarExceptions( calendarId:{0}{1} ){{ id calendarId name start finish isOpen{2} }} }}", calendarId, filter, deleted), ql, sl );
		auto children = co_await Any( childrenRead );
		auto& o = Json::AsObject( children, sl );
		rows.Patterns = move( Json::AsArray(o.at("calendarPatterns"), sl) );
		rows.Exceptions = move( Json::AsArray(o.at("calendarExceptions"), sl) );
		if( rows.Patterns.empty() )
			co_return;
		vector<uint32> ids;
		for( let& p : rows.Patterns )
			ids.push_back( Json::AsNumber<uint32>(Json::AsObject(p, sl), "id", sl) );
		auto shiftsRead = SystemRead( Ƒ("calendarShifts( calendarPatternId:[{}] ){{ id calendarPatternId name days start minutes breaks }}", Str::Join(ids)), ql, sl );
		auto shifts = co_await Any( shiftsRead );
		if( shifts.is_array() )
			rows.Shifts = move( shifts.get_array() );
	}

	α Server::FindTimeArg( const QL::Input& q, sv key, const Schedule& c, SL sl )ε->optional<TimePoint>{
		let p = q.FindPtr<jvalue>( key );
		if( !p || p->is_null() )
			return nullopt;
		REFUSE_IF( !p->is_string(), "{} is a dateTime, not {}.", key, serialize(*p) );
		try{
			return c.ToTimePoint( string{p->get_string()}, sl );
		}
		catch( const std::exception& e ){
			REFUSE( "{} is not an ISO time:  {}", key, e.what() );
		}
	}
}