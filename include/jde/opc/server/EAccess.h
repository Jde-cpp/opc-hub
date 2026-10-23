#pragma once
#include <open62541/types.h>
#include <jde/access/usings.h>

namespace Jde::Opc::Server{
	enum class EAccess : uint8{
		None			= 0,
		Read			= UA_ACCESSLEVELMASK_READ,
		Write			= UA_ACCESSLEVELMASK_WRITE,
		HistoryRead		= UA_ACCESSLEVELMASK_HISTORYREAD,
		HistoryWrite	= UA_ACCESSLEVELMASK_HISTORYWRITE,
		SemanticChange	= UA_ACCESSLEVELMASK_SEMANTICCHANGE,
		StatusWrite		= UA_ACCESSLEVELMASK_STATUSWRITE,
		TimestampWrite	= UA_ACCESSLEVELMASK_TIMESTAMPWRITE,
		All				= Read | Write | HistoryRead | HistoryWrite | SemanticChange | StatusWrite | TimestampWrite
	};

	//Node ACLs are stored in the generic Access::ERights vocabulary - the same columns as every other resource, and what the
	//node-access page writes (node-access.ts: "NOT EAccess/EWriteAccess").  EAccess is open62541's UA_ACCESSLEVELMASK layout and
	//the two share no bit positions (ERights::Read=0x2 is UA WRITE), so the only way across is this translation - never a cast.
	//Policy:  Read covers the history read, Update the value and history writes, Delete the history write, Administer the
	//status/timestamp/semantic-change bits;  Create, Purge, Subscribe and Execute have no node-level bit (methods go through
	//GetUserExecutable, a subscription needs Read).  ERights::All comes out as EAccess::All.  access-review3 #4.
	constexpr α ToAccess( Access::ERights rights )ι->EAccess{
		using enum Access::ERights;
		uint8 y{};
		auto add = [&]( Access::ERights right, EAccess access ){ if( !empty(rights & right) ) y |= underlying(access); };
		add( Read, EAccess::Read | EAccess::HistoryRead );
		add( Update, EAccess::Write | EAccess::HistoryWrite );
		add( Delete, EAccess::HistoryWrite );
		add( Administer, EAccess::StatusWrite | EAccess::TimestampWrite | EAccess::SemanticChange );
		return (EAccess)y;
	}
}