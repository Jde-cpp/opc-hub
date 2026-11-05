#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "utils/Pumps.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	struct WriteTests : ::testing::Test{
		Ω SetUpTestSuite()->void{ GetConnection( OpcServerSlug ); }
	protected:
		α SetUp()->void override{
			let server = Server::FindUAServer();
			if( !server )
				GTEST_SKIP() << "Needs the embedded OpcServer.";
			_ua = server->Ptr();
			_node = NodeId{ PumpsNs(), Rpm1 };//HistGroupTests groups it; no other test writes it.
		}
		Ω Hold( UA_Server* ua, const NodeId& node, UA_Int32 held )ε->void{
			UA_Variant v; UA_Variant_setScalar( &v, &held, &UA_TYPES[UA_TYPES_INT32] );
			UAε( UA_Server_writeValue(ua, node, v) );
		}
		//updateVariable's answer:  the value it reads back.
		Ω Update( const NodeId& node, jvalue value )ε->jvalue{
			const jobject vars{ {"opc", OpcServerSlug}, {"id", node.ToJson()}, {"value", move(value)} };
			let written = Socket().QuerySync( "updateVariable( opc: $opc, id: $id, value: $value ){ value }", vars );
			return written.as_object().at( "value" );
		}
		//node( opc, id ){ columns }:  the id form's read of one node.
		Ω Read( const NodeId& node, sv columns )ε->jobject{
			const jobject vars{ {"opc", OpcServerSlug}, {"id", node.ToJson()} };
			return Socket().QuerySync( Ƒ("node( opc: $opc, id: $id ){{ {} }}", columns), vars ).as_object();
		}
		//updateNode's answer:  the columns it reads back.
		Ω UpdateNode( const NodeId& node, jobject attributes, sv columns )ε->jobject{
			const jobject vars{ {"opc", OpcServerSlug}, {"id", node.ToJson()}, {"attributes", move(attributes)} };
			return Socket().QuerySync( Ƒ("updateNode( opc: $opc, id: $id, attributes: $attributes ){{ {} }}", columns), vars ).as_object();
		}
		UA_Server* _ua{};
		NodeId _node;
	};

	//updateVariable types a value as the hist edits do (ValueTypesAwait):  an enumeration of namespace 0 goes out as the Int32 it
	//is encoded as, which the server takes for one, though open62541 has a type of its own for ServerState.
	TEST_F( WriteTests, Enum ){
		const RetypedNode retyped{ _ua, _node, UA_NS0ID_SERVERSTATE };
		constexpr UA_Int32 shutdown{ UA_SERVERSTATE_SHUTDOWN };
		EXPECT_EQ( Update(_node, shutdown), jvalue{shutdown} );
	}

	//A DataType the gateway has no built-in type for takes the type of the node's value, so 9 goes out as the Int32 the node
	//holds, not the json's Int64, which would read back in the Long form:  the abstract Number, and BaseDataType, whose
	//built-in type, Variant, says nothing of a value's.
	TEST_F( WriteTests, AnAbstractDataTypeTakesTheValuesType ){
		for( UA_UInt32 dataType : {UA_NS0ID_NUMBER, UA_NS0ID_BASEDATATYPE} ){
			const RetypedNode retyped{ _ua, _node, dataType };
			Hold( _ua, _node, 7 );
			EXPECT_EQ( Update(_node, 9), jvalue{9} ) << dataType;
		}
	}
	//node( opc, id ){ … } reads the attributes of a node's class plainly (clients spec *Attributes*, #417) - the Attributes tab asks by the
	//class it knows - each answered, or carrying the status the server refused it with, never an error:  the permissions, which open62541
	//doesn't serve, come back BadAttributeIdInvalid.  No View:  the embedded server has none.
	TEST_F( WriteTests, AttributesOfEveryClass ){
		let ns = PumpsNs();
		struct Case{ NodeId Node; UA_NodeClass Class; sv Columns; };
		const vector<Case> cases{
			{ NodeId{ns, Pump1}, UA_NODECLASS_OBJECT, "eventNotifier" },
			{ _node, UA_NODECLASS_VARIABLE, "dataValue dataType valueRank arrayDimensions accessLevel userAccessLevel minimumSamplingInterval historizing" },
			{ NodeId{ns, ToHertz}, UA_NODECLASS_METHOD, "executable userExecutable" },
			{ NodeId{UA_NS0ID_BASEOBJECTTYPE}, UA_NODECLASS_OBJECTTYPE, "isAbstract" },
			{ NodeId{UA_NS0ID_BASEDATAVARIABLETYPE}, UA_NODECLASS_VARIABLETYPE, "value dataType valueRank arrayDimensions isAbstract" },
			{ NodeId{UA_NS0ID_HASCOMPONENT}, UA_NODECLASS_REFERENCETYPE, "isAbstract symmetric inverseName" },
			{ NodeId{UA_NS0ID_NUMBER}, UA_NODECLASS_DATATYPE, "isAbstract dataTypeDefinition" } };
		const flat_set<sv> mayRefuse{ "rolePermissions", "userRolePermissions", "accessRestrictions", "dataTypeDefinition" };
		for( let& c : cases ){
			let columns = Ƒ( "id nodeClass name browse description writeMask userWriteMask rolePermissions userRolePermissions accessRestrictions {}", c.Columns );
			let y = Read( c.Node, columns );
			SCOPED_TRACE( Ƒ("{}:  {}", c.Node.ToString(), serialize(y)) );
			EXPECT_EQ( Json::FindNumber<uint>(y, "nodeClass"), c.Class );
			EXPECT_TRUE( y.at("name").is_string() );
			EXPECT_TRUE( y.at("browse").is_object() );
			EXPECT_TRUE( y.at("writeMask").is_number() );
			EXPECT_TRUE( y.at("userWriteMask").is_number() );
			for( let column : Str::Split(columns, ' ') ){
				ASSERT_TRUE( y.contains(column) ) << column;
				let& v = y.at( column );
				let refused = column!="dataValue" && v.is_object() && v.as_object().contains("sc") && !v.as_object().contains("v");
				if( refused )
					EXPECT_TRUE( UA_StatusCode_isBad(v.as_object().at("sc").to_number<StatusCode>()) ) << column;
				else
					EXPECT_FALSE( mayRefuse.contains(column) && column!="dataTypeDefinition" ) << column << " answered";
				EXPECT_TRUE( !refused || mayRefuse.contains(column) ) << column << " refused";
			}
		}
	}

	//`dataValue`:  the Value as the DataValue the read brings - the reading, its status and both timestamps - where `value` is the reading
	//alone (Value::ToJson), which drops them:  the Attributes tab's Value row.  The server stamps a write as it takes it.
	TEST_F( WriteTests, DataValueCarriesBothTimestamps ){
		UA_Variant held; UA_Variant_init( &held );
		UAε( UA_Server_readValue(_ua, _node, &held) );
		let written = UA_Server_writeValue( _ua, _node, held );
		UA_Variant_clear( &held );
		UAε( written );
		let y = Read( _node, "value dataValue" );
		SCOPED_TRACE( serialize(y) );
		let& dv = y.at( "dataValue" ).as_object();
		EXPECT_EQ( dv.at("v"), y.at("value") );
		EXPECT_EQ( dv.at("sc"), jvalue{0} );
		for( sv stamp : {"source", "server"} ){
			ASSERT_TRUE( dv.contains(stamp) ) << stamp;
			let& t = dv.at( stamp ).as_object();
			EXPECT_GT( Json::AsNumber<int64_t>(t, "seconds"), 1'700'000'000 ) << stamp;//a real time, not 1601's zero.
			EXPECT_TRUE( t.contains("nanos") ) << stamp;
		}
	}

	//updateNode( opc, id, attributes:{ description } ):  written where the node's WriteMask has the bit - pumpManual's, which the OpcServer sets
	//after the load (Server::ConfigurePumps), the nodeset declaring none - and refused on a pump without, whatever the user holds:  open62541
	//answers BadUserAccessDenied for a bit the node's mask lacks as for one the user's rights lack.  A bare string keeps the locale the node's
	//text has - the nodeset's `en` - which is how it reads back on a session with no locale of its own;  {locale, text} adds a text in
	//another, which that session never reads (open62541 answers it the first text added).  An attribute updateNode has no type for is
	//refused before any write.  The description is put back after.
	TEST_F( WriteTests, DescriptionWritesWhereTheMaskAllows ){
		const NodeId masked{ PumpsNs(), PumpManual }, unmasked{ PumpsNs(), Pump1 };
		UA_LocalizedText before; UA_LocalizedText_init( &before );
		UAε( UA_Server_readDescription(_ua, masked, &before) );
		absl::Cleanup restore = [&]{
			if( let sc = UA_Server_writeDescription(_ua, masked, before); sc )
				ADD_FAILURE() << "pumpManual's description wasn't put back:  " << UA_StatusCode_name( sc );
			UA_LocalizedText_clear( &before );
		};
		const string written{ "Written by WriteTests through updateNode." };
		let y = UpdateNode( masked, {{"description", written}}, "description writeMask userWriteMask" );
		EXPECT_EQ( y.at("description"), jvalue{written} ) << serialize( y );
		EXPECT_NE( Json::AsNumber<UA_UInt32>(y, "writeMask") & UA_WRITEMASK_DESCRIPTION, 0u ) << serialize( y );
		EXPECT_NE( Json::AsNumber<UA_UInt32>(y, "userWriteMask") & UA_WRITEMASK_DESCRIPTION, 0u ) << serialize( y );
		UA_LocalizedText after; UA_LocalizedText_init( &after );
		UAε( UA_Server_readDescription(_ua, masked, &after) );
		EXPECT_EQ( ToSV(after.text), written ) << "the server holds it";
		EXPECT_EQ( ToSV(after.locale), "en" ) << "under the locale it had";
		UA_LocalizedText_clear( &after );
		let german = UpdateNode( masked, {{"description", jobject{{"locale", "de"}, {"text", "Von WriteTests geschrieben."}}}}, "description" );
		EXPECT_EQ( german.at("description"), jvalue{written} ) << "the session has no locale, so it reads the first text added:  " << serialize( german );
		try{
			UpdateNode( unmasked, {{"description", written}}, "description" );
			ADD_FAILURE() << "a pump with no WriteMask took a description";
		}
		catch( const Exception& e ){
			EXPECT_TRUE( e.Code()==UA_STATUSCODE_BADUSERACCESSDENIED || e.Code()==UA_STATUSCODE_BADNOTWRITABLE ) << e.what();
		}
		EXPECT_THROW( UpdateNode(masked, {{"browse", "pumpManual"}}, "browse"), Exception ) << "no type to write a BrowseName as";
		EXPECT_THROW( UpdateNode(masked, {{"value", 1}}, "value"), Exception ) << "updateVariable's";
		EXPECT_THROW( UpdateNode(masked, {}, "description"), Exception ) << "nothing to write";
	}
}