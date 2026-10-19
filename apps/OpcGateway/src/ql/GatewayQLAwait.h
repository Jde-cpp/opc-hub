#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/ql/types/RequestQL.h>
#include <jde/web/server/IHttpRequestAwait.h>
#include <jde/ql/IQLAwaitExe.h>

namespace Jde::Opc::Gateway{
	struct Namespaces; struct UAClient;
	struct IGatewayQLAwait : noncopyable{
	protected:
		α GetClient( QL::IQLAwaitExe* await )ι->TAwait<sp<UAClient>>::Task;
		sp<UAClient> _client;
	};
	struct GatewayQLAwait final : QL::IQLTableAwaitExe, IGatewayQLAwait{
		using base = QL::IQLTableAwaitExe;
		GatewayQLAwait( QL::TableQL q, QL::Creds creds, SRCE )ι:base{move(q), move(creds), sl}{}
		//A gateway custom query's await:  opcSessions, search, serverConnection{opcSessions|opcConnections}, the history groups, or
		//anything keyed by `opc`.  Null for any other, so a host QL (OpcHub, through Gateway::CustomQuery) hands it every query
		//and app/access tables never reach ConnectAwait.
		Ω Test( QL::TableQL& q, QL::Creds executer, SL sl )->up<TAwait<jvalue>>;
		α Suspend()ι->void override{ GetClient( this ); }
		//The connection-attribute readers, untransformed - each builds what `q` asks for - shared with testConnection, which answers
		//all of them in one object for a connection that is not saved.  Strand-only:  connection attributes are sync UA services.
		Ω ServerDescription( const QL::TableQL& q, const sp<UAClient>& client )ε->jobject;
		Ω SecurityPolicyUri( const sp<UAClient>& client )ε->string;
		Ω SecurityMode( const sp<UAClient>& client )ε->string;
		Ω NamespaceRows( const QL::TableQL& q, const Gateway::Namespaces& namespaces )ι->jarray;//namespaces{ index uri }:  the server's NamespaceArray as NamespacesAwait read it, index by index - no UA call.
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
	struct GatewayQLMAwait final : QL::IQLTableMutationExe, IGatewayQLAwait{
		using base = QL::IQLTableMutationExe;
		GatewayQLMAwait( QL::MutationQL&& q, QL::Creds creds, SRCE )ι:base{move(q), move(creds), sl}{}
		Ω IsApplicable( const QL::MutationQL& m )ι->bool;//the server-bound mutations, updateVariable and every hist edit but a group's.
		Ω Test( QL::MutationQL& q, QL::Creds executer, SL sl )->up<TAwait<jvalue>>;
		α Suspend()ι->void override;
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};
}