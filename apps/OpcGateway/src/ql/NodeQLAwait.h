#pragma once
#include <jde/ql/types/TableQL.h>
#include "../uatypes/Browse.h"
#include "../uatypes/ExpectedNodeId.h"
#include "../async/ReadAwait.h"

namespace Jde::Web::Server{ struct SessionInfo; }
namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	using BrowsePathResponse = flat_map<string,ExpectedNodeId>;
	struct NodeQLAwait final: TAwaitEx<jvalue,TAwait<BrowsePathResponse>::Task>{//Execute's awaits all go through Any(), so its task type is free - this one from when the translate was a UAStrandAwait<BrowsePathResponse>.
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