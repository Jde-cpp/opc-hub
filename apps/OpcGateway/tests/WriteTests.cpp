#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "../../OpcServer/src/globals.h"
#include "../../OpcServer/src/UAServer.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	struct WriteTests : ::testing::Test{
		Ω SetUpTestCase()->void{
			if( !SelectServerCnnctn( OpcServerSlug ) )
				CreateServerCnnctn();
		};
	protected:
		α SetUp()->void override{
			let server = Server::FindUAServer();
			if( !server )
				GTEST_SKIP() << "Needs the embedded OpcServer.";
			_ua = server->Ptr();
			_node = Rpm1( _ua );
		}
		Ω Rpm1( UA_Server* ua )ε->NodeId{//HistGroupTests groups it; no other test writes it.
			size_t ns{};
			UAε( UA_Server_getNamespaceByName(ua, UA_STRING((char*)"urn:jde:pumps"), &ns) );
			return NodeId{ (NsIndex)ns, (UA_UInt32)6012 };
		}
		Ω Hold( UA_Server* ua, const NodeId& node, UA_Int32 held )ε->void{
			UA_Variant v; UA_Variant_setScalar( &v, &held, &UA_TYPES[UA_TYPES_INT32] );
			UAε( UA_Server_writeValue(ua, node, v) );
		}
		//updateVariable's answer:  the value it reads back.
		Ω Update( const NodeId& node, jvalue value )ε->jvalue{
			const jobject vars{ {"opc", OpcServerSlug}, {"id", node.ToJson()}, {"value", move(value)} };
			let written = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("updateVariable( opc: $opc, id: $id, value: $value ){ value }", vars, true) );
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