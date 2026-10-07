#pragma once
#include <condition_variable>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/app/usings.h>
#include <jde/web/client/socket/IClientSocketSession.h>
#include <jde/web/server/Sessions.h>
#include <jde/app/proto/App.FromClient.pb.h>
#include <jde/app/proto/App.FromServer.pb.h>

namespace Jde::App::Server::Tests{
	constexpr sv Host{ "127.0.0.1" };//sessions are endpoint-bound; connect by ip so the socket's remote address matches the endpoint sessions are minted with.
	Ξ Port()ι->PortType{ return Settings::FindNumber<PortType>("/http/app/port").value_or(1972); }

	using FromClientTrans = Proto::FromClient::Transmission;
	using FromServerTrans = Proto::FromServer::Transmission;
	using FromServerMessage = Proto::FromServer::Message;

	//Raw protocol client: writes arbitrary FromClient transmissions and records every FromServer message, so tests can
	//assert on exception replies, pushed messages, and server-initiated closes without the AppClient's typed task layer.
	struct RawClientSession final : Web::Client::TClientSocketSession<FromClientTrans,FromServerTrans>{
		using base = Web::Client::TClientSocketSession<FromClientTrans,FromServerTrans>;
		RawClientSession( sp<net::io_context> ioc, optional<ssl::context> ctx={} )ι: base{ ioc, ctx }{}

		//returns the first recorded message matching pred and removes it; nullopt on timeout.
		α WaitFor( function<bool(const FromServerMessage&)> pred, std::chrono::seconds timeout=std::chrono::seconds{10} )ι->optional<FromServerMessage>{
			std::unique_lock l{ _mtx };
			optional<FromServerMessage> y;
			_cv.wait_for( l, timeout, [&]{
				for( auto p=_messages.begin(); p!=_messages.end(); ++p ){
					if( pred(*p) ){
						y = move( *p );
						_messages.erase( p );
						return true;
					}
				}
				return false;
			});
			return y;
		}
		α WaitForException( RequestId requestId, std::chrono::seconds timeout=std::chrono::seconds{10} )ι->optional<FromServerMessage>{
			return WaitFor( [=](const FromServerMessage& m){ return m.request_id()==requestId && m.value_case()==FromServerMessage::kException; }, timeout );
		}
		α WaitForClose( std::chrono::seconds timeout=std::chrono::seconds{10} )ι->bool{
			std::unique_lock l{ _mtx };
			return _cv.wait_for( l, timeout, [&]{ return _closeCount>0; } );
		}
		α CloseCount()ι->uint{ std::unique_lock l{ _mtx }; return _closeCount; }
		α CloseCode()ι->beast::error_code{ std::unique_lock l{ _mtx }; return _closeCode; }//why the socket ended: websocket::error::closed means the server sent a close frame, anything else that it dropped the transport.
	private:
		α Query( string&&, jobject, bool, SL )ι->Web::Client::ClientSocketAwait<jvalue> override{ ASSERT(false); return { {}, {}, {} }; }
		α Subscribe( string&&, jobject, sp<QL::IListener>, SL )ε->Web::Client::ClientSocketAwait<jarray> override{ ASSERT(false); return { {}, {}, {} }; }
		α Unsubscribe( vector<QL::SubscriptionId>&&, SL )ι->void override{ ASSERT(false); }
		α OnRead( FromServerTrans&& t )ι->void override{
			{
				std::unique_lock _{ _mtx };
				for( auto i=0; i<t.messages_size(); ++i )
					_messages.push_back( move(*t.mutable_messages(i)) );
			}
			_cv.notify_all();
		}
		α OnClose( beast::error_code ec )ι->void override{
			{
				std::unique_lock _{ _mtx };
				if( ++_closeCount==1 )
					_closeCode = ec;
			}
			_cv.notify_all();
			base::OnClose( ec );
		}

		std::mutex _mtx;
		std::condition_variable _cv;
		std::vector<FromServerMessage> _messages;
		uint _closeCount{};
		beast::error_code _closeCode;
	};

	Ξ Connect()ε->sp<RawClientSession>{
		auto session = ms<RawClientSession>( Executor() );
		BlockVoidAwait( session->RunSession( string{Host}, Port() ) );
		return session;
	}
	//mints a server-side session bound to Host, the address raw client sockets connect from.
	Ξ MintSession( Jde::UserPK userPK={1} )ι->SessionPK{
		return Web::Server::Sessions::Add( userPK, string{Host}, true )->SessionId;
	}

	struct RegisteredInstance{ App::ProgramPK Program{}; ProgInstPK Instance{}; App::ConnectionPK Connection{}; bool AuthResult{}; };
	//registers session as an application instance (kInstance) and returns the pks the server minted for it.
	//authResource: the schema the instance asks to be the admin authorizer for (M10).  Empty for an app that authorizes nothing,
	//which is every caller here bar the ones testing that arm;  userPK is the session's user, the one the gate tests.
	//the two halves of RegisterInstance, for a test that needs several registrations in flight at once (RegistrationTests).
	Ξ SendInstance( RawClientSession& session, str application, str instanceName, str host, PortType webPort, uint32 pid=1234, str authResource="", Jde::UserPK userPK={1} )ε->RequestId{
		FromClientTrans t;
		auto& m = *t.add_messages();
		const auto requestId = session.NextRequestId();
		m.set_request_id( requestId );
		auto& instance = *m.mutable_instance();
		instance.set_application( application );
		instance.set_instance_name( instanceName );
		instance.set_host( host );
		instance.set_web_port( webPort );
		instance.set_pid( pid );
		instance.set_session_id( MintSession(userPK) );
		instance.set_auth_resource( authResource );
		session.Write( move(t) );
		return requestId;
	}
	Ξ AwaitInstance( RawClientSession& session, RequestId requestId, str instanceName, std::chrono::seconds timeout=std::chrono::seconds{10} )ε->RegisteredInstance{
		auto reply = session.WaitFor( [requestId](const FromServerMessage& m){ return m.request_id()==requestId && m.value_case()==FromServerMessage::kConnectionInfo; }, timeout );
		THROW_IF( !reply, "No ConnectionInfo reply for instance '{}' within {}s.", instanceName, timeout.count() );
		const auto& info = reply->connection_info();
		return { info.app_pk(), info.instance_pk(), info.connection_pk(), info.auth_result() };
	}
	Ξ RegisterInstance( RawClientSession& session, str application, str instanceName, str host, PortType webPort, uint32 pid=1234, str authResource="", Jde::UserPK userPK={1} )ε->RegisteredInstance{
		return AwaitInstance( session, SendInstance(session, application, instanceName, host, webPort, pid, authResource, userPK), instanceName );
	}
}
