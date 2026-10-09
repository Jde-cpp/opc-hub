#include "ClientSocketSession.h"
#include <jde/web/usings.h>
#include <jde/web/server/Web.FromServer.h>
#include "ServerMock.h"
#include "jde/fwk/log/logTags.h"

#define let const auto

namespace Jde::Web::Mock{
	ClientSocketSession::ClientSocketSession( sp<net::io_context> ioc, optional<ssl::context>& ctx )ι:
		base{ ioc, ctx }
	{}

	α ClientSocketSession::OnAck( uint32 serverSocketId )ι->void{
		SetId( serverSocketId );
		INFOT( ELogTags::SocketClientRead, "[{}] {} AppClientSocketSession created: {}.", Id(), IsSsl() ? "Ssl" : "Plain", Host() );
	}

	α ClientSocketSession::SetSessionId( str strSessionId, RequestId /*requestId*/ )->Server::Sessions::UpsertAwait::Task{
		//LogRead( Ƒ("sessionId: '{}'", strSessionId), requestId );
		try{
			auto sessionInfo = co_await Server::Sessions::UpsertAwait( strSessionId, "127.0.0.1", true, AppClient() );
			base::SetInfo( Server::ToProto(*sessionInfo) );
			//Write( FromServer::CompleteTrans(requestId) );
		}
		catch( runtime_error& e ){
			//WriteException( move(e), requestId );
		}
	}


	α ClientSocketSession::OnRead( Proto::FromServerTransmission&& transmission )ι->void{
		auto size = transmission.messages_size();
		for( auto i=0; i<size; ++i ){
			auto m = transmission.mutable_messages( i );
			using enum Proto::FromServerMessage::ValueCase;
			let requestId = m->request_id();
			switch( m->Value_case() ){
			case kAck:
				OnAck( m->ack() );
				break;
			case kSessionId:{
				auto h = std::any_cast<ClientSocketAwait<SessionPK>::Handle>( IClientSocketSession::PopTask(requestId).Handle );
				SetSessionId(Ƒ("{:x}", m->session_id()), requestId );
				h.promise().Resume( m->session_id(), h );
				break;}
			case kEchoText:{
				auto h = std::any_cast<ClientSocketAwait<string>::Handle>( IClientSocketSession::PopTask(requestId).Handle );
				h.promise().SetValue( move(*m->mutable_echo_text()) );
				h.resume();
				break;}
			case kException:{
				if( auto task = requestId ? PopTask( requestId ) : PendingTask{}; task.Fail )
					task.Fail( Exception{move(*m->mutable_exception())} );
				else
					LOG( requestId ? ELogLevel::Critical : ELogLevel::Warning, ELogTags::SocketClientRead, "[{}]Failed to process incoming exception '{}'.", hex(requestId), m->exception() );
				break;}
			default:
				BREAK;
			}
		}
	}
	α ClientSocketSession::Connect( SessionPK sessionId, SL sl )ι->ClientSocketAwait<SessionPK>{
		Proto::FromClientTransmission t;
		auto request = t.add_messages();
		request->set_session_id( sessionId );
		let requestId = NextRequestId();
		request->set_request_id( requestId );
		return ClientSocketAwait<SessionPK>{ t, requestId, shared_from_this(), sl };
	}
	α ClientSocketSession::Echo( str x, SL sl )ι->ClientSocketAwait<string>{
		Proto::FromClientTransmission t;
		auto request = t.add_messages();
		request->set_echo( x );
		let requestId = NextRequestId();
		request->set_request_id( requestId );
		return ClientSocketAwait<string>{ t, requestId, shared_from_this(), sl };
	}
	α ClientSocketSession::CloseServerSide( SL sl )ι->ClientSocketAwait<string>{
		Proto::FromClientTransmission t;
		auto request = t.add_messages();
		request->mutable_close_server_side();
		let requestId = NextRequestId();
		request->set_request_id( requestId );
		return ClientSocketAwait<string>{ t, requestId, shared_from_this(), sl };
	}
	α ClientSocketSession::BadTransmissionClient( SL sl )ι->ClientSocketAwait<string>{
		let requestId =  NextRequestId();
		return ClientSocketAwait<string>{ "ABCDEFG", requestId, shared_from_this(), sl }; //need destructed.
	}
	α ClientSocketSession::BadTransmissionServer( SL sl )ι->ClientSocketAwait<string>{
		Proto::FromClientTransmission t;
		auto request = t.add_messages();
		request->mutable_bad_transmission_server();
		let requestId = NextRequestId();
		request->set_request_id( requestId );
		return ClientSocketAwait<string>{ t, requestId, shared_from_this(), sl };
	}

	α ClientSocketSession::OnClose( beast::error_code ec )ι->void{
		++_onCloseCount;//incremented before the base call: base::OnClose drains _tasks, which resumes whoever is blocked on the request, so the count has to be visible by then.
		base::OnClose( ec );
	}
}