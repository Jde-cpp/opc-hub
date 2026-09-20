#include <open62541/server_config_default.h>
#include <open62541/server_pubsub.h>
#include <NodesetLoader/backendOpen62541.h>
#include <jde/fwk/settings.h>
#include <jde/opc/UAException.h>
#include <jde/opc/pubsub/PubSub.h>
#include "../src/globals.h"
#include "../src/UAServer.h"
#include "../src/pubsub/PubSubReader.h"

#define let const auto
namespace Jde::Opc::Server::Tests{
	//The pumps nodeset the tests config lists under /opcServer/configFiles - the same file the emulator loads.
	Ω pumpsNodeset()ε->fs::path{
		for( let& p : Settings::FindPathArray("/opcServer/configFiles") ){
			if( p.filename()=="pumps.NodeSet2.xml" )
				return p;
		}
		THROW( "pumps.NodeSet2.xml is not in /opcServer/configFiles." );
	}
	//An in-process Part 14 publisher: its own headless UA_Server holding the pumps nodeset, publishing the contract the
	//OpcServer reads - what the PLC emulator does, minus the emulator.  Port 4850 keeps it off the OpcServer's 4840.
	struct Publisher final : noncopyable{
		Publisher( const jobject& settings, const fs::path& nodeset )ε:
			Contract{ settings }{
			UA_ServerConfig config{};
			UAε( UA_ServerConfig_setMinimal(&config, 4850, nullptr) );
			_server = UA_Server_newWithConfig( &config );
			THROW_IF( !_server, "UA_Server_newWithConfig failed." );
			THROW_IF( !NodesetLoader_loadFile(_server, nodeset.string().c_str(), nullptr), "Publisher could not load '{}'.", nodeset.string() );
			Contract.Resolve( *_server );
			_writer = mu<PubSub::Writer>( *_server, Contract );
			UAε( UA_Server_run_startup(_server) );
		}
		~Publisher(){
			UA_Server_run_shutdown( _server );
			UA_Server_delete( _server );
		}
		α Iterate()ι->void{ UA_Server_run_iterate( _server, false ); }
		α Write( uint field, double value, UA_StatusCode status=UA_STATUSCODE_GOOD )ε->void{//the reading with its quality, as PlcServer::Write.
			UA_WriteValue write; UA_WriteValue_init( &write );
			write.nodeId = Contract.Fields[field].Node;
			write.attributeId = UA_ATTRIBUTEID_VALUE;
			UA_Variant_setScalar( &write.value.value, &value, &UA_TYPES[UA_TYPES_DOUBLE] );
			write.value.hasValue = true;
			write.value.status = status;
			write.value.hasStatus = true;
			UAε( UA_Server_write(_server, &write) );
		}
		PubSub::Config Contract;
	private:
		UA_Server* _server{};
		up<PubSub::Writer> _writer;
	};

