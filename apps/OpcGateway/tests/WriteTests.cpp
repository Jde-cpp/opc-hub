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
}