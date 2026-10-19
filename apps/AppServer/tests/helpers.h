#pragma once
#include <condition_variable>
#include <thread>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/app/usings.h>
#include <jde/web/client/socket/IClientSocketSession.h>
#include <jde/web/server/Sessions.h>
#include <jde/app/proto/App.FromClient.pb.h>
#include <jde/app/proto/App.FromServer.pb.h>
#include <jde/ql/QLAwait.h>
#include "../src/appStartup.h"
#include "../src/ql/AppServerQL.h"

namespace Jde::App::Server::Tests{
	constexpr sv Host{ "127.0.0.1" };//sessions are endpoint-bound; connect by ip so the socket's remote address matches the endpoint sessions are minted with.
	Ξ Port()ι->PortType{ return Settings::FindNumber<PortType>("/http/app/port").value_or(1972); }

	//on the server's own QL, as System unless told otherwise.
	Ξ RunQL( string query, jobject vars={}, Jde::UserPK executer={Jde::UserPK::System} )ε->jvalue{
		return BlockAwait<QL::QLAwait<jvalue>,jvalue>( QL::QLAwait<jvalue>{move(query), move(vars), executer, Server::QLPtr()} );
	}
	//the status f failed with;  None when it did not.
	Ξ Status( function<void()> f )->EHttpStatus{
		try{
			f();
		}
		catch( const Exception& e ){
			return e.HttpStatus();
		}
		return EHttpStatus::None;
	}
	Ξ Id( const jvalue& v )ε->uint{ return Json::AsNumber<uint>( Json::AsObject(v), "id" ); }
	//by slug, find-or-create:  the default (mysql) run keeps its db, and a second create trips the unique slug index.
	Ξ FindOrCreate( sv table, sv slug )ε->uint{
		auto row = RunQL( Ƒ("{}( slug:$slug, deleted:null ){{ id }}", table), {{"slug", slug}} );
		if( !row.is_object() || !row.get_object().contains("id") )
			row = RunQL( Ƒ("create{}{}( slug:$slug, name:$slug ){{ id }}", (char)std::toupper(table[0]), table.substr(1)), {{"slug", slug}} );
		return Id( row );
	}
	Ξ User( sv slug )ε->Jde::UserPK{ return Jde::UserPK{ (Jde::UserPK::Type)FindOrCreate("user", slug) }; }

	using FromClientTrans = Proto::FromClient::Transmission;
	using FromServerTrans = Proto::FromServer::Transmission;
	using FromServerMessage = Proto::FromServer::Message;
	using FromClientMessage = Proto::FromClient::Message;

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

	//a transmission of one message under requestId, `fill`ed.
	Ξ OneMessage( RequestId requestId, function<void(FromClientMessage&)> fill )->FromClientTrans{
		FromClientTrans t;
		auto& m = *t.add_messages();
		m.set_request_id( requestId );
		fill( m );
		return t;
	}
	//OneMessage written to session, under requestId when it answers the server's own request or a test picks it.
	Ξ Send( RawClientSession& session, RequestId requestId, function<void(FromClientMessage&)> fill )->RequestId{
		session.Write( OneMessage(requestId, move(fill)) );
		return requestId;
	}
	//under a fresh request id, returned.
	Ξ Send( RawClientSession& session, function<void(FromClientMessage&)> fill )->RequestId{ return Send( session, session.NextRequestId(), move(fill) ); }
	//polls pred every 50ms until it holds or timeout passes;  whether it held.
	Ξ WaitUntil( function<bool()> pred, std::chrono::milliseconds timeout=std::chrono::seconds{5} )->bool{
		for( const auto expiration = steady_clock::now()+timeout; !pred(); std::this_thread::sleep_for(std::chrono::milliseconds{50}) ){
			if( steady_clock::now()>expiration )
				return false;
		}
		return true;
	}

	Ξ Connect()ε->sp<RawClientSession>{
		auto session = ms<RawClientSession>( Executor() );
		BlockVoidAwait( session->RunSession( string{Host}, Port() ) );
		return session;
	}
	//mints a server-side session bound to Host, the address raw client sockets connect from.
	Ξ MintSession( Jde::UserPK userPK={1} )ι->SessionPK{
		return Web::Server::Sessions::Add( userPK, string{Host}, true )->SessionId;
	}

	struct RegisteredInstance : App::ConnectionPks{ bool AuthResult{}; };
	//registers session as an application instance (kInstance) and returns the pks the server minted for it.
	//authResource: the schema the instance asks to be the admin authorizer for (M10).  Empty for an app that authorizes nothing,
	//which is every caller here bar the ones testing that arm;  userPK is the session's user, the one the gate tests.
	//the two halves of RegisterInstance, for a test that needs several registrations in flight at once (RegistrationTests).
	Ξ SendInstance( RawClientSession& session, str application, str instanceName, str host, PortType webPort, uint32 pid=1234, str authResource="", Jde::UserPK userPK={1} )ε->RequestId{
		return Send( session, [&](FromClientMessage& m){
			auto& instance = *m.mutable_instance();
			instance.set_application( application );
			instance.set_instance_name( instanceName );
			instance.set_host( host );
			instance.set_web_port( webPort );
			instance.set_pid( pid );
			instance.set_session_id( MintSession(userPK) );
			instance.set_auth_resource( authResource );
		});
	}
	Ξ AwaitInstance( RawClientSession& session, RequestId requestId, str instanceName, std::chrono::seconds timeout=std::chrono::seconds{10} )ε->RegisteredInstance{
		auto reply = session.WaitFor( [requestId](const FromServerMessage& m){ return m.request_id()==requestId && m.value_case()==FromServerMessage::kConnectionInfo; }, timeout );
		THROW_IF( !reply, "No ConnectionInfo reply for instance '{}' within {}s.", instanceName, timeout.count() );
		const auto& info = reply->connection_info();
		return { {info.app_pk(), info.instance_pk(), info.connection_pk()}, info.auth_result() };
	}
	Ξ RegisterInstance( RawClientSession& session, str application, str instanceName, str host, PortType webPort, uint32 pid=1234, str authResource="", Jde::UserPK userPK={1} )ε->RegisteredInstance{
		return AwaitInstance( session, SendInstance(session, application, instanceName, host, webPort, pid, authResource, userPK), instanceName );
	}
	//Runs query on a thread of its own - it blocks until the instance answers - and hands `answer` the first ClientQuery `instance`
	//is sent whose text contains `asks`.  Fails the test when the instance was never asked or the query threw.
	Ξ AskInstance( RawClientSession& instance, string query, sv asks, function<void(const FromServerMessage& asked)> answer )->jvalue{
		jvalue y; string failure;
		std::thread ql{ [&]{ try{ y = RunQL( move(query) ); }catch( const std::exception& e ){ failure = e.what(); } } };
		const auto asked = instance.WaitFor( [asks](const FromServerMessage& m){ return m.value_case()==FromServerMessage::kClientQuery && m.client_query().query().contains(asks); } );
		if( asked )
			answer( *asked );
		ql.join();//unconditional - the query times out on its own when nothing answered.
		EXPECT_TRUE( asked ) << "the instance was never asked:  " << failure;
		EXPECT_TRUE( failure.empty() ) << failure;
		return y;
	}
}
