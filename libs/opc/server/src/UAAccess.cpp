#include <jde/opc/server/UAAccess.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/app/client/IAppClient.h>
#include <jde/access/IAcl.h>
#include <jde/opc/server/INodeAccess.h>
#include <jde/opc/server/UAConfig.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"

#define let const auto

namespace Jde::Opc::Server{
	ELogTags _tags = ( ELogTags )( (EOpcLogTags)ELogTags::Access | EOpcLogTags::Opc );
}
namespace Jde::Opc::Server::UAAccess{
	constexpr auto _renewInterval{ 30s };//O2: bounds what a genuinely-dead session costs - one AppServer round trip per interval, not one per node access.
	//Expiration is the snapshot ActivateSession took and it was never renewed, so a long-lived OPC session hit a hard wall at
	//whatever the web session's expiry was then - a day for a socket-backed one (/http/socketTimeout) - and every rights, browse
	//and subscribe check below was denied from that moment on, while the web session behind it was still alive and being
	//refreshed by the gateway on every request.  Re-ask the authority when the snapshot lapses; deny only if it agrees.
	//open62541 calls the access-control plugin with its serviceMutex held, so anything that waits here stops every OPC
	//client, not just this session.  The renewal used to be a BlockAwait on that thread:  a *hung* AppServer froze the
	//whole server for the socket deadline (/web/client/socketRequestTimeout, 60s), and the timeout then tore down the
	//app-client socket as collateral (opcserver-review3 L20).  It is now posted and answered on the io thread;  the
	//answers land here, keyed by session id rather than by SessionContext*, so a session closing under an in-flight
	//renewal cannot be written through.  Shared with the renewals in flight, which a server deleted under them leaves it to.
	struct Renewals final{
		std::mutex Mutex;
		flat_map<SessionPK,TimePoint> Answers;
	};
	//The plugin's state, at ac->context:  what the host gave Init.
	struct AccessControlContext final{
		bool AllowAnonymous{};
		bool AllowCertificate{};
		bool AllowIssued{};
		bool AllowUsername{};//the username token type AND a non-empty login list - an empty one would advertise a token nobody can present.
		vector<std::pair<string,string>> Users;//name, password.
		string UserTokenPolicyUri;//"" names each security policy for itself.
		sp<App::Client::IAppClient> AppClient;//the hub, which the logins and renewals ask.
		sp<INodeAccess> NodeAccess;
		const sp<UAAccess::Renewals> Renewals{ ms<UAAccess::Renewals>() };
	};
	//ac's, or the server's for a caller that passes none:  UAHistory's checks, and the tests'.
	Ω context( UA_Server* server, UA_AccessControl* ac )ι->AccessControlContext&{
		return *static_cast<AccessControlContext*>( (ac ? ac : &UA_Server_getConfig(server)->accessControl)->context );
	}

	Ω renew( sp<App::Client::IAppClient> appClient, sp<Renewals> renewals, SessionPK sessionId )ι->TAwait<Web::FromServer::SessionInfo>::Task{
		try{
			//Same call ActivateSession made, against the same authority - this is a re-read of the session, not a new one.
			if( auto await = appClient->SessionInfoAwait(sessionId); await ){
				let info = co_await *await;
				std::scoped_lock _{ renewals->Mutex };
				renewals->Answers.insert_or_assign( sessionId, Protobuf::ToTimePoint(info.expiration()) );
			}
		}
		catch( runtime_error& e ){
			//A failure is an answer:  the authority was reached and had nothing for this session, or is unreachable - either
			//way the grace below must not keep serving the lapsed snapshot.  Epoch reads as "expired long ago".
			std::scoped_lock _{ renewals->Mutex };
			renewals->Answers.insert_or_assign( sessionId, TimePoint{} );
			Exception{ move(e), ExceptionArgs{ELogLevel::Debug, _tags}, SRCE_CUR };
		}
	}

	//The context for a *read* callback (GetUserAccessLevel, GetUserRightsMask) - which, unlike browse or add-reference, open62541
	//also makes with no session at all.  When a session times out it keeps the session's subscriptions for TransferSubscriptions
	//(UA_Session_remove), and their monitored items go on sampling with sub->session == NULL, so every read arrives here with the
	//session id and context both null - by design, and "no access" is the answer it expects ("Session for read operations can be
	//NULL. For example for a MonitoredItem where the underlying Subscription was detached", ua_services_attribute.c, 1.5.9).
	//Asserting on that wrote a CRITICAL twice a second for the rest of the subscription's lifetime - ~83 min at the default
	//lifetime count - after every lost session (soak-findings #9).  A *session* that arrives without the context ActivateSession
	//installs is still a bug, and still asserts.
	Ω readContext( const UA_NodeId* sessionId, void* sessionContext, SRCE )ι->SessionContext*{
		let ctx = static_cast<SessionContext*>( sessionContext );
		if( !ctx && sessionId )
			ASSERTSL( ctx, sl );
		return ctx;
	}

