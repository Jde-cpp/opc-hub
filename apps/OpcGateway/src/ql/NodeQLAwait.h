#pragma once
#include <jde/ql/QLHook.h>
#include "../uatypes/Browse.h"
#include "../uatypes/ExpectedNodeId.h"
#include "../async/ReadAwait.h"

namespace Jde::Web::Server{ struct SessionInfo; }
namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	using BrowsePathResponse = flat_map<string,ExpectedNodeId>;
	struct NodeQLAwait final: TAwaitEx<jvalue,TAwait<BrowsePathResponse>::Task>{//Execute's task type matches the UAStrandAwait<BrowsePathResponse> it co_awaits.
		using base = TAwaitEx<jvalue,TAwait<BrowsePathResponse>::Task>;
		NodeQLAwait( QL::TableQL&& query, sp<UAClient> client, SRCE )ι:
			base{ sl }, _client{client}, _query{move(query)}
		{}
		α Execute()ι->TAwait<BrowsePathResponse>::Task override;
	private:
		sp<UAClient> _client;
		QL::TableQL _query;
	};
}