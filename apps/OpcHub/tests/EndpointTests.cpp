//Servers phase 2 (#382):  the hub's own OPC UA endpoint, and the hub-shape login - a short issued token, this process's web
//session, answered in-process from the one session map (HubAppClient::SessionInfoAwait) where a split gateway asks over its socket.
#include <jde/fwk/io/json.h>
#include <jde/tests/WaitUntil.h>
#include <jde/opc/tests/UATestClient.h>
#include "../../OpcGateway/src/server/GatewayServer.h"
#include "../../OpcGateway/tests/utils/helpers.h"
#include "helpers.h"
#define let const auto

namespace Jde::Opc::Hub::Tests{
	using Opc::Tests::UATestClient;
	constexpr UA_UInt16 _own{ 1 };

	struct EndpointTests : ::testing::Test{
		Ω Ssl()ι->Crypto::CryptoSettings{ return Crypto::CryptoSettings{ Json::FindDefaultObject(Settings::AsObject("/http"), "ssl"), {} }; }//the hub's pair:  the endpoint's and the client's.
		Ω Url()ι->string{ return Ƒ( "opc.tcp://{}:{}", Host, Settings::FindNumber<PortType>("/gateway/server/port").value_or(4841) ); }
	};

	TEST_F( EndpointTests, ShortIssuedTokenLogsInThroughTheSessionMap ){
		WebSession session;//a hub session as the page holds one;  its id in hex is the token the gateway's clients present.
		UATestClient client{ Url(), Ssl(), UATestClient::Issued(session.Authorization) };
		ASSERT_EQ( client.Connect(), UA_STATUSCODE_GOOD );
		let connection = Gateway::Tests::GetConnection( Gateway::Tests::OpcServerSlug );
		optional<NodeId> connected;
		ASSERT_TRUE( WaitUntil([&]{ connected = client.Translate( NodeId::ObjectsFolder(), {{_own, connection.Slug}, {_own, "_Connection"}, {_own, "Connected"}} ); return connected.has_value(); }) );
		let value = client.Read( *connected );
		EXPECT_TRUE( value.hasValue && UA_Variant_hasScalarType(&value.value, &UA_TYPES[UA_TYPES_BOOLEAN]) ) << UAException::Message( value.status );
	}

	TEST_F( EndpointTests, ASessionTheMapDoesNotHoldIsRefused ){
		UATestClient client{ Url(), Ssl(), UATestClient::Issued("abcdef") };
		EXPECT_NE( client.Connect(), UA_STATUSCODE_GOOD );
	}
}