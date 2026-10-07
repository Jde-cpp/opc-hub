#pragma once
#include <jde/web/server/Sessions.h>
#include <jde/web/client/socket/IClientSocketSession.h>
#include "proto/test.pb.h"
#include <jde/web/client/socket/ClientSocketAwait.h>

namespace Jde::Web::Mock{
	using namespace Jde::Web::Client;
	struct ClientSocketSession final : TClientSocketSession<Proto::FromClientTransmission,Proto::FromServerTransmission>{
		using base = TClientSocketSession<Proto::FromClientTransmission,Proto::FromServerTransmission>;
		ClientSocketSession( sp<net::io_context> ioc, optional<ssl::context>& ctx )ι;
		~ClientSocketSession(){ TRACET(ELogTags::Test, "ClientSocketSession::~ClientSocketSession"); }

		α Connect( SessionPK sessionId, SRCE )ι->ClientSocketAwait<SessionPK>;
		α Echo( str x, SRCE )ι->ClientSocketAwait<string>;
		α CloseServerSide( SRCE )ι->ClientSocketAwait<string>;
		α BadTransmissionClient( SRCE )ι->ClientSocketAwait<string>;
		α BadTransmissionServer( SRCE )ι->ClientSocketAwait<string>;
		α SetSessionId( str strSessionId, RequestId requestId )->Server::Sessions::UpsertAwait::Task;
		//A server-initiated close reaches OnRead as websocket::error::closed and no async_close of ours ever completes, so
		//OnRead has to fire OnClose itself - counted here because that is the only externally visible proof it did.
		α OnCloseCount()Ι->uint{ return _onCloseCount.load(); }
	private:
		α Query( string&&, jobject, bool, SL )ι->ClientSocketAwait<jvalue> override{ ASSERT(false); return { {}, {}, {} }; }
		α Subscribe( string&&, jobject, sp<QL::IListener>, SL )ε->ClientSocketAwait<jarray> override{ ASSERT(false); return { {}, {}, {} }; }
		α Unsubscribe( vector<QL::SubscriptionId>&&, SL )ι->void override{ ASSERT(false); }

		α OnRead( Proto::FromServerTransmission&& transmission )ι->void override;
		α OnClose( beast::error_code ec )ι->void override;

		α OnAck( uint32 ack )ι->void;
		atomic<uint> _onCloseCount{};
	};
}