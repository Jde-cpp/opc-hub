#include "UAConfig.h"
#include <open62541/config.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/accesscontrol_default.h>
#include <open62541/plugin/certificategroup_default.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/app/client/IAppClient.h>
#include "access/UAAccess.h"
#include "UATrust.h"
#include "jde/fwk/crypto/CryptoSettings.h"
#include "jde/fwk/settings.h"

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags = ( ELogTags )EOpcLogTags::Opc;
	UAConfig::UAConfig()ε:
		UA_ServerConfig{
			.logging = &_logger,
		}{
		try{
			if( auto ssl = Settings::FindObject("/opcServer/ssl"); ssl )
				SetupSecurityPolicies( Crypto::CryptoSettings{*ssl} );
			else
				SetupUnsecured();
		}
		catch( std::exception& ){
			UA_ServerConfig_clear( this );
			throw;//rethrow original: `throw move(e)` slices to std::exception, losing the derived type and Jde::Exception state.
		}
		auto accessResource = Settings::FindString( "/opcServer/resource" ).value_or( "default" );
		UA_LocalizedText_clear( &applicationDescription.applicationName );// setDefaultConfig/setBasics already allocated applicationName; clear before overwriting or it leaks.
		applicationDescription.applicationName = UA_LOCALIZEDTEXT_ALLOC( "en-US", Ƒ("Jde-Cpp OpcServer [{}]", accessResource).c_str() );
	}

	//No /opcServer/ssl.  This used to be UA_ServerConfig_setDefault, which is setMinimalCustomBuffer - and that installs
	//UA_AccessControl_default( config, allowAnonymous=true, … ), whose getUserRightsMask/getUserAccessLevel/allowBrowseNode
	//answer 0xFFFFFFFF/0xFF/true to every session, never reaches UAAccess::Init, and hard-codes port 4840 while ignoring
	///opc/tokenTypes/* (opcserver-review3 #7).  Silently, too: the missing "UserToken Uris" INFO was the only tell, and
	//a hidden `ssl::` or a mistyped key reads here exactly like a deliberate omission.  Same sequence as the vendor's,
	//with our access control in place of its allow-all and the configured port.
	//"/opcServer/address": the interface the endpoint listens on.  Absent or null: every interface - open62541's own default, an
	//empty host in the url.  The current-user install binds loopback (args/install-user, install-issues #16), so Windows raises
	//no firewall prompt for it.  setBasics writes serverUrls as opc.tcp://:port; this puts the host in, which the tcp layer then
	//binds to alone and the discovery url carries.
	Ω applyAddress( UA_ServerConfig& config, PortType port )ε->void{
		let address = Settings::FindString( "/opcServer/address" ).value_or( "" );
		if( address.empty() )
			return;
		UA_Array_delete( config.serverUrls, config.serverUrlsSize, &UA_TYPES[UA_TYPES_STRING] );
		config.serverUrls = (UA_String*)UA_Array_new( 1, &UA_TYPES[UA_TYPES_STRING] );
		THROW_IF( !config.serverUrls, "Could not allocate the server url." );
		config.serverUrlsSize = 1;
		config.serverUrls[0] = UA_STRING_ALLOC( Ƒ("opc.tcp://{}:{}", address, port).c_str() );
		INFO( "OPC UA endpoint bound to {}:{} only ('/opcServer/address').", address, port );
	}

	α UAConfig::SetupUnsecured()ε->void{
		let port = Settings::FindNumber<PortType>( "/opcServer/port" ).value_or( 4840 );
		WARN( "No '/opcServer/ssl':  the OPC UA server on port {} runs unencrypted - one None endpoint, no trust list, no client-certificate verification.  Both shipped configs set it.", port );
		UAε( UA_ServerConfig_setBasics_withPort(this, port) );
		applyAddress( *this, port );
		UAε( UA_ServerConfig_addSecurityPolicyNone(this, nullptr) );
		UAAccess::Init( *this );//throws "No allowed policies set." when /opc/tokenTypes/* leaves nothing enabled - a server nobody can activate a session on, which is the fail-closed answer.
		UAε( UA_ServerConfig_addAllEndpoints(this) );
	}

	α UAConfig::SetupSecurityPolicies( const Crypto::CryptoSettings& settings, SL sl )ε->void{
		Crypto::EnsureKeyCertificate( settings, sl );
		auto certificate = ToUAByteString( Crypto::ReadCertificate(settings.Certificate.Path, sl) );
		auto privateKey = ToUAByteString( Crypto::ReadPrivateKey(settings.PrivateKey) );
		SetConfig( Settings::FindNumber<PortType>("/opcServer/port").value_or(4840), move(certificate), move(privateKey) );
		UA_String_clear( &applicationDescription.applicationUri );
		let uri = settings.Certificate.SanUri();
		if( uri.empty() )//clients compare their configured applicationUri against ours; an empty one rejects every endpoint.
			WARN( "ssl certificate '{}' has no URI entry in its subjectAltName '{}' - applicationUri will be empty.", settings.Certificate.Path.string(), settings.Certificate.SubjectAltName );
		applicationDescription.applicationUri = UA_STRING_ALLOC( uri.c_str() );
	}

	α UAConfig::SetConfig( PortType port, ByteStringPtr&& certificate, const ByteStringPtr&& privateKey )ε->void{
    UAε( UA_ServerConfig_setBasics_withPort(this, port) );
		applyAddress( *this, port );

    UA_TrustListDataType list;
    UA_TrustListDataType_init( &list );
		UATrust::LoadTrustList( list );//also primes the mtime cache the runtime rescan (UATrust::VerifyCertificate) diffs against. An unreadable cert now logs CRITICAL and is skipped rather than aborting startup - matches Access loadTrustAnchors.

    /* Set up the parameters */
    UA_KeyValuePair params[2];
    size_t paramsSize = 2;

    params[0].key = UA_QualifiedName{ 0, "max-trust-listsize"_uv };
    UA_Variant_setScalar( &params[0].value, &maxTrustListSize, &UA_TYPES[UA_TYPES_UINT32] );
    params[1].key = UA_QualifiedName{ 0, "max-rejected-listsize"_uv };
    UA_Variant_setScalar( &params[1].value, &maxRejectedListSize, &UA_TYPES[UA_TYPES_UINT32] );

    UA_KeyValueMap paramsMap;
    paramsMap.map = params;
    paramsMap.mapSize = paramsSize;

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

		UAAccess::Init( *this );

    UAε( UA_ServerConfig_addAllEndpoints(this) );
	}

	α UAConfig::AddSecurityPolicies( ByteStringPtr&& certificate, const ByteStringPtr&& privateKey )ε->void{
    UA_ByteString localCertificate = *certificate;
    UA_ByteString localPrivateKey  = *privateKey;

    // Load the private key and convert to the DER format. Use an empty password on the first try -- maybe the key does not require a password.
    UA_ByteString decryptedPrivateKey = UA_BYTESTRING_NULL;
    UA_ByteString keyPassword = UA_BYTESTRING_NULL;
    if ( privateKey->length > 0 )
        UAε( UA_CertificateUtils_decryptPrivateKey(localPrivateKey, keyPassword, &decryptedPrivateKey) );
    /* Basic256Sha256 */
    UAε( UA_ServerConfig_addSecurityPolicyBasic256Sha256(this, &localCertificate,&decryptedPrivateKey) );

    //UAε( UA_ServerConfig_addSecurityPolicyAes256Sha256RsaPss(this, &localCertificate, &decryptedPrivateKey) );
    //UAε( UA_ServerConfig_addSecurityPolicyAes128Sha256RsaOaep(this, &localCertificate, &decryptedPrivateKey) );
    UAε( UA_ServerConfig_addSecurityPolicyNone(this, &localCertificate) );
    //UAε( UA_ServerConfig_addSecurityPolicyEccNistP256(this, &localCertificate, &decryptedPrivateKey);
    UA_ByteString_memZero( &decryptedPrivateKey );
    UA_ByteString_clear( &decryptedPrivateKey );
	}
}