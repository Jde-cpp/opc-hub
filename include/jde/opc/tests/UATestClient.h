#pragma once
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>
#include <open62541/plugin/certificategroup_default.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/uatypes/UAString.h>
#include <jde/opc/uatypes/opcHelpers.h>
#ifndef let
#define let const auto
#endif

namespace Jde::Opc::Tests{
	//The synchronous open62541 client the OpcServer's HistoryTests connected with, hoisted for the gateway's endpoint suites too
	//(docs/servers/plan.md Phase 2):  a session on `url` with `ssl`'s pair as the client's own certificate and the policies that
	//pair gives, the channel None unless `mode` says otherwise, and the identity the caller makes - Issued, a hub session as the
	//gateway's clients present one;  Certificate, the pair itself as an x509 token;  Username;  Anonymous.  The server's
	//certificate is accepted unseen, since the suites' servers are the process's own.  Not the gateway's UAClient, which would need a
	//server_connections row for the endpoint and its certificate in its own trust list.
	struct UATestClient final : noncopyable{
		Ω Issued( str token )ι->UA_ExtensionObject{
			auto t = UA_IssuedIdentityToken_new();
			t->tokenData = UA_BYTESTRING_ALLOC( token.c_str() );
			return wrap( t, UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );
		}
		Ω Certificate( const Crypto::CryptoSettings& ssl )ε->UA_ExtensionObject{
			const auto der = Crypto::ReadCertificate( ssl.Certificate.Path );
			auto t = UA_X509IdentityToken_new();
			UA_ByteString view{ der.size(), (UA_Byte*)der.data() };
			UA_ByteString_copy( &view, &t->certificateData );
			return wrap( t, UA_TYPES[UA_TYPES_X509IDENTITYTOKEN] );
		}
		Ω Username( str name, str password )ι->UA_ExtensionObject{
			auto t = UA_UserNameIdentityToken_new();
			t->userName = UA_STRING_ALLOC( name.c_str() );
			t->password = UA_STRING_ALLOC( password.c_str() );
			return wrap( t, UA_TYPES[UA_TYPES_USERNAMEIDENTITYTOKEN] );
		}
		Ω Anonymous()ι->UA_ExtensionObject{ UA_ExtensionObject y; UA_ExtensionObject_init( &y ); return y; }//the empty token.

		//Built, not connected:  Connect answers the status, for a login a test expects refused.
		UATestClient( string url, const Crypto::CryptoSettings& ssl, UA_ExtensionObject identity, UA_MessageSecurityMode mode=UA_MESSAGESECURITYMODE_NONE )ε:_url{ move(url) }{
			UA_ClientConfig config{};
			auto certificate = ToUAByteString( Crypto::ReadCertificate(ssl.Certificate.Path) );
			auto pem = ToUAByteString( Crypto::ReadPrivateKey(ssl.PrivateKey) );
			UA_ByteString key = UA_BYTESTRING_NULL;
			UAε( UA_CertificateUtils_decryptPrivateKey(*pem, UA_BYTESTRING_NULL, &key) );
			const auto configured = UA_ClientConfig_setDefaultEncryption( &config, *certificate, key, nullptr, 0, nullptr, 0 );
			UA_ByteString_clear( &key );
			UAε( configured );
			config.certificateVerification.clear( &config.certificateVerification );
			UA_CertificateGroup_AcceptAll( &config.certificateVerification );
			config.securityMode = mode;
			if( mode==UA_MESSAGESECURITYMODE_NONE ){
				UA_String_clear( &config.securityPolicyUri );
				UA_String_copy( &UA_SECURITY_POLICY_NONE_URI, &config.securityPolicyUri );
			}
			UA_String_clear( &config.clientDescription.applicationUri );
			config.clientDescription.applicationUri = UA_STRING_ALLOC( ssl.Certificate.SanUri().c_str() );
			UA_ExtensionObject_clear( &config.userIdentityToken );
			config.userIdentityToken = identity;//taken over, with the token it points to.
			_client = UA_Client_newWithConfig( &config );
			THROW_IF( !_client, "UA_Client_newWithConfig failed." );
		}
		~UATestClient(){
			if( _client ){
				UA_Client_disconnect( _client );
				UA_Client_delete( _client );
			}
		}
		α Connect()ι->UA_StatusCode{ return UA_Client_connect( _client, _url.c_str() ); }
		α ConnectOrThrow()ε->void{ UAε( Connect() ); }
		operator UA_Client*()ι{ return _client; }
		α Ptr()ι->UA_Client*{ return _client; }
		α Url()Ι->str{ return _url; }

