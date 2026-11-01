#include <open62541/plugin/accesscontrol_default.h>
#include "utils/ClientFixture.h"
#include "utils/TestUaServer.h"
#include "../src/UAClient.h"
#include "../src/auth/OpcServerSession.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	//security-matrix #10:  open62541's own tcp manager connected to the first address a name resolves to and never tried the next,
	//so a name whose first address was dead failed BadConnectionRejected one address short of the server - found as
	//`opc.tcp://JDE-CPP:49320`, IPv6 link-local ahead of IPv4 against External, which listens on IPv4 alone;  the asio manager (#526)
	//tries each in turn, and the probe stays for an address that drops the SYN, which it counts as dead after three seconds.  The hermetic form of it:  `localhost` resolves to ::1
	//first on windows, and the server here is bound to 127.0.0.1.  UAClient::ReachableUrl probes the first address of a name with
	//several and, only when it takes no connection, substitutes the first that does.  Where `localhost` resolves to 127.0.0.1
	//first the url comes back as given and the connect works as it always did - the test passes either way, and says which.
	class HostNameTests : public ClientFixture{
	protected:
		static constexpr EThrowawayPort Port{ EThrowawayPort::HostName };
		Ω TearDownTestSuite()->void{
			PurgeServerCnnctn( Slug );
		}
		inline static const string Slug{ "opcTestsHostName" };
	};
	TEST_F( HostNameTests, AnAddressOrAnUnknownNameComesBackAsGiven ){
		EXPECT_EQ( UAClient::ReachableUrl("opc.tcp://127.0.0.1:4840", 0), "opc.tcp://127.0.0.1:4840" );
		EXPECT_EQ( UAClient::ReachableUrl("opc.tcp://[::1]:4840/path", 0), "opc.tcp://[::1]:4840/path" );
		EXPECT_EQ( UAClient::ReachableUrl("opc.tcp://no-such-host.invalid:4840", 0), "opc.tcp://no-such-host.invalid:4840" );
		EXPECT_EQ( UAClient::ReachableUrl("not a url", 0), "not a url" );
	}

	TEST_F( HostNameTests, ANameWhoseFirstAddressIsDeadStillConnects ){
		TestUaServer ipv4Only{ Port, []( UA_ServerConfig& config ){//TestUaServer binds 127.0.0.1 - nothing listens on ::1.
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, nullptr) );
			TestUaServer::Anonymous( config );//nothing here is about credentials.
		}};
		let url = Url( Port, "localhost" );
		let reachable = UAClient::ReachableUrl( url, 0 );
		INFO( "MATRIX|hostname|{}|{}", url, reachable==url ? "the first address answers - the url is used as given" : reachable );
		EXPECT_TRUE( reachable==url || reachable==Url(Port) ) << reachable;
		EXPECT_EQ( UAClient::ReachableUrl(Url(Port, "localhost")+"/some/path", 0).ends_with("/some/path"), true );

		GetConnection( Slug, url, "" );
		Attempt( Slug, Credential{} );
		ASSERT_FALSE( _exception ) << _exception->what();//BadConnectionRejected, until #10:  open62541 stopped at ::1.
		ASSERT_TRUE( _client );
		EXPECT_EQ( "ok (None/None)", Negotiated(_client) );
		EXPECT_EQ( _client->Url(), url );//the connection keeps the name it was given...
		EXPECT_EQ( _client->ConnectUrl(), reachable );//...and connects where the probe said to.
	}
}
