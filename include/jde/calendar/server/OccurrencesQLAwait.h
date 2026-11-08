#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::QL{ struct IQL; }
namespace Jde::Calendar::Server{
	//calendarOccurrences( calendar:"plant", start:"2026-10-19T00:00:00Z", end:"2026-10-26T00:00:00Z" ){ shift name day start end breaks{ start end } }
	//(docs/calendar/spec.md *Reads*):  the calendar by slug or id, `calendar`, or by `id`, and Schedule::Occurrences over [start, end)
	//from the server's own rows, so the web and a remote tool get one resolver's answer.  A system table resolving no view
	//(QL::SetSystemTables), claimed in App::Server::CustomQuery, which the AppServer's QL and the hub's HubQL both call.  It takes
	//Read on calendars;  the four tables are read as System through `ql`, live rows only.  A bound without a zone is a local time
	//in the calendar's zone, as the mutations read one.  Refused, BadRequest:  a missing bound, one outside the years 1 to 9999, an end not after its
	//start, and a range over a year of the calendar's days;  a calendar not found, or deleted, is NotFound.
	struct OccurrencesQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		//ql:  the host's, App::Server::QLPtr() - the AppServer's LocalQL, or the HubQL in the hub.
		OccurrencesQLAwait( QL::TableQL&& q, QL::Creds creds, sp<QL::IQL> ql, SRCE )ι:base{ move(q), move(creds), sl }, _ql{ move(ql) }{}
		static constexpr sv Command{ "calendarOccurrences" };
		Ω IsApplicable( const QL::TableQL& q )ι->bool{ return q.JsonName==Command; }
	private:
		α Query()ι->TAwait<jvalue>::Task override;
		sp<QL::IQL> _ql;
	};
}