#pragma once
#include <jde/web/client/usings.h>
#include <jde/web/server/IWebsocketSession.h>
#include <jde/web/server/Sessions.h>
#include "proto/test.pb.h"

namespace Jde::Web::Mock{
	using namespace Jde::Web::Server;
	using namespace Jde::Web::Client;
	struct ServerSocketSession final : TWebsocketSession<Proto::FromServerTransmission,Proto::FromClientTransmission>{
		using base = TWebsocketSession<Proto::FromServerTransmission,Proto::FromClientTransmission>;
		ServerSocketSession( sp<IRestStream> stream, beast::flat_buffer&& buffer, TRequestType&& request, tcp::endpoint&& userEndpoint, uint32 connectionIndex )ι;
		α OnRead( Proto::FromClientTransmission&& transmission )ι->void override;
		α SendAck( uint32 serverSocketId )ι->void override;
		α LocalQL()Ι->sp<QL::IQL> override{ return nullptr; }
		α UserPK()Ι->Jde::UserPK override{ return Session() ? Session()->UserPK : Jde::UserPK{}; }
	private:
		α WriteException( runtime_error&& e, RequestId requestId, SRCE )ι->void override;
		α WriteException( std::string&&, Jde::RequestId, SL )ι->void override{ ASSERT(false); }
		α WriteException( Exception&& e )ι->void{ WriteException( move(e), 0 ); }
		α WriteSubscription( const jvalue&, RequestId )ι->void override{ ASSERT(false); }
		β WriteSubscription( uint32 /*appPK*/, uint32 /*appInstancePK*/, const Logging::Entry&, const QL::Subscription& )ι->void override{ ASSERT(false); }
		α WriteSubscriptionAck( flat_set<QL::SubscriptionId>&&, RequestId )ι->void override{ ASSERT(false); }
		α WriteComplete( RequestId )ι->void override{ ASSERT(false); }
		α OnConnect( SessionPK sessionId, RequestId requestId )ι->Server::Sessions::UpsertAwait::Task;
		α SendQueryClient( QL::TableQL&&, Jde::UserPK, RequestId )ε->void override{ ASSERT(false); }

		vector<sp<DB::AppSchema>> _schemas;
	};
}