	Ω delivered( Renewals& renewals, SessionPK sessionId )ι->optional<TimePoint>{//whatever the io thread has answered since the last look, whichever way it went.
		optional<TimePoint> y;
		std::scoped_lock _{ renewals.Mutex };
		if( auto p = renewals.Answers.find(sessionId); p!=renewals.Answers.end() ){
			y = p->second;
			renewals.Answers.erase( p );
		}
		return y;
	}
	Ω renewed( SessionContext& ctx, TimePoint expiration )ι->void{
		ctx.Expiration = expiration;
		ctx.RenewAt = HalfLife( expiration );
		ctx.Answered = ctx.Denied = false;//fresh again - the next lapse is a new question, and gets its own grace.
		DBG( "[{}]Renewed session for user '{}' to '{}'", hex(ctx.SessionId), ctx.UserPK.Value, ToIsoString(ctx.Expiration) );
	}

	//Asking only once the snapshot had lapsed could never renew a session on its own.  The snapshot *is* the authority's
	//expiry - ActivateSession's ask slid the session and copied the result - so the two lapse at the same instant, and an
	//ask made after it finds a session Sessions::Extend will not revive.  It only ever worked for a session something else
	//had slid in between, and for a user who reaches the AppServer through the gateway alone nothing does:  the gateway
	//slides its own copy of the session, and asks again only once that has lapsed.  So an OPC session in continuous use
	//was denied at exactly /http/socketTimeout - a day in, reachable only by a run that long (soak-findings #13: 82,815
	//writes, then BadUserAccessDenied).  So ask at half-life, while the ask can still slide it, and again at half of what
	//remains if that did not.  Only an extension is taken:  ahead of the lapse the snapshot is still the authority's own
	//word, and a failure - the AppServer restarting, say - must not cut it short.  What a failure means is left to the lapse.
	Ω renewAhead( AccessControlContext& context, SessionContext& ctx, TimePoint now )ι->void{
		if( let answer = delivered(*context.Renewals, ctx.SessionId); answer ){
			if( *answer>ctx.Expiration )
				renewed( ctx, *answer );
			else
				ctx.RenewAt = std::max( HalfLife(ctx.Expiration), now+_renewInterval );
		}
		else if( now-ctx.LastRenewal>=_renewInterval ){
			ctx.LastRenewal = now;
			renew( context.AppClient, context.Renewals, ctx.SessionId );//posts the request and returns; it is answered on the io thread.
		}
	}

