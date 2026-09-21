#pragma once
#include <boost/beast/ssl.hpp>
#include <jde/web/usings.h>
#include <jde/web/client/socket/IClientSocketSession.h>
#include <jde/web/client/socket/ClientSocketAwait.h>
#include <jde/opc/uatypes/NodeId.h>
#include "Opc.FromClient.pb.h"
#include "Opc.FromServer.pb.h"
#include <jde/app/proto/Common.pb.h>

namespace Jde::Opc::Gateway::Tests{
	struct IListener{
		β OnData( string opcId, NodeId nodeId, const vector<FromServer::Value>& values )ι->void=0;
	};
	//An error the gateway *answered* with - it came back over a working socket, unlike a transport failure.  Anything that reacts
	//to a dead connection must not react to these: the gateway is fine and it is the thing behind it that failed (an OPC server
	//that is down, a rejected write).  The soak's reconnect-on-failure did, and killed itself trying to re-subscribe to a server
	//that was still down (soak-findings #12).  Move/Throw keep the type across BlockAwait's rethrow.
	//Only OnRead's kException path produces one; CloseTasks fails tasks with a plain Exception, so a dead socket never reads
	//as an answer (subscription-disconnect #1).
	struct GatewayErrorResponse final : Exception{
		using Exception::Exception;
		α Move()ι->up<Exception> override{ return mu<GatewayErrorResponse>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
	};
	struct GatewayClientSocket final : Web::Client::TClientSocketSession<FromClient::Transmission,FromServer::Transmission>{
		using base = Web::Client::TClientSocketSession<FromClient::Transmission,FromServer::Transmission>;
		Τ using await = Web::Client::ClientSocketAwait<T>;
		GatewayClientSocket( sp<net::io_context> ioc, optional<ssl::context>& ctx )ι;
		//RemoveShutdown pairs Connect's AddShutdown: it registers a raw `this`, and nothing else takes it back out, so a
		//session that dies with the test left Process::Shutdown a dangling IShutdown* to call at exit (the same pairing
		//IAppClient and RemoteLog spell out).  Latent until the client stopped waiting out every request's full timeout -
		//that wait was holding these sessions alive to the end of the process (emulator-review #11).
		~GatewayClientSocket(){ Process::RemoveShutdown( this ); TRACET(ELogTags::Test, "GatewayClientSocket::~GatewayClientSocket"); }

		α Connect( SessionPK sessionId, SRCE )ι->await<uint32>;
		α Query( string&& query, jobject variables, bool returnRaw, SRCE )ι->await<jvalue> override;
		α QuerySync( string&& query, jobject variables={} )ε->jvalue;
		α Subscribe( ServerCnnctnNK slug, const vector<NodeId>& nodes, sp<IListener> listener, SRCE )ε->await<FromServer::SubscriptionAck>;
		α Subscribe( string&& /*query*/, jobject /*variables*/, sp<QL::IListener> /*listener*/, SL )ε->await<jarray> override{ ASSERT(false); throw "noimpl"; }
		α Unsubscribe( vector<QL::SubscriptionId>&&, SL )ι->void override{ ASSERT(false); }
		α LogSubscribe( jobject&& ql, jobject vars, sp<IListener> listener, SRCE )ε->await<jarray>;

		α Unsubscribe( ServerCnnctnNK slug, const vector<NodeId>& nodeIds, SRCE )ε->await<FromServer::UnsubscribeAck>;
		Ω PendingSubscriptionRecords()ι->uint;//subscribe + unsubscribe requests still awaiting their ack, across every socket - a failed request may leave none (m2-closing #15).
	private:
		α CloseTasks( beast::error_code ec )ι->void override;
		α HandleException( std::any&& h, Exception&& e )ι;//e arrives typed by the caller: GatewayErrorResponse or a transport failure.
		α OnRead( FromServer::Transmission&& transmission )ι->void override;
		α OnClose( beast::error_code ec )ι->void override;
		α OnAck( uint32 ack )ι->void;
	};
	α Socket()ι->GatewayClientSocket&;
}