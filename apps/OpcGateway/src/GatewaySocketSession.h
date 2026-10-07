#pragma once
#include <jde/web/server/Sessions.h>
#include <jde/web/client/usings.h>
#include "ql/GatewayQL.h"
#include "types/MonitoringNodes.h"
#include <jde/web/server/IWebsocketSession.h>

namespace Jde::Proto{ class Query; }
namespace Jde::Opc{ struct NodeId; }
namespace Jde::Opc::Gateway{
	using namespace Jde::Web::Server;
	using namespace Jde::Web::Client;
	struct GatewaySocketSession final: TWebsocketSession<FromServer::Transmission,FromClient::Transmission>, IDataChange{
		using base = TWebsocketSession<FromServer::Transmission,FromClient::Transmission>;
		GatewaySocketSession( sp<IRestStream> stream, beast::flat_buffer&& buffer, TRequestType&& request, tcp::endpoint&& userEndpoint, uint32 connectionIndex )ι;
		α OnRead( FromClient::Transmission&& transmission )ι->void override;
		α SendDataChange( const ServerCnnctnNK& opcNK, const NodeId& node, const Value& value )ι->void override;
		α to_string()Ι->string override{ return Ƒ( "{:x}", Id() ); }
		α UserPK()Ι->Jde::UserPK override{ return Session() ? Session()->UserPK : Jde::UserPK{}; }
		α WriteException( runtime_error&& e, Jde::RequestId requestId, SRCE )ι->void override;
	private:
		α CreateSubscription( sp<UAClient> client, flat_set<NodeId> nodes, RequestId requestId )ι->VoidAwait::Task;
		α LocalQL()Ι->sp<QL::IQL> override{ return QLPtr(); }
		α OnClose()ι->void override;
		α ProcessTransmission( FromClient::Transmission&& transmission )ι->void;
		α SendQueryClient( QL::TableQL&&, Jde::UserPK, Jde::RequestId )ε->void override{ throw Exception{ "NoImpl" }; }
		α SetSessionId( str sessionId, RequestId requestId )->Sessions::UpsertAwait::Task;
		α SharedFromThis()ι->sp<GatewaySocketSession>{ return std::dynamic_pointer_cast<GatewaySocketSession>(shared_from_this()); }

		α GraphQL( Jde::Proto::Query&& q, uint requestId )ι->TAwait<jvalue>::Task;
		α Subscribe( ServerCnnctnNK&& opcId, flat_set<NodeId> nodes, uint32 requestId )ι->TAwait<sp<UAClient>>::Task;
		α Unsubscribe( ServerCnnctnNK&& opcId, flat_set<NodeId> nodes, uint32 requestId )ι->void;

		α WriteSubscription( const jvalue& j, Jde::RequestId requestId )ι->void override;
		α WriteSubscription( uint32 /*appPK*/, uint32 /*appInstancePK*/, const Logging::Entry& /*e*/, const QL::Subscription& /*sub*/ )ι->void override{ ASSERT(false); }
		α WriteSubscriptionAck( flat_set<QL::SubscriptionId>&& subscriptionIds, Jde::RequestId requestId )ι->void override;
		α WriteComplete( Jde::RequestId requestId )ι->void override;
		α WriteException( string&& e, Jde::RequestId requestId, SL sl )ι->void override;
		α WriteException( Exception&& e )ι->void{ WriteException( move(e), 0 ); }

		α SendAck( uint32 id )ι->void override;
	};
}