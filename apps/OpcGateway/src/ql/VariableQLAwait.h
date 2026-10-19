#pragma once
#include <jde/ql/QLHook.h>
#include "../uatypes/Browse.h"

namespace Jde::Web::Server{ struct SessionInfo; }
namespace Jde::QL{ struct MutationQL; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	struct VariableQLAwait final: TAwaitEx<jvalue,TAwait<sp<UAClient>>::Task>{
		using base = TAwaitEx<jvalue,TAwait<sp<UAClient>>::Task>;
		VariableQLAwait( QL::MutationQL&& mutation, sp<Web::Server::SessionInfo> session, SRCE )ι:
			base{ sl }, _mutation{move(mutation)}, _session{move(session)}
		{}
		α Execute()ι->TAwait<sp<UAClient>>::Task override;//its own type is the connect's;  the type lookup, the write and the read-back go through Any().
	private:
		QL::MutationQL _mutation;
		sp<Web::Server::SessionInfo> _session;
	};
}