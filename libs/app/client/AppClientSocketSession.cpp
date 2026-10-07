#include <jde/web/client/ClientSsl.h>
#include "jde/fwk/exceptions/Exception.h"
#include "jde/fwk/log/logTags.h"
#include <jde/app/client/AppClientSocketSession.h>
#include <jde/fwk/process/execution.h>
#include <jde/web/client/socket/ClientQL.h>
#include <jde/web/client/socket/ClientSocketAwait.h>
#include <jde/app/client/clientSubscriptions.h>
#include <jde/app/proto/app.FromClient.h>
#include <jde/app/proto/app.FromServer.h>
#include <jde/app/proto/common.h>
#include <jde/app/client/appClient.h>
#include <jde/app/client/clientSubscriptions.h>
#include <jde/app/client/IAppClient.h>

#define let const auto

namespace Jde::App{
	using Web::Client::ClientSocketAwait; using Protobuf::ToString;
	constexpr ELogTags _tags{ ELogTags::SocketClientRead };

namespace Client{
	constexpr uint8 _maxExecuteDepth{ 4 };

	StartSocketAwait::StartSocketAwait( SessionPK sessionId, sp<Access::Authorize> authorize, sp<IAppClient> appClient, SL sl )ι:
		base{ sl },
		_appClient{ appClient },
		_sessionId{ sessionId },
		_session{ ms<Client::AppClientSocketSession>(Executor(), IsSsl() ? Web::Client::Ssl::MakeContext() : optional<ssl::context>{}, move(authorize), move(appClient)) }
	{}

	Ω closeSession( sp<AppClientSocketSession> session, SL sl )ι->VoidTask{
		try{
			co_await session->Close( false, sl );
		}
		catch( Exception& e ){
		}
	}

	α StartSocketAwait::Suspend()ι->void{
		RunSession();
	}
	α StartSocketAwait::RunSession()ι->VoidTask{
		try{
			co_await _session->RunSession( Host(), Port() );//Web::Client
			THROW_IF( Process::ShuttingDown(), "Shutting down." );
			SendSessionId();
		}
		catch( runtime_error& e ){
			closeSession( _session, _sl );
			ResumeExp( move(e) );
		}
	}
	α StartSocketAwait::SendSessionId()ι->ClientSocketAwait<Proto::FromServer::ConnectionInfo>::Task{
		try{
			auto info = co_await _session->Connect( _sessionId );//handshake
			_session->SetInfo( move(*info.mutable_session_info()) );
			_appClient->SetSession( move(_session) );
			_appClient->ServerPublicKey = {
				{ info.certificate_modulus().begin(), info.certificate_modulus().end() },
				{ info.certificate_exponent().begin(), info.certificate_exponent().end() }
			};
			Resume( move(info) );
		}
		catch( runtime_error& e ){
			if( _session )
				closeSession( _session, _sl );
			ResumeExp( move(e) );
		}
	}

	AppClientSocketSession::AppClientSocketSession( sp<net::io_context> ioc, optional<ssl::context> ctx, sp<Access::Authorize> authorize, sp<IAppClient> appClient )ι:
		base( ioc, ctx ),
		_appClient{ appClient },
		_authorize{ authorize }
	{}

	α AppClientSocketSession::Connect( SessionPK sessionId, SL sl )ι->ClientSocketAwait<Proto::FromServer::ConnectionInfo>{
		let requestId = NextRequestId();
		let instanceName = Client::InstanceName();
		LOGSL( ELogLevel::Information, sl, ELogTags::SocketClientWrite, "[{}]Connect: '{}', authResource: '{}'.", hex(requestId), instanceName, _appClient->ResourceSchema );
		//M10: ResourceSchema is what OpcServer sets to its `opc.*` schema (opcServerStartup, before this connect); the gateway leaves
		//it empty and asks to authorize nothing.  Until this was passed, the AppServer's `if( instance.auth_resource().size() )` arm
		//never ran and every delegated admin check fell back to the AppServer's own Authorize.
		return ClientSocketAwait<Proto::FromServer::ConnectionInfo>{ ToString(FromClient::Instance(Process::AppName(), instanceName, sessionId, requestId, _appClient->ResourceSchema)), requestId, shared_from_this(), sl };
	}