	Ω expired( AccessControlContext& context, SessionContext* ctx )ι->bool{//non-authenticated paths set Expiration=TimePoint::max(); only JWT/SessionInfo sessions carry a real expiry.
		if( !ctx )
			return false;
		if( let now = Clock::now(); ctx->Expiration>=now ){
			if( now>=ctx->RenewAt )
				renewAhead( context, *ctx, now );
			return false;
		}
		if( ctx->SessionId ){
			//A failure that answers a question asked ahead of the lapse says nothing about it - that ask may be hours old -
			//so it is dropped and the lapse asks its own.  An expiry is good whenever it was read.
			if( let answer = delivered(*context.Renewals, ctx->SessionId); answer && (*answer>ctx->Expiration || ctx->LastRenewal>ctx->Expiration) ){
				ctx->Expiration = *answer;
				ctx->Answered = true;
			}
			if( ctx->Expiration>=Clock::now() ){
				renewed( *ctx, ctx->Expiration );
				return false;
			}
			if( !ctx->Answered ){
				if( Clock::now()-ctx->LastRenewal>=_renewInterval ){
					ctx->LastRenewal = Clock::now();
					renew( context.AppClient, context.Renewals, ctx->SessionId );//posts the request and returns; it is answered on the io thread.
				}
				//Serve the lapsed snapshot only while the question is outstanding, bounded by one renewal interval.  A snapshot
				//going stale is the ordinary case - the web session behind it is alive and being refreshed - so denying on
				//every lapse would fail live sessions over what used to be a blocking wait of milliseconds;  an authority
				//that never answers must not become a way in, hence the bound.  Any answer, including a failure, ends it.
				if( Clock::now()-ctx->Expiration<_renewInterval )
					return false;
			}
		}
		//Was a DBG per denied access, so at the shipped log level a user denied for the rest of the session was recorded
		//nowhere - the only account of soak-findings #13 was the client's.  Information, once per lapse.
		if( !ctx->Denied ){
			ctx->Denied = true;
			let why = ctx->Expiration==TimePoint{} ? string{"could not be renewed"} : Ƒ( "expired at '{}'", ToIsoString(ctx->Expiration) );//epoch is renew()'s failure.
			INFO( "[{}]Session for user '{}' {} - denying its access until it activates again.", hex(ctx->SessionId), ctx->UserPK.Value, why );
		}
		return true;
	}
	//The username token's login list is matched by username() the way open62541's default plugin matches its static one.
	Ω setContext( UA_AccessControl& ac, const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess )ι->AccessControlContext&{
		auto context = new AccessControlContext{};
		context->AllowAnonymous = settings.Tokens.Anonymous;
		context->AllowCertificate = settings.Tokens.Certificate;
		context->AllowIssued = settings.Tokens.Issued;
		context->UserTokenPolicyUri = settings.UserTokenPolicyUri;
		context->Users = settings.Users;
		context->AllowUsername = !context->Users.empty();
		context->AppClient = move( appClient );
		context->NodeAccess = move( nodeAccess );
		ac.context = context;
		return *context;
	}

	Ω clearAccessControl( UA_AccessControl* ac )ι->void{
		delete static_cast<AccessControlContext*>( ac->context );
		ac->context = nullptr;
		UA_Array_delete( ac->userTokenPolicies, ac->userTokenPoliciesSize, &UA_TYPES[UA_TYPES_USERTOKENPOLICY] );
		ac->userTokenPolicies = nullptr;
		ac->userTokenPoliciesSize = 0;
	}

	constexpr sv AnonymousPolicy{ "open62541-anonymous-policy" };
	constexpr sv UsernamePolicy{ "open62541-username-policy" };
	constexpr sv CertificatePolicy{ "open62541-certificate-policy" };
	//The four token types, as Init offers them and ActivateSession matches them.  Credential names what a token of the type
	//would carry in the clear under the None policy;  an anonymous one carries nothing.
	struct TokenPolicy final{ bool AccessControlContext::*Allowed; UA_UserTokenType Type; sv PolicyId; sv Label; sv Credential; };
	constexpr std::array<TokenPolicy,4> TokenPolicies{{
		{ &AccessControlContext::AllowAnonymous, UA_USERTOKENTYPE_ANONYMOUS, AnonymousPolicy, "Anonymous", {} },
		{ &AccessControlContext::AllowUsername, UA_USERTOKENTYPE_USERNAME, UsernamePolicy, "Username", "Username/Password" },
		{ &AccessControlContext::AllowCertificate, UA_USERTOKENTYPE_CERTIFICATE, CertificatePolicy, "Certificate", "x509 Certificate" },
		{ &AccessControlContext::AllowIssued, UA_USERTOKENTYPE_ISSUEDTOKEN, "open62541-issuedtoken-policy", "IssuedToken", "IssuedToken" }
	}};
	//The vendor's match:  a token's policyId begins with its policy's.
	Ω hasPolicy( const UA_String& policyId, sv prefix )ι->bool{ return ToSV( policyId ).starts_with( prefix ); }

