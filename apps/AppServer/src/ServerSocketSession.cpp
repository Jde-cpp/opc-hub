#include <jde/fwk/co/AnyAwait.h>
#include <jde/db/IDataSource.h>//ReloadHosts' CacheAwait instantiates its query here.
#include "ServerSocketSession.h"
#include <jde/fwk/chrono.h>
#include <jde/app/proto/LogProto.h>
#include <jde/app/proto/app.FromServer.h>
#include <jde/app/proto/app.FromClient.h>
#include <jde/app/proto/common.h>
#include <jde/web/server/Web.FromServer.h>
#include <jde/access/Authorize.h>
#include <jde/access/server/accessServer.h>
#include "LocalClient.h" // !important
#include "appStartup.h" // !important
#include "WebServer.h"
#define let const auto

namespace Jde::App::Server{
	constexpr uint8 _maxExecuteDepth{ 4 };//how many times a kExecute payload may nest another Transmission.
	constexpr uint8 _maxFailedAdoptions{ 5 };//consecutive failed session-id adoptions on an unauthenticated socket before it's dropped.
	ServerSocketSession::ServerSocketSession( sp<IRestStream> stream, beast::flat_buffer&& buffer, TRequestType&& request, tcp::endpoint&& userEndpoint, uint32 connectionIndex )ι:
		base{ move(stream), move(buffer), move(request), move(userEndpoint), connectionIndex }
	{}
	α ServerSocketSession::AddInstance( Proto::FromClient::Instance instance, RequestId requestId )ι->TAwait<sp<Web::Server::SessionInfo>>::Task{
		let _ = shared_from_this();
		try{
			auto info = co_await Web::Server::Sessions::UpsertAwait( Ƒ("{:x}", instance.session_id()), _userEndpoint.address().to_string(), true, nullptr );
			SetSessionInfo( info );
			let [appPK,instancePK,connectionPK] = App::AddConnection( instance.application(), instance.instance_name(), instance.host(), instance.pid(), false );
			//The `hosts` re-load after the insert, awaited:  it used to be AddConnection's synchronous LoadEnum - a BlockAwait on
			//this executor thread over a MySQL query co_spawned onto the same pool.  With executor.threads:2, two instances
			//re-registering in the same instant (every AppServer bounce with two apps alive) parked both threads with their
			//queries queued behind them - neither registration completed and the OpcServer died on its startup timeout
			//(emulator-review W1).  Any(): this coroutine's own await type is the session's, and the pairing rule allows no other.
			co_await Any( App::ReloadHosts() );
			INFOT( ELogTags::SocketServerRead, "[{}.{}]Adding application app:{}@{}:{} pid:{}, instancePK:{}, connectionPK:{}, sessionId: {}, endpoint: '{}'", hex(Id()), hex(requestId), instance.application(), instance.host(), instance.web_port(), instance.pid(), hex(instancePK), hex(connectionPK), hex(instance.session_id()), _userEndpoint.address().to_string() );
			Server::RemoveExisting( instance.host(), instance.web_port() );
			optional<bool> authResult;
			auto& resource = *instance.mutable_auth_resource();
			if( instance.auth_resource().size() ){
				try{
				 	Authorizer()->TestSchemaAdmin( resource, UserPK() );//Administer on each of the schema's active root resources;  a schema with no active root is a no-op, not a denial
					INFOT( ELogTags::Access, "[{}.{}]Instance '{}' authorizor with resource '{}'", hex(Id()), hex(requestId), instance.instance_name(), resource );
					Authorizer()->AddAdminAuthorizer( resource, std::dynamic_pointer_cast<Access::IAdminAcl>(shared_from_this()), UserPK() );
					authResult = true;
				}
				catch( runtime_error& e ){
					INFOT( ELogTags::Access, "[{}.{}]Instance '{}' denied authorizor with resource '{}': {} - grant its user Administer on the schema's root resources and reconnect; until then the flat rule applies.", hex(Id()), hex(requestId), instance.instance_name(), resource, e.what() );
					authResult = false;
				}
			}

			//Publish the whole registration in one critical section: a _sessions visitor on another thread sees all of it or
			//none of it.  The pks used to be three plain stores above this lock, so a forward could match on ProgramPK and
			//read ConnectionPK 0 from before the write (appserver-review3 #14).
			{ lg _{_registrationMutex}; _pks = Registration{appPK, instancePK, connectionPK}; _instance = move( instance ); }
			Write( FromServer::ConnectionInfo(appPK, instancePK, connectionPK, requestId, AppClient()->PublicKey(), Web::Server::ToProto(*info), authResult) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
			NoteFailedAdoption();
		}
	}
	α ServerSocketSession::AddSession( Proto::FromClient::AddSession m, RequestId requestId, SL /*sl*/ )ι->TAwait<Jde::UserPK>::Task{
		let _ = shared_from_this();
		try{
			LogRead( Ƒ("AddSession user: '{}', endpoint: '{}', provider: {}, is_socket: {}", m.domain()+"/"+m.login_name(), m.user_endpoint(), m.provider_pk(), m.is_socket()), requestId );
			let userPK = co_await Access::Server::Authenticate( m.login_name(), m.provider_pk(), m.domain() );

			auto info = Web::Server::Sessions::Add( userPK, move(*m.mutable_user_endpoint()), m.is_socket() );
			LogWrite( Ƒ("AddSession id: {:x}", info->SessionId), requestId );
			Write( FromServer::Session(Web::Server::ToProto(*info), requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}
	α ServerSocketSession::Execute( string&& bytes, optional<Jde::UserPK> userPK, RequestId clientRequestId, uint8 depth )ι->void{
		try{
			//kExecute carries a Transmission inside a bytes field, so every level is a fresh ParseFromString starting at depth 0 - protobuf's own recursion guard never sees the nesting.  Without this cap one frame can drive ProcessTransmission thousands of levels deep and overflow the stack.
			THROW_IF( depth>_maxExecuteDepth, "Execute nesting depth {} exceeds the maximum of {}.", depth, _maxExecuteDepth );
			auto t = Protobuf::Deserialize<Proto::FromClient::Transmission>( move(bytes) );
			ProcessTransmission( move(t), userPK, clientRequestId, depth );
		}
		catch( runtime_error& e ){
			WriteException( move(e), clientRequestId );
		}
	}

	α ServerSocketSession::ForwardExecution( Proto::FromClient::ForwardExecution&& m, bool anonymous, RequestId requestId, SL sl )ι->ForwardExecutionAwait::Task{
		sv functionSuffix = anonymous ? "Anonymous" : "";
		LogRead( Ƒ("ForwardExecution{} appPK: {}, appInstancePK: {:x}, size: {:10L}", functionSuffix, m.app_pk(), m.app_instance_pk(), m.execution_transmission().size()), requestId );
		try{
			THROW_IF( !SessionId(), "A forwarded execution requires an authenticated session." );
			//Anonymous means "forward with no creds" - pass UserPK{0} so ExecuteRequest emits kExecuteAnonymous; otherwise the target runs under the caller's identity.  Mirrors the kExecuteAnonymous path.
			string result = co_await ForwardExecutionAwait{ anonymous ? Jde::UserPK{0} : UserPK(), move(m), SharedFromThis(), sl };
			LogWrite( Ƒ("ForwardExecution{} size: {:10L}", functionSuffix, result.size()), requestId );
			Write( FromServer::Execute(move(result), requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}
	α ServerSocketSession::GraphQL( string&& query, jobject vars, bool returnRaw, RequestId requestId, optional<Jde::UserPK> executer )ι->QL::QLAwait<jvalue>::Task{
		let _ = shared_from_this();
		try{
			LogRead( Ƒ("GraphQL{}: {}", returnRaw ? "*" : "", query), requestId );
			auto ql = QL::Parse( move(query), move(vars), Server::Schemas(), returnRaw );
			auto j = co_await QL::QLAwait( move(ql), {executer.value_or(Jde::UserPK{0})}, LocalQL() );//run as the on-behalf-of user threaded through ProcessTransmission (a forwarded request carries the end-user's pk), not the sending session's identity.
			auto y = serialize( move(j) );
			LogWrite( Ƒ("GraphQL: {}", y.substr(0,100)), requestId );
			Write( FromServer::GraphQL(move(y), requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}
	α ServerSocketSession::LocalQL()Ι->sp<QL::IQL>{
		return QLPtr();
	}

	α ServerSocketSession::SaveLogEntry( Log::Proto::LogEntryClient entry, RequestId requestId )ι->void{
		let pks = Pks();
		if( !pks.Program || !pks.Instance ){
			WriteException( Exception{"ApplicationId or InstanceId not set.", ELogLevel::Warning}, requestId );
			return;
		}
		Logging::Entry y{ LogProto::FromLogEntry(move(entry)) };
		Logging::Log( move(y), pks.Program, pks.Instance );
	}
	α ServerSocketSession::SendAck( uint32 id )ι->void{
		Write( FromServer::Ack(id) );
	}

	α ServerSocketSession::SessionInfo( SessionPK sessionId, RequestId requestId )ι->void{
		LogRead( Ƒ("SessionInfo={}", hex(sessionId)), requestId );
		//A registered app instance asks about a session because a user is acting through it - the OpcServer's UAAccess
		//renewal when its expiry snapshot lapses, the gateway's lookup when its local copy has - so the ask slides the
		//session (Sessions::Extend).  That is what keeps an OPC session alive whose browser reaches this server through
		//nothing but the gateway:  sessions used to slide only on requests received here, so such a session expired after
		///http/socketTimeout, the renewal then got NotFound, and the subscription was denied (web-review2 "S2 remainder";
		//owner ruling 2026-09-04 "issued token: renew", shape (a) in post-mvp.md).  The cost is that a session stays alive as
		//long as something is still asking about it - a dead browser's, for the life of its OPC subscription.  An
		//unregistered socket gets a read only:  it has not proven possession of the id (UpdateExpiration's endpoint check,
		//on the SetSessionId path, is that proof).  An expired session is never revived either way.
		auto info = InstancePK() ? Web::Server::Sessions::Extend( sessionId ) : Web::Server::Sessions::Find( sessionId );
		if( info ){
			LogWrite( Ƒ("SessionInfo userPK: {}, endpoint: {}, hasSocket: {}", info->UserPK.Value, info->UserEndpoint, info->HasSocket), requestId );
			Write( FromServer::Session(Web::Server::ToProto(*info), requestId) );
		}else//NotFound, not the default 500 - the status is the only thing that survives the wire to tell a caller "no such session" from "could not ask", and the gateway caches an anonymous user for the first but must not for the second.
			WriteException( Exception{Ƒ("[{}] Session not found.", hex(sessionId)), ExceptionArgs{EHttpStatus::NotFound}}, requestId );
	}
	α ServerSocketSession::SetSessionId( SessionPK sessionId, RequestId requestId )->Web::Server::Sessions::UpsertAwait::Task{
		let _ = shared_from_this();
		try{
			LogRead( Ƒ("SetSessionId={:x}", sessionId), requestId );
			auto info = co_await Web::Server::Sessions::UpsertAwait( Ƒ("{:x}", sessionId), _userEndpoint.address().to_string(), true, nullptr );
			SetSessionInfo( move(info) );
			Write( FromServer::Complete(requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
			NoteFailedAdoption();
		}
	}
	α ServerSocketSession::NoteFailedAdoption()ι->void{
		if( Session() || ++_failedAdoptions<_maxFailedAdoptions )//an authenticated socket, or still under the limit - a legitimate client adopts once, so only unauthenticated guessing accumulates.
			return;
		WARNT( ELogTags::SocketServerRead, "[{}]Closing socket after {} failed session-id adoptions.", hex(Id()), _failedAdoptions );
		Close();
	}
	//The remote admin check as an awaitable any coroutine can co_await. Suspend launches a glue coroutine of
	//QueryClientAwait's task type; the strand stays free to deliver the reply. The BlockTAwait this replaces
	//deadlocked the self-registration case - the reply only arrives on the strand the block was holding
	//(reviews/todo.md §12), recovered only by the QueryClient timeout as a wrongful denial.
	//The answer comes from the OpcServer (OpcServerQL's adminCheck → OpcAuthorize::TestAdminNode) - it alone knows which
	//resource governs a node.  Its exception, "not ready" during its startup included, resumes as a denial:  an admin check never
	//guesses.  Two identities travel here:  _userPK is the user under test, the executer is the session's own (the OpcServer's).
	struct TestAdminAwait final : AnyVoidAwait{
		TestAdminAwait( sp<IWebsocketSession> session, string resource, string criteria, Jde::UserPK userPK, SL sl )ι:
			AnyVoidAwait{sl}, _session{move(session)}, _resource{move(resource)}, _criteria{move(criteria)}, _userPK{userPK}{}
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->QueryClientAwait::Task;
		sp<IWebsocketSession> _session;
		string _resource, _criteria;
		Jde::UserPK _userPK;
	};
	α TestAdminAwait::Execute()ι->QueryClientAwait::Task{
		try{
			string q = "adminCheck( user:$user ){isAdmin resource( resource:$resource, criteria:$criteria )}";//adminCheck, not permissionRight:  the client parses this schemaless, and only a registered system table - one no schema owns - passes QL::Parse.
			auto ql = QL::ParseQuery( move(q), jobject{{"user", _userPK.Value}, {"resource", _resource}, {"criteria", _criteria}}, {}, true, Source() );
			let result = co_await _session->QueryClient( move(ql), _session->UserPK(), Source() );
			if( result.at("adminCheck").at("isAdmin").as_bool() )
				Resume();
			else
				ResumeExp( Exception{Source(), {}, "User {:x} is not admin for resource '{}' with criteria '{}'.", _userPK.Value, _resource, _criteria} );
		}
		catch( Exception& e ){
			ResumeExp( move(e) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α ServerSocketSession::TestAdmin( str resource, str criteria, Jde::UserPK userPK, SL sl )ι->up<AnyVoidAwait>{
		return mu<TestAdminAwait>( shared_from_this(), resource, criteria, userPK, sl );
	}

	α ServerSocketSession::OnRead( Proto::FromClient::Transmission&& t )ι->void{
		ProcessTransmission( move(t), UserPK() ? optional<Jde::UserPK>{UserPK()} : nullopt, nullopt, 0 );
	}

	α ServerSocketSession::GetJwt( Jde::RequestId requestId )ι->TAwait<jobject>::Task{
		let _ = shared_from_this();
		try{
			THROW_IF( !Session(), "Not logged in to system." );
			jobject vars{ {"id", UserPK().Value} };
			let user = co_await QL::QLAwait<jobject>( "user(id:$id){name slug}", move(vars), {UserPK::System}, QLPtr() );
			let info = Web::Server::Sessions::Find( SessionId() );
			THROW_IF( !info, "Session not found." );
			let expiration = Chrono::ToClock<Clock,steady_clock>( info->Expiration );
			Write( FromServer::Jwt(Server::GetJwt(UserPK(), string{user.at("name").as_string()}, string{user.at("slug").as_string()}, _userEndpoint.address().to_string(), SessionId(), expiration, {}), requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}
	//kJwt is a *delegated* validation, not an adoption: JwtLoginAwait::Execute relays a browser's token over the
	//gateway's own registered app socket to ask who it belongs to.  So the answer is echoed back and nothing is installed
	//here - binding it would silently re-identify a registered server-side socket as whichever user logged in last.
	α ServerSocketSession::Login( string&& jwt, RequestId requestId )ι->TAwait<sp<Web::Server::SessionInfo>>::Task{
		let _ = shared_from_this();
		try{
			let session = co_await Sessions::UpsertAwait( "Bearer " + move(jwt), _userEndpoint.address().to_string(), true, Server::AppClient() );
			Write( FromServer::Session(Web::Server::ToProto(*session), requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}

	α ServerSocketSession::ProcessTransmission( Proto::FromClient::Transmission&& transmission, optional<Jde::UserPK> userPK, optional<RequestId> clientRequestId, uint8 depth )ι->void{
		uint cLog{};
		if( transmission.messages_size()==0 )
			LogRead( "No messages in transmission.", 0, ELogLevel::Error );

		for( auto i=0; i<transmission.messages_size(); ++i ){
			auto& m = *transmission.mutable_messages( i );
			using enum Proto::FromClient::Message::ValueCase;
			let requestId = clientRequestId.value_or( m.request_id() );
			switch( m.value_case() ){
			[[unlikely]]case kInstance:
				AddInstance( move(*m.mutable_instance()), requestId );
				break;
			[[likely]] case kAddSession:{
				AddSession( move(*m.mutable_add_session()), requestId, SRCE_CUR );
				break;}
			case kException:
				if( !requestId ){
					DBGT( ELogTags::SocketServerRead | ELogTags::Exception, "[{:x}.{:x}]Exception - {}", Id(), 0, m.exception().what() );
				}else{
					auto e = ProtoUtils::ToException( move(*m.mutable_exception()) );//keeps status/category - Exception{what} dropped them.
					auto what = e->What();
					//Both routers key on an id this session was given, and both draw from its NextRequestId counter, so an id can
					//only be pending in one of them (finding #12).  QueryClient first: its map is this session's own.
					if( !ResumeQueryException(requestId, move(*e)) && !ForwardExecutionAwait::ResumeExp(move(*e), requestId, ConnectionPK()) )
						LogRead( Ƒ("Exception not handled - {}", what), requestId, ELogLevel::Critical );
				}
				break;
			case kExecute:
			case kExecuteAnonymous:{
				bool isAnonymous = m.value_case()==kExecuteAnonymous;
				auto bytes = isAnonymous ? move( *m.mutable_execute_anonymous() ) : move( *m.mutable_execute()->mutable_transmission() );
				optional<Jde::UserPK> executor = m.value_case()==kExecuteAnonymous ? nullopt : optional<Jde::UserPK>( {m.execute().user_pk()} );
				LogRead( Ƒ("Execute{} size: {:10L}", isAnonymous ? "Anonymous" : "", bytes.size()), requestId );
				Execute( move(bytes), executor, requestId, uint8(depth+1) );
				break;}
			case kExecuteResponse:
				if( !ForwardExecutionAwait::Resume(move(*m.mutable_execute_response()), requestId, ConnectionPK()) )
					LogRead( Ƒ("ExecuteResponse requestId:{} not found.", requestId), requestId, ELogLevel::Critical );
				break;
			case kForwardExecution:
			case kForwardExecutionAnonymous:{
				let anonymous = m.value_case()==kForwardExecutionAnonymous;
				auto forward = anonymous ? m.mutable_forward_execution_anonymous() : m.mutable_forward_execution();
				ForwardExecution( move(*forward), anonymous, requestId );
				break;}
			case kJwt:
				Login( move(*m.mutable_jwt()), requestId );
				break;
			[[likely]]case kQuery:{
				auto& query = *m.mutable_query();
				auto& variableString = *query.mutable_variables();
				try{
					jobject variables = variableString.empty() ? jobject{} : Json::Parse( variableString );
					GraphQL( move(*query.mutable_text()), move(variables), query.return_raw(), requestId, userPK );
				}
				catch( runtime_error& e ){
					WriteException( move(e), requestId );
				}
				break;}
			case kQueryResult:
				QueryClientResults( move(*m.mutable_query_result()), requestId );
				break;
			[[likely]]case kLogEntry:
				++cLog;
				SaveLogEntry( move(*m.mutable_log_entry()), requestId );
				break;
			case kRequestType:
				if( m.request_type()==Proto::FromClient::ERequestType::Jwt )
					GetJwt( requestId );
				else
					WriteException( Exception{"RequestType: '{}' not implemented.", (uint32)m.request_type()}, requestId );
				break;
			case kSessionId:
				if( !m.session_id() )
					WriteException( Exception{"SessionId not set."}, requestId );
				else
					SetSessionId( m.session_id(), requestId );
			break;
			case kSessionInfo:
				SessionInfo( m.session_info(), requestId );
				break;
/*			[[likely]]case kStatus:{
				auto& status = *m.mutable_status();
				//:10L
				LogRead( "Status", requestId );
				let pks = Pks(); Server::BroadcastStatus( pks.Program, pks.Instance, Instance().host(), move(status) );
				break;}*/
			case kSubscription:{
				auto& s = *m.mutable_subscription();
				auto& ql = *s.mutable_text();
				auto& variablesString = *s.mutable_variables();
				try{
					auto vars = variablesString.empty() ? jobject{} : Json::Parse( move(variablesString) );
					LogRead( Ƒ("Subscription - {}", ql.substr(0, Web::MaxLogLength())), requestId, ELogLevel::Trace, ELogTags::SocketClientReadSub );
					THROW_IF( !userPK, "A subscription requires an authenticated session." );
					Write( FromServer::SubscriptionAck(AddSubscription(move(ql), move(vars), requestId, *userPK), requestId) );
				}
				catch( runtime_error& e ){
					WriteException( move(e), requestId );
				}
				break;}
			case kUnsubscription:{
				auto& v = *m.mutable_unsubscription();
				LogRead( Ƒ("Unsubscription - {}", v.request_ids().size()), requestId );
				vector<QL::SubscriptionId> subIds;
				for_each( v.request_ids(), [&](auto id){subIds.emplace_back(id);} );
				RemoveSubscription( move(subIds), requestId );
				break;}
			default:
				LogRead( Ƒ("Unknown message type '{}'", underlying(m.value_case())), requestId, ELogLevel::Critical );
			}
		}
		if( cLog )
			TRACET( ELogTags::SocketServerRead, "[{}]Log entries received: {}.", hex(Id()), cLog );
	}

	α ServerSocketSession::SendQueryClient( QL::TableQL&& query, Jde::UserPK executer, RequestId requestId )ι->void{
		auto q = query.ToString();
		LogWrite( Ƒ("QueryClient: {}", q.substr(0,100)), requestId );
		Write( FromServer::QueryClient(move(q), move(query.Variables), executer, query.ReturnRaw, requestId) );
	}
	α ServerSocketSession::OnClose()ι->void{
		if( !IsOpen() )
			return;
		LogRead( "OnClose", 0 );
		Server::OnSessionDisconnect( SharedFromThis() );
		base::OnClose();
	}

	α ServerSocketSession::WriteComplete( RequestId requestId )ι->void{
		LogWrite( "Complete", requestId );
		Write( FromServer::Complete(requestId) );
	}
	α ServerSocketSession::WriteException( runtime_error&& e, RequestId requestId, SL sl )ι->void{
		LogWriteException( e, requestId, ELogLevel::Debug, sl );
		Write( FromServer::Exception(move(e), requestId) );
	}
	α ServerSocketSession::WriteException( std::string&& e, Jde::RequestId requestId, SL sl )ι->void{
		LogWriteException( e, requestId, ELogLevel::Debug, sl );
		Write( FromServer::Exception(move(e), requestId) );
	}
	α ServerSocketSession::WriteSubscriptionAck( flat_set<QL::SubscriptionId>&& subscriptionIds, RequestId requestId )ι->void{
		Write( FromServer::SubscriptionAck(move(subscriptionIds), requestId) );
	}
	α ServerSocketSession::WriteSubscription( const jvalue& j, RequestId requestId )ι->void{
		auto serialized = serialize( j );
		LogWrite( Ƒ("Subscription: {}", serialized.substr(0,100)), requestId );
		Write( FromServer::Subscription(move(serialized), requestId) );
	}
	α ServerSocketSession::WriteSubscription( App::ProgramPK appPK, App::ProgInstPK instancePK, const Logging::Entry& e, const QL::Subscription& sub )ι->void{
		LogWrite( Ƒ("Subscription: {}", e.Text), 0 );
		Write( FromServer::LogSubscription(appPK, instancePK, e, sub) );
	}
}