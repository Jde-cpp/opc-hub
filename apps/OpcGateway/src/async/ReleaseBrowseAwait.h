#pragma once
#include <jde/opc/uatypes/Owned.h>
#include "UAAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//BrowseNext with releaseContinuationPoints, for the continuation points a browse answered with:  each holds one of the session's
	//few slots until the session closes, and a server out of them refuses a later browse that needs one BadNoContinuationPoints.
	//The gateway never browses on from one - a `references` read stops at its cap (reviews-issue/416-address-space.md #9), the search
	//crawl at the server's.  The points are copied, so the browse's answer may go first;  with none it resumes at once.  Answers the
	//service result, else the first point's refusal - BadContinuationPointInvalid for one the server no longer holds.
	struct ReleaseBrowseAwait final : UAServiceAwait<StatusCode, UA_BrowseNextResponse, UA_TYPES_BROWSENEXTREQUEST, UA_TYPES_BROWSENEXTRESPONSE>{
		ReleaseBrowseAwait( const UA_BrowseResponse& browse, sp<UAClient> client, SRCE )ι;
		α Suspend()ι->void override;
	private:
		α Request()ι->void* override;
		α Convert( UA_BrowseNextResponse&& response )ε->StatusCode override;
		vector<Owned<UA_ByteString,UA_TYPES_BYTESTRING>> _points;//the request's array:  owned here until the answer.
		UA_BrowseNextRequest _request{};//borrows _points, so never cleared.
	};
}