	//No identity:  UserPK{}.  Siemens OPC Scout v10 sends an empty policyId, which is not compliant;  the vendor's plugin takes it
	//as the anonymous policy, and so does this.
	Ω anonymous( const UA_AnonymousIdentityToken& token, const AccessControlContext& context )ε->up<SessionContext>{
		if( !context.AllowAnonymous || (token.policyId.data && !hasPolicy(token.policyId, AnonymousPolicy)) )
			throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };
		return SessionContext::Unexpiring( UserPK{} );
	}
	//A name and password from the login list, decrypted by the server before it gets here.  Not offered - no list - means the
	//endpoint carries no username policy, so this is a client ignoring the endpoint description.  The list carries no UserPK:
	//the hub's row for the login name does (ResolveUser), or UserPK{}, no rights.
	Ω username( const UA_UserNameIdentityToken& token, const AccessControlContext& context )ε->up<SessionContext>{
		if( !context.AllowUsername || !hasPolicy(token.policyId, UsernamePolicy) || (!token.userName.length && !token.password.length) )
			throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };
		let name = ToSV( token.userName );
		let password = ToSV( token.password );
		THROW_IFX( !std::ranges::any_of(context.Users, [&](let& user){ return user.first==name && user.second==password; }), UAException{UA_STATUSCODE_BADUSERACCESSDENIED} );
		return SessionContext::Unexpiring( ResolveUser(*context.AppClient, name) );
	}
	//A certificate the session PKI trusts, whose key is a hub user's.
	Ω certificate( const UA_X509IdentityToken& token, UA_ServerConfig& config, const AccessControlContext& context )ε->up<SessionContext>{
		if( !hasPolicy(token.policyId, CertificatePolicy) || !config.sessionPKI.verifyCertificate )
			throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };
		UAε( config.sessionPKI.verifyCertificate(&config.sessionPKI, &token.certificateData) );
		auto publicKey = Crypto::ExtractPublicKey( std::span<byte>{(byte*)token.certificateData.data, token.certificateData.length}, SRCE_CUR );
		let exp = publicKey.ExponentInt();
		let user = context.AppClient->QuerySync( Ƒ("user( modulus: \"{}\", exponent: {}, deleted: null ){{id slug name}}", publicKey.ModulusHex(), exp), {} );
		THROW_IF( user.empty(), "Certificate user not found: modulus: {}, exponent: {}", publicKey.ModulusHex(), exp );
		return SessionContext::Unexpiring( UserPK{QL::AsId<UserPK::Type>(user)} );
	}
	//A hub session:  its id, under 9 bytes, or a jwt the hub signed.  The context carries the session's expiry.
	Ω issued( const UA_IssuedIdentityToken& token, const AccessControlContext& context )ε->up<SessionContext>{
		THROW_IFX( !token.tokenData.length, Exception("Empty issued token", {_tags}) );
		if( token.tokenData.length<9 ){
			let sessionId = Str::TryTo<SessionPK,16>( ToSV(token.tokenData) );//attacker-chosen plaintext on the None endpoint.
			THROW_IFX( !sessionId, UAException{UA_STATUSCODE_BADIDENTITYTOKENINVALID} );
			let sessionInfo = BlockAwait<TAwait<Web::FromServer::SessionInfo>, Web::FromServer::SessionInfo>( move(*context.AppClient->SessionInfoAwait(*sessionId)) );
			return mu<SessionContext>( sessionInfo.user_endpoint(), Protobuf::ToTimePoint(sessionInfo.expiration()), (SessionPK)sessionInfo.session_id(), UserPK{sessionInfo.user_pk()} );
		}
		Web::Jwt jwt{ ToSV(token.tokenData) };
		context.AppClient->Verify( jwt );
		return mu<SessionContext>( string{}, jwt.Expires(), Str::TryTo<SessionPK,16>(jwt.SessionId).value_or(0), jwt.UserPK );
	}

	Ω assignFunctions( UA_AccessControl& ac )ι{
		ac.clear = &clearAccessControl;
		ac.activateSession = &ActivateSession;
		ac.closeSession = &CloseSession;
		ac.getUserRightsMask = &GetUserRightsMask;
		ac.getUserAccessLevel = &GetUserAccessLevel;
		ac.getUserExecutable = &GetUserExecutable;
		ac.getUserExecutableOnObject = &GetUserExecutableOnObject;
		ac.allowAddNode = &AllowAddNode;
		ac.allowAddReference = &AllowAddReference;
		ac.allowDeleteNode = &AllowDeleteNode;
		ac.allowDeleteReference = &AllowDeleteReference;
		ac.allowBrowseNode = &AllowBrowseNode;
		ac.allowTransferSubscription = &AllowTransferSubscription;
		ac.allowHistoryUpdateUpdateData = &AllowHistoryUpdateUpdateData;
		ac.allowHistoryUpdateDeleteRawModified = &AllowHistoryUpdateDeleteRawModified;
	}
}
namespace Jde::Opc::Server{
	α UAAccess::Init( UAConfig& config, const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess )ε->void{
		//here, not at the first session:  every callback below dereferences both on the UA thread.
		THROW_IF( !appClient, "A UA server needs its host's appClient:  every session's login asks it." );
		THROW_IF( !nodeAccess, "A UA server needs its host's nodeAccess:  every Browse, Read and Write asks it." );
		auto& ac = config.accessControl;
		assignFunctions( ac );
		auto& context = setContext( ac, settings, move(appClient), move(nodeAccess) );
		let numOfPolicies = context.UserTokenPolicyUri.size() ? 1 : config.securityPoliciesSize;
		vector<string> tokenTypes;
		for( let& policy : TokenPolicies ){
			if( context.*policy.Allowed )
				tokenTypes.push_back( policy.Type==UA_USERTOKENTYPE_USERNAME ? Ƒ("{}({} users)", policy.Label, context.Users.size()) : string{policy.Label} );
		}
		THROW_IF( tokenTypes.empty(), "No allowed policies set." );
		INFOT( (ELogTags)EOpcLogTags::Server, "UserToken Uris:  [{}]", Str::Join(tokenTypes) );
		//An explicit UserTokenPolicyUri is stamped on every token policy below whether or not the server offers that
		//security policy;  a client then has no endpoint to present its token on and every ActivateSession fails.  (The
		//unsecured config was where this bit - None alone, under a default of Basic256Sha256; it is gone, security-matrix #5 -
		//but a policy named here and not carried, an Aes one say, still lands in this warning.)
		if( context.UserTokenPolicyUri.size() ){//otherwise each policy names itself, which always matches.
			string offered; bool found{};
			for( uint i=0; i<config.securityPoliciesSize; ++i ){
				found = found || ToSV( config.securityPolicies[i].policyUri )==context.UserTokenPolicyUri;
				offered += ToString( config.securityPolicies[i].policyUri )+",";
			}
			if( !offered.empty() )
				offered.pop_back();
			if( !found )
				WARNT( (ELogTags)EOpcLogTags::Server, "The user token policy '{}' is not one of this server's security policies [{}] - no client can present a user token.", context.UserTokenPolicyUri, offered );
		}

		ac.userTokenPoliciesSize = tokenTypes.size() * numOfPolicies;
		ac.userTokenPolicies = ( UA_UserTokenPolicy* )UA_Array_new( ac.userTokenPoliciesSize, &UA_TYPES[UA_TYPES_USERTOKENPOLICY] );
		uint n{};
		vector<string> uris;
		for( uint i = 0; i < numOfPolicies; ++i ){
			const UA_String utpUri = context.UserTokenPolicyUri.size() ? ToUV( context.UserTokenPolicyUri ) : config.securityPolicies[i].policyUri;
			uris.push_back( ToString(utpUri) );
			for( let& policy : TokenPolicies ){
				if( !(context.*policy.Allowed) )
					continue;
				auto& offered = ac.userTokenPolicies[n++];
				offered.tokenType = policy.Type;
				offered.policyId = AllocUAString( policy.PolicyId );//heap-owned:  UA_Array_delete in clearAccessControl deep-frees policyId, and a ToUV view would free a string literal.
				if( policy.Credential.size() && UA_String_equal(&utpUri, &UA_SECURITY_POLICY_NONE_URI) )
					DBGT( (ELogTags)EOpcLogTags::Server, "{} Authentication configured, but no encrypting SecurityPolicy. This can leak credentials on the network.", policy.Credential );
				UA_String_copy( &utpUri, &offered.securityPolicyUri );
			}
		}
		INFOT( (ELogTags)EOpcLogTags::Server, "UserToken Uris:  [{}]", Str::Join(uris) );
	}
	α UAAccess::ActivateSession( UA_Server *server, UA_AccessControl *ac, const UA_EndpointDescription *endpointDescription, const UA_ByteString *secureChannelRemoteCertificate, const UA_NodeId *sessionId, const UA_ExtensionObject *userIdentityToken, void **sessionContext )ι->UA_StatusCode{
		//The token's maker builds the context, installed at the single success point below.  open62541 passes &session->context
		//on *every* ActivateSession and re-activation is explicitly allowed (accesscontrol.h: "can be called several times for
		//a Session"), while closeSession only ever sees the last pointer - so assigning straight through leaked the previous
		//context on every reconnect, which the vendor's own client does on a channel renew (opcserver-review3 L24).
		//A local, not an early delete:  a maker that throws must leave the session's existing context untouched.
		//Re-activation must keep the user, though:  1.5.9 refuses an identity change itself, but compares an issued token's
		//user id, which it leaves empty, so a different user's token still arrives here - the success point refuses it.
		//Issued tokens only:  the stack holds a username or certificate to the same name or subject, and a username's UserPK
		//may rightly move between activations - UserPK{} until the hub inserts the user (ResolveUser).
		try{
			let& context = *static_cast<AccessControlContext*>( ac->context );
			up<SessionContext> ctx;
			const UA_DataType* type{ &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN] };
			if( userIdentityToken->encoding==UA_EXTENSIONOBJECT_ENCODED_NOBODY ){//the empty token is an anonymous one.
				UA_AnonymousIdentityToken token; UA_AnonymousIdentityToken_init( &token );
				ctx = anonymous( token, context );
			}
			else{
				THROW_IFX( userIdentityToken->encoding<UA_EXTENSIONOBJECT_DECODED, UAException{UA_STATUSCODE_BADIDENTITYTOKENINVALID} );//not decodable.
				type = userIdentityToken->content.decoded.type;
				let data = userIdentityToken->content.decoded.data;
				if( type==&UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN] )
					ctx = anonymous( *static_cast<const UA_AnonymousIdentityToken*>(data), context );
				else if( type==&UA_TYPES[UA_TYPES_USERNAMEIDENTITYTOKEN] )
					ctx = username( *static_cast<const UA_UserNameIdentityToken*>(data), context );
				else if( type==&UA_TYPES[UA_TYPES_X509IDENTITYTOKEN] )
					ctx = certificate( *static_cast<const UA_X509IdentityToken*>(data), *UA_Server_getConfig(server), context );
				else if( type==&UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] )
					ctx = issued( *static_cast<const UA_IssuedIdentityToken*>(data), context );
				else
					throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };//a token type the server does not offer.
			}
			ASSERT( ctx );
			if( let existing = static_cast<SessionContext*>(*sessionContext); existing && type==&UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] && existing->UserPK!=ctx->UserPK )
				throw UAException{ UA_STATUSCODE_BADIDENTITYCHANGENOTSUPPORTED };
			delete static_cast<SessionContext*>( *sessionContext );//the one this call replaces, if the session is re-activating.
			*sessionContext = ctx.release();
			return UA_STATUSCODE_GOOD;
		}
		catch( const UAException& e ){
			return e.Code();
		}
		catch( const runtime_error& ){
			return UA_STATUSCODE_BADIDENTITYTOKENINVALID;
		}
	}
	//The hub's user for a login name the list matched - the row the hub's own OPC password login creates for it
	//(Access::Server::Authenticate: login_name under the connection's OpcServer-type provider), so what is granted to that
	//user in the hub applies here.  The same authority and the same sync call as the certificate branch; two round trips,
	//since a login name is unique only per provider and this server does not know which connection row names it - every
	//OpcServer-type provider's user of that name is a candidate, and there is normally one.  Not found is the first login on
	//a fresh install:  the hub inserts the user only after this session activates, so that session runs with no identity
	//(UserPK{}) and the next login resolves it - the shipped resources are unenforced, so the first-run walk is unaffected;
	//an enforced one waits for the re-login.
	α UAAccess::ResolveUser( App::Client::IAppClient& appClient, sv loginName )ι->UserPK{
		UserPK y{};
		try{
			let providers = appClient.QuerySync<jarray>( "providers( providerTypeId:$type ){ id }", {{"type", underlying(Access::EProviderType::OpcServer)}} );
			flat_set<Access::ProviderPK> opcProviders;
			for( let& p : providers )
				opcProviders.insert( Json::AsNumber<Access::ProviderPK>(p.as_object(), "id") );
			let users = appClient.QuerySync<jarray>( "users( loginName:$name, deleted:null ){ id providerId }", {{"name", string{loginName}}} );
			for( let& u : users ){
				let& o = u.as_object();
				if( !opcProviders.contains(Json::FindNumber<Access::ProviderPK>(o, "providerId").value_or(0)) )
					continue;
				if( !y )
					y = UserPK{ Json::AsNumber<UserPK::Type>(o, "id") };
				else
					WARNT( (ELogTags)EOpcLogTags::Server, "Login name '{}' names more than one OpcServer user in the hub - using {}.", loginName, y.Value );
			}
			LOG( y ? ELogLevel::Debug : ELogLevel::Information, (ELogTags)EOpcLogTags::Server, "Username login '{}' {}.", loginName, y ? Ƒ("is user {}", y.Value) : string{"is not yet a user of the hub - this session carries no identity until the next login"} );
		}
		catch( const std::exception& e ){
			WARNT( (ELogTags)EOpcLogTags::Server, "Username login '{}' could not be resolved to a hub user: {}", loginName, e.what() );
		}
		return y;
	}
	α UAAccess::UserName( UA_Server& server, UserPK user )ι->string{
		let acl = context( &server, nullptr ).AppClient->Acl();
		return acl ? acl->UserName( user ) : string{};
	}
	α UAAccess::CloseSession( UA_Server* server, UA_AccessControl* ac,const UA_NodeId* sessionId, void* sessionContext )ι->void{
		SessionContext* ctx = static_cast<SessionContext*>( sessionContext );
		if( ctx && ctx->SessionId ){//an answer that arrived for a session nobody will ask about again.
			auto& renewals = *context( server, ac ).Renewals;
			std::scoped_lock _{ renewals.Mutex };
			renewals.Answers.erase( ctx->SessionId );
		}
		delete ctx;
	}
	α UAAccess::GetUserRightsMask( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, void *nodeContext )ι->UA_UInt32{
		ASSERT( nodeId );
		let ctx = readContext( sessionId, sessionContext );//null for a detached subscription's read - denied below, silently.
		auto& context = UAAccess::context( server, ac );
		if( !nodeId || !ctx || expired(context, ctx) )
			return 0;
		//Was Rights( <schema>, "node", … ) - a resource name nothing ever creates, which Authorize::Rights answers with
		//ERights::All, so every session got every UA_WRITEMASK bit the node's own writeMask allowed:  DisplayName,
		//Description and AccessLevel rewritable by a user with no rights at all, and AccessLevel is the Value-write gate
		//(opcserver-review3 #8).  The node's own acl is the answer, exactly as getUserAccessLevel reads it.
		let rights = context.NodeAccess->NodeRights( *nodeId, ctx->UserPK );
		UA_UInt32 mask = 0;
		if( !empty(rights & Access::ERights::Update) )
			mask = UA_WRITEMASK_ARRRAYDIMENSIONS | UA_WRITEMASK_BROWSENAME | UA_WRITEMASK_CONTAINSNOLOOPS | UA_WRITEMASK_DATATYPE | UA_WRITEMASK_DESCRIPTION | UA_WRITEMASK_DISPLAYNAME | UA_WRITEMASK_EVENTNOTIFIER | UA_WRITEMASK_EXECUTABLE | UA_WRITEMASK_HISTORIZING | UA_WRITEMASK_INVERSENAME | UA_WRITEMASK_ISABSTRACT  | UA_WRITEMASK_MINIMUMSAMPLINGINTERVAL | UA_WRITEMASK_NODECLASS | UA_WRITEMASK_NODEID | UA_WRITEMASK_SYMMETRIC | UA_WRITEMASK_USEREXECUTABLE | UA_WRITEMASK_VALUERANK | UA_WRITEMASK_VALUEFORVARIABLETYPE | UA_WRITEMASK_DATATYPEDEFINITION;
		if( !empty(rights & Access::ERights::Administer) )
			mask |= UA_WRITEMASK_ROLEPERMISSIONS | UA_WRITEMASK_ACCESSRESTRICTIONS | UA_WRITEMASK_ACCESSLEVELEX | UA_WRITEMASK_USERWRITEMASK | UA_WRITEMASK_ACCESSLEVEL | UA_WRITEMASK_USERACCESSLEVEL | UA_WRITEMASK_WRITEMASK;
		return mask;
	}
	α UAAccess::GetUserAccessLevel( UA_Server* server, UA_AccessControl* ac, const UA_NodeId* sessionId, void* sessionContext, const UA_NodeId* nodeId, void* /*nodeContext*/ )ι->UA_Byte{
		ASSERT( nodeId );
		if( !nodeId )
			return 0;
		//only read identifier.numeric once the id is known numeric; string/GUID ids are valid here and go straight to UserRights.
		if( nodeId->namespaceIndex==0 && nodeId->identifierType==UA_NODEIDTYPE_NUMERIC && nodeId->identifier.numeric==UA_NS0ID_SERVER_NAMESPACEARRAY )
			return UA_ACCESSLEVELMASK_READ;
		let ctx = readContext( sessionId, sessionContext );//null for a detached subscription's sampling - the source of #9's assert storm; denied below, silently.
		auto& context = UAAccess::context( server, ac );
		if( !ctx || expired(context, ctx) )
			return 0;
		return underlying( context.NodeAccess->UserRights(*nodeId, ctx->UserPK) );
	}

	α UAAccess::GetUserExecutable( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *methodId, void *methodContext )ι->UA_Boolean{
		ASSERT( methodId );
		if( !methodId )
			return false;
		if( methodId->namespaceIndex==0 && methodId->identifierType==UA_NODEIDTYPE_NUMERIC && methodId->identifier.numeric==UA_NS0ID_SERVER_NAMESPACEARRAY )
			return true;
		WARNT( (ELogTags)EOpcLogTags::Server, "GetUserExecutable: MethodId {} not enforced; denying by default.", NodeId{*methodId}.ToString() );
		return false;//deny-by-default, matching the other unenforced Allow* callbacks; method execution has no ACL yet.
	}
	α UAAccess::GetUserExecutableOnObject( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *methodId, void *methodContext, const UA_NodeId *objectId, void *objectContext )ι->UA_Boolean{ ASSERT(false); return false; }
	α UAAccess::AllowAddNode( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_AddNodesItem *item )ι->UA_Boolean{ ASSERT(false); return false; }
	α UAAccess::AllowAddReference( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_AddReferencesItem *item )ι->UA_Boolean{
		ASSERT( item );
		let ctx = static_cast<SessionContext*>( sessionContext ); ASSERT( ctx );
		auto& context = UAAccess::context( server, ac );
		if( !item || !ctx || expired(context, ctx) )
			return false;
		//Was Test( "variables", Subscribe ) - a resource name nothing creates, so Authorize::Test returned silently and
		//every session could restructure the address space (opcserver-review3 #8).  A reference is a write to its source
		//node, so Update on that node is the right;  the nodeset loader is unaffected - open62541 skips this callback
		//entirely for its adminSession (ua_services_nodemanagement.c Operation_addReference).
		return !empty( context.NodeAccess->NodeRights(item->sourceNodeId, ctx->UserPK) & Access::ERights::Update );
	}
	α UAAccess::AllowDeleteNode( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_DeleteNodesItem *item )ι->UA_Boolean{ ASSERT(false); return false; }
	α UAAccess::AllowDeleteReference( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_DeleteReferencesItem *item )ι->UA_Boolean{ ASSERT(false); return false; }
	α UAAccess::AllowBrowseNode( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, void *nodeContext )ι->UA_Boolean{
		ASSERT( nodeId );
		let ctx = static_cast<SessionContext*>( sessionContext ); ASSERT( ctx );
		auto& context = UAAccess::context( server, ac );
		if( !nodeId || !ctx || expired(context, ctx) )
			return false;
		//Was Test( "browse", Read ) - a resource name nothing creates, so browse was ungated for every session
		//(opcserver-review3 #8).  Now a folder listing (MayBrowse).  Since 1.5.9 this also gates every attribute read but
		//Value and RolePermissions, and every hop of a path translation.  Values stay gated by GetUserAccessLevel.
		return context.NodeAccess->MayBrowse( *nodeId, ctx->UserPK );
	}
	α UAAccess::AllowTransferSubscription( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *oldSessionId, void *oldSessionContext, const UA_NodeId *newSessionId, void *newSessionContext )ι->UA_Boolean{ ASSERT(false); return false; }
	//An edit of a node's history needs Update on the node, and a delete of it Delete, granted on a resource that is
	//enforced:  where none governs the node, reads answer All and these answer None (historian spec *Authorization*,
	//#237).  open62541 doesn't call them itself - the history backend does, per value and per range, as its default
	//plugin does.
	α UAAccess::AllowHistoryUpdateUpdateData( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, UA_PerformUpdateType performInsertReplace, const UA_DataValue *value )ι->UA_Boolean{
		ASSERT( nodeId );
		let ctx = static_cast<SessionContext*>( sessionContext ); ASSERT( ctx );
		auto& context = UAAccess::context( server, ac );
		if( !nodeId || !ctx || expired(context, ctx) )
			return false;
		return !empty( context.NodeAccess->EditRights(*nodeId, ctx->UserPK) & Access::ERights::Update );
	}
	α UAAccess::AllowHistoryUpdateDeleteRawModified( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, UA_DateTime startTimestamp, UA_DateTime endTimestamp, bool isDeleteModified )ι->UA_Boolean{
		ASSERT( nodeId );
		let ctx = static_cast<SessionContext*>( sessionContext ); ASSERT( ctx );
		auto& context = UAAccess::context( server, ac );
		if( !nodeId || !ctx || expired(context, ctx) )
			return false;
		return !empty( context.NodeAccess->EditRights(*nodeId, ctx->UserPK) & Access::ERights::Delete );
	}
}