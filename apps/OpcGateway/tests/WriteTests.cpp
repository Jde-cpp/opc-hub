#include <absl/cleanup/cleanup.h>
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
	};

	TEST_F( WriteTests, Enum ){
		GTEST_SKIP() << "Need to find correct node id.";
		// jobject vars{ {"opc", OpcServerSlug}, {"id", jobject{{"ns", 4}, {"i", 6001}}} };
		// let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>(	Socket().Query("variable( opc: $opc, id: $id ){ value }", vars, true) );
		// TRACET( ELogTags::Test, "Read enum value: {}.", serialize(value) );
		// vars["value"] = 2;
		// let afterWrite = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>(	Socket().Query("updateVariable( opc: $opc, id: $id, value: $value ){ value }", vars, true) );
		// TRACET( ELogTags::Test, "After write enum value: {}.", serialize(afterWrite) );
		// ASSERT_EQ( afterWrite.as_object().at("value").as_int64(), 2 );
	}

	//updateVariable types a value as the hist edits do (ValueTypesAwait):  a DataType the gateway has no built-in type for,
	//here the abstract Number, takes the type of the node's value, so 9 goes out as the Int32 the node holds, not the json's
	//Int64, which would read back in the Long form.  Rpm1, which no test here uses, is retyped for the test and put back after.
	TEST_F( WriteTests, AnAbstractDataTypeTakesTheValuesType ){
		let server = Server::FindUAServer();
		if( !server )
			GTEST_SKIP() << "Needs the embedded OpcServer.";
		let ua = server->Ptr();
		size_t ns{};
		UAε( UA_Server_getNamespaceByName(ua, UA_STRING((char*)"urn:jde:pumps"), &ns) );
		const NodeId node{ (NsIndex)ns, (UA_UInt32)6012 };
		UA_ReadValueId id; UA_ReadValueId_init( &id );
		id.nodeId = node;
		id.attributeId = UA_ATTRIBUTEID_VALUE;
		const Value original{ UA_Server_read(ua, &id, UA_TIMESTAMPSTORETURN_NEITHER) };
		UAε( UA_Server_writeDataType(ua, node, UA_NODEID_NUMERIC(0, UA_NS0ID_NUMBER)) );
		absl::Cleanup restore = [&]{//the value first:  the node takes Double back only once it no longer holds an Int32.
			EXPECT_EQ( UA_Server_writeDataValue(ua, node, original), UA_STATUSCODE_GOOD );
			EXPECT_EQ( UA_Server_writeDataType(ua, node, UA_NODEID_NUMERIC(0, UA_NS0ID_DOUBLE)), UA_STATUSCODE_GOOD );
		};
		UA_Int32 held{ 7 };
		UA_Variant v; UA_Variant_setScalar( &v, &held, &UA_TYPES[UA_TYPES_INT32] );
		UAε( UA_Server_writeValue(ua, node, v) );
		const jobject vars{ {"opc", OpcServerSlug}, {"id", node.ToJson()}, {"value", 9} };
		let written = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("updateVariable( opc: $opc, id: $id, value: $value ){ value }", vars, true) );
		EXPECT_EQ( written.as_object().at("value"), jvalue{9} ) << serialize( written );
	}
}