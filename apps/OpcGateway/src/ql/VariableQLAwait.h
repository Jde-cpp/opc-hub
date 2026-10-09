#pragma once
#include <jde/ql/QLHook.h>
#include "../uatypes/Browse.h"
#include "../async/ReadAwait.h"
#include "../async/WriteAwait.h"

namespace Jde::Web::Server{ struct SessionInfo; }
namespace Jde::QL{ struct MutationQL; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	struct VariableQLAwait final: TAwaitEx<jvalue,TAwait<sp<UAClient>>::Task>{
		using base = TAwaitEx<jvalue,TAwait<sp<UAClient>>::Task>;
		VariableQLAwait( QL::MutationQL&& mutation, sp<Web::Server::SessionInfo> session, SRCE )ι:
			base{ sl }, _mutation{move(mutation)}, _session{move(session)}
		{}
		α Execute()ι->TAwait<sp<UAClient>>::Task override;
	private:
		α Write( Value&& value )ι->TAwait<WriteResponse>::Task;
		α Read( QL::TableQL&& query )ι->TAwait<ReadResponse>::Task;

		sp<UAClient> _client;
		QL::MutationQL _mutation;
		NodeId _nodeId;
		sp<Web::Server::SessionInfo> _session;
	};
}