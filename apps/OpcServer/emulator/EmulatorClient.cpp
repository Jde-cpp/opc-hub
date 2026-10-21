#include "EmulatorClient.h"
#include <jde/fwk/str.h>
#include <jde/opc/uatypes/BrowsePath.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include <jde/opc/ClientSecurity.h>
#include <jde/opc/ServerTrust.h>

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Client };

	EmulatorClient::EmulatorClient( string url, string applicationUri, string serverApplicationUri, const Crypto::CryptoSettings& certificate, SL sl )ε:
		_url{ move(url) },
		_applicationUri{ Str::Replace(applicationUri, " ", "%20") },
		_serverApplicationUri{ Str::Replace(serverApplicationUri, " ", "%20") },
		_logger{ 0 }{
		_config.logging = &_logger;
		_config.eventLoop = UA_EventLoop_new_POSIX( _config.logging );
		auto tcp = UA_ConnectionManager_new_POSIX_TCP( "tcp connection manager"_uv );
		_config.eventLoop->registerEventSource( _config.eventLoop, (UA_EventSource*)tcp );
		_config.timeout = 10000;
		_config.stateCallback = StateCallback;
		//no noReconnect: open62541's initial connect must close the None discovery channel and reopen with the selected
		//secured endpoint (that reopen counts as a reconnect); blocking it aborts every connect with BadNotConnected.
		//A genuine session drop is caught by the loop's IsActivated() check, which Disconnect()s and Connect()s afresh.
		_ptr = UA_Client_newWithConfig( &_config );
		THROW_IFSL( !_ptr, "UA_Client_newWithConfig failed." );
		UA_Client_getConfig( _ptr )->eventLoop->logger = _config.logging;
		try{
			Configure( certificate, sl );//a throw, the certificate unreadable included, is caught below.
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

	α EmulatorClient::Configure( const Crypto::CryptoSettings& certificate, SL sl )ε->void{
		auto config = UA_Client_getConfig( _ptr );
		//A UA-enabled PLC checks who it is talking to: without the verifier, setDefault's AcceptAll would take any certificate the
		//endpoint answers with, so anything that can occupy the OpcServer's address collects our issued token and our process
		//values.  The gateway's, anchored on this app's own list, /emulator/trustedCertDirs - see jde/opc/ServerTrust.h.
		ClientSecurity::Configure( *config, {.Certificate=certificate, .Secure=true, .ServerUri=_serverApplicationUri, .AdvertisedUri=_applicationUri, .TrustRoot="/emulator"}, (Jde::Handle)(uint)_ptr, _url, sl );//the client pointer is the id StateCallback logs, so the verifier's lines and the connect failure line up.
		vector<string> policyUris;
		for( size_t i=0; i<config->securityPoliciesSize; ++i )
			policyUris.emplace_back( ToString(config->securityPolicies[i].policyUri) );
		INFO( "Client security policies: {}, applicationUri filter: '{}', advertised applicationUri: '{}'.", Str::Join(policyUris), ToString(config->applicationUri), ToString(config->clientDescription.applicationUri) );
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

	α EmulatorClient::Connect( string issuedToken, SL sl )ε->void{
		//SessionId() is 0 while the app socket is down or still logging in - a backoff, not a token to present.
		THROW_IFSL( issuedToken=="0", "No AppServer session yet - not connecting to '{}'.", _url );
		//UAAccess::ActivateSession reads a token under 9 chars as the AppServer session id in hex; anything longer is parsed as a JWT.
		THROW_IFSL( issuedToken.empty() || issuedToken.size()>8, "Issued token '{}' must be the AppServer session id in hex (1-8 chars).", issuedToken );
		//issued token = the AppServer session id, as the gateway sends it (UAClient::Create).  open62541 overwrites the
		//policyId with the endpoint's before ActivateSession, and the server ignores it for issued tokens.  ActivateSession
		//copies the config's token, so replacing it between sessions is what the next connect presents.
		auto identityToken = UA_IssuedIdentityToken_new();
		identityToken->policyId = AllocUAString( "open62541-issuedtoken-policy"sv );
		UA_ByteString_allocBuffer( &identityToken->tokenData, issuedToken.size() );
		memcpy( identityToken->tokenData.data, issuedToken.data(), issuedToken.size() );
		auto config = UA_Client_getConfig( _ptr );
		UA_ExtensionObject_clear( &config->userIdentityToken );
		UA_ExtensionObject_setValue( &config->userIdentityToken, identityToken, &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );
		DBG( "Connecting to '{}'.", _url );
		Check( UA_Client_connect(_ptr, _url.c_str()), Ƒ("connect '{}'", _url), EOpcLogTags::Client, sl );
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
		Check( UA_Client_getNamespaceIndex(_ptr, ToUV(uri), &index), Ƒ("namespace '{}' is not on the server", uri), EOpcLogTags::Client, sl );
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
		Check( sc, Ƒ("browse path '{}'", browsePath.ToString()), EOpcLogTags::Client, sl );
		THROW_IFSL( UA_NodeId_isNull(&y), "No target for browse path '{}'.", browsePath.ToString() );
		return y;
	}
	//UA_Client_writeValueAttribute sends the variant alone; a reading's quality rides in the DataValue, so this is the
	//Write service itself.  Good needs nothing more than Write.  Anything else needs StatusWrite on the node's AccessLevel
	//and on the user's (open62541 copyAttributeIntoNode: BadWriteNotSupported, then BadUserAccessDenied) - the caller
	//decides what a refusal means (Emulator::Cycle).
	α EmulatorClient::Write( const NodeId& node, const UA_Variant& value, UA_StatusCode status, SL sl )ε->void{
		UA_WriteValue write;
		UA_WriteValue_init( &write );
		write.nodeId = node;//borrowed for the call, as is the variant; the request is not cleared.
		write.attributeId = UA_ATTRIBUTEID_VALUE;
		write.value.value = value;
		write.value.hasValue = true;
		write.value.status = status;
		write.value.hasStatus = true;//Good included: the node stores the status as written, which is how a reading returns to Good.
		UA_WriteRequest request;
		UA_WriteRequest_init( &request );
		request.nodesToWrite = &write;
		request.nodesToWriteSize = 1;
		auto response = UA_Client_Service_write( _ptr, request );
		auto sc = response.responseHeader.serviceResult;
		if( !sc )
			sc = response.resultsSize ? response.results[0] : UA_STATUSCODE_BADUNEXPECTEDERROR;
		UA_WriteResponse_clear( &response );
		Check( sc, Ƒ("write {}", node.ToString()), EOpcLogTags::Client, sl );
	}
	α EmulatorClient::CreateSubscription( Duration publishingInterval, SL sl )ε->UA_UInt32{
		auto request = UA_CreateSubscriptionRequest_default();
		request.requestedPublishingInterval = std::chrono::duration<double,std::milli>( publishingInterval ).count();
		auto response = UA_Client_Subscriptions_create( _ptr, request, nullptr, nullptr, nullptr );
		let sc = response.responseHeader.serviceResult;
		let id = response.subscriptionId;
		UA_CreateSubscriptionResponse_clear( &response );
		Check( sc, "createSubscription", EOpcLogTags::Client, sl );
		return id;
	}
	α EmulatorClient::Monitor( UA_UInt32 subscription, const NodeId& node, Duration samplingInterval, void* context, UA_Client_DataChangeNotificationCallback callback, SL sl )ε->UA_UInt32{
		auto item = UA_MonitoredItemCreateRequest_default( node );
		item.requestedParameters.samplingInterval = std::chrono::duration<double,std::milli>( samplingInterval ).count();
		auto result = UA_Client_MonitoredItems_createDataChange( _ptr, subscription, UA_TIMESTAMPSTORETURN_BOTH, item, context, callback, nullptr );
		let sc = result.statusCode;
		let id = result.monitoredItemId;
		UA_MonitoredItemCreateResult_clear( &result );
		Check( sc, Ƒ("monitor {}", node.ToString()), EOpcLogTags::Client, sl );
		return id;
	}
}