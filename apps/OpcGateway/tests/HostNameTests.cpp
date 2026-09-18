#include <open62541/plugin/accesscontrol_default.h>
#include "utils/helpers.h"
#include "utils/TestUaServer.h"
#include "../src/UAClient.h"
#include "../src/auth/OpcServerSession.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	//security-matrix #10:  open62541 connects to the first address a name resolves to and never tries the next, so a name whose
	//first address is dead fails BadConnectionRejected one address short of the server - found as `opc.tcp://JDE-CPP:49320`, IPv6
	//link-local ahead of IPv4 against Kepware, which listens on IPv4 alone.  The hermetic form of it:  `localhost` resolves to ::1
	//first on windows, and the server here is bound to 127.0.0.1.  UAClient::ReachableUrl probes the first address of a name with
	//several and, only when it takes no connection, substitutes the first that does.  Where `localhost` resolves to 127.0.0.1
	//first the url comes back as given and the connect works as it always did - the test passes either way, and says which.
	class HostNameTests : public ::testing::Test{
	protected:
		static constexpr uint16_t Port{ 4858 };//4855-4857 the other throwaway servers.
		Ω TearDownTestSuite()->void{
			if( auto row = SelectServerCnnctn(Slug); row )
				PurgeServerCnnctn( row->Id );
		}
		α TearDown()ι->void override{
			if( _client )
				UAClient::RemoveClient( move(_client) );
		}
		α Connect()ι->ConnectAwait::Task;
		inline static const string Slug{ "opcTestsHostName" };
		up<Exception> _exception;
		sp<UAClient> _client;
		atomic_flag _done;
	};
	α HostNameTests::Connect()ι->ConnectAwait::Task{
		try{
			_client = co_await UAClient::GetClient( Slug, Credential{} );
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		_done.test_and_set();
		_done.notify_all();
	}

	TEST_F( HostNameTests, AnAddressOrAnUnknownNameComesBackAsGiven ){
		EXPECT_EQ( UAClient::ReachableUrl("opc.tcp://127.0.0.1:4840", 0), "opc.tcp://127.0.0.1:4840" );
		EXPECT_EQ( UAClient::ReachableUrl("opc.tcp://[::1]:4840/path", 0), "opc.tcp://[::1]:4840/path" );
		EXPECT_EQ( UAClient::ReachableUrl("opc.tcp://no-such-host.invalid:4840", 0), "opc.tcp://no-such-host.invalid:4840" );
		EXPECT_EQ( UAClient::ReachableUrl("not a url", 0), "not a url" );
	}

	TEST_F( HostNameTests, ANameWhoseFirstAddressIsDeadStillConnects ){
		TestUaServer ipv4Only{ Port, []( UA_ServerConfig& config ){//TestUaServer binds 127.0.0.1 - nothing listens on ::1.
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, nullptr) );
			UAε( UA_AccessControl_default(&config, true, nullptr, 0, nullptr) );//anonymous:  nothing here is about credentials.
			UAε( UA_ServerConfig_addAllEndpoints(&config) );
		}};
		let url = Ƒ( "opc.tcp://localhost:{}", Port );
		let reachable = UAClient::ReachableUrl( url, 0 );
		INFO( "MATRIX|hostname|{}|{}", url, reachable==url ? "the first address answers - the url is used as given" : reachable );
		EXPECT_TRUE( reachable==url || reachable==Ƒ("opc.tcp://127.0.0.1:{}", Port) ) << reachable;
		EXPECT_EQ( UAClient::ReachableUrl(Ƒ("opc.tcp://localhost:{}/some/path", Port), 0).ends_with("/some/path"), true );

		GetConnection( Slug, url, "" );
		Connect();
		_done.wait( false );
		ASSERT_FALSE( _exception ) << _exception->what();//BadConnectionRejected, until #10:  open62541 stopped at ::1.
		ASSERT_TRUE( _client );
		EXPECT_EQ( "ok (None/None)", Negotiated(_client) );
		EXPECT_EQ( _client->Url(), url );//the connection keeps the name it was given...
		EXPECT_EQ( _client->ConnectUrl(), reachable );//...and connects where the probe said to.
	}
}
