#include "UAAccess.h"
#include <open62541/plugin/accesscontrol_default.h>
#include <jde/app/client/IAppClient.h>
#include <jde/access/IAcl.h>
#include "../UAConfig.h"
#include "OpcAuthorize.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

#define let const auto

typedef struct {
	UA_Boolean allowAnonymous;
	size_t usernamePasswordLoginSize;
	UA_UsernamePasswordLogin *usernamePasswordLogin;
	UA_UsernamePasswordLoginCallback loginCallback;
	void *loginContext;
	UA_CertificateGroup verifyX509;

	bool AllowCertificate;
	bool AllowIssued;
	bool AllowUsername;///opc/tokenTypes/username (default true) AND a non-empty login list - an empty one would advertise a token nobody can present.
	UA_String UserTokenPolicyUri;
} AccessControlContext;


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
	//renewal cannot be written through.
	static std::mutex _renewalsMutex;
	static flat_map<SessionPK,TimePoint> _renewals;

	Ω renew( SessionPK sessionId, UserPK user )ι->TAwait<Web::FromServer::SessionInfo>::Task{
		try{
			//Same call ActivateSession made, against the same authority - this is a re-read of the session, not a new one.
			if( auto await = AppClient()->SessionInfoAwait(sessionId); await ){
				let info = co_await *await;
				std::scoped_lock _{ _renewalsMutex };
				_renewals.insert_or_assign( sessionId, Protobuf::ToTimePoint(info.expiration()) );
			}
		}
		catch( runtime_error& e ){
			//A failure is an answer:  the authority was reached and had nothing for this session, or is unreachable - either
			//way the grace below must not keep serving the lapsed snapshot.  Epoch reads as "expired long ago".
			std::scoped_lock _{ _renewalsMutex };
			_renewals.insert_or_assign( sessionId, TimePoint{} );
			Exception{ move(e), ExceptionArgs{ELogLevel::Debug, _tags}, SRCE_CUR };
		}
	}

	//The context for a *read* callback (GetUserAccessLevel, GetUserRightsMask) - which, unlike browse or add-reference, open62541
	//also makes with no session at all.  When a session times out it keeps the session's subscriptions for TransferSubscriptions
	//(UA_Session_remove), and their monitored items go on sampling with sub->session == NULL, so every read arrives here with the
	//session id and context both null - by design, and "no access" is the answer it expects ("Session for read operations can be
	//NULL. For example for a MonitoredItem where the underlying Subscription was detached", ua_services_attribute.c, 1.5.6).
	//Asserting on that wrote a CRITICAL twice a second for the rest of the subscription's lifetime - ~83 min at the default
	//lifetime count - after every lost session (soak-findings #9).  A *session* that arrives without the context ActivateSession
	//installs is still a bug, and still asserts.
	Ω readContext( const UA_NodeId* sessionId, void* sessionContext, SRCE )ι->SessionContext*{
		let ctx = static_cast<SessionContext*>( sessionContext );
		if( !ctx && sessionId )
			ASSERTSL( ctx, sl );
		return ctx;
	}

	Ω expired( SessionContext* ctx )ι->bool{//non-authenticated paths set Expiration=TimePoint::max(); only JWT/SessionInfo sessions carry a real expiry.
		if( !ctx || ctx->Expiration>=Clock::now() )
			return false;
		if( ctx->SessionId ){
			{//whatever the io thread has delivered since the last check, whichever way it went.
				std::scoped_lock _{ _renewalsMutex };
				if( auto p = _renewals.find(ctx->SessionId); p!=_renewals.end() ){
					ctx->Expiration = p->second;
					ctx->Answered = true;
					_renewals.erase( p );
				}
			}
			if( ctx->Expiration>=Clock::now() ){
				ctx->Answered = false;//fresh again - the next lapse is a new question, and gets its own grace.
				DBG( "[{}]Renewed session for user '{}' to '{}'", hex(ctx->SessionId), ctx->UserPK.Value, ToIsoString(ctx->Expiration) );
				return false;
			}
			if( !ctx->Answered ){
				if( Clock::now()-ctx->LastRenewal>=_renewInterval ){
					ctx->LastRenewal = Clock::now();
					renew( ctx->SessionId, ctx->UserPK );//posts the request and returns; it is answered on the io thread.
				}
				//Serve the lapsed snapshot only while the question is outstanding, bounded by one renewal interval.  A snapshot
				//going stale is the ordinary case - the web session behind it is alive and being refreshed - so denying on
				//every lapse would fail live sessions over what used to be a blocking wait of milliseconds;  an authority
				//that never answers must not become a way in, hence the bound.  Any answer, including a failure, ends it.
				if( Clock::now()-ctx->Expiration<_renewInterval )
					return false;
			}
		}
		DBG( "Session for user '{}' expired at '{}'", ctx->UserPK.Value, ToIsoString(ctx->Expiration) );
		return true;
	}
	//Startup installs it (opcServerStartup: SetAcl, then explicitly on the cached schema), so the cast is the schema's own type.
	Ω authorizer()ι->OpcAuthorize&{ return static_cast<OpcAuthorize&>( *GetSchema().Authorizer ); }
	//The username token's login list, matched by ActivateSession's username branch the way open62541's default plugin matches
	//its static one:  /opc/users ([{name,password}]).  An opt-in - nothing shipped configures it (reviews/install-issues.md #1:
	//a fresh install logs in with Google, by ruling no username is seeded); the test configs do, for the gateway's password
	//login (PasswordTests).  Plain text, on purpose:  a hashed per-user store is the hub's job.  Nothing here throws -
	//setContext is noexcept - a malformed entry is logged and skipped.
	Ω loadUsers( AccessControlContext& cntxt )ι->void{
		vector<std::pair<string,string>> users;
		auto add = [&]( const jarray& a, sv source ){
			for( let& v : a ){
				let name = v.is_object() ? Json::FindDefaultSV( v.get_object(), "name" ) : sv{};
				if( !name.empty() )
					users.emplace_back( string{name}, string{Json::FindDefaultSV(v.get_object(), "password")} );
				else
					WARNT( (ELogTags)EOpcLogTags::Server, "{}: a user without a name is skipped.", source );
			}
		};
		if( let a = Settings::FindArray("/opc/users"); a )
			add( *a, "/opc/users" );
		if( users.empty() )
			return;
		cntxt.usernamePasswordLogin = ( UA_UsernamePasswordLogin* )UA_calloc( users.size(), sizeof(UA_UsernamePasswordLogin) );
		for( let& [name, password] : users ){
			auto& login = cntxt.usernamePasswordLogin[cntxt.usernamePasswordLoginSize++];
			login.username = UA_String_fromChars( name.c_str() );
			login.password = UA_String_fromChars( password.c_str() );
		}
	}
	Ω setContext( UA_AccessControl& ac )ι->AccessControlContext&{
    auto cntxt = ( AccessControlContext* )UA_malloc( sizeof(AccessControlContext) );
    memset( cntxt, 0, sizeof(AccessControlContext) );
    cntxt->allowAnonymous = Settings::FindBool( "/opc/tokenTypes/anonymous" ).value_or( false );
		cntxt->AllowCertificate = Settings::FindBool( "/opc/tokenTypes/certificate" ).value_or( true );
		cntxt->AllowIssued = Settings::FindBool( "/opc/tokenTypes/issued" ).value_or( true );
		if( Settings::FindBool("/opc/tokenTypes/username").value_or(true) )
			loadUsers( *cntxt );
		cntxt->AllowUsername = cntxt->usernamePasswordLoginSize>0;
		cntxt->UserTokenPolicyUri = AllocUAString( Settings::FindString("/opc/userTokenPolicyUri").value_or("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256") );
		ac.context = cntxt;
		return *cntxt;
	}

	Ω clearAccessControl( UA_AccessControl* ac )ι->void{
		if( auto context = (AccessControlContext*)ac->context ){
			UA_String_clear( &context->UserTokenPolicyUri );
			for( size_t i=0; i<context->usernamePasswordLoginSize; ++i ){
				UA_String_clear( &context->usernamePasswordLogin[i].username );
				UA_String_clear( &context->usernamePasswordLogin[i].password );
			}
			UA_free( context->usernamePasswordLogin );
			UA_free( context );
			ac->context = nullptr;
		}
		UA_Array_delete( ac->userTokenPolicies, ac->userTokenPoliciesSize, &UA_TYPES[UA_TYPES_USERTOKENPOLICY] );
		ac->userTokenPolicies = nullptr;
		ac->userTokenPoliciesSize = 0;
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
	α UAAccess::Init( UAConfig& config )ε->void{
		auto& ac = config.accessControl;
		assignFunctions( ac );
		auto& context = setContext( ac );
    let numOfPolicies = context.UserTokenPolicyUri.length ? 1 : config.securityPoliciesSize;
    uint policies{}; string log{};
    if( context.allowAnonymous ){
			log += "Anonymous,";
      ++policies;
		}
    if( context.AllowUsername ){
			log += Ƒ( "Username({} users),", context.usernamePasswordLoginSize );
      ++policies;
		}
    if( context.AllowCertificate ){
			log += "Certificate,";
      ++policies;
		}
    if( context.AllowIssued ){
			log += "IssuedToken,";
      ++policies;
		}
		THROW_IF( !policies, "No allowed policies set." );
		if( !log.empty() )
			log.pop_back();//drop trailing comma
		INFOT( (ELogTags)EOpcLogTags::Server, "UserToken Uris:  [{}]", move(log) );
		log.clear();
		//An explicit /opc/userTokenPolicyUri is stamped on every token policy below whether or not the server offers that
		//security policy;  a client then has no endpoint to present its token on and every ActivateSession fails.  (The
		//unsecured config was where this bit - None alone, under a default of Basic256Sha256; it is gone, security-matrix #5 -
		//but a policy named here and not carried, an Aes one say, still lands in this warning.)
		if( context.UserTokenPolicyUri.length ){//otherwise each policy names itself, which always matches.
			string offered; bool found{};
			for( uint i=0; i<config.securityPoliciesSize; ++i ){
				found = found || UA_String_equal( &context.UserTokenPolicyUri, &config.securityPolicies[i].policyUri );
				offered += ToString( config.securityPolicies[i].policyUri )+",";
			}
			if( !offered.empty() )
				offered.pop_back();
			if( !found )
				WARNT( (ELogTags)EOpcLogTags::Server, "/opc/userTokenPolicyUri '{}' is not one of this server's security policies [{}] - no client can present a user token.", ToString(context.UserTokenPolicyUri), offered );
		}

		ac.userTokenPoliciesSize = policies * numOfPolicies;
    ac.userTokenPolicies = ( UA_UserTokenPolicy* )UA_Array_new( ac.userTokenPoliciesSize, &UA_TYPES[UA_TYPES_USERTOKENPOLICY] );
    policies = 0;
    for( uint i = 0; i < numOfPolicies; ++i ){
			const UA_String utpUri = context.UserTokenPolicyUri.length ? context.UserTokenPolicyUri : config.securityPolicies[i].policyUri;
			log += ToString( utpUri ) + ",";
      if( context.allowAnonymous ){
      	ac.userTokenPolicies[policies].tokenType = UA_USERTOKENTYPE_ANONYMOUS;
        ac.userTokenPolicies[policies].policyId = UA_STRING_ALLOC( "open62541-anonymous-policy" );// must be heap-owned: UA_Array_delete in clearAccessControl deep-frees policyId; a ToUV view would free a string literal.
        UA_String_copy( &utpUri, &ac.userTokenPolicies[policies].securityPolicyUri );
        ++policies;
      }
      if( context.AllowUsername ){
      	ac.userTokenPolicies[policies].tokenType = UA_USERTOKENTYPE_USERNAME;
        ac.userTokenPolicies[policies].policyId = UA_STRING_ALLOC( "open62541-username-policy" );// heap-owned, as the others: clearAccessControl deep-frees policyId.
        if( UA_String_equal(&utpUri, &UA_SECURITY_POLICY_NONE_URI) )
					DBGT( (ELogTags)EOpcLogTags::Server, "Username/Password Authentication configured, but no encrypting SecurityPolicy. This can leak credentials on the network." );
        UA_String_copy( &utpUri, &ac.userTokenPolicies[policies].securityPolicyUri );
        ++policies;
      }
      if( context.AllowCertificate ){
        ac.userTokenPolicies[policies].tokenType = UA_USERTOKENTYPE_CERTIFICATE;
        ac.userTokenPolicies[policies].policyId = UA_STRING_ALLOC( "open62541-certificate-policy" );// must be heap-owned: UA_Array_delete in clearAccessControl deep-frees policyId; a ToUV view would free a string literal.
        if( UA_String_equal(&utpUri, &UA_SECURITY_POLICY_NONE_URI) )
					DBGT( (ELogTags)EOpcLogTags::Server, "x509 Certificate Authentication configured, but no encrypting SecurityPolicy. This can leak credentials on the network." );
        UA_String_copy( &utpUri, &ac.userTokenPolicies[policies].securityPolicyUri );
				++policies;
			}
      if( context.AllowIssued ){
        ac.userTokenPolicies[policies].tokenType = UA_USERTOKENTYPE_ISSUEDTOKEN;
        ac.userTokenPolicies[policies].policyId = UA_STRING_ALLOC( "open62541-issuedtoken-policy" );
        if( UA_String_equal(&utpUri, &UA_SECURITY_POLICY_NONE_URI) )
					DBGT( (ELogTags)EOpcLogTags::Server, "IssuedToken Authentication configured, but no encrypting SecurityPolicy. This can leak credentials on the network." );
				UA_String_copy( &utpUri, &ac.userTokenPolicies[policies].securityPolicyUri );
				++policies;
      }
    }
		if( !log.empty() )
			log.pop_back();//drop trailing comma; loop may not have run when numOfPolicies==0, leaving log empty (pop_back on empty is UB).
		INFOT( (ELogTags)EOpcLogTags::Server, "UserToken Uris:  [{}]", move(log) );
	}
	α UAAccess::ActivateSession( UA_Server *server, UA_AccessControl *ac, const UA_EndpointDescription *endpointDescription, const UA_ByteString *secureChannelRemoteCertificate, const UA_NodeId *sessionId, const UA_ExtensionObject *userIdentityToken, void **sessionContext )ι->UA_StatusCode{
		const UA_String anonymous_policy = "open62541-anonymous-policy"_uv;
		const UA_String certificate_policy = "open62541-certificate-policy"_uv;
		const UA_String username_policy = "open62541-username-policy"_uv;

    AccessControlContext *context = ( AccessControlContext* )ac->context;
    UA_ServerConfig *config = UA_Server_getConfig( server );

    /* The empty token is interpreted as anonymous */
    UA_AnonymousIdentityToken anonToken;
    UA_ExtensionObject tmpIdentity;
    if( userIdentityToken->encoding == UA_EXTENSIONOBJECT_ENCODED_NOBODY ) {
        UA_AnonymousIdentityToken_init( &anonToken );
        UA_ExtensionObject_init( &tmpIdentity );
        UA_ExtensionObject_setValueNoDelete( &tmpIdentity,
                                            &anonToken,
                                            &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN]);
        userIdentityToken = &tmpIdentity;
    }

		//Built here and installed at the single success point below.  open62541 passes &session->context on *every*
		//ActivateSession and re-activation is explicitly allowed (accesscontrol.h: "can be called several times for a
		//Session"), while closeSession only ever sees the last pointer - so assigning straight through leaked the previous
		//context on every reconnect, which the vendor's own client does on a channel renew (opcserver-review3 L24).
		//A local, not an early delete:  a branch that throws must leave the session's existing context untouched.
		up<SessionContext> ctx;
		try{
			/* Could the token be decoded? */
			if( userIdentityToken->encoding < UA_EXTENSIONOBJECT_DECODED )
				throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };

			const UA_DataType *tokenType = userIdentityToken->content.decoded.type;
			if( tokenType == &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN] ) {
					/* Anonymous login */
					if( !context->allowAnonymous )
							throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };

					const UA_AnonymousIdentityToken *token = ( UA_AnonymousIdentityToken* )
							userIdentityToken->content.decoded.data;

					/* Match the beginnig of the PolicyId.
					* Compatibility notice: Siemens OPC Scout v10 provides an empty
					* policyId. This is not compliant. For compatibility, assume that empty
					* policyId == ANONYMOUS_POLICY */
					if( token->policyId.data &&
						( token->policyId.length < anonymous_policy.length ||
							strncmp( (const char*)token->policyId.data,
											( const char* )anonymous_policy.data,
											anonymous_policy.length) != 0)) {
							throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };
					}
					ctx = mu<SessionContext>( string{}, TimePoint::max(), SessionPK{}, UserPK{} );//UserPK{}==0: unauthenticated. Every later callback dereferences sessionContext, so it must be non-null on the GOOD path.
			} else if( tokenType == &UA_TYPES[UA_TYPES_USERNAMEIDENTITYTOKEN] ) {
				/* Username and password */
				if( !context->AllowUsername )//not offered (no login list) - the endpoint carries no username policy, so this is a client ignoring the endpoint description.
					throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };
				const UA_UserNameIdentityToken *userToken = ( UA_UserNameIdentityToken* )
						userIdentityToken->content.decoded.data;

				/* Match the beginnig of the PolicyId */
				if( userToken->policyId.length < username_policy.length ||
						strncmp( (const char*)userToken->policyId.data,
										( const char* )username_policy.data,
										username_policy.length) != 0) {
						throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };
				}

				/* The userToken has been decrypted by the server before forwarding
					* it to the plugin. This information can be used here. */
				/* if( userToken->encryptionAlgorithm.length > 0 ) {} */

				/* Empty username and password */
				if( userToken->userName.length == 0 && userToken->password.length == 0 )
						throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };

				/* Try to match username/pw */
				UA_Boolean match = false;
				if( context->loginCallback ) {
						if( context->loginCallback(&userToken->userName, &userToken->password,
								context->usernamePasswordLoginSize, context->usernamePasswordLogin,
								sessionContext, context->loginContext) == UA_STATUSCODE_GOOD)
								match = true;
				} else {
						for( size_t i = 0; i < context->usernamePasswordLoginSize; i++ ) {
								if( UA_String_equal(&userToken->userName, &context->usernamePasswordLogin[i].username) &&
										UA_ByteString_equal( &userToken->password, &context->usernamePasswordLogin[i].password )) {
										match = true;
										break;
								}
						}
				}
				if( !match )
						throw UAException{ UA_STATUSCODE_BADUSERACCESSDENIED };
				ctx = mu<SessionContext>( string{}, TimePoint::max(), SessionPK{}, ResolveUser(ToSV(userToken->userName)) );//the list carries no UserPK - the hub's row for the login name does (or UserPK{}: no rights).  Must be non-null so later callbacks don't deref null.
			} else if( tokenType == &UA_TYPES[UA_TYPES_X509IDENTITYTOKEN] ) {
				const UA_X509IdentityToken *userToken = ( UA_X509IdentityToken* )userIdentityToken->content.decoded.data;
				if( userToken->policyId.length < certificate_policy.length ||
					strncmp( (const char*)userToken->policyId.data,
									( const char* )certificate_policy.data,
									certificate_policy.length) != 0) {
						throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };
				}
				THROW_IFX( !config->sessionPKI.verifyCertificate, UAException{UA_STATUSCODE_BADIDENTITYTOKENINVALID} );
				UAε( config->sessionPKI.verifyCertificate(&config->sessionPKI, &userToken->certificateData) );
				auto publicKey = Crypto::ExtractPublicKey( std::span<byte>{(byte*)userToken->certificateData.data, userToken->certificateData.length}, SRCE_CUR );
				let exp = publicKey.ExponentInt();
				let user = AppClient()->QuerySync( Ƒ("user( modulus: \"{}\", exponent: {} ){{id slug name}}", publicKey.ModulusHex(), exp), {} );
				THROW_IF( user.empty(), "Certificate user not found: modulus: {}, exponent: {}", publicKey.ModulusHex(), exp );
				ctx = mu<SessionContext>( string{}, TimePoint::max(), SessionPK{}, UserPK{QL::AsId<UserPK::Type>(user)} );
			}
			else if( tokenType == &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] ) {
				const UA_IssuedIdentityToken* userToken = ( UA_IssuedIdentityToken* )userIdentityToken->content.decoded.data;
				THROW_IFX( !userToken->tokenData.length, Exception("Empty issued token", {_tags}) );
				if( userToken->tokenData.length<9 ){
					let token = string{ ToSV(userToken->tokenData) };
					uint end{};
					let sessionId = Str::TryTo<SessionPK>( token, &end, 16 );//attacker-chosen plaintext on the None endpoint;  std::stoul threw invalid_argument for a non-hex token, past this ι boundary.
					THROW_IFX( !sessionId || end!=token.size(), UAException{UA_STATUSCODE_BADIDENTITYTOKENINVALID} );
					let sessionInfo = BlockAwait<TAwait<Web::FromServer::SessionInfo>, Web::FromServer::SessionInfo>( 	move(*AppClient()->SessionInfoAwait(*sessionId)) );
					ctx = mu<SessionContext>( sessionInfo.user_endpoint(), Protobuf::ToTimePoint(sessionInfo.expiration()), (SessionPK)sessionInfo.session_id(), UserPK{sessionInfo.user_pk()} );
				}
				else{
					Web::Jwt jwt{ ToSV(userToken->tokenData) };
					AppClient()->Verify( jwt );
					ctx = mu<SessionContext>( string{}, jwt.Expires(), Str::TryTo<SessionPK>(jwt.SessionId, 0, 16).value_or(0), jwt.UserPK );
				}
			}
			else {
					/* Unsupported token type */
					throw UAException{ UA_STATUSCODE_BADIDENTITYTOKENINVALID };
			}
			ASSERT( ctx );
			delete static_cast<SessionContext*>( *sessionContext );//the one this call replaces, if the session is re-activating.
			*sessionContext = ctx.release();
	    return UA_STATUSCODE_GOOD;
		}
		catch( const UAException& e ){
			return e.Code();
		}
		catch( const runtime_error& e ){
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
	α UAAccess::ResolveUser( sv loginName )ι->UserPK{
		UserPK y{};
		try{
			let providers = AppClient()->QuerySync<jarray>( "providers( providerTypeId:$type ){ id }", {{"type", underlying(Access::EProviderType::OpcServer)}} );
			flat_set<Access::ProviderPK> opcProviders;
			for( let& p : providers )
				opcProviders.insert( Json::AsNumber<Access::ProviderPK>(p.as_object(), "id") );
			let users = AppClient()->QuerySync<jarray>( "users( loginName:$name ){ id providerId }", {{"name", string{loginName}}} );
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
	α UAAccess::CloseSession( UA_Server* server, UA_AccessControl* ac,const UA_NodeId* sessionId, void* sessionContext )ι->void{
		SessionContext* ctx = static_cast<SessionContext*>( sessionContext );
		if( ctx && ctx->SessionId ){//an answer that arrived for a session nobody will ask about again.
			std::scoped_lock _{ _renewalsMutex };
			_renewals.erase( ctx->SessionId );
		}
		delete ctx;
	}
	α UAAccess::GetUserRightsMask( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, void *nodeContext )ι->UA_UInt32{
		ASSERT( nodeId );
		let ctx = readContext( sessionId, sessionContext );//null for a detached subscription's read - denied below, silently.
		if( !nodeId || !ctx || expired(ctx) )
			return 0;
		//Was Rights( <schema>, "node", … ) - a resource name nothing ever creates, which Authorize::Rights answers with
		//ERights::All, so every session got every UA_WRITEMASK bit the node's own writeMask allowed:  DisplayName,
		//Description and AccessLevel rewritable by a user with no rights at all, and AccessLevel is the Value-write gate
		//(opcserver-review3 #8).  The node's own acl is the answer, exactly as getUserAccessLevel reads it.
		let rights = authorizer().NodeRights( *nodeId, ctx->UserPK );
		UA_UInt32 mask = 0;
		if( !empty(rights & Access::ERights::Update) )
			mask = UA_WRITEMASK_ARRRAYDIMENSIONS | UA_WRITEMASK_BROWSENAME | UA_WRITEMASK_CONTAINSNOLOOPS | UA_WRITEMASK_DATATYPE | UA_WRITEMASK_DESCRIPTION | UA_WRITEMASK_DISPLAYNAME | UA_WRITEMASK_EVENTNOTIFIER | UA_WRITEMASK_EXECUTABLE | UA_WRITEMASK_HISTORIZING | UA_WRITEMASK_INVERSENAME | UA_WRITEMASK_ISABSTRACT  | UA_WRITEMASK_MINIMUMSAMPLINGINTERVAL | UA_WRITEMASK_NODECLASS | UA_WRITEMASK_NODEID | UA_WRITEMASK_SYMMETRIC | UA_WRITEMASK_USEREXECUTABLE | UA_WRITEMASK_VALUERANK | UA_WRITEMASK_VALUEFORVARIABLETYPE | UA_WRITEMASK_DATATYPEDEFINITION;
		if( !empty(rights & Access::ERights::Administer) )
			mask |= UA_WRITEMASK_ROLEPERMISSIONS | UA_WRITEMASK_ACCESSRESTRICTIONS | UA_WRITEMASK_ACCESSLEVELEX | UA_WRITEMASK_USERWRITEMASK | UA_WRITEMASK_ACCESSLEVEL | UA_WRITEMASK_USERACCESSLEVEL | UA_WRITEMASK_WRITEMASK;
		return mask;
	}
	α UAAccess::GetUserAccessLevel( UA_Server* /*server*/, UA_AccessControl* /*ac*/, const UA_NodeId* sessionId, void* sessionContext, const UA_NodeId* nodeId, void* /*nodeContext*/ )ι->UA_Byte{
		ASSERT( nodeId );
		if( !nodeId )
			return 0;
		//only read identifier.numeric once the id is known numeric; string/GUID ids are valid here and go straight to UserRights.
		if( nodeId->namespaceIndex==0 && nodeId->identifierType==UA_NODEIDTYPE_NUMERIC && nodeId->identifier.numeric==UA_NS0ID_SERVER_NAMESPACEARRAY )
			return UA_ACCESSLEVELMASK_READ;
		let ctx = readContext( sessionId, sessionContext );//null for a detached subscription's sampling - the source of #9's assert storm; denied below, silently.
		if( !ctx || expired(ctx) )
			return 0;
		return underlying( authorizer().UserRights(*nodeId, ctx->UserPK) );
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
		if( !item || !ctx || expired(ctx) )
			return false;
		//Was Test( "variables", Subscribe ) - a resource name nothing creates, so Authorize::Test returned silently and
		//every session could restructure the address space (opcserver-review3 #8).  A reference is a write to its source
		//node, so Update on that node is the right;  the nodeset loader is unaffected - open62541 skips this callback
		//entirely for its adminSession (ua_services_nodemanagement.c Operation_addReference).
		return !empty( authorizer().NodeRights(item->sourceNodeId, ctx->UserPK) & Access::ERights::Update );
	}
	α UAAccess::AllowDeleteNode( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_DeleteNodesItem *item )ι->UA_Boolean{ ASSERT(false); return false; }
	α UAAccess::AllowDeleteReference( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_DeleteReferencesItem *item )ι->UA_Boolean{ ASSERT(false); return false; }
	α UAAccess::AllowBrowseNode( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, void *nodeContext )ι->UA_Boolean{
		ASSERT( nodeId );
		let ctx = static_cast<SessionContext*>( sessionContext ); ASSERT( ctx );
		if( !nodeId || !ctx || expired(ctx) )
			return false;
		//Was Test( "browse", Read ) - a resource name nothing creates, so browse was ungated for every session
		//(opcserver-review3 #8).  Read on the node itself, the same right that opens its value.
		return !empty( authorizer().NodeRights(*nodeId, ctx->UserPK) & Access::ERights::Read );
	}
	α UAAccess::AllowTransferSubscription( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *oldSessionId, void *oldSessionContext, const UA_NodeId *newSessionId, void *newSessionContext )ι->UA_Boolean{ ASSERT(false); return false; }
	α UAAccess::AllowHistoryUpdateUpdateData( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, UA_PerformUpdateType performInsertReplace, const UA_DataValue *value )ι->UA_Boolean{ ASSERT(false); return false; }
	α UAAccess::AllowHistoryUpdateDeleteRawModified( UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, UA_DateTime startTimestamp, UA_DateTime endTimestamp, bool isDeleteModified )ι->UA_Boolean{ ASSERT(false); return false; }
}