#include "../src/GatewayAppClient.h"
#include "../src/async/ConnectAwait.h"
#include "../src/UAClient.h"
#include "../src/auth/OpcServerSession.h"
#include "utils/ITest.h"
#include "utils/helpers.h"
#include <jde/fwk/co/BlockAwait.h>

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	//Historian 5a (#222):  the collector's own credential (spec *Collector contract*) - the same token as a session's, keyed apart,
	//so UAClient::GetClient hands it a client of its own and never a screen's.
	TEST( CredentialTests, TheKeyIsApartFromEverySession ){
		Credential token{ Token{"abc"} };
		Credential user{ User{"user1", "0123456789ABCD"} };
		Credential cert{ Crypto::PublicKey{} };
		for( let& cred : {Credential{}, token, user, cert} ){
			let collector = Credential::Collector( cred );
			EXPECT_TRUE( collector.IsCollector() );
			EXPECT_FALSE( cred.IsCollector() );
			EXPECT_EQ( collector.Type(), cred.Type() ) << "the token is the session's - only the key differs";
			EXPECT_NE( collector, cred ) << cred.ToString();
			EXPECT_EQ( collector, Credential::Collector(cred) );
			EXPECT_TRUE( cred<collector ) << "ordered apart, so a map keyed by credential holds both";
			EXPECT_FALSE( collector<cred );
			EXPECT_NE( collector.ToString().find("(collector)"), string::npos ) << collector.ToString();
			flat_map<Credential,int> keyed{ {cred, 1}, {collector, 2} };
			EXPECT_EQ( keyed.size(), 2u );
			EXPECT_EQ( keyed.at(cred), 1 );
			EXPECT_EQ( keyed.at(collector), 2 );
		}
		EXPECT_FALSE( Credential::Collector(token)==Credential::Collector(user) ) << "two collector credentials still differ by token";
	}

	struct CollectorTests : ITest{};
	//A collector connect beside the session's:  a second client on the connection, and each key finds only its own.
	TEST_F( CollectorTests, TheCollectorGetsItsOwnClient ){
		let cred = SessionCredential( Jwt().UserPK );
		ASSERT_EQ( UAClient::Find(OpcServerSlug, cred), _client ) << "the suite's client is the session's";
		let sessionsBefore = SessionCounts().size();
		auto collector = BlockAwait( ConnectAwait{string{OpcServerSlug}, Credential::Collector(cred)} );
		ASSERT_TRUE( collector );
		ASSERT_NE( collector, _client ) << "the collector was handed the session's client";//ASSERT:  the removal below would take the suite's client.
		EXPECT_TRUE( collector->Credential().IsCollector() );
		EXPECT_EQ( UAClient::Find(OpcServerSlug, cred), _client ) << "the session's lookup found the collector's client";
		EXPECT_EQ( UAClient::Find(OpcServerSlug, Credential::Collector(cred)), collector );
		EXPECT_EQ( UAClient::Find(OpcServerSlug, Credential::Collector(cred)), BlockAwait(ConnectAwait{string{OpcServerSlug}, Credential::Collector(cred)}) ) << "a second collector connect reuses its client";
		EXPECT_EQ( SessionCounts().size(), sessionsBefore ) << "the collector is no web session";
		UAClient::RemoveClient( move(collector) );
		EXPECT_EQ( UAClient::Find(OpcServerSlug, cred), _client ) << "removing the collector's client took the session's";
	}
}