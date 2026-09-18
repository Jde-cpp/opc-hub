#pragma once
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/opc/uatypes/Logger.h>
#include <jde/opc/uatypes/NodeId.h>

namespace Jde::Opc::Emulator{
	//The emulator's session on the OpcServer - the client half of a UA-enabled PLC.  Single-threaded: every call runs on
	//the emulator's loop thread and the data-change callbacks fire inside Iterate().  The recipe - policies
	//[None, Basic256Sha256], the two applicationUris, the token's auth policy, setDefault LAST - is
	//the gateway's UAClient::Create/Configuration, the one configuration this server is known to accept.
	struct EmulatorClient final : noncopyable{
		//applicationUri: this client's identity - advertised, and the SAN of `certificate`.  serverApplicationUri: the endpoint
		//filter, the server's own uri; empty takes any endpoint.
		EmulatorClient( string url, string applicationUri, string serverApplicationUri, const Crypto::CryptoSettings& certificate, string issuedToken, SRCE )ε;
		~EmulatorClient();
		α Connect( SRCE )ε->void;//synchronous - returns with the session activated or throws.
		α Disconnect()ι->void;
		α IsActivated()Ι->bool;
		α Iterate( uint32 timeoutMs )ι->UA_StatusCode;
		α Namespace( sv uri, SRCE )ε->NsIndex;
		α Resolve( sv path, NsIndex defaultNs, const flat_map<string,NsIndex>& nsAliases, SRCE )ε->NodeId;//browse path from Objects, see BrowsePath.
		α Write( const NodeId& node, const UA_Variant& value, SRCE )ε->void;
		α CreateSubscription( Duration publishingInterval, SRCE )ε->UA_UInt32;
		α DeleteSubscription( UA_UInt32 subscription )ι->void;//before a clean Disconnect - see the definition.
		α Monitor( UA_UInt32 subscription, const NodeId& node, Duration samplingInterval, void* context, UA_Client_DataChangeNotificationCallback callback, SRCE )ε->UA_UInt32;
		α Url()Ι->str{ return _url; }
	private:
		α Configure( const UA_ByteString& certificate, const UA_ByteString& privateKey, SL sl )ε->void;
		Ω StateCallback( UA_Client* ua, UA_SecureChannelState channelState, UA_SessionState sessionState, UA_StatusCode connectStatus )ι->void;
		string _url, _applicationUri, _serverApplicationUri, _token;
		Logger _logger;
		UA_ClientConfig _config{};//newWithConfig shallow-copies this; UA_Client_delete clears the client's copy - never UA_ClientConfig_clear(&_config).
		UA_Client* _ptr{};
	};
}
