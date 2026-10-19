#pragma once
#include <open62541/client.h>
#include "../uatypes/Browse.h"
#include "../uatypes/ExpectedNodeId.h"
#include "UAAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//TranslateBrowsePathsToNodeIds for each path from its own start - a template's members from the group's root (spec
	//*Templates*).  Not the typed call:  it is synchronous, and drives the client's event loop until the answer, so the client's
	//data changes stall for the round trip.  The i-th answer is the i-th path's node, or why the server refused it, BadNoMatch
	//for a path that leads nowhere.  Resumes with an exception when the service itself fails.
	struct TranslateBrowsePathsAwait final : UAServiceAwait<vector<ExpectedNodeId>, UA_TranslateBrowsePathsToNodeIdsResponse, UA_TYPES_TRANSLATEBROWSEPATHSTONODEIDSREQUEST, UA_TYPES_TRANSLATEBROWSEPATHSTONODEIDSRESPONSE>{
		TranslateBrowsePathsAwait( vector<UABrowsePath>&& paths, sp<UAClient> client, SRCE )ι:UAServiceAwait{ move(client), "translateBrowsePathsToNodeIds", sl }, _paths{ move(paths) }{}
		α Suspend()ι->void override;
	private:
		α Request()ι->void* override;
		α Convert( UA_TranslateBrowsePathsToNodeIdsResponse&& response )ε->vector<ExpectedNodeId> override;
		vector<UABrowsePath> _paths;//the request's array:  owned here until the answer.
		UA_TranslateBrowsePathsToNodeIdsRequest _request{};//borrows _paths, so never cleared.
	};
}