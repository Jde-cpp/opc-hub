#pragma once
//The gateway's endpoint over groups of the embedded OpcServer's pump nodes, as the served-node suites drive it (GatewayNodesTests,
//GatewayValuesTests):  a UA client on it logged in by this program's hub session, and the served nodes found by their paths.
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
		//Until the collector has described the member - its path written - which Describe does once its item is made.
		Ω WaitPath( const sp<HistGroupHost>& host, uint32 index )ε->void{
			Stopwatch sw;
			while( !host->Attributes(index) || !host->Attributes(index)->Path )
				sw.CheckTimeout( 15s, 10ms );
		}
	};
}