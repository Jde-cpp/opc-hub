#pragma once
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::QL{ struct IQL; }
namespace Jde::Calendar::Server{
	namespace CalendarTable{ constexpr sv Calendars{ "calendars" }, Patterns{ "calendar_patterns" }, Shifts{ "calendar_shifts" }, Exceptions{ "calendar_exceptions" }; }

	//Every edit of the calendar's four tables (apps/AppServer/config/app-meta.jsonnet, docs/calendar/spec.md *Validation*):  create,
	//update, delete, restore and purge on calendars, calendarPatterns and calendarExceptions, create, update and purge on
	//calendarShifts, which has no `deleted`.  App::Server::CustomMutation routes them here, so the AppServer's QL and the hub's
	//HubQL validate alike.  In order:  the table's rights for the mutation's type;  the library's checks on the row as it would
	//be, the calendar's sibling rows read first through `ql` as System;  the refusals that need the database, a second pattern
	//with no effective date and a calendar's only pattern deleted, and the natural key's refusal of a date a soft-deleted
	//pattern holds, spelled so the caller knows to purge it;  a zone-less time read in the calendar's zone and an `effective`
	//or `cycleStart` moved back to its production day's start;  then the stock QL::MutationAwait, whose ids, `deleted` marks
	//and subscription fan-out are the generic mutation's.  Around it:  a calendar's create is followed by its first pattern, a
	//change of timeZone or dayStart by the rewrite of every pattern's dates and every exception's bounds under the new
	//settings, and a purge is preceded by its children's, since the foreign keys are enforced.
	struct CalendarMAwait final : QL::IQLTableMutationExe{
		using base = QL::IQLTableMutationExe;
		//ql:  the host's, App::Server::QLPtr() - the AppServer's LocalQL, or the HubQL in the hub - for the sibling reads and the side effects' rows.
		CalendarMAwait( QL::MutationQL&& m, QL::Creds creds, sp<QL::IQL> ql, SRCE )ι:base{ move(m), move(creds), sl }, _ql{ move(ql) }{}
		Ω IsApplicable( const QL::MutationQL& m )ι->bool;
	private:
		α Query()ι->TAwait<jvalue>::Task override;
		sp<QL::IQL> _ql;
	};
}