	α AppClientSocketSession::CloseTasks( beast::error_code ec )ι->void{
		auto f = [this, ec]( std::any&& h )->void {
			HandleException( move(h), CodeException{ec, _tags, ELogLevel::NoLog}, false );
		};
		base::CloseTasks( f );
		_subscriptionRequests.clear();
		ClearSubscriptions();//server-side subscriptions died with the socket; reconnect re-subscribes.
	}
	α AppClientSocketSession::ClearSubscriptions()ι->void{
		ul _{ _subsMutex };
		_subs.clear();
	}
	α AppClientSocketSession::ListenRemote( sp<QL::IListener> listener, QL::Subscription&& sub )ι->void{
		ul _{ _subsMutex };
		_subs.try_emplace( sub.Id ).first->second.emplace( move(listener) );
	}
	α AppClientSocketSession::StopListenRemote( sp<QL::IListener> listener, vector<QL::SubscriptionId> ids )ι->flat_set<QL::SubscriptionId>{
		flat_set<QL::SubscriptionId> y;
		ul _{ _subsMutex };
		if( ids.empty() ){//all of listener's subscriptions.
			for( auto idListeners = _subs.begin(); idListeners!=_subs.end(); ){
				if( idListeners->second.erase(listener) )
					y.emplace( idListeners->first );
				idListeners = idListeners->second.empty() ? _subs.erase( idListeners ) : next(idListeners);
			}
		}
		else{
			for( let id : ids ){
				auto kv = _subs.find( id );
				if( kv==_subs.end() || !kv->second.erase(listener) )
					continue;
				y.emplace( id );
				if( kv->second.empty() )
					_subs.erase( kv );
			}
		}
		return y;
	}
	//The listeners for an id, copied out from under the lock.  Callbacks must not run while _subsMutex is held: a listener that
	//subscribes or unsubscribes in response to an event re-enters ListenRemote/StopListenRemote/ClearSubscriptions, all of which
	//take the same mutex exclusively - and the mutex is not recursive, so that is a hang (formally, UB).  Copying the
	//shared_ptrs also keeps every listener alive across its own callback, which holding the lock only did by accident.
	//The trade is that a listener can be dropped between the snapshot and the call, so it may see one event after
	//unsubscribing - which is the ordinary cost of not holding a lock across a callback, and cheaper than the alternative.
	α AppClientSocketSession::ListenersFor( QL::SubscriptionId id )Ι->flat_set<sp<QL::IListener>>{
		rl _{ _subsMutex };
		let kv = _subs.find( id );
		return kv==_subs.end() ? flat_set<sp<QL::IListener>>{} : kv->second;//_subs never holds an empty set - StopListenRemote erases the key when the last listener goes - so empty means absent.
	}
	α AppClientSocketSession::OnSubscription( const jobject& m, QL::SubscriptionId clientId )ι->void{
		let listeners = ListenersFor( clientId );
		if( listeners.empty() ){
			WARNT( ELogTags::QL, "[{}]Could not find subscription.", clientId );
			return;
		}
		for( let& listener : listeners ){
			try{
				listener->OnChange( m, clientId );
			}
			catch( runtime_error& )
			{}
		}
	}
	α AppClientSocketSession::OnClose( beast::error_code ec )ι->void{
		let _ = shared_from_this();//SetSession below drops a strong reference, and everything after it runs on this.
		let isLive = _appClient->LoadSession().get()==this;//a secondary session (tests create their own) - clearing the live session for one of those would make IAppClient::Shutdown skip closing it, and reconnecting would be spurious.
		if( isLive )
			_appClient->SetSession( nullptr );
		base::OnClose( ec );
		if( isLive && !Process::ShuttingDown() )
			App::Client::Connect( _appClient );
	}
	α AppClientSocketSession::OnMessage( string&& j, RequestId requestId )ι->void{
		DBG( "[{}]OnMessage: {}", hex(requestId), j.substr(0, Web::MaxLogLength()) );
		try{
			OnSubscription( Json::Parse(j), requestId );
		}
		catch( Exception& e ){
			e.SetLevel( ELogLevel::Error );
		}
	}
	α AppClientSocketSession::SessionInfo( SessionPK sessionId, SL sl )ι->ClientSocketAwait<Web::FromServer::SessionInfo>{
		let requestId = NextRequestId();
		return ClientSocketAwait<Web::FromServer::SessionInfo>{ FromClient::Session(sessionId, requestId), requestId, shared_from_this(), sl };
	}
	α AppClientSocketSession::ClientQuery( Proto::FromServer::ClientQuery proto, Jde::UserPK executer, RequestId requestId )ι->TAwait<jvalue>::Task{
		DBG( "[{}.{}]ClientQuery: executer='{}', size='{}'.", hex(Id()), hex(requestId), executer.Value, proto.query().substr(0, Web::MaxLogLength()) );
		try{
			auto vars = proto.variables().empty() ? jobject{} : parse( proto.variables() ).as_object();
			auto result = co_await *_appClient->ClientQuery( QL::Parse(move(*proto.mutable_query()), move(vars), {}, proto.raw()), executer );
			Write( FromClient::QueryResult(serialize(result), requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}
	α AppClientSocketSession::Query( string&& q, jobject variables, bool returnRaw, SL sl )ι->ClientSocketAwait<jvalue>{
		let requestId = NextRequestId();
		LOGSL( ELogLevel::Debug, sl, ELogTags::SocketClientWrite, "[{}]{}.", hex(requestId), q.substr(0, Web::MaxLogLength()) );

		return ClientSocketAwait<jvalue>{ FromClient::Query(move(q), move(variables), requestId, returnRaw), requestId, shared_from_this(), sl };
	}
	α AppClientSocketSession::Subscribe( string&& q, jobject vars, sp<QL::IListener> listener, SL sl )ε->await<jarray>{
		let requestId = NextRequestId();
		LOGSL( ELogLevel::Debug, sl, ELogTags::SocketClientWriteSub, "[{}]{} {}.", hex(requestId), q.substr(0, Web::MaxLogLength()), serialize(vars) );
		auto subscriptions = QL::ParseSubscriptions( q, vars, _appClient->SubscriptionSchemas, sl );
		_subscriptionRequests.emplace( requestId, SubscriptionRequest{listener, move(subscriptions), q, vars} );
		return ClientSocketAwait<jarray>{ FromClient::Subscription(move(q), move(vars), requestId), requestId, shared_from_this(), sl };
	}

	α AppClientSocketSession::Unsubscribe( vector<QL::SubscriptionId>&& ids, SL sl )ι->void{
		if( ids.empty() )
			return;
		let requestId = NextRequestId();
		LOGSL( ELogLevel::Debug, sl, ELogTags::SocketClientWriteSub, "[{}]Unsubscribe: {}.", hex(requestId), ids.size() );
		Write( FromClient::Unsubscription(ids, requestId) );
	}

	template<class T,class... Args> Ω resume( std::any&& hAny, T&& v/*, fmt::format_string<Args const&...>&& m="", const Args&... args*/ )ι->void{
		auto h = std::any_cast<typename ClientSocketAwait<T>::Handle>( &hAny );
		ASSERT_DESC( h, Ƒ("typeT={}, typeV={}", typeid(typename ClientSocketAwait<T>::Handle).name(), hAny.type().name()) );
		if( h ){
			h->promise().SetValue( move(v) );
			h->resume();
		}
	}

	template<class... Args> Ω resumeJValue( std::any&& hAny, string&& v )ι->void{
		try{
			resume<jvalue>( move(hAny), Json::ParseValue(move(v)) );
		}
		catch( runtime_error& e ){
			if( auto h = std::any_cast<typename ClientSocketAwait<jvalue>::Handle>(&hAny); h ){
				h->promise().SetExp( move(e) );
				h->resume();
			}
			else
				ASSERT_DESC( false, "hAny is null for jvalue." );
		}
	}

	template<class T,class... Args>
	α resumeScaler( std::any&& h, T v )ι->void{
		resume( move(h), move(v) );
	}

	α AppClientSocketSession::Execute( string&& bytes, optional<Jde::UserPK> userPK, RequestId clientRequestId, uint8 depth )ι->void{
		try{
			THROW_IF( depth>_maxExecuteDepth, "Execute nesting depth {} exceeds the maximum of {}.", uint32(depth), uint32(_maxExecuteDepth) );//uint32: fmt renders a uint8 as the character it numbers, which is blank here.
			auto t = Protobuf::Deserialize<Proto::FromServer::Transmission>( move(bytes) );
			ProcessTransmission( move(t), userPK, clientRequestId, depth );
		}
		catch( runtime_error& e ){
			WriteException( move(e), clientRequestId );
		}
	}

	α AppClientSocketSession::OnRead( Proto::FromServer::Transmission&& t )ι->void{
		ProcessTransmission( move(t), nullopt, nullopt, 0 );//depth 0:  the AppServer is speaking on its own socket, so it may name the user it is acting for.
	}

	α AppClientSocketSession::ProcessTransmission( Proto::FromServer::Transmission&& t, optional<Jde::UserPK> userPK, optional<RequestId> clientRequestId, uint8 depth )ι->void{
		for( auto i=0; i<t.messages_size(); ++i ){
			auto m = t.mutable_messages( i );
			using enum Proto::FromServer::Message::ValueCase;
			let requestId = clientRequestId.value_or( m->request_id() );
			//Only for kinds that answer a request of ours.  Popping for every kind erased the handle of an unrelated
			//in-flight await whenever a server-originated push happened to carry a colliding id - see FromServer::IsResponse.
			std::any hAny = requestId && FromServer::IsResponse( m->value_case() ) ? IClientSocketSession::PopTask( requestId ) : nullptr;
			switch( m->value_case() ){
			[[unlikely]] case kAck:
				SetId( m->ack() );
				if( !_qlServer )
					_qlServer = ms<Web::Client::ClientQL>( shared_from_this(), move(_authorize) );
				INFO( "[{}]AppClientSocketSession created: {}://{}.", hex(Id()), IsSsl() ? "https" : "http", Host() );
				break;
			case kClientQuery:{
				auto& q = *m->mutable_client_query();
				let claimed = Jde::UserPK{ q.executer_pk() };
				if( !depth )
					ClientQuery( move(q), claimed, requestId );
				else if( claimed && claimed!=userPK.value_or(Jde::UserPK{0}) )
					WriteException( Exception{SRCE_CUR, {ELogLevel::Critical, ELogTags::Access}, "[{}]A forwarded ClientQuery may not name executer '{}'.", hex(Id()), claimed.Value}, requestId );
				else
					ClientQuery( move(q), userPK.value_or(Jde::UserPK{0}), requestId );
				break;}
			case kConnectionInfo:
				DBG( "[{}]ConnectionInfo: connection: '{}'.", hex(Id()), hex(m->connection_info().connection_pk()) );
				resume( move(hAny), move(*m->mutable_connection_info()) );
				break;
			case kGeneric:
				DBG( "[{}]Generic: '{}'.", hex(Id()), m->generic() );
				resume( move(hAny), move(*m->mutable_generic()) );
				break;
			[[likely]] case kStrings:{
				auto& res = *m->mutable_strings();
				DBG( "[{}]Strings: count='{}'.", hex(Id()), res.messages().size()+res.files().size()+res.functions().size()+res.threads().size() );
				resume( move(hAny), move(*m->mutable_strings()) );
				}break;
			case kJwt:
				DBG( "[{}]Jwt: size='{}'.", hex(Id()), m->jwt().size() );
				resume( move(hAny), Web::Jwt{move(*m->mutable_jwt())} );
				break;
			case kProgress://TODO not awaitable
				DBG( "[{}]Progress: '{}'.", hex(Id()), m->progress() );
				resumeScaler( move(hAny), m->progress() );
				break;
			case kSessionInfo:{
				auto& res = *m->mutable_session_info();
				DBG( "[{}]SessionInfo: expiration: '{}', session_id: '{}', user_pk: '{}', user_endpoint: '{}'.", hex(Id()), ToIsoString(Protobuf::ToTimePoint(res.expiration())), hex(res.session_id()), res.user_pk(), res.user_endpoint() );
				resume( move(hAny), move(res) );
				}break;
			case kQueryResult:
				DBG( "[{}.{}]query: '{}'.", hex(Id()), hex(requestId), m->query_result().substr(0, Web::MaxLogLength()) );
				resumeJValue( move(hAny), move(*m->mutable_query_result()) );
				break;
			case kSubscriptionAck:
				if( !_subscriptionRequests.erase_if(requestId, [&](auto&& kv){
					auto& request = kv.second;
					flat_set<QL::SubscriptionId> ids;
					for( auto& sub : request.Subscriptions ){
						if( sub.Id == 0 )
							sub.Id = requestId;
						ids.emplace( sub.Id );
						ListenRemote( request.Listener, move(sub) );
					}
					//Remembered on the ack, not on the request: this is the point at which the subscription is known to
					//exist, and it is what a reconnect re-issues.  Clear() drops the ids above; this outlives them.
					Subscriptions::Remember( move(request.Query), move(request.Variables), request.Listener, ids );
					return true;
				}) ){ //request not found.
					HandleException( move(hAny), Exception{"SubscriptionAck: '{}' not found.", requestId}, requestId );
				}
				else{ //found the request.
					jarray y;
					for_each( m->subscription_ack().server_ids(), [&](auto id){y.emplace_back(id);} );
					DBGT( _tags | ELogTags::Subscription, "[{}]SubscriptionAck: '{}'.", hex(Id()), serialize(y) );
					resume( move(hAny), move(y) );
				}
				break;
			[[likely]]case kSubscription:
				OnMessage( move(*m->mutable_subscription()), requestId );
			break;
			case kException:{
				_subscriptionRequests.erase( requestId );
				auto e = App::ProtoUtils::ToException( move(*m->mutable_exception()) );
				HandleException( move(hAny), move(*e), requestId );
				break;}
			case kExecute:
			case kExecuteAnonymous:{
				bool isAnonymous = m->value_case()==kExecuteAnonymous;
				auto bytes = isAnonymous ? move( *m->mutable_execute_anonymous() ) : move( *m->mutable_execute()->mutable_transmission() );
				optional<Jde::UserPK> runAsPK = isAnonymous ? nullopt : depth ? userPK : optional<Jde::UserPK>( {m->execute().user_pk()} );
				LogRead( Ƒ("Execute{} size: {:10L}", isAnonymous ? "Anonymous" : "", bytes.size()) );
				Execute( move(bytes), runAsPK, requestId, uint8(depth+1) );
				break;}
			case kExecuteResponse://wait for use case.
			case kStringPks://strings already saved in db, no need to send.  not being requested by client yet.
				CRITICAL( "[{}]No use case has been implemented on client app '{}'.", hex(Id()), underlying(m->value_case()) );
				break;
			case kTraces://a log subscription made from a C++ client.  Nothing consumes one since IListener::OnTraces went (ql-refactor B2) - the browser's logs page is the stream's consumer.
				WARN( "[{}]Dropped {} log trace(s) for request {}: a C++ client has no trace listener.", hex(Id()), m->traces().values_size(), requestId );
				break;
			//[[unlikely]]
			// case kStatus:
			// 	CRITICAL( "[{:x}]Web only call not implemented on client app '{}'.", Id(), (uint)m->value_case() );
			// break;
			case VALUE_NOT_SET:
				break;
			}
		}
	}
	α AppClientSocketSession::HandleException( std::any&& h, Exception&& e, RequestId requestId )ι->void{
		auto handle = [&]( sv /*msg*/, auto await ){
			await->promise().ResponseMessage = "Error: {}";
			await->promise().MessageArgs.emplace_back( e.what() );
			await->promise().SetExp( move(e) );
			await->resume();
		};
		if( auto await = std::any_cast<ClientSocketAwait<Proto::FromServer::ConnectionInfo>::Handle>(&h) )
			handle( "Exception<ConnectionInfo>: '{}'.", await );
		else if( auto await = std::any_cast<ClientSocketAwait<uint32>::Handle>(&h) )
			handle( "Exception<uint32>: '{}'.", await );
		else if( auto await = std::any_cast<ClientSocketAwait<string>::Handle>(&h) )
			handle( "Exception<string>: '{}'.", await );
		else if( auto await = std::any_cast<ClientSocketAwait<Proto::FromServer::Strings>::Handle>(&h) )
			handle( "Exception<Strings>: '{}'.", await );
		else if( auto await = std::any_cast<ClientSocketAwait<Web::FromServer::SessionInfo>::Handle>(&h) )
			handle( "Exception<SessionInfo>: '{}'.", await );
		else if( auto await = std::any_cast<ClientSocketAwait<jvalue>::Handle>(&h) )
			handle( "Exception<jvalue>: '{}'.", await );
		else if( auto await = std::any_cast<ClientSocketAwait<jarray>::Handle>(&h) )
			handle( "Exception<jarray>: '{}'.", await );
		else if( auto await = std::any_cast<ClientSocketAwait<Web::Jwt>::Handle>(&h) )
			handle( "Exception<Jwt>: '{}'.", await );
		else{
			let severity{ requestId ? ELogLevel::Critical : ELogLevel::Debug };
			ASSERT_DESC( !requestId, Ƒ("Type Not Expected={}", h.type().name()) );
			LOG( severity, _tags, "[{}]Failed to process incoming exception '{}'.", hex(requestId), e.what() );
		}
	}
	α AppClientSocketSession::WriteException( runtime_error&& e, RequestId requestId )ι->void{
		Write( FromClient::Exception(move(e), requestId) );
	}
	α AppClientSocketSession::WriteException( string&& e, RequestId requestId )ι->void{
		Write( FromClient::Exception(move(e), requestId) );
	}
}}