	struct PubSubTests : ::testing::Test{
	protected:
		//Startup already built a reader on its server, but sibling fixtures replace the global UAServer (Initialize), so
		//rebuild the OpcServer side here exactly as opcServerStartup does: load, run, subscribe.
		Ω SetUpTestCase()ε->void{
			Server::Initialize( GetSchemaPtr() );
			auto& ua = GetUAServer();
			ua.Load( pumpsNodeset() );
			ua.Run();
			StartPubSub( Settings::AsObject("/opcServer/pubsub") );
		}
		Ω ReadDouble( const NodeId& node )ι->optional<double>{
			optional<double> y;
			UA_Variant v; UA_Variant_init( &v );
			if( UA_Server_readValue(GetUAServer().Ptr(), node, &v)==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&v, &UA_TYPES[UA_TYPES_DOUBLE]) )
				y = *(UA_Double*)v.data;
			UA_Variant_clear( &v );
			return y;
		}
		//The target variable's whole DataValue - UA_Server_readValue returns any non-Good status instead of the value.
		Ω Read( const NodeId& node )ι->std::pair<optional<double>,UA_StatusCode>{
			UA_ReadValueId id; UA_ReadValueId_init( &id );
			id.nodeId = node;
			id.attributeId = UA_ATTRIBUTEID_VALUE;
			auto dv = UA_Server_read( GetUAServer().Ptr(), &id, UA_TIMESTAMPSTORETURN_NEITHER );
			std::pair<optional<double>,UA_StatusCode> y{ {}, dv.status };
			if( dv.hasValue && UA_Variant_hasScalarType(&dv.value, &UA_TYPES[UA_TYPES_DOUBLE]) )
				y.first = *(UA_Double*)dv.value.data;
			UA_DataValue_clear( &dv );
			return y;
		}
	};

	TEST_F( PubSubTests, ReaderTargetsResolveToTheNodeset ){
		let reader = PubSub(); ASSERT_TRUE( reader );
		let& contract = reader->Config();
		ASSERT_EQ( contract.Fields.size(), 5u );
		EXPECT_EQ( contract.Fields[0].Name, "pump1.motorRpm" );
		EXPECT_EQ( *contract.Fields[0].Node.Numeric(), 6012u );
		EXPECT_EQ( *contract.Fields[4].Node.Numeric(), 6054u );
		for( let& f : contract.Fields )
			EXPECT_EQ( f.Type, &UA_TYPES[UA_TYPES_DOUBLE] ) << f.Name;
	}

	//A published value lands in the OpcServer's target variable: the whole UADP path - writer sampling, network message,
	//reader decode against the shared metadata, TargetVariables write.
	TEST_F( PubSubTests, PublishedValueLandsInTargetVariable ){
		let& contract = PubSub()->Config();
		Publisher publisher{ Settings::AsObject("/opcServer/pubsub"), pumpsNodeset() };
		constexpr double expected{ 1234.5 };
		publisher.Write( 0, expected );
		publisher.Write( 4, expected*2 );
		optional<double> got, gotManual;
		for( let deadline = steady_clock::now()+10s; steady_clock::now()<deadline; std::this_thread::sleep_for(50ms) ){
			publisher.Iterate();
			got = ReadDouble( contract.Fields[0].Node );
			gotManual = ReadDouble( contract.Fields[4].Node );
			if( got && *got==expected && gotManual && *gotManual==expected*2 )
				break;
		}
		ASSERT_TRUE( got ) << "pump1.motorRpm never became readable";
		EXPECT_EQ( *got, expected );
		ASSERT_TRUE( gotManual );
		EXPECT_EQ( *gotManual, expected*2 );

		//Second act - sustained delivery.  The first network message is always a keyframe, so the pair above landing proves
		//nothing about the ones after it:  at keyFrameCount=10 the writer sent nine delta frames per keyframe, the reader
		//discards every delta ("Only keyframes are supported", ua_pubsub_reader.c), and this test stayed green while the
		//OpcServer dropped 90 % of what the emulator published (emulator-review #2/T1).  A second pair must land within a
		//couple of publishing intervals:  with keyframes only it takes one;  at 10 the next keyframe is ten away.
		constexpr double second{ 987.25 };
		publisher.Write( 0, second );
		publisher.Write( 4, second*2 );
		let limit = 2*contract.PublishingInterval + 500ms;//two intervals, plus slack for the 50 ms poll and the writer's timer phase.
		let start = steady_clock::now();
		for( ; steady_clock::now()<start+limit; std::this_thread::sleep_for(50ms) ){
			publisher.Iterate();
			got = ReadDouble( contract.Fields[0].Node );
			gotManual = ReadDouble( contract.Fields[4].Node );
			if( got && *got==second && gotManual && *gotManual==second*2 )
				break;
		}
		let took = Chrono::ToString( duration_cast<Duration>(steady_clock::now()-start) );
		EXPECT_EQ( got.value_or(0), second ) << "the second write did not land within " << Chrono::ToString(limit) << " (" << took << ") - delta frames being discarded?  PubSub::Writer keyFrameCount must stay 0.";
		EXPECT_EQ( gotManual.value_or(0), second*2 ) << "pumpManual.motorRpm's second write did not land within " << Chrono::ToString(limit) << " (" << took << ").";
	}

	//A reading's quality (OPC 10000-4 7.38) crosses the wire with it:  the writer's STATUSCODE field content mask sends
	//DataValue-encoded fields, and the reader writes the whole DataValue into the target variable.  Uncertain with its
	//info bits, then Bad - which still carries the value, or the reader would skip the field - then back to Good, each
	//within a couple of publishing intervals.  Ends Good so the node is left as the other tests expect it.
	TEST_F( PubSubTests, PublishedStatusLandsInTargetVariable ){
		let& contract = PubSub()->Config();
		Publisher publisher{ Settings::AsObject("/opcServer/pubsub"), pumpsNodeset() };
		let& node = contract.Fields[1].Node;
		let limit = 2*contract.PublishingInterval + 500ms;
		constexpr UA_StatusCode pinnedHigh{ UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED | 0x0400 | 0x0200 };//InfoType DataValue + LimitBits High
		double value{ 1500 };
		for( let status : {pinnedHigh, (UA_StatusCode)UA_STATUSCODE_BADSENSORFAILURE, (UA_StatusCode)UA_STATUSCODE_GOOD} ){
			publisher.Write( 1, ++value, status );
			std::pair<optional<double>,UA_StatusCode> got;
			//the first message of a fresh publisher may take longer than an interval to go out - the first act's 10 s covers it.
			for( let deadline = steady_clock::now()+( status==pinnedHigh ? Duration{10s} : Duration{limit} ); steady_clock::now()<deadline; std::this_thread::sleep_for(50ms) ){
				publisher.Iterate();
				got = Read( node );
				if( got.first==optional<double>{value} && got.second==status )
					break;
			}
			EXPECT_EQ( got.second, status ) << "expected " << UA_StatusCode_name( status ) << ", the target variable has " << UA_StatusCode_name( got.second ) << " - PubSub::Writer's dataSetFieldContentMask must carry STATUSCODE.";
			EXPECT_EQ( got.first, optional<double>{value} ) << UA_StatusCode_name( status );
		}
	}
}
