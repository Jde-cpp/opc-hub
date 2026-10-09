#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/ql/types/RequestQL.h>
#include <jde/web/server/IHttpRequestAwait.h>
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	struct UAClient;
	struct IGatewayQLAwait : noncopyable{
	protected:
		α GetClient( QL::IQLAwaitExe* await )ι->TAwait<sp<UAClient>>::Task;
		sp<UAClient> _client;
	};
	struct GatewayQLAwait final : QL::IQLTableAwaitExe, IGatewayQLAwait{
		using base = QL::IQLTableAwaitExe;
		GatewayQLAwait( QL::TableQL q, QL::Creds creds, SRCE )ι:base{move(q), move(creds), sl}{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool;//a gateway custom query: opcSessions, search, serverConnection{opcSessions|opcConnections}, or anything keyed by `opc` - the queries a server-bound await answers.  A host QL (OpcHub) asks before handing the query over, so app/access tables never reach ConnectAwait.
		Ω Test( QL::TableQL& q, QL::Creds executer, SL sl )->up<TAwait<jvalue>>;
		α Suspend()ι->void override{ GetClient( this ); }
	private:
		α Query()ι->TAwait<jvalue>::Task override;
		α ConnectionAttributes( QL::TableQL&& q, sp<UAClient> client )ι->jvalue;
		α ServerDescription( QL::TableQL&& q, sp<UAClient> client )ε->jobject;
		α Namespaces( QL::TableQL&& q, sp<UAClient> client, flat_map<NodeId, Value>&& values )ε->jvalue;
		α SecurityPolicyUri( QL::TableQL&& q, sp<UAClient> client )ε->jvalue;
		α SecurityMode( QL::TableQL&& q, sp<UAClient> client )ε->jvalue;
	};
	struct GatewayQLMAwait final : QL::IQLTableMutationExe, IGatewayQLAwait{
		using base = QL::IQLTableMutationExe;
		GatewayQLMAwait( QL::MutationQL&& q, QL::Creds creds, SRCE )ι:base{move(q), move(creds), sl}{}
		Ω IsApplicable( const QL::MutationQL& m )ι->bool;//the server-bound mutations, updateVariable and the hist edits with `opc`; Test also routes updateLogSettings for the standalone gateway.
		Ω Test( QL::MutationQL& q, QL::Creds executer, SL sl )->up<TAwait<jvalue>>;
		α Suspend()ι->void override{ GetClient( this ); }
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}