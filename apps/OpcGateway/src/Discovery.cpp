#include "Discovery.h"
#include <jde/fwk/io/crc.h>
#include <jde/opc/UAException.h>
#include "async/ConnectionManager.h"

#define let const auto

namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	using ClientPtr = up<UA_Client, decltype(&::UA_Client_delete)>;

	α Discovery::ParseUrl( str url )ι->optional<UrlName>{
		if( !url.starts_with("opc.tcp://") )
			return nullopt;
		UA_String host{}, path{}; UA_UInt16 port{ 4840 };
		const UA_String uaUrl{ url.size(), (UA_Byte*)url.data() };//views into `url` - nothing is allocated.
		if( UA_parseEndpointUrl(&uaUrl, &host, &port, &path) || !host.length )
			return nullopt;
		string name{ (const char*)host.data, host.length };
		boost::system::error_code ec;
		if( name.front()=='[' || (boost::asio::ip::make_address(name, ec), !ec) )
			return nullopt;
		return UrlName{ move(name), port, string{(const char*)path.data, path.length} };
	}
	α Discovery::UrlHandle( str url )ι->Jde::Handle{
		return (Jde::Handle)IO::Crc::Calc32c( url ) << 32;
	}
	α Discovery::ApplicationTypeName( UA_ApplicationType type )ι->sv{
		switch( type ){
		case UA_APPLICATIONTYPE_SERVER: return "Server";
		case UA_APPLICATIONTYPE_CLIENT: return "Client";
		case UA_APPLICATIONTYPE_CLIENTANDSERVER: return "ClientAndServer";
		case UA_APPLICATIONTYPE_DISCOVERYSERVER: return "DiscoveryServer";
		default: return {};
		}
	}
	//The discovery client for `url`, its channel preset to it:  with config.endpoint empty, the connect UA_Client_getEndpoints and
	//UA_Client_findServers make fetches the endpoints and then SELECTS one for its own channel (ua_client_connect.c
	//endpointUnconfigured), and a None-only client finds none at a url whose endpoints are all secured - External publishes its None
	//endpoint under the hostname url alone - so the very listing that would explain the failure fails the same way.  A configured
	//endpoint skips the selection (connectIterate: "an exact endpoint was configured"), and the discovery services are what every
	//server answers on a None channel.  `logger` outlives the client:  the config keeps a pointer to it.
	Ω discoveryClient( str url, Duration timeout, Logger& logger, SL sl )ε->ClientPtr{
		//The name looked up with a deadline first:  open62541's own getaddrinfo has none (#353 review #8).
		if( let name = Discovery::ParseUrl(url); name && AsioConnectionManager::Resolve(name->Host, name->Port, 3s).empty() )
			throw Exception{ sl, {EHttpStatus::BadGateway}, "'{}' did not resolve within 3 s.", name->Host };
		UA_ClientConfig config{};
		config.logging = &logger;
		if( let sc = UA_ClientConfig_setDefault(&config); sc ){
			UA_ClientConfig_clear( &config );
			throw UAException{ sc, Ƒ("Could not configure a discovery client for '{}'", url), {.HttpStatus=EHttpStatus::InternalServerError}, sl };
		}
		config.timeout = (UA_UInt32)std::clamp<int64_t>( duration_cast<milliseconds>(timeout).count(), 1, (int64_t)std::numeric_limits<UA_UInt32>::max() );//the connect's deadline and the call's (connectSync).
		config.endpoint.endpointUrl = UA_STRING_ALLOC( url.c_str() );
		config.endpoint.securityMode = UA_MESSAGESECURITYMODE_NONE;
		UA_String_copy( &UA_SECURITY_POLICY_NONE_URI, &config.endpoint.securityPolicyUri );
		ClientPtr client{ UA_Client_newWithConfig(&config), ::UA_Client_delete };//takes a copy - from here the client owns the config, and UA_Client_delete clears it.
		if( !client ){
			UA_ClientConfig_clear( &config );
			throw Exception{ sl, {EHttpStatus::InternalServerError}, "Could not create a discovery client for '{}'.", url };
		}
		return client;
	}
	//An array a discovery service allocated, freed with its scope.
	template<class T>
	struct UAArray final{
		UAArray( const UA_DataType& type )ι:_type{type}{}
		~UAArray(){ UA_Array_delete( Data, Size, &_type ); }
		α Span()Ι->std::span<const T>{ return { Data, Size }; }
		T* Data{}; size_t Size{};
	private:
		const UA_DataType& _type;
	};

	α Discovery::FindServers( str url, Duration timeout, Jde::Handle h, SL sl )ε->vector<Server>{
		Logger logger{ h };
		auto client = discoveryClient( url, timeout, logger, sl );
		UAArray<UA_ApplicationDescription> found{ UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION] };
		if( let sc = UA_Client_findServers(client.get(), url.c_str(), 0, nullptr, 0, nullptr, &found.Size, &found.Data); sc )
			throw UAException{ sc, Ƒ("'{}' did not answer FindServers", url), {.HttpStatus=EHttpStatus::BadGateway}, sl };
		vector<Server> y;
		for( let& desc : found.Span() ){
			Server server{ ToString(desc.applicationUri), ToString(desc.productUri), ToString(desc.applicationName.text), desc.applicationType, ToString(desc.gatewayServerUri), ToString(desc.discoveryProfileUri), {} };
			for( let& discoveryUrl : std::span{desc.discoveryUrls, desc.discoveryUrlsSize} )
				server.DiscoveryUrls.emplace_back( ToString(discoveryUrl) );
			INFO( "[{}]FindServers '{}' ({}) at [{}]", hex(h), server.ApplicationUri, ApplicationTypeName(server.Type), Str::Join(server.DiscoveryUrls) );
			y.push_back( move(server) );
		}
		return y;
	}

	α Discovery::GetEndpoints( str url, Duration timeout, Jde::Handle h, SL sl )ε->vector<Endpoint>{
		Logger logger{ h };
		auto client = discoveryClient( url, timeout, logger, sl );
		UAArray<UA_EndpointDescription> found{ UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION] };
		if( let sc = UA_Client_getEndpoints(client.get(), url.c_str(), &found.Size, &found.Data); sc )
			throw UAException{ sc, Ƒ("'{}' did not answer GetEndpoints", url), {.HttpStatus=EHttpStatus::BadGateway}, sl };
		vector<Endpoint> y;
		for( let& ep : found.Span() ){
			Endpoint endpoint{ ToString(ep.endpointUrl), ToString(ep.server.applicationUri), ToString(ep.securityPolicyUri), ep.securityMode, ep.securityLevel, {}, {} };
			for( let& utp : std::span{ep.userIdentityTokens, ep.userIdentityTokensSize} )
				endpoint.TokenPolicies.push_back( Endpoint::TokenPolicy{ToTokenType(utp.tokenType), utp.securityPolicyUri.length ? ToString(utp.securityPolicyUri) : endpoint.PolicyUri, ToString(utp.policyId)} );
			let der = std::as_bytes( std::span{ep.serverCertificate.data, ep.serverCertificate.length} );
			endpoint.Certificate.assign( der.begin(), der.end() );
			y.push_back( move(endpoint) );
		}
		return y;
	}
}