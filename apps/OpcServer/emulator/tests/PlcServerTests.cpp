//PlcServer - the emulated PLC's own UA server:  the loopback bind (#5), the contract's field map, the local write the
//publisher samples.  One server per suite on a test port; PubSubTests' Publisher is the same shape minus the class.
#include <thread>
#include <jde/fwk/settings.h>
#include <jde/opc/uatypes/UAString.h>
#include "../PlcServer.h"

#define let const auto
namespace Jde::Opc::Emulator::Tests{
	struct PlcServerTests : ::testing::Test{
	protected:
		Ω nodeset()ε->fs::path{
			let p = Settings::FindPath( "/emulator/plc/nodeset" ); THROW_IF( !p, "/emulator/plc/nodeset is required." );
			return *p;
		}
		Ω contract()ε->PubSub::Config{ return PubSub::Config{ Settings::AsObject("/emulator/pubsub") }; }
		Ω SetUpTestCase()ε->void{
			_plc = mu<PlcServer>( Settings::FindNumber<UA_UInt16>("/emulator/plc/port").value_or(4851), Settings::FindString("/emulator/plc/bind").value_or("127.0.0.1"), nodeset(), contract() );
		}
		Ω TearDownTestCase()ι->void{ _plc.reset(); }
		Ω serverUrls( const PlcServer& plc )ι->vector<string>{
			vector<string> y;
			let& config = *UA_Server_getConfig( plc.Ptr() );
			for( size_t i=0; i<config.serverUrlsSize; ++i )
				y.push_back( ToString(config.serverUrls[i]) );
			return y;
		}
		Ω readDouble( const NodeId& node )ι->optional<double>{
			optional<double> y;
			UA_Variant v; UA_Variant_init( &v );
			if( UA_Server_readValue(_plc->Ptr(), node, &v)==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&v, &UA_TYPES[UA_TYPES_DOUBLE]) )
				y = *(UA_Double*)v.data;
			UA_Variant_clear( &v );
			return y;
		}
		Ω read( const NodeId& node )ι->UA_DataValue{//the whole DataValue, as the publisher samples it; the caller clears it.
			UA_ReadValueId id; UA_ReadValueId_init( &id );
			id.nodeId = node;
			id.attributeId = UA_ATTRIBUTEID_VALUE;
			return UA_Server_read( _plc->Ptr(), &id, UA_TIMESTAMPSTORETURN_NEITHER );
		}
		static up<PlcServer> _plc;
	};
	up<PlcServer> PlcServerTests::_plc;

	//#5:  setMinimal leaves "opc.tcp://:<port>" - every interface, anonymous-full over a writable nodeset.
	TEST_F( PlcServerTests, BindsLoopbackByDefault ){
		EXPECT_EQ( serverUrls(*_plc), vector<string>{ Ƒ("opc.tcp://127.0.0.1:{}", _plc->Port()) } );
	}
	TEST_F( PlcServerTests, AnEmptyBindIsEveryInterface ){//the deliberately LAN-visible demo - it WARNs, and it is opt-in.
		PlcServer wide{ 4853, "", nodeset(), contract() };
		EXPECT_EQ( serverUrls(wide), vector<string>{"opc.tcp://:4853"} );
	}
	TEST_F( PlcServerTests, FindFieldFollowsTheContract ){
		ASSERT_EQ( _plc->Contract().Fields.size(), 5u );
		EXPECT_EQ( _plc->FindField("pump1.motorRpm"), optional<uint>{0} );
		EXPECT_EQ( _plc->FindField("pumpManual.motorRpm"), optional<uint>{4} );
		EXPECT_FALSE( _plc->FindField("pump1.status") );//subscribed, never published
		EXPECT_FALSE( _plc->FindField("nothing.here") );
	}
	TEST_F( PlcServerTests, WriteLandsInTheLocalNodeThePublisherSamples ){
		_plc->Write( 0, 1234.5 );
		_plc->Write( 4, 42 );
		EXPECT_EQ( readDouble(_plc->Contract().Fields[0].Node), optional<double>{1234.5} );
		EXPECT_EQ( readDouble(_plc->Contract().Fields[4].Node), optional<double>{42} );
	}
	//The quality is stored with the value - Bad included, because the OpcServer's reader skips a field without one - and
	//the next Good write replaces it.
	TEST_F( PlcServerTests, WriteCarriesTheStatus ){
		let& node = _plc->Contract().Fields[1].Node;
		constexpr UA_StatusCode pinnedHigh{ UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED | 0x0400 | 0x0200 };//InfoType DataValue + LimitBits High
		for( let status : {pinnedHigh, (UA_StatusCode)UA_STATUSCODE_BADSENSORFAILURE, (UA_StatusCode)UA_STATUSCODE_GOOD} ){
			_plc->Write( 1, 777, status );
			auto dv = read( node );
			EXPECT_EQ( dv.status, status ) << UA_StatusCode_name( status );
			ASSERT_TRUE( dv.hasValue && UA_Variant_hasScalarType(&dv.value, &UA_TYPES[UA_TYPES_DOUBLE]) ) << UA_StatusCode_name( status );
			EXPECT_EQ( *(UA_Double*)dv.value.data, 777 );
			UA_DataValue_clear( &dv );
		}
	}
	TEST_F( PlcServerTests, WriteRefusesAnUnknownField ){
		EXPECT_ANY_THROW( _plc->Write(99, 1) );
	}
	TEST_F( PlcServerTests, IterateDrivesThePublisherWithoutAListener ){//the writer sends UADP to a port nothing reads: a send, not an error.
		for( uint i=0; i<5; ++i ){
			EXPECT_NO_THROW( _plc->Iterate() );
			std::this_thread::sleep_for( 20ms );
		}
	}
}
