#include "../../OpcGateway/src/GatewayAppClient.h"
#include "helpers.h"
#define let const auto

//OEE's tables in the hub (docs/oee/spec.md *Reads*, *Authorization*):  HubQL's mutation ladder calls Gateway::CustomMutation, so
//OeeMAwait refuses through the in-process QL as the gateway's own does.  The rules themselves are the gateway suite's
//(apps/OpcGateway/tests/OeeTests.cpp).
namespace Jde::Opc::Hub::Tests{
	TEST( OeeTests, HubRefusesAsTheGatewayDoes ){
		try{
			Gateway::AppClient()->QuerySync<jobject>( R"(createOeeStop( oeeMachineId:1, start:0 ){ id })", {} );
			FAIL() << "a generic mutation of the engine's table was accepted";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::BadRequest );
			EXPECT_NE( string{e.what()}.find("engine"), string::npos ) << e.what();
		}
		EXPECT_TRUE( Gateway::AppClient()->QuerySync<jarray>(R"(oeeMachines{ id })", {}).empty() ) << "the tables are synced and empty";
	}
}