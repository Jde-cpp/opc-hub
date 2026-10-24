#include <jde/opc/server/UAConfig.h>
#include <open62541/config.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/accesscontrol_default.h>
#include <open62541/plugin/certificategroup_default.h>
#include <jde/fwk/crypto/issue.h>
#include <jde/app/client/IAppClient.h>
#include <jde/opc/server/INodeAccess.h>
#include <jde/opc/server/UAAccess.h>
#include <jde/opc/server/UAHistory.h>

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags = ( ELogTags )EOpcLogTags::Opc;
	UAConfig::UAConfig( const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess, UAHistory* history )ε:
		UA_ServerConfig{
			.logging = &_logger,
		},
		_trust{ settings.TrustedCertDirs, settings.RejectedCertDir }{
		try{
			//One shape:  secured.  Without a certificate this used to build a second one - a None endpoint with no certificate and
			//no trust list (SetupUnsecured), and before that open62541's allow-all default (opcserver-review3 #7) - which open62541's
			//own core lets nobody but an anonymous session use:  a non-anonymous token on a None channel under a None token policy is
			//skipped unless allowNonePolicyPassword, and then only a password (reviews/security-matrix.md #5, deleted 09-18).  The
			//host refuses a config without one (the OpcServer's ReadUASettingsTests.NoSslIsRefused);  here an empty block is the
			//default paths, so settings.Ssl always names a pair, made when absent.
			SetupSecurityPolicies( settings, move(appClient), move(nodeAccess) );
		}
		catch( std::exception& ){
			UA_ServerConfig_clear( this );
			throw;//rethrow original: `throw move(e)` slices to std::exception, losing the derived type and Jde::Exception state.
		}
		UA_LocalizedText_clear( &applicationDescription.applicationName );// setDefaultConfig/setBasics already allocated applicationName; clear before overwriting or it leaks.
		applicationDescription.applicationName = UA_LOCALIZEDTEXT_ALLOC( "en-US", settings.ApplicationName.c_str() );
		//The history backend, only with a historian:  open62541 answers every HistoryRead Bad_NotSupported while the
		//database's context is null, which is the answer of a server with no historian, or whose path another process
		//holds.  open62541 publishes HistoryServerCapabilities from these flags, each set only for what is served:  raw
		//and modified reads, and the four edits the plugin has a callback for - it has none for DeleteAtTime, which stays
		//QL-only (historian spec *UA backend*).
		if( history && history->Enabled() ){
			historyDatabase = history->Database();
			nodeLifecycle = UAHistory::Lifecycle();
			accessHistoryDataCapability = true;
			maxReturnDataValues = history->ReadLimit();
			insertDataCapability = replaceDataCapability = updateDataCapability = deleteRawCapability = true;
		}
	}

	//The interface the endpoint listens on.  Empty:  every interface - open62541's own default, an empty host in the url.  The
	//OpcServer's current-user install binds loopback (args/install-user, install-issues #16), so Windows raises no firewall
	//prompt for it.  setBasics writes serverUrls as opc.tcp://:port; this puts the host in, which the tcp layer then binds to
	//alone and the discovery url carries.
	Ω applyAddress( UA_ServerConfig& config, str address, PortType port )ε->void{
		if( address.empty() )
			return;
		UA_Array_delete( config.serverUrls, config.serverUrlsSize, &UA_TYPES[UA_TYPES_STRING] );
		config.serverUrls = (UA_String*)UA_Array_new( 1, &UA_TYPES[UA_TYPES_STRING] );
		THROW_IF( !config.serverUrls, "Could not allocate the server url." );
		config.serverUrlsSize = 1;
		config.serverUrls[0] = UA_STRING_ALLOC( Ƒ("opc.tcp://{}:{}", address, port).c_str() );
		INFO( "OPC UA endpoint bound to {}:{} only.", address, port );
	}

	α UAConfig::SetupSecurityPolicies( const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess, SL sl )ε->void{
		let& ssl = settings.Ssl;
		Crypto::EnsureKeyCertificate( ssl, sl );
		auto certificate = ToUAByteString( Crypto::ReadCertificate(ssl.Certificate.Path, sl) );
		auto privateKey = ToUAByteString( Crypto::ReadPrivateKey(ssl.PrivateKey) );
		SetConfig( settings, move(certificate), move(privateKey), move(appClient), move(nodeAccess) );
		UA_String_clear( &applicationDescription.applicationUri );
		let uri = ssl.Certificate.SanUri();
		if( uri.empty() )//clients compare their configured applicationUri against ours; an empty one rejects every endpoint.
			WARN( "ssl certificate '{}' has no URI entry in its subjectAltName '{}' - applicationUri will be empty.", ssl.Certificate.Path.string(), ssl.Certificate.SubjectAltName );
		applicationDescription.applicationUri = UA_STRING_ALLOC( uri.c_str() );
	}

	//The host's limits over open62541's own (spec *Limits*):  only what the host set, so a server whose host sets none - the
	//OpcServer - runs on the defaults setBasics gave it, as it always has.
	Ω applyLimits( UA_ServerConfig& config, const UASettings::Limits& limits )ε->void{
		let ms = []( Duration d ){ return (UA_Double)duration_cast<std::chrono::duration<double,std::milli>>( d ).count(); };
		if( limits.MaxSessions ){
			config.maxSessions = *limits.MaxSessions;
			config.maxSecureChannels = std::max( config.maxSecureChannels, config.maxSessions );//a session per channel:  past the 100 channels, maxSessions alone admits none more.
		}
		if( limits.MaxMonitoredItems )
			config.maxMonitoredItems = *limits.MaxMonitoredItems;
		if( limits.SessionTimeout ){
			THROW_IF( *limits.SessionTimeout<=Duration::zero(), "limits.sessionTimeout must be positive, not {} ms.", duration_cast<milliseconds>(*limits.SessionTimeout).count() );
			config.maxSessionTimeout = ms( *limits.SessionTimeout );
		}
		if( limits.MinSamplingInterval ){
			THROW_IF( *limits.MinSamplingInterval<=Duration::zero(), "limits.minSamplingInterval must be positive, not {} ms.", duration_cast<milliseconds>(*limits.MinSamplingInterval).count() );
			config.samplingIntervalLimits.min = ms( *limits.MinSamplingInterval );
			config.samplingIntervalLimits.max = std::max( config.samplingIntervalLimits.max, config.samplingIntervalLimits.min );
		}
		if( limits.MaxAsyncOperations )
			config.maxAsyncOperationQueueSize = *limits.MaxAsyncOperations;
	}

	//The cancel of an async operation, to the host's handler when it set one (OnAsyncCancel):  under the service lock.
	α UAConfig::AsyncCancelled( UA_Server* server, const void* out )ι->void{
		let config = static_cast<const UAConfig*>( UA_Server_getConfig(server)->context );
		if( config && config->_asyncCancel )
			config->_asyncCancel( out );
	}

	α UAConfig::SetConfig( const UASettings& settings, ByteStringPtr&& certificate, const ByteStringPtr&& privateKey, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess )ε->void{
		UAε( UA_ServerConfig_setBasics_withPort(this, settings.Port) );
		applyAddress( *this, settings.Address, settings.Port );
		applyLimits( *this, settings.Limit );
		//A client's Write of a callback value source holds this long for its answer (servers spec *Writes*);  the cancel past it, or at
		//shutdown, reaches the host through this, the base open62541 copies into the server, so its `context` is this object.
		if( settings.WriteTimeout )
			asyncOperationTimeout = (UA_Double)duration_cast<std::chrono::duration<double,std::milli>>( *settings.WriteTimeout ).count();
		context = this;
		asyncOperationCancelCallback = AsyncCancelled;

		UA_TrustListDataType list;
		UA_TrustListDataType_init( &list );
		_trust.LoadTrustList( list );//also primes the mtime cache the runtime rescan (UATrust::Verify) diffs against. An unreadable cert now logs CRITICAL and is skipped rather than aborting startup - matches Access loadTrustAnchors.

		//The certificate groups' parameters.  The rejected list capped:  setBasics leaves it 0, unbounded.
		maxRejectedListSize = UATrust::MaxRejected;
		UA_KeyValuePair params[2];
		params[0].key = UA_QualifiedName{ 0, "max-trust-listsize"_uv };
		UA_Variant_setScalar( &params[0].value, &maxTrustListSize, &UA_TYPES[UA_TYPES_UINT32] );
		params[1].key = UA_QualifiedName{ 0, "max-rejected-listsize"_uv };
		UA_Variant_setScalar( &params[1].value, &maxRejectedListSize, &UA_TYPES[UA_TYPES_UINT32] );
		UA_KeyValueMap paramsMap;
		paramsMap.map = params;
		paramsMap.mapSize = 2;

		if( secureChannelPKI.clear )
			secureChannelPKI.clear( &secureChannelPKI );
		UA_NodeId defaultApplicationGroup = UA_NODEID_NUMERIC( 0, UA_NS0ID_SERVERCONFIGURATION_CERTIFICATEGROUPS_DEFAULTAPPLICATIONGROUP );
		try{
			UAε( UA_CertificateGroup_Memorystore(&secureChannelPKI, &defaultApplicationGroup, &list, logging, &paramsMap) );
			if( sessionPKI.clear )
				sessionPKI.clear( &sessionPKI );
			UA_NodeId defaultUserTokenGroup = UA_NODEID_NUMERIC( 0, UA_NS0ID_SERVERCONFIGURATION_CERTIFICATEGROUPS_DEFAULTUSERTOKENGROUP );
			UAε( UA_CertificateGroup_Memorystore(&sessionPKI, &defaultUserTokenGroup, &list, logging, &paramsMap) );
		}
		catch( std::exception& ){
			UA_TrustListDataType_clear( &list );
			throw;//rethrow original: `throw move(e)` slices to std::exception, losing the derived type and Jde::Exception state.
		}
		UA_TrustListDataType_clear( &list );
		AddSecurityPolicies( move(certificate), move(privateKey) );

		UAAccess::Init( *this, settings, move(appClient), move(nodeAccess) );

		//None=false keeps the None policy, which AddSecurityPolicies added, but offers no endpoint on it:  a None channel then
		//answers GetEndpoints and FindServers and nothing else (securityPolicyNoneDiscoveryOnly, ua_services.c), so a hardened
		//site offers the secured endpoints alone and is still discoverable (spec *Limits*).
		if( settings.None ){
			UAε( UA_ServerConfig_addAllEndpoints(this) );
		}
		else{
			securityPolicyNoneDiscoveryOnly = true;
			UAε( UA_ServerConfig_addAllSecureEndpoints(this) );
			INFO( "No None endpoint:  the None policy answers discovery alone." );
		}
	}

	α UAConfig::AddSecurityPolicies( ByteStringPtr&& certificate, const ByteStringPtr&& privateKey )ε->void{
		UA_ByteString localCertificate = *certificate;
		UA_ByteString localPrivateKey = *privateKey;
		//The private key in DER, decrypted with an empty password:  the key may need none.
		UA_ByteString decryptedPrivateKey = UA_BYTESTRING_NULL;
		UA_ByteString keyPassword = UA_BYTESTRING_NULL;
		if( privateKey->length>0 )
			UAε( UA_CertificateUtils_decryptPrivateKey(localPrivateKey, keyPassword, &decryptedPrivateKey) );
		UAε( UA_ServerConfig_addSecurityPolicyBasic256Sha256(this, &localCertificate, &decryptedPrivateKey) );
		//The three current RSA policies, so a client takes the strongest it shares - open62541 orders endpoints by the policy's
		//securityLevel - and one that carries Basic256Sha256 alone (most installed clients) still connects (reviews/security-matrix.md
		//#4;  EndpointTests).  User tokens stay under UASettings::UserTokenPolicyUri, Basic256Sha256 by default.
		UAε( UA_ServerConfig_addSecurityPolicyAes128Sha256RsaOaep(this, &localCertificate, &decryptedPrivateKey) );
		UAε( UA_ServerConfig_addSecurityPolicyAes256Sha256RsaPss(this, &localCertificate, &decryptedPrivateKey) );
		UAε( UA_ServerConfig_addSecurityPolicyNone(this, &localCertificate) );
		UA_ByteString_memZero( &decryptedPrivateKey );
		UA_ByteString_clear( &decryptedPrivateKey );
	}
}