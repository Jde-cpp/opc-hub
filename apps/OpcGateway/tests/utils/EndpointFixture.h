#pragma once
//The gateway's endpoint over groups of the embedded OpcServer's pump nodes, as the served-node suites drive it (GatewayNodesTests,
//GatewayValuesTests, GatewayHistoryTests, GatewayWritesTests):  a UA client on it logged in by this program's hub session, the served
//nodes found by their paths, and source variables of types the pumps don't have.
#include <open62541/client_highlevel.h>
#include <jde/tests/WaitUntil.h>
#include <jde/opc/tests/UATestClient.h>
#include <jde/opc/server/UAServer.h>
#include "../../src/server/GatewayServer.h"
#include "HistGroupFixture.h"
#ifndef let
#define let const auto
#endif

namespace Jde::Opc::Gateway::Tests{
	using Opc::Tests::UATestClient;
	constexpr UA_UInt16 _own{ 1 };//the endpoint's own namespace, the folders' (spec *Namespaces*).

	struct EndpointFixture : HistGroupFixture{
		α SetUp()->void override{
			HistGroupFixture::SetUp();
			if( !IsSkipped() && !GatewayServer::Instance() )
				GTEST_SKIP() << "The gateway serves no endpoint:  /gateway/server.";
		}
		Ω Ssl()ι->Crypto::CryptoSettings{ return Crypto::CryptoSettings{ Json::FindDefaultObject(Settings::AsObject("/http/gateway"), "ssl"), {} }; }
		Ω Url()ι->string{ return Ƒ( "opc.tcp://127.0.0.1:{}", Settings::FindNumber<PortType>("/gateway/server/port").value_or(4841) ); }
		Ω Issued()ι->UA_ExtensionObject{ return UATestClient::Issued( Ƒ("{:x}", AppClient()->SessionId()) ); }//this program's hub session:  the suite's user.
		Ω Client()ε->up<UATestClient>{
			auto y = mu<UATestClient>( Url(), Ssl(), Issued() );
			y->ConnectOrThrow();
			return y;
		}
		//The endpoint's index for a connection's remote URI, as its NamespaceArray has it;  none until a node of that namespace is served.
		Ω Mapped( sv remote, const ServerCnnctnNK& slug=OpcServerSlug )ι->optional<UA_UInt16>{
			let uri = GatewayServer::NamespaceUri( slug, remote );
			for( let& [index, ns] : GatewayServer::Instance()->Server()->Namespaces() ){
				if( ns==uri )
					return (UA_UInt16)index;
			}
			return nullopt;
		}
		//A pump member's served node by its path, Objects/<slug>/<pump>/<name>, each segment in the pumps' mapped namespace.
		Ω Served( UATestClient& client, sv pump, sv name, const ServerCnnctnNK& slug=OpcServerSlug, sv remote=PumpsUri )ε->optional<NodeId>{
			let ns = Mapped( remote, slug );
			return ns ? client.Translate( NodeId::ObjectsFolder(), {{_own, slug}, {*ns, string{pump}}, {*ns, string{name}}} ) : nullopt;
		}
		Ω Folder( UATestClient& client, sv pump, const ServerCnnctnNK& slug=OpcServerSlug )ε->optional<NodeId>{
			let ns = Mapped( PumpsUri, slug );
			return ns ? client.Translate( NodeId::ObjectsFolder(), {{_own, slug}, {*ns, string{pump}}} ) : nullopt;
		}
		Ω Double( const Value& v )ι->optional<double>{ return v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_DOUBLE]) ? optional<double>{ *(UA_Double*)v.value.data } : nullopt; }
		Ω UserAccessLevel( UATestClient& client, const NodeId& node )ε->UA_Byte{
			UA_Byte y{};
			UAε( UA_Client_readUserAccessLevelAttribute(client, node, &y) );
			return y;
		}
		//A served node under the connection folder by its name alone:  a member the source holds under Objects.
		Ω ServedRoot( UATestClient& client, sv name )ε->optional<NodeId>{
			let ns = Mapped( PumpsUri );
			return ns ? client.Translate( NodeId::ObjectsFolder(), {{_own, OpcServerSlug}, {*ns, string{name}}} ) : nullopt;
		}
		//A variable of the embedded OpcServer under Objects, in the pumps' namespace, for a member of a type the pumps don't have, with
		//`accessLevel`:  writable, and with the status and timestamp bits where a test writes those.  A repeat of the test finds it,
		//and puts `value` back over the last run's write.
		Ω AddSourceVariable( UA_UInt32 node, const char* name, UA_UInt32 dataType, void* value, const UA_DataType& type, UA_Byte accessLevel=UA_ACCESSLEVELMASK_READ|UA_ACCESSLEVELMASK_WRITE )ε->void{
			auto ua = Server::GetUAServer().Ptr();
			if( UA_NodeClass c; !UA_Server_readNodeClass(ua, Node(node), &c) ){
				UA_Variant initial; UA_Variant_setScalar( &initial, value, &type );
				UAε( UA_Server_writeValue(ua, Node(node), initial) );
				return;
			}
			auto attributes = UA_VariableAttributes_default;
			UA_Variant_setScalar( &attributes.value, value, &type );
			attributes.dataType = UA_NODEID_NUMERIC( 0, dataType );
			attributes.displayName = UA_LOCALIZEDTEXT( (char*)"", (char*)name );
			attributes.accessLevel = accessLevel;
			UAε( UA_Server_addVariableNode(ua, Node(node), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(_ns, (char*)name), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
		}
		//Until the collector has described the member - its path written - which Describe does once its item is made.
		Ω WaitPath( const sp<HistGroupHost>& host, uint32 index )ε->void{
			Stopwatch sw;
			while( !host->Attributes(index) || !host->Attributes(index)->Path )
				sw.CheckTimeout( 15s, 10ms );
		}
	};
}