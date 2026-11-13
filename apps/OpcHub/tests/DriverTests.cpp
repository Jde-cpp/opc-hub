//Open Protocol phase 4 (#466), the hub's shape:  a driver row's server inside the hub, on ports of this suite's own
//(Opc.Hub.Tests.jsonnet).  The collector's session, which presents the process's pair, opens by the server's own-certificate
//rule and no lookup, as the user the hub enrolls for that certificate (hubStartup.cpp), the one the bundled OpcServer's lookup
//finds, not the hub's own, the system's.  And a web session's short issued token is answered from this process's session map,
//as the endpoint's is (EndpointTests).
#include <jde/fwk/process/execution.h>
#include <jde/ql/ql.h>
#include "../../OpcGateway/src/GatewayAppClient.h"
#include "../../OpcGateway/tests/utils/DriverRows.h"
#include "../../OpcGateway/src/hist/HistHost.h"
#include "helpers.h"
#define let const auto

namespace Jde::Opc::Hub::Tests{
	using namespace Gateway::Tests;
	using Gateway::Credential;
	using Gateway::DriverHost;
	using Gateway::HistHost;
	using Gateway::ServerCnnctn;
	using OpenProtocol::Controller;

	struct DriverTests : ::testing::Test{};

	TEST_F( DriverTests, TheCollectorsSessionOpensAsItsCertificatesUser ){
		Controller station{ *Executor(), ControllerPort(), {.Identity={ .ControllerName="Hub station", .SystemType=3 }} };
		const string slug{ "hub-driver" };
		PurgeServerCnnctn( slug );
		let created = CreateDriverRow( slug );
		ASSERT_TRUE( created ) << created.error();
		let id = *created;
		absl::Cleanup purge = [id]{ PurgeServerCnnctn( id ); };
		let host = WaitHost( slug );
		ASSERT_TRUE( host ) << "the row's server never started";
		EXPECT_EQ( ControllerPort(), 4745 ) << "the suite's own, clear of the gateway suite's 4645";
		EXPECT_EQ( host->Port(), 5170+id ) << "the suite's own range, clear of a live hub's 4870 and the gateway suite's 4980";
		let sessions = [&]( sv type ){
			vector<Opc::Server::UAAccess::Session> y;
			for( auto& session : host->Sessions() ){
				if( session.Type==type )
					y.push_back( move(session) );
			}
			return y;
		};

		let collector = HistHost::Instance().Collector( slug );//the process's pair, for its channel and its token alike.
		ASSERT_TRUE( WaitUntil([&]{
			try{
				return OpFlag( ReadOp(slug, "Connected", collector) )==true;
			}
			catch( const std::exception& ){
				return false;
			}
		}, 15s) ) << "the collector never read the link up";
		let own = sessions( "Certificate" );
		ASSERT_EQ( own.size(), 1u );
		let client = Gateway::AppClient();
		let key = Crypto::ExtractPublicKey( Crypto::ReadCertificate(client->SslSettings->Certificate.Path) );
		let enrolled = client->QuerySync( Ƒ("user( modulus: \"{}\", exponent: {}, deleted: null ){{id}}", key.ModulusHex(), key.ExponentInt()), {} );
		ASSERT_FALSE( enrolled.empty() ) << "the hub enrolls its certificate at its start";
		EXPECT_EQ( own.front().User, Jde::UserPK{QL::AsId<Jde::UserPK::Type>(enrolled)} ) << "the certificate's enrolled user";
		EXPECT_NE( own.front().User, Jde::UserPK{Jde::UserPK::System} ) << "not the hub's own";

		WebSession session;//a hub session as the page holds one;  its id in hex is the token the gateway's clients present.
		Credential token{ session.Authorization };
		EXPECT_EQ( OpText(ReadOp(slug, "Url", token)), ControllerUrl() );
		let web = sessions( "IssuedToken" );
		ASSERT_EQ( web.size(), 1u );
		EXPECT_EQ( web.front().User, Jde::UserPK{1} );
	}
}