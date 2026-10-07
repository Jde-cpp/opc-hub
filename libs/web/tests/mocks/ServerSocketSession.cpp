#include "ServerSocketSession.h"
#include <jde/web/server/IWebsocketSession.h>
#include "../../server/Streams.h"
#include "ServerMock.h"

#define let const auto
namespace Jde::Web::Mock{
	ServerSocketSession::ServerSocketSession( sp<IRestStream> stream, beast::flat_buffer&& buffer, TRequestType&& request, tcp::endpoint&& userEndpoint, uint32 connectionIndex )ι:
		base{ move(stream), move(buffer), move(request), move(userEndpoint), connectionIndex }
	{}

	α ServerSocketSession::OnConnect( SessionPK sessionId, RequestId requestId )ι->Server::Sessions::UpsertAwait::Task{
		try{
			auto info = co_await Server::Sessions::UpsertAwait{ Ƒ("{:x}", sessionId), _userEndpoint.address().to_string(), true, AppClient(), true };
			base::SetSessionInfo( info );//as the real sessions do (GatewaySocketSession::SetSessionId, ServerSocketSession::SetSessionId) - without it SessionId() is 0 and nothing bound to the socket knows which session it runs under.
			Proto::FromServerTransmission t;
			auto m = t.add_messages();
			m->set_request_id( requestId );
			m->set_session_id( sessionId );
			Write( move(t) );
		}
		catch( runtime_error& e ){
			Proto::FromServerTransmission t;
			auto m = t.add_messages();
			m->set_request_id( requestId );
			m->set_exception( e.what() );
			Write( move(t) );
		}
	}

	α ServerSocketSession::WriteException( runtime_error&& e, RequestId, SL )ι->void{
		Proto::FromServerTransmission t;
		auto m = t.add_messages();
		m->set_exception( e.what() );
		Write( move(t) );
	}

	α ServerSocketSession::SendAck( uint32 id )ι->void{
		LogWrite( Ƒ("Ack id: {:x}", id), 0 );
		Proto::FromServerTransmission t;
		t.add_messages()->set_ack( id );
		Write( move(t) );
	}


	α ServerSocketSession::OnRead( Proto::FromClientTransmission&& messages )ι->void{
		for( const Proto::FromClientMessage& m : messages.messages() ){
			using enum Proto::FromClientMessage::ValueCase;
			let requestId = m.request_id();
			switch( m.Value_case() ){
			case kSessionId:
				OnConnect( m.session_id(), requestId );
			break;
			case kEcho:{
				Proto::FromServerTransmission t;
				auto res = t.add_messages();
				res->set_request_id( requestId );
				res->set_echo_text( m.echo() );
				Write( move(t) );
				break;
			}
			case kCloseServerSide:
				Close();
				break;
			case kBadTransmissionServer:
				IWebsocketSession::Write( "ABCDEFG" );
				break;
			default:
				BREAK;
				break;
			}
		}
	}
	α RequestHandler::WebsocketSession( sp<IRestStream>&& stream, beast::flat_buffer&& buffer, TRequestType req, tcp::endpoint userEndpoint, uint32 connectionIndex )ι->sp<IWebsocketSession>{
		return ms<ServerSocketSession>( move(stream), move(buffer), move(req), move(userEndpoint), connectionIndex );
	};
}