		//The node's Value, or the read's status when it failed.
		α Read( const UA_NodeId& node )ι->Value{
			UA_Variant v; UA_Variant_init( &v );
			const auto sc = UA_Client_readValueAttribute( _client, node, &v );
			return sc ? Value{ sc } : Value{ move(v) };
		}
		//The node's DataValue with the timestamps asked for - Read asks for neither - or the read's status when the service failed.
		α ReadValue( const UA_NodeId& node, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH )ι->Value{
			UA_ReadValueId id; UA_ReadValueId_init( &id );
			id.nodeId = node;//a view:  not cleared.
			id.attributeId = UA_ATTRIBUTEID_VALUE;
			UA_ReadRequest request; UA_ReadRequest_init( &request );
			request.nodesToRead = &id;
			request.nodesToReadSize = 1;
			request.timestampsToReturn = timestamps;
			auto response = UA_Client_Service_read( _client, request );
			Value y{ response.responseHeader.serviceResult ? Value{ response.responseHeader.serviceResult } : response.resultsSize==1 ? Value{ response.results[0] } : Value{ UA_STATUSCODE_BADUNEXPECTEDERROR } };
			UA_ReadResponse_clear( &response );
			return y;
		}
		//The node's children by its forward hierarchical references, a folder listing:  each browse name and target.
		struct Child final{ string BrowseName; NodeId Id; };
		α Browse( const UA_NodeId& node )ε->vector<Child>{
			UA_BrowseRequest request; UA_BrowseRequest_init( &request );
			request.nodesToBrowse = UA_BrowseDescription_new();
			request.nodesToBrowseSize = 1;
			UA_NodeId_copy( &node, &request.nodesToBrowse[0].nodeId );
			request.nodesToBrowse[0].browseDirection = UA_BROWSEDIRECTION_FORWARD;
			request.nodesToBrowse[0].referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES );
			request.nodesToBrowse[0].includeSubtypes = true;
			request.nodesToBrowse[0].resultMask = UA_BROWSERESULTMASK_ALL;
			auto response = UA_Client_Service_browse( _client, request );
			vector<Child> y;
			auto status = response.responseHeader.serviceResult;
			if( !status && response.resultsSize==1 ){
				status = response.results[0].statusCode;
				for( size_t i=0; !status && i<response.results[0].referencesSize; ++i ){
					const auto& reference = response.results[0].references[i];
					y.push_back( {ToString(reference.browseName.name), NodeId{reference.nodeId.nodeId}} );
				}
			}
			UA_BrowseResponse_clear( &response );
			UA_BrowseRequest_clear( &request );
			UAε( status );
			return y;
		}
		//TranslateBrowsePathsToNodeIds from `start` along `path`, each segment a namespace index and a browse name, over
		//hierarchical references:  the first target, or none.
		α Translate( const UA_NodeId& start, const vector<std::pair<UA_UInt16,string>>& path )ε->optional<NodeId>{
			UA_BrowsePath browsePath; UA_BrowsePath_init( &browsePath );
			UA_NodeId_copy( &start, &browsePath.startingNode );
			browsePath.relativePath.elementsSize = path.size();
			browsePath.relativePath.elements = (UA_RelativePathElement*)UA_Array_new( path.size(), &UA_TYPES[UA_TYPES_RELATIVEPATHELEMENT] );
			for( size_t i=0; i<path.size(); ++i ){
				auto& element = browsePath.relativePath.elements[i];
				element.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES );
				element.includeSubtypes = true;
				element.targetName = UA_QUALIFIEDNAME_ALLOC( path[i].first, path[i].second.c_str() );
			}
			UA_TranslateBrowsePathsToNodeIdsRequest request; UA_TranslateBrowsePathsToNodeIdsRequest_init( &request );
			request.browsePaths = &browsePath;
			request.browsePathsSize = 1;
			auto response = UA_Client_Service_translateBrowsePathsToNodeIds( _client, request );
			optional<NodeId> y;
			auto status = response.responseHeader.serviceResult;
			if( !status && response.resultsSize==1 && response.results[0].statusCode==UA_STATUSCODE_GOOD && response.results[0].targetsSize )
				y.emplace( response.results[0].targets[0].targetId.nodeId );
			UA_TranslateBrowsePathsToNodeIdsResponse_clear( &response );
			UA_BrowsePath_clear( &browsePath );
			UAε( status );
			return y;
		}
		//A subscription publishing every `publishingMs`, its id.
		α Subscribe( double publishingMs=100 )ε->UA_UInt32{
			auto request = UA_CreateSubscriptionRequest_default();
			request.requestedPublishingInterval = publishingMs;
			auto response = UA_Client_Subscriptions_create( _client, request, nullptr, nullptr, nullptr );
			const auto status = response.responseHeader.serviceResult;
			const auto id = response.subscriptionId;
			UA_CreateSubscriptionResponse_clear( &response );
			UAε( status );
			return id;
		}
		//A data-change item on `node` sampled every `samplingMs`, both timestamps asked for:  each notification lands in `into` as the
		//client is pumped - Iterate, or any synchronous call - on the caller's thread, so `into` outlives the client.  The server's
		//revised sampling interval.
		α Monitor( UA_UInt32 subscription, const UA_NodeId& node, double samplingMs, vector<Value>& into )ε->double{
			auto item = UA_MonitoredItemCreateRequest_default( node );//a view of `node`:  not cleared.
			item.requestedParameters.samplingInterval = samplingMs;
			item.requestedParameters.queueSize = 10;
			auto result = UA_Client_MonitoredItems_createDataChange( _client, subscription, UA_TIMESTAMPSTORETURN_BOTH, item, &into, notified, nullptr );
			const auto status = result.statusCode;
			const auto revised = result.revisedSamplingInterval;
			UA_MonitoredItemCreateResult_clear( &result );
			UAε( status );
			return revised;
		}
		α Iterate( UA_UInt32 ms )ι->void{ UA_Client_run_iterate( _client, ms ); }//the client's loop for `ms`:  publish responses in, their notifications out.
	private:
		Ω notified( UA_Client*, UA_UInt32, void*, UA_UInt32, void* context, UA_DataValue* value )ι->void{ static_cast<vector<Value>*>( context )->emplace_back( *value ); }
		Ω wrap( void* token, const UA_DataType& type )ι->UA_ExtensionObject{
			UA_ExtensionObject y; UA_ExtensionObject_init( &y );
			UA_ExtensionObject_setValue( &y, token, &type );
			return y;
		}
		string _url;
		UA_Client* _client{};
	};
}