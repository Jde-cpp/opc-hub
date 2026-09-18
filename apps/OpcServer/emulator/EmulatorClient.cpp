#include "EmulatorClient.h"
#include <jde/fwk/str.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <open62541/plugin/securitypolicy_default.h>
#include <jde/opc/uatypes/BrowsePath.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include <jde/opc/ServerTrust.h>

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Client };
	Ω check( UA_StatusCode sc, string what, SL sl )ε->void{ THROW_IFX( sc, UAException(sc, move(what), {.Tags=EOpcLogTags::Client}, sl) ); }

	EmulatorClient::EmulatorClient( string url, string applicationUri, string serverApplicationUri, const Crypto::CryptoSettings& certificate, string issuedToken, SL sl )ε:
		_url{ move(url) },
		_applicationUri{ Str::Replace(applicationUri, " ", "%20") },
		_serverApplicationUri{ Str::Replace(serverApplicationUri, " ", "%20") },
		_token{ move(issuedToken) },
		_logger{ 0 }{
		//UAAccess::ActivateSession reads a token under 9 chars as the AppServer session id in hex; anything longer is parsed as a JWT.
		THROW_IFSL( _token.empty() || _token.size()>8, "Issued token '{}' must be the AppServer session id in hex (1-8 chars).", _token );
		//credential material first: a throw here leaves nothing allocated.
		auto cert = ToUAByteString( Crypto::ReadCertificate(certificate.Certificate.Path) );
		auto key = ToUAByteString( Crypto::ReadPrivateKey(certificate.PrivateKey) );
		_config.logging = &_logger;
		_config.eventLoop = UA_EventLoop_new_POSIX( _config.logging );
		auto tcp = UA_ConnectionManager_new_POSIX_TCP( "tcp connection manager"_uv );
		_config.eventLoop->registerEventSource( _config.eventLoop, (UA_EventSource*)tcp );
		_config.timeout = 10000;
		_config.stateCallback = StateCallback;
		//no noReconnect: open62541's initial connect must close the None discovery channel and reopen with the selected
		//Basic256Sha256 endpoint (that reopen counts as a reconnect); blocking it aborts every connect with BadNotConnected.
		//A genuine session drop is caught by the loop's IsActivated() check, which Disconnect()s and Connect()s afresh.
		//issued token = the AppServer session id, as the gateway sends it (UAClient::Create).  open62541 overwrites the
		//policyId with the endpoint's before ActivateSession, and the server ignores it for issued tokens.
		auto identityToken = UA_IssuedIdentityToken_new();
		identityToken->policyId = AllocUAString( "open62541-issuedtoken-policy"sv );
		UA_ByteString_allocBuffer( &identityToken->tokenData, _token.size() );
		memcpy( identityToken->tokenData.data, _token.data(), _token.size() );
		UA_ExtensionObject_setValue( &_config.userIdentityToken, identityToken, &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );
		_ptr = UA_Client_newWithConfig( &_config );
		THROW_IFSL( !_ptr, "UA_Client_newWithConfig failed." );
		UA_Client_getConfig( _ptr )->eventLoop->logger = _config.logging;
		try{
			Configure( *cert, *key, sl );
		}
		catch( ... ){
			UA_Client_delete( _ptr );
			_ptr = nullptr;
			throw;
		}
	}
	EmulatorClient::~EmulatorClient(){
		if( _ptr ){
			UA_Client_disconnect( _ptr );
			UA_Client_delete( _ptr );
		}
	}

	α EmulatorClient::Configure( const UA_ByteString& certificate, const UA_ByteString& privateKey, SL sl )ε->void{
		auto config = UA_Client_getConfig( _ptr );
		ASSERT( !config->securityPoliciesSize );
		constexpr uint size{ 2 };
		uint initialized{};//policies actually constructed; on a throw before ownership transfers to config, clear those and free the block.
		auto policies = (UA_SecurityPolicy*)UA_malloc( sizeof(UA_SecurityPolicy)*size );
		try{
			check( UA_SecurityPolicy_None(&policies[0], UA_BYTESTRING_NULL, &_logger), "UA_SecurityPolicy_None", sl ); ++initialized;
			check( UA_SecurityPolicy_Basic256Sha256(&policies[1], certificate, privateKey, &_logger), "UA_SecurityPolicy_Basic256Sha256", sl ); ++initialized;
			//Two different uris (#15).  config->applicationUri only FILTERS the server's endpoints (matchEndpoint: keep those
			//whose server advertises it; empty keeps all), so it is the server's.  clientDescription's is what we advertise
			//as ourselves, and the server verifies it against the SAN of the certificate we present (validateCertificate) -
			//against each other, never against its own - so it is this device's.  The gateway keeps the same two apart since
			//security-matrix #8 (UAClient::Configuration);  until then it set both from one string and introduced itself as the
			//server it was calling.
			UA_String_clear( &config->applicationUri );
			if( _serverApplicationUri.size() )
				config->applicationUri = AllocUAString( _serverApplicationUri );
			UA_String_clear( &config->clientDescription.applicationUri );
			config->clientDescription.applicationUri = AllocUAString( _applicationUri );
			//the user token rides an encrypted policy (/opc/userTokenPolicyUri) - this is the policy that encrypts it.
			auto grown = (UA_SecurityPolicy*)UA_realloc( config->authSecurityPolicies, sizeof(UA_SecurityPolicy)*(config->authSecurityPoliciesSize+1) );
			check( grown ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADOUTOFMEMORY, "authSecurityPolicies", sl );
			config->authSecurityPolicies = grown;
			check( UA_SecurityPolicy_Basic256Sha256(&config->authSecurityPolicies[config->authSecurityPoliciesSize], certificate, privateKey, config->logging), "auth UA_SecurityPolicy_Basic256Sha256", sl );
			++config->authSecurityPoliciesSize;
		}
		catch( ... ){
			for( uint i=0; i<initialized; ++i )
				policies[i].clear( &policies[i] );
			UA_free( policies );
			throw;
		}
		config->securityPolicies = policies;
		config->securityPoliciesSize = size;
		config->secureChannelLifeTime = 60*60*1000;
		//A UA-enabled PLC checks who it is talking to: without this, setDefault's AcceptAll below would take any certificate
		//the endpoint answers with, so anything that can occupy the OpcServer's address collects our issued token and our
		//process values.  The gateway's verifier, anchored on this app's own list, /emulator/trustedCertDirs - see jde/opc/ServerTrust.h.
		ServerTrust::Install( *config, "/emulator", (Jde::Handle)(uint)_ptr, _url, sl );//the client pointer is the id StateCallback logs, so the verifier's lines and the connect failure line up.
		//after the policies: setDefault back-fills only what is unset (the verifier just installed, the UDP/interrupt event
		//sources) and skips its own None policy because securityPoliciesSize!=0.  Before them it would install a
		//default policy set this block then trips over.
		check( UA_ClientConfig_setDefault(config), "UA_ClientConfig_setDefault", sl );
		vector<string> policyUris;
		for( size_t i=0; i<config->securityPoliciesSize; ++i )
			policyUris.emplace_back( ToString(config->securityPolicies[i].policyUri) );
		INFO( "Client security policies: {}, applicationUri filter: '{}', advertised applicationUri: '{}', token: {} chars.", Str::Join(policyUris), ToString(config->applicationUri), ToString(config->clientDescription.applicationUri), _token.size() );
	}

	α EmulatorClient::StateCallback( UA_Client* ua, UA_SecureChannelState channelState, UA_SessionState sessionState, UA_StatusCode connectStatus )ι->void{
		constexpr array<sv,6> sessionStates{ "Closed", "CreateRequested", "Created", "ActivateRequested", "Activated", "Closing" };
		LOG( connectStatus ? ELogLevel::Warning : ELogLevel::Debug, _tags, "[{}]channelState: '{}', sessionState: '{}', connectStatus: '({}){}'", hex((uint)ua), UAException::Message(channelState), FromEnum(sessionStates, sessionState), hex(connectStatus), UAException::Message(connectStatus) );
		//BadCertificateUntrusted reads the same whether we rejected the server's certificate or it rejected ours; only the
		//verifier knows which, so say so here.  Already logged by the verifier itself - this ties it to the failed connect.
		if( connectStatus ){
			if( let rejection = ServerTrust::Rejection(*UA_Client_getConfig(ua)); rejection.size() )
				WARNT( _tags, "[{}]{}", hex((uint)ua), rejection );
		}
	}

	α EmulatorClient::Connect( SL sl )ε->void{
		DBG( "Connecting to '{}'.", _url );
		check( UA_Client_connect(_ptr, _url.c_str()), Ƒ("connect '{}'", _url), sl );
		INFO( "Session activated on '{}'.", _url );
	}
	α EmulatorClient::Disconnect()ι->void{
		UA_Client_disconnect( _ptr );
	}
	α EmulatorClient::IsActivated()Ι->bool{
		UA_SecureChannelState channel; UA_SessionState session; UA_StatusCode status;
		UA_Client_getState( _ptr, &channel, &session, &status );
		return session==UA_SESSIONSTATE_ACTIVATED && status==UA_STATUSCODE_GOOD;
	}
	α EmulatorClient::Iterate( uint32 timeoutMs )ι->UA_StatusCode{
		return UA_Client_run_iterate( _ptr, timeoutMs );
	}
	//Deleting the subscription before a clean Disconnect is what keeps the ten in-flight publish requests quiet:  the server
	//answers them BadNoSubscription, which open62541 debug-logs, instead of answering a subscription the client has just
	//forgotten, which processPublishResponse's default branch reports at WARNING each time.  ι - failing here is cosmetic.
	α EmulatorClient::DeleteSubscription( UA_UInt32 subscription )ι->void{
		if( let sc = UA_Client_Subscriptions_deleteSingle(_ptr, subscription); sc )
			DBG( "deleteSubscription {} on '{}' - {}", subscription, _url, UA_StatusCode_name(sc) );
	}
	α EmulatorClient::Namespace( sv uri, SL sl )ε->NsIndex{
		UA_UInt16 index{};
		check( UA_Client_getNamespaceIndex(_ptr, UA_String{uri.size(), (UA_Byte*)uri.data()}, &index), Ƒ("namespace '{}' is not on the server", uri), sl );
		return index;
	}
	α EmulatorClient::Resolve( sv path, NsIndex defaultNs, const flat_map<string,NsIndex>& nsAliases, SL sl )ε->NodeId{
		BrowsePath browsePath{ path, defaultNs, nsAliases, sl };
		UA_TranslateBrowsePathsToNodeIdsRequest request;
		UA_TranslateBrowsePathsToNodeIdsRequest_init( &request );
		request.browsePaths = &browsePath;//borrowed for the call; the request is not cleared.
		request.browsePathsSize = 1;
		auto response = UA_Client_Service_translateBrowsePathsToNodeIds( _ptr, request );
		auto sc = response.responseHeader.serviceResult;
		if( !sc )
			sc = response.resultsSize ? response.results[0].statusCode : UA_STATUSCODE_BADNOMATCH;
		NodeId y;
		if( !sc && response.results[0].targetsSize )
			y = NodeId{ response.results[0].targets[0].targetId.nodeId };
		UA_TranslateBrowsePathsToNodeIdsResponse_clear( &response );
		check( sc, Ƒ("browse path '{}'", browsePath.ToString()), sl );
		THROW_IFSL( UA_NodeId_isNull(&y), "No target for browse path '{}'.", browsePath.ToString() );
		return y;
	}
	α EmulatorClient::Write( const NodeId& node, const UA_Variant& value, SL sl )ε->void{
		check( UA_Client_writeValueAttribute(_ptr, node, &value), Ƒ("write {}", node.ToString()), sl );
	}
	α EmulatorClient::CreateSubscription( Duration publishingInterval, SL sl )ε->UA_UInt32{
		auto request = UA_CreateSubscriptionRequest_default();
		request.requestedPublishingInterval = std::chrono::duration<double,std::milli>( publishingInterval ).count();
		auto response = UA_Client_Subscriptions_create( _ptr, request, nullptr, nullptr, nullptr );
		let sc = response.responseHeader.serviceResult;
		let id = response.subscriptionId;
		UA_CreateSubscriptionResponse_clear( &response );
		check( sc, "createSubscription", sl );
		return id;
	}
	α EmulatorClient::Monitor( UA_UInt32 subscription, const NodeId& node, Duration samplingInterval, void* context, UA_Client_DataChangeNotificationCallback callback, SL sl )ε->UA_UInt32{
		auto item = UA_MonitoredItemCreateRequest_default( node );
		item.requestedParameters.samplingInterval = std::chrono::duration<double,std::milli>( samplingInterval ).count();
		auto result = UA_Client_MonitoredItems_createDataChange( _ptr, subscription, UA_TIMESTAMPSTORETURN_BOTH, item, context, callback, nullptr );
		let sc = result.statusCode;
		let id = result.monitoredItemId;
		UA_MonitoredItemCreateResult_clear( &result );
		check( sc, Ƒ("monitor {}", node.ToString()), sl );
		return id;
	}
}
