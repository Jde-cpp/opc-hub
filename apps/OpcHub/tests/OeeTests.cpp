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
		try{//recomputeOee is registered by its whole name in the hub too (gatewayStartup.cpp).
			Gateway::AppClient()->QuerySync<jobject>( R"(recomputeOee( machine:999999, start:"2026-01-01T00:00:00Z", end:"2026-01-02T00:00:00Z" ){ machine })", {} );
			FAIL() << "a machine that isn't there was recomputed";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::NotFound ) << e.what();
		}
		try{//the engine's tables refuse a generic select by their meta's noSelect, at whatever ladder reaches SelectAwait (#402).
			Gateway::AppClient()->QuerySync<jarray>( R"(oeeIntervals{ id })", {} );
			FAIL() << "a generic select of the engine's table was answered";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::BadRequest );
			EXPECT_NE( string{e.what()}.find("no generic query"), string::npos ) << e.what();
		}
		EXPECT_TRUE( Gateway::AppClient()->QuerySync<jarray>(R"(oeeLines{ id })", {}).empty() ) << "the lines' tables too (#408)";
		try{//a line's rules run in the hub as in the gateway.
			Gateway::AppClient()->QuerySync<jobject>( R"(createOeeLine( name:"hubLine", slug:"hubLine", level:"Plant" ){ id })", {} );
			FAIL() << "a line at no level was made";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::BadRequest ) << e.what();
		}
		try{
			Gateway::AppClient()->QuerySync<jarray>( R"(oee( line:999999, start:"2026-01-01T00:00:00Z", end:"2026-01-02T00:00:00Z" ){ oee })", {} );
			FAIL() << "a line that isn't there was read";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::NotFound ) << e.what();
		}
		try{//and the reads are system tables there too.
			Gateway::AppClient()->QuerySync<jarray>( R"(oee( machine:999999, start:"2026-01-01T00:00:00Z", end:"2026-01-02T00:00:00Z" ){ oee })", {} );
			FAIL() << "a machine that isn't there was read";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::NotFound ) << e.what();
		}
	}
}