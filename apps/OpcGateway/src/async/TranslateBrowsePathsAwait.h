#pragma once
#include <open62541/client.h>
#include "../uatypes/Browse.h"
#include "../uatypes/ExpectedNodeId.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//TranslateBrowsePathsToNodeIds for each path from its own start - a template's members from the group's root (spec
	//*Templates*) - through open62541's generic asynchronous service on the client's strand:  the typed call is synchronous,
	//and drives the client's event loop until the answer, so the client's data changes stall for the round trip.  The i-th
	//answer is the i-th path's node, or why the server refused it, BadNoMatch for a path that leads nowhere.  Resumes with an
	//exception when the service itself fails.
	struct TranslateBrowsePathsAwait final : TAwait<vector<ExpectedNodeId>>{
		using base = TAwait<vector<ExpectedNodeId>>;
		TranslateBrowsePathsAwait( vector<UABrowsePath>&& paths, sp<UAClient> client, SRCE )ι:base{ sl }, _paths{ move(paths) }, _client{ move(client) }{}
		α Suspend()ι->void override;
		Ω OnResponse( UA_Client* client, void* await, UA_UInt32 requestId, void* response )ι->void;
		α OnComplete( const UA_TranslateBrowsePathsToNodeIdsResponse& response )ι->void;
	private:
		vector<UABrowsePath> _paths;//the request's array:  owned here until the answer.
		sp<UAClient> _client;
		RequestId _requestId{};